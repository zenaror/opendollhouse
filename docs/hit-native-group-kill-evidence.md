# Opcode nativo 69: registro, parada e transição

Evidência estática de 2026-10-15. O opcode `0x45` da imagem fornecida não
sustenta ligar o callback legado `kill_group(uint8_t)` a um cancelamento das
notas da própria thread. Ele resolve uma variável, consulta um registro global
que contém TrackIDs e pode parar outro track ou preparar uma transição nele.
O operando zero é erro, não o identificador da instância atual.

## Identidade e método

Imagem externa `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Base PE `0x400000`. Nos trechos `.text`/`.rdata` citados, offset de arquivo é
VA menos `0x400000`; endereços de globais de runtime não são offsets de arquivo.
Não foi executado o original nem alterado projeto Ghidra ou recurso comercial.
Não está estabelecida equivalência com outro arquivo executável.

Foram lidos primeiro os exports existentes
`reports/ghidra/supplied-functions/0057e3dc.{c,asm}.txt`,
`0057dac6.{c,asm}.txt` e os exports de inicialização. `objdump` pontual confirmou
os bytes e os destinos de vtables. A seleção reprodutível com hash, VAs,
offsets e bytes está em
`reports/hit-native-group-kill-selected.asm.txt`.

O HSM externo `SoundData/SimsGeneratedHitSource.hsm`, SHA256
`ca6bef035809b2cdacdc2c14a98582cd4c4702c53cc6640beceaa548c06d55b6`, nomeia
`seqgroup_kill=69` (linha 67), `fade_dest=40`, `fade_var=41`,
`fade_speed=42`, `fade_on=45`, `isplaying=46`, `source=47` (121–128) e
`Instance=1` (168). Os nomes corroboram o código; não substituem o tracing.
A referência Simitone pinada permanece distinta: seu FreeSO `f86bbd3`,
`HITInterpreter.cs:651–663`, interpreta um byte constante e só implementa
`Instance=0` com `KillVocals()` da própria thread. Não transportar esse
mapeamento para o perfil nativo.

## Encoding e identidade do alvo

| Operação | VA | Offset de arquivo |
|---|---:|---:|
| Dispatch: opcode menos 1, tabela de ponteiros | `0x57e3f8–0x57e408` | `0x17e3f8` |
| Entrada da tabela para opcode `0x45` | `0x57f818` | `0x17f818` |
| Handler apontado pela entrada | `0x57f1ec` | `0x17f1ec` |
| Leitura de um byte/avanço do PC | `0x57f85a` | `0x17f85a` |
| Getter de variável, vtable +`0x88` | `0x57e10f` | `0x17e10f` |
| Lookup do TrackID | `0x573466` | `0x173466` |
| Find no hash, sem criação | `0x575b11` | `0x175b11` |

A instrução ordinária é de dois bytes: opcode e localização de variável.
`0x57f1f3–0x57f209` exige localização 1–127 e chama o getter com ela.
O **valor** retornado vira índice em
`manager + 0x1a4 + 4*valor`, por `0x57f215–0x57f21f`. Não existe comparação
com limites do valor entre o getter e esse acesso. Zero como valor da variável
acessa a entrada zero; zero como byte operando segue o erro
`TRACK ERROR: kTrackCmdSeqGroupKill Src` em `0x57f2e4`.

Há um detalhe importante para bytes malformados: `0x57f85a` lê byte unsigned;
se maior que 127, chama o callback de erro `Illegal Register Id` e retorna 1.
O handler então pode continuar com localização 1. O callback padrão instalado
em `0x57ca63` é `0x57c05d`, que também chama parada +`0x38` na thread. Portanto
não descrever o caso >127 como uma recusa limpa sem efeitos, nem copiá-lo
como comportamento seguro no ODH.

O identificador corrente é obtido de `this+0xc4` (controller) pela vtable
+`0x14` em `0x57f222–0x57f22a`. Ambas as vtables `0x6719b4`/`0x671a74`
apontam esse slot para `0x41e897`, que lê `controller+8`. O construtor
`0x57d77e` armazena ali seu argumento TrackID. Logo a entrada contém um
**TrackID de controller**, não diretamente um ponteiro nem um World source.
Se o byte for `Instance=1`, o valor pode vir de R1/caller, mas essa associação
é uma escolha do bytecode e da invocação, não uma regra do opcode.

`0x573466` consulta a estrutura em `manager+0x80`. `0x575b11` calcula o bucket,
percorre nós comparando chave em `node+4` e devolve iterador; o wrapper retorna
`node+8` ou zero. Não aloca nem insere. O handler não verifica null depois de
resolver uma entrada anterior não zero. Um TrackID obsoleto depende de uma
invariante externa; não equivale a grupo vazio suportado.

Startup `0x57ca50–0x57ca6d` zera `0x80b` DWORDs a partir de `manager+0x17c`,
cobrindo também esse registro. Isso não prova o domínio semântico de índices.
Valores negativos, excessivos ou com wrap entram na aritmética de endereço
x86 sem guard local; a extensão do array e a política de validação precisam
ser provadas antes de expor um provider seguro. Não imitar acesso fora de faixa.

## Ramos do handler

Se a entrada anterior é zero ou igual ao TrackID corrente, nenhum áudio é
parado. Ao final, `0x57f2d1–0x57f2dc` grava o TrackID corrente nessa entrada.
Esse mesmo store ocorre nos dois ramos abaixo.

Se a entrada anterior é diferente, o handler resolve o track e lê sua variável
19 (`extvol`):

- Se extvol é zero, chama a vtable +`0x38` desse track em `0x57f262`.
- Se extvol não é zero, lê `fade_on` 45 e `fade_var` 41. Se já são ativo/19,
  mantém os parâmetros. Caso contrário escreve, pelo writer +`0x84`,
  `fade_speed` 42=200, `fade_var` 41=19, `fade_on` 45=1
  (`0x57f286–0x57f2b3`).
- Nesse segundo ramo, restaura PC do **caller** ao início da instrução
  (`this+0x1a4` → `this+0x19c`, `0x57f2b9–0x57f2bf`) e grava deadline
  `manager.clock+2` em `this+0x198` (`0x57f2c1–0x57f2cb`).

O handler não escreve `fade_dest` 40 nem a flag `this+0x1cf` de parada após
transição. O destino é o valor existente; o default fresco é zero, mas um
script pode tê-lo alterado. Não afirmar “fade para zero e destrói a thread”
para todo caso. Os setters também podem redirecionar ao provider compartilhado;
esta unidade não estabelece todas as combinações desse provider.

O registro é sobrescrito **já no primeiro processamento**, inclusive quando
há transição/retry. Na reexecução, sem intervenção de outro caller, a entrada
já corresponde ao TrackID corrente: passa pelo ramo igual e avança. Não é
uma espera comprovada até a transição do alvo terminar. Outra chamada pode
mudar a entrada entre esses polls.

## Clock, transição e continuidade

O scheduler de thread `0x57dac6` (offset `0x17dac6`) só trabalha se
`this+0x1c4 !=0` e `this+0x1c8 ==0`. Antes de executar bytecode ele processa
`fade_on` em `0x57daf8–0x57db61`: lê a variável indicada por ID41, destino
ID40, passo ID42, ajusta sinal do passo em direção ao destino, soma e limita
quando ultrapassa o destino. Zera ID45 se passo zero ou houver ultrapassagem.
No caso de igualdade exata, a condição de ultrapassagem é estrita: o clearing
pode ocorrer no poll seguinte. Não converter esses passos em milissegundos.

Em `0x57db67–0x57db86`, somente se `this+0x1cf` já estiver ligado e ID45
estiver zero o scheduler chama parada +`0x38`. Opcode 69 não liga essa flag.
Uma transição iniciada por ele pode atingir volume zero enquanto `isplaying`
continua 1 e a thread segue com seu script.

`0x57db8b` captura `manager+0x58`; `0x57dbac–0x57dbd7` executa instruções
quando o deadline é não zero e não supera esse clock. Depois da chamada ao
handler, `0x57dbdc–0x57dc20` ignora o retorno AL e volta a testar o deadline.
Assim, retorno 1 do handler **não significa yield**. Empty/equal e parada
imediata do alvo podem continuar com a instrução seguinte no mesmo poll;
o ramo com deadline `clock+2` adia a reexecução do caller.

O poll do manager `0x57d230` (offset `0x17d230`) incrementa `manager+0x58`
quando sua flag +`0x54` permite (`0x57d268–0x57d26d`) e itera um snapshot da
fila chamando slot +`0x1c`, que nas vtables examinadas aponta para
`0x57dac6`. A unidade prova esse domínio de clock, sem provar frequência,
relação com World ticks, tempo de dispositivo ou ordem completa dos callers.
Fade é um passo por invocação do scheduler da thread; o clock pode ter regras
adicionais de pausa. Não converter `+2` em dois World ticks ou milissegundos.

## O que a parada +0x38 realmente faz

As vtables `0x6719b4`/`0x671a74` apontam +`0x38` para `0x57e037`
(offset `0x17e037`). Não é apenas uma iteração pelas notas da própria thread:

1. Limpa o estado ativo `this+0x1c4`; chama +`0x98(0)` do objeto de áudio
   em `this+0x1d0`, quando presente.
2. Chama `0x57f91e(0)`: solicita parada ao objeto de áudio, libera a referência
   da patch, chama teardown/destrutor do objeto e zera `this+0xbc`/`this+0x1d0`.
   Armazena patch bruto ID50=0 em `0x57f979–0x57f981`.
3. Zera ID45 (`this+0xa8`), flag de parada após transição +`0x1cf` e escreve
   ID46/isplaying=0 pelo writer local +`0x94` em `0x57e060–0x57e074`.
4. Zera provider compartilhado +`0xc8`, restaura PC ao entry do controller,
   zera deadline +`0x198`, retira a thread da fila via `0x57d349`, limpa
   lista +`0x1a8` e remove sua contribuição de ducking via `0x57d5c4(this,0)`.

O caminho de patch examinado cria objeto com vtable `0x671c84` por
`0x5800bf`; +`0x98` aponta para `0x5804ba`. Este consulta o recurso ligado,
testa estado de reprodução e chama +`0x28` no objeto do backend. O corpo de
parada e o teardown são comprovados; a quantidade de vozes internas ou
compartilhamento de buffers desse backend não foi estabelecida. Não afirmar
“cancela todos os handles PCM do owner ODH” como equivalência nativa.

A thread parada não prossegue naturalmente até outro NoteOn: está inativa,
desenfileirada e sem binding de patch. Reinicialização/reativação são caminhos
separados; a estrutura da thread não é deletada por `0x57e037`.
A thread **que executou opcode69**, nos ramos válidos, não é parada por esse
handler e pode alcançar NoteOn posterior, no mesmo poll ou após o retry,
conforme seu bytecode. Não confundir com o erro de operando zero, que chama
o callback padrão de parada da thread emissora.

Não se observou clearing desse registro pelo corpo de `0x57e037`; ele tem
outros mecanismos de estado/fila/ducking. O ciclo completo de remoção do mapa
e limpeza de entradas de grupo ainda precisa de tracing. Não presumir que
parar um track apaga toda referência a seu TrackID.

## Contrato mínimo e próxima unidade

O perfil nativo deve recusar explicitamente opcode69 até existir um caminho
com localização de variável, valor resolvido, identidade de TrackID, registro
compartilhado e resultado continue/retry no domínio correto de clock. O
callback legado que recebe byte constante não representa esse contrato.
Manter a implementação legada identificada como comportamento de referência.

Primeira investigação seguinte: criação/reutilização e ownership no mapa
`manager+0x80`, a partir de `0x573466`/`0x575b11` e dos callers
`0x57348c`/`0x57376b`. Determinar como TrackID identifica instâncias simultâneas
ou reutilizadas e como entradas ficam válidas durante parada/destruição.
Depois: domínio válido do índice do registro, seus demais escritores/limpeza,
provider compartilhado e ordem/frequência dos polls. Só então implementar
transições e parada com política de notas/ownership sustentada por evidência.

A evidência basta para impedir o wiring incorreto e definir os campos do
futuro contrato, não para declarar reprodução fiel de grupos ou cancelamento
nativo completo. Não houve validação dinâmica do executável ou áudio físico.
