# Resultados de binding HIT e origem dos controles compartilhados

Investigação estática de 2026-10-17, base ODH `c34874410729c74b85d7ed147cc07a80db404a6d`.
A fonte externa é `../referencias/The Sims/Sims.exe`, PE32 i386, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
O executável não foi executado nem modificado. Foram reutilizados os exports
Ghidra existentes antes dos recortes adicionais; não foi necessário executar
Ghidra ou alterar seus projetos/configurações. Complementa
[ownership](hit-native-backend-ownership-evidence.md) e
[lifecycle](hit-native-controller-lifecycle-evidence.md).

Reprodução: `python3 reports/hit-native-binding-outcomes/extract.py`.
O manifesto deriva VA/RVA/raw dos cabeçalhos PE e contém SHA256 por trecho;
os 25 recortes somam 4.070 bytes comparados com o arquivo, sem
instruções inválidas ou `.byte` nos limites escolhidos. Contagens de linhas
incluem continuações de bytes impressas pelo objdump, não representam uma
contagem semântica de instruções. As vtables e strings são registradas à parte.

## O helper limpa antes de procurar

`0x57f91e` não faz preflight nem compara o ID novo com o anterior:

1. Se `controller+bc` existe, chama `wrapper+98(0)` quando há wrapper;
   chama `objeto+7c` e zera `controller+bc`.
2. Se `controller+1d0` existe, chama `wrapper+0c`, depois o destrutor
   `wrapper+00(1)` e zera `controller+1d0`.
3. Escreve os 32 bits solicitados em `controller+e8` (ID50), em `0x57f97b`.
   Não há clamp, conversão signed/unsigned, comparação com ID antigo ou
   restauração do ID anterior no ramo de chave ausente.
4. Zero retorna `AL=1`. Não zero consulta o catálogo único `0x573466` e
   guarda imediatamente o objeto retornado em `controller+bc`.
5. Ausente retorna `AL=0`. Patch válida (`type=0xa3cd96b7`) adquire `+78`,
   cria wrapper por `+a0`, aplica controles, copia patch.ID26 para controller
   pelo writer público e retorna `AL=1`.
6. Tipo incorreto chama `manager+c8(controller,"Bad Patch ID")`, depois
   retorna `AL=1`. Não chama `+78` nem cria wrapper para esse objeto.

Um retorno falso portanto não significa que o binding anterior foi preservado.
O wrapper pode receber várias solicitações de parada no teardown, porque
`+0c` e o destrutor também encaminham parada. A última liberação da patch
pode já ter zerado seu backend; a parada do wrapper consulta a patch atual.

## O diagnóstico padrão muda o estado

O startup `0x57ca63` instala `0x57c05d` em `manager+c8`. Esse callback
chama `controller+38` em `0x57c0a9`, isto é, stop `0x57e037` nas duas
vtables de controller examinadas. Assim, com o callback padrão, tipo incorreto
não é somente uma mensagem com um ponteiro inválido retido:

- Stop faz binding(0) recursivo. O objeto de tipo incorreto ainda está em
  `controller+bc`, portanto recebe `+7c` apesar de não ter recebido `+78`.
- Para Track concreto de vtable `0x6719b4`, `+7c=0x57d6f1` apenas decrementa
  `objeto+14` e retorna. Se era zero, passa a `0xffffffff`; não há destruição
  ou limpeza nesse corpo. Trata-se de uma anomalia estática, não de um crash
  observado, e não exige reproduzir underflow no owner seguro do ODH.
- Stop limpa `+bc/+1d0/ID50`, ativo, ID45, byte fade-stop, ID46, provider+c8,
  deadline+198 e pilha+1a8; restaura PC ao entry e remove da fila. A última
  escrita local é ID46=0, deixando zero=true, positive=false, sign=false.
- Stop não escreve +194 nem pausa+1c8. O helper externo retorna sucesso
  depois que o callback retorna.

Um callback substituído pode ter outros efeitos. As conclusões de stop nesta
unidade são explicitamente do callback padrão instalado, não de qualquer
função arbitrária que o manager possa receber.

## Resultado depende do caller

Tabela para provider próprio e callback padrão, após a chamada indicada:

| Pedido | Helper direto / writer local ID50 | Initializer/restart `0x57dc59` | LoadL5 ID50 |
| --- | --- | --- | --- |
| ID0 | Sucesso; ID50=0, sem wrapper; não desativa por si só | Segue; ID50=0, ativo=1, ID46=1, pausado | Sucesso; continua sem áudio, estado ativo preservado |
| Chave ausente | Falha; ID50 solicitado fica bruto, sem wrapper; helper não diagnostica | Ignora falha; conserva ID50 solicitado, ativo=1, ID46=1, pausado | Falha do writer gera "Could not set register"; callback padrão stop limpa ID50/ativo/ID46 |
| Objeto de outro tipo | "Bad Patch ID" faz stop; ID50=0, sem wrapper; helper retorna sucesso | Ignora o resultado e continua depois do stop: ID50=0, ativo=1, ID46=1, pausado | Retorna sucesso após stop; não gera o segundo diagnóstico do writer |
| Patch válida | Sucesso, wrapper novo; ID26 copiado da patch | Segue a inicialização e pausa | Sucesso; bytecode pode continuar |

O initializer chama binding em `0x57de60` e imediatamente aplica controles
em `0x57de67/0x57de6e`, sem testar AL. Depois restaura PC e loop-PC,
limpa listas+1b0/+1b4, define deadline+198=Clock, escreve ID46=1 pelo writer
local, liga ativo e chama pausa. As flags finais são zero=false,
positive=true, sign=false. A fila fica removida pela pausa; o caller de evento
pode posteriormente aplicar controles e chamar resume para reinscrever.
O stop diagnóstico no caminho wrong-kind também limpou a pilha+1a8, ao
contrário de um restart comum que a preserva. O provider+c8 fica nulo após
esse stop; o writer público trata nulo como provider próprio.

LoadL5 avança PC sobre o operando e chama o writer público em `0x57e4cd`.
Ao contrário do initializer, testa AL em `0x57e4d3`; falso vai ao diagnóstico
`0x57f56a`. O writer público `0x57e150` preserva AL retornado pelo destino,
embora o export C antigo o apresente como `void`. É o assembly que fecha esse
contrato. O stop diagnóstico restaura PC ao entry e zera deadline, não deixa
PC no byte seguinte como a escrita bem-sucedida normal.

Antes dos efeitos, o writer local `0x57e191` grava flags de sinal/zero e o
valor bruto no banco. Com argumento silent não zero, retorna sem binding.
No sucesso de Patch, a escrita posterior de ID26 também altera flags: o
resultado final do writer ID50 segue patch.ID26, não necessariamente o ID50
original. No helper direto ID0/ausente, não há escrita de flags; no writer
ID50 esses ramos mantêm flags do valor solicitado até um eventual diagnóstico.

## +194 pertence ao controller, não à geração do backend

O construtor zera `controller+194` em `0x57d9af`. NoteOn temporizado escreve
Clock+duração em `0x57e470`. Binding, stop e initializer completos não têm
store direto nesse campo. Se uma deadline antiga sobreviver a um rebind e o
poll chegar ao ramo `0x57dc22`, ao vencer ela para **o wrapper corrente** em
`+1d0`, se houver, e zera +194 mesmo sem wrapper. Isso é uma consequência
condicional do fluxo estático; não prova uma execução específica do jogo.
Uma API sem opcode1 pode manter esse campo fora do subconjunto, mas não deve
alegar que o original o limpa ao trocar a patch ou após diagnóstico.

## Origem dos metadados e controles

A aquisição `patch+78` só chama initializer quando `patch+14==0`.
`0x57fe8f` consulta **a própria patch**, pelo seu getter+88:

| ID da patch | Uso na criação da geração |
| --- | --- |
| 38 | Escolhe modo 1 quando não zero, senão 2 |
| 26 | Escolhe factory espacial engine+70 ou comum engine+5c |
| 27 | Booleano passado à factory espacial |
| 28/29 | Limites enviados ao backend espacial +ec/+f4 |
| 43 | Aquisição/carga do recurso por patch+60 quando não zero |

A sequência não copia controles do primeiro controller para a patch nem usa
seus overlays para escolher a factory. `metadataIs3d`/`metadataStreaming`,
se usados na API ODH, devem representar a fonte patch dessa geração, não
metadados arbitrários do controller que primeiro a adquire. Os nomes de
campos ODH não são nomes extraídos do executável.

Cada binding novo chama `0x57fa52` e `0x57fa16` **antes** de copiar ID26 da
patch ao controller. O initializer e o writer ID50 chamam ambos novamente
após o helper. Logo a primeira aplicação pode consultar o ID26 anterior do
controller; a segunda já usa o ID26 copiado. É incorreto colapsar esse fluxo
em um único apply baseado sempre nos metadados novos.

Os setters normais de controller não reinicializam a patch:

- IDs18/19/20/27/30/31/32/56/57 chamam `0x57fa52`: ela usa o wrapper,
  escolhe pan ou posição pelo **controller.ID26**, calcula o controle de
  ganho e chama wrapper+40. Valores lidos e descartados no corpo não provam
  um efeito próprio de backend para cada ID.
- ID21 chama `0x57fa16`: combina controller.ID21 com patch.ID21, subtrai3600
  e chama wrapper+44. O wrapper aplica sua própria transformação adicional;
  não é uma prova de escala DSP linear.
- IDs28/29, quando controller.ID26!=0, encaminham limites ao wrapper+ a4/a8
  e daí ao backend espacial. O corpo não verifica wrapper nulo nesse ramo;
  não reproduzir uma dereferência inválida como requisito do ODH.
- IDs33/34/35 encaminham diretamente aos slots wrapper+ac/+b4/+b0 quando
  há wrapper. ID26 e ID38 apenas escrevem banco/flags nesse dispatch; não
  substituem a factory nem alteram a classe da geração existente.
- ID50 é o setter que executa teardown/rebinding. Para a mesma patch, se
  outro controller ainda a mantém adquirida, a geração pode continuar viva;
  se o contador chegou a zero, a próxima aquisição inicializa outra geração.

Wrapper+40/+44/+48 encaminham para seu backend+c0; posição+4c consulta
patch+98 e os limites usam wrapper+c4. Portanto controles emitidos por um
controller podem sobrescrever o estado que outro wrapper da mesma patch
observa. O primeiro controller não tem exclusividade permanente desses
controles. O registry deve separar fonte de metadados da patch, aquisição e
resultado de binding, aplicação dos controles e estado lógico do controller.

Esta unidade não altera runtime/CMake, não cria commits, não executa o jogo e
não valida PCM, temporização física, concorrência ou hardware. Uma política
segura para aliases/underflow é uma escolha ODH separada dos efeitos lógicos
confirmados pelos recortes.
