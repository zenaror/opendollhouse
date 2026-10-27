# Mapa nativo de entradas HIT: origem, ponteiros e precedência

Investigação estática de 2026-10-17. O caminho de carga inspecionado popula
`manager+0x168` com HOT `TrackData`. Cada valor é um **ponteiro para um buffer
HIT mais um offset local**, e uma definição posterior da mesma chave substitui
a anterior. O mapa é compartilhado pelo manager; os buffers HIT são separados.
A ordem completa de descoberta dos arquivos permanece sem prova.

Continuação posterior: [ordem de descoberta](hit-native-load-order-evidence.md)
identificou raízes na ordem de chamada e arquivos na ordem da API Win32.
O [catálogo global de objetos](hit-object-catalog-validation.md) separa
o EventMapping local do Track vencedor no namespace compartilhado Track/Patch;
o mapa TrackData documentado aqui continua uma estrutura distinta.

## Proveniência e reprodução

Alvo externo: `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
PE32 x86, image base `0x400000`. A revisão ODH na abertura da investigação era
`0a3faf52bea21260006f53991dce7907b8899b34`. Nenhum executável original foi
executado ou modificado, e não há validação de áudio físico ou hardware.

O diretório ignorado registro arquivado não distribuído preserva `EVIDENCE.txt`,
`collect.py`, `manifest.json`, `artifact-hashes.json`, 22 recortes de assembly
e `direct-call-xrefs.txt`. Executar, a partir do checkout:

O roteiro automatizado desta observação usou ferramentas locais não
distribuídas. Os resultados e limites descritos são históricos; esse roteiro
não é um comando reproduzível a partir de um clone público.

O script confirma o hash do alvo, calcula o mapeamento PE a partir dos headers,
extrai bytes/assembly por intervalo e enumera chamadas diretas selecionadas
com `objdump`. Não carrega o executável como processo. Hashes deste recibo:

| Artefato | SHA256 |
| --- | --- |
| `collect.py` | `ae57ef5d8c48eda632fca4f67c04f5b8c842d8d0f953f3f274448b8059e6b8f7` |
| `manifest.json` | `61fc32f160781bcad69b8ed45a4c6c75a9caa3217e195fae2476f6faaa7a8628` |
| `EVIDENCE.txt` | `9a79fdbc9e6f359b8c799abdf4b8af9b2c187eceb58732aac9ecc5488f5913a6` |

Todos os endereços de instruções abaixo pertencem à `.text`: RVA e offset de
arquivo coincidem com `VA-0x400000` **neste PE**. Não é uma regra geral de PE.
O slot global `0x6f6a78`, na parte virtual de `.data`, não recebe um offset de
arquivo por essa subtração: seu valor é estado de execução.

| Operação | VA | RVA / offset de arquivo |
| --- | --- | --- |
| Publicar manager global | `0x57173e` | `0x17173e` |
| Carga de um grupo de arquivos | `0x5725c4` | `0x1725c4` |
| Registrar callback `TrackData` | `0x57b319` | `0x17b319` |
| Callback de registro | `0x57bb98` | `0x17bb98` |
| Lookup por chave de bytecode | `0x57d0a9` | `0x17d0a9` |
| Inserir/substituir entrada | `0x57d169` | `0x17d169` |
| Carregar buffer HIT | `0x57d19b` | `0x17d19b` |
| Lookup da chave inicial | `0x57dedb` | `0x17dedb` |
| Busca do opcode | `0x57e3dc` | `0x17e3dc` |
| Call7 | `0x57e6bf` | `0x17e6bf` |
| Hash get-or-insert | `0x582457` | `0x182457` |

O manifesto registra limites exclusivos, comprimentos e hashes dos bytes
originais de cada recorte, além dos hashes do assembly. Os caminhos comerciais
permanecem externos ao Git; o documento descreve sua interpretação estática.

## Carga e associação entre HOT e HIT

`0x571717–0x57177d` aloca o manager, publica seu ponteiro em `[0x6f6a78]` e
chama `0x5725c4` para uma raiz inicial e para uma lista adicional do owner em
`+0xd0`. Há outra chamada direta em `0x5725ad`. O mapa pertence ao manager,
não a cada instância de arquivo HIT.

`0x5725c4` procura `*.hit`, percorre uma lista de arquivos e deriva caminhos
com o mesmo basename. Carrega `.hsm` por `0x57d3bd` em `0x5726e3`, `.hit`
por `0x57d19b` em `0x572741` e prepara o parser `.hot` em `0x5727a2`.
As strings `.hot`, `.hit`, `.hsm` e `*.hit` estão respectivamente em
`0x6cefd8`, `0x6cefe0`, `0x6cefe8` e `0x6ceff0`.

O loader HIT completo, `0x57d19b–0x57d230`, obtém o tamanho do arquivo,
aloca um buffer e grava seu ponteiro em `manager+0xa4` em `0x57d1ed`.
Registra o ponteiro na coleção `manager+0xac` por `0x601324` e lê o arquivo
inteiro para esse buffer em `0x57d1fa`. Publica ainda tamanho em `+0x50`
e ponteiro em `+0x4c`. Esse caminho não concatena buffers, não realoca os
operandos do bytecode e não interpreta HIT ENTP.

O HOT registra a seção `TrackData`, string `0x6cf434`, com callback
`0x57b109`. O callback chama `0x57bb98`; esse converte chave e valor por
`0x5d81cf`, obtém o manager global em `0x57bc34` e chama `0x57d169`.
O parser `0x57b3ed` percorre as linhas e despacha cada `key=value` para o
callback da seção corrente. O último registro processado de uma chave vence.

`0x57d169` lê `[manager+0xa4]`, soma o offset recebido em `0x57d182`, consulta
`manager+0x168` e grava o resultado em `[entry+4]` em `0x57d191`.
A soma usa aritmética DWORD; não há redução da chave a 16 bits nem validação
contra o tamanho do HIT nessa rotina. O valor armazenado identifica tanto o
buffer de origem quanto sua posição. Somar tamanhos de HITs para produzir um
PC global seria uma representação portátil adicional, não o layout original.

## Duplicados, ausência e destinos entre arquivos

`0x582457` cresce a tabela conforme necessário, escolhe um bucket pela chave
unsigned e compara o DWORD completo nos nós da cadeia. Se a chave existe,
retorna seu par; se não existe, aloca um nó de 12 bytes por `0x5825c5` e copia
o par fornecido. Colisões de bucket mantêm chaves distintas; repetição da
mesma chave permite que `0x57d169` sobrescreva o payload.

A regra comprovada é **última definição processada vence**, inclusive entre
grupos de recursos. Ainda não equivale a uma ordem alfabética, de expansão
ou de instalação: falta determinar a ordem da lista produzida por `0x5d8632`
e a formação da lista externa de raízes `+0xd0`.

Tanto `0x57d0a9` quanto `0x57dedb` fornecem `(key,0)` ao hash. Com alocação
bem-sucedida, chave ausente insere e retorna zero. ODH pode recusar ausência
explicitamente; isso é uma salvaguarda portátil, sem reproduzir o uso de um
ponteiro nulo pelo restante do caminho nativo.

O lookup inicial acessa `owner+0x31c`. Em `0x57de73`, o initializer obtém
`owner` de `[controller+0xc4]` antes de chamar `0x57dedb`. O construtor
`0x57d6f8` grava a chave em `this+0x31c` em `0x57d78b` e inicializa
`this+0xc4=this` em `0x57d7b4`. Portanto a indireção começa apontando para
o próprio objeto; ela não prova um objeto de metadados sempre separado.
Esses stores foram reconfirmados por `objdump` no alvo e também constam de
registro arquivado não distribuído, SHA256
`f47a09871210980300475258ba76a38511c78602918bdb52b044d77479fc20bf`.

O initializer grava o destino em `controller+0x19c` e `+0x1a0` em
`0x57de84/0x57de8a`. O interpretador lê `+0x19c` como ponteiro de byte,
desreferencia o opcode e avança o ponteiro. Call7 resolve uma chave pelo
mesmo manager, empilha o ponteiro de retorno e instala o novo ponteiro.
Assim, a estrutura nativa permite chamada e retorno entre buffers HIT;
não há vínculo obrigatório a um único arquivo no campo de PC.

## CSV7: chave numérica, referência HSM ou equate

O campo CSV7 de `Track` não exige prefixo `@` no original. A confirmação
adicional usa `objdump` fresco sobre o mesmo executável: parser `0x57b921`,
helper `0x57b554` e classificação de caracteres `0x6368c3`. Em `0x57bacc`,
o parser extrai CSV7 e chama o helper em `0x57baf0` (bytes
`e8 5f fa ff ff`). Guarda o resultado em ESI, depois o empilha em
`0x57bb1d`; o construtor recebe esse argumento em `[EBP+0x24]` e o grava
em `this+0x31c`. A chamada seguinte, `0x57bafd`, corresponde a CSV8,
o campo de patch, e não à chave CSV7.

O helper compara o primeiro byte com `@` em `0x57b56a`
(`80 38 40`). Esse caminho remove o prefixo e consulta símbolos por
`0x57d4ab` em `0x57b595`. Sem `@`, classifica o primeiro caractere por
`0x6368c3` em `0x57b5b1`; o classificador testa o bit de dígito `0x04`.
Quando o primeiro caractere é um dígito, chama `0x5d81cf` em `0x57b5d0`,
usando a conversão numérica já descrita para `TrackData`. Assim, números
decimais e formas como `0x...` podem fornecer a chave diretamente.
Tokens iniciados por letra seguem a consulta de equates do parser em
`0x57b5bf`; não devem ser convertidos automaticamente em hexadecimal
apenas por conterem A–F. A origem dessa tabela foi confirmada: o callback
`EventMappingEquate`, registrado em `0x57b28d` por `0x57b12f`, chega a
`0x57bda1`, converte o valor por `0x5d81cf` e escreve no mapa `parser+0x18`
por `0x5758af`, com store final em `0x57bdd0`. O helper CSV7 usa precisamente
esse campo `+0x18` e a mesma função `0x5758af`. Portanto aliases já definidos
em `EventMappingEquate` são uma terceira forma de fornecer a chave.
Preservar um alias desconhecido como não resolvido é política defensiva ODH;
não é necessário inventar uma chave zero para admiti-lo.

Os recortes adicionais estão em
registro arquivado não distribuído. Mapeamentos e hashes
dos bytes originais, com fim exclusivo, são:

| VA inicial–fim exclusivo | RVA / offset inicial | Bytes | SHA256 |
| --- | --- | --- | --- |
| `0x57bacc–0x57bb32` | `0x17bacc` | 102 | `2cdae30cf0f297afdc02891c4844499b8d151def9a9c2b89d087066204b1aa0b` |
| `0x57b554–0x57b5e6` | `0x17b554` | 146 | `4478a0d17f95423ce7f74ee29353c8210c2e030c33529c68d3ba238330ba8526` |

O callback de equates foi lido adicionalmente em `0x57bda1–0x57bdd7`,
offset `0x17bda1`, 54 bytes, SHA256
`36674e48649e5c19e6e94c9adfcf6390a8f312aa3986dc383660e3a0de97133f`.

O classificador foi lido adicionalmente em `0x6368c3–0x6368eb`, offset
`0x2368c3`, 40 bytes, SHA256
`66c1fcb92f6f0483e3f1ac6e4bf604265122427f8ee32250390e1c393785ef56`.
Essa prova trata de sintaxe e roteamento estático do campo; não é execução
dos eventos comerciais que motivaram a correção do parser.

## Ingestão HOT: filename de Patch entre aspas

A leitura adicional do callback `Patch`, `0x57b5e6`, distingue a extração
nativa de um índice CSV fixo. Em `0x57b65d` e `0x57b67d`, ele consome os
dois primeiros campos pelo delimitador vírgula de `0x6c0fdc` (`2c 00`).
Preserva o segundo como nome da patch. Depois carrega o delimitador aspas
de `0x6cbad0` (`22 00`): a chamada `0x57b693` consome o prefixo até a
primeira aspas; `0x57b6b9` extrai o conteúdo até a próxima aspas como
filename. O trecho intermediário antes da primeira aspas é descartado.
Assim, um campo textual adicional nesse trecho não exige deslocar índices
por hipótese: o algoritmo nativo procura as aspas após os dois primeiros
campos. As flags seguintes são lidas a partir do sufixo posterior ao filename.

`0x5d4a0d` procura o delimitador, retorna o prefixo e substitui a string
consumida pelo sufixo após o delimitador; `0x5d499c` copia strings. Os
recortes frescos de `0x57b650–0x57b70e`, `0x5d4a0d–0x5d4b3c` e
`0x5d499c–0x5d49b7`, mais os delimitadores e seus offsets, estão em
registro arquivado não distribuído, SHA256
`ef6d3c4e0026ea9aefc8da9e9c3dd3cfe1eab99721b709aa682d5c13fd8d4d35`.
Cada intervalo traz o hash dos bytes originais. Isso prova extração textual;
não prova localização, decodificação ou reprodução do arquivo de áudio.

As comparações de flags seguintes aceitam exatamente o valor1. O recorte
adicional `0x57b70e–0x57b767` está em
registro arquivado não distribuído, SHA256
`b71563146b57a8774f2229c11ab7e653c01d187f2d00b8e0def3105b41c0a630`.
Seus 33 registros de instrução foram comparados aos bytes originais; o recibo
`root-patch-flags-audit.json` conserva o intervalo e hash dos bytes. A leitura
malformada/overflow do parser original continua fora do contrato portátil.

## ENTP, limites e contrato portátil

A extração completa encontrou três chamadas diretas de `0x582457`:
`0x57d0c2` (lookup), `0x57d18c` (inserção) e `0x57defc` (entrada inicial).
O único caller direto encontrado de `0x57d169` é o callback HOT. Junto à
leitura completa do loader HIT, isso estabelece a população por `TrackData`
e a ausência de merge/fallback ENTP **nesse caminho**. Não demonstra que
nenhum caminho indireto ou alias ainda não rastreado possa manipular o mapa.
ENTP deve continuar sendo estrutura independente, sem inventar precedência
sobre ou sob o HOT.

O código inspecionado usa buffers e hash dinâmicos. Não estabelece os tetos
portáteis de 1 MiB por HIT, 8192 entradas ou uma profundidade de pilha ODH.
Também não valida offset contra a região de bytecode em `0x57d169`.
Falha de abertura do HIT preserva `+0xa4`, e o caller inspecionado continua
para HOT; o loader ignora o resultado da leitura após abrir o arquivo.
Esses caminhos residuais não definem um contrato seguro de recursos inválidos.

Um contrato portátil sustentado por esta evidência recebe grupos válidos em
**ordem explícita do caller**, mantém cada HIT com identidade estável e aplica
cada registro HOT a um mapa `key → {resource, local_pc}`, substituindo
entradas anteriores. Entrada inicial e Call7 consultam esse mesmo mapa.
Execução, loop e frames de retorno precisam conservar identidade do recurso
junto ao PC para representar destinos entre arquivos. Validação de offsets,
recusa de ausência e limites de memória devem ser declarados como políticas
ODH. Isso não comprova descoberta automática na ordem original nem execução
completa dos scripts comerciais.

## Correções e continuidade da investigação

[hit-native-call-validation.md](hit-native-call-validation.md) descreveu o
valor do hash como offset de bytecode e manteve sua origem como lacuna. Este
rastreio demonstra que o valor **nativo** é ponteiro absoluto derivado de
`buffer HIT + offset HOT`; `entry_pc` portátil pode continuar sendo um offset
quando acompanhado da identidade correta de recurso. O operando de Call7
continua sendo uma chave, conforme a interpretação anterior.

A ausência no hash também deixa de ser somente uma possibilidade: os dois
callers inspecionados fornecem zero para a inserção. O campo `+0x31c` descrito
em [hit-native-controller-execution-evidence.md](hit-native-controller-execution-evidence.md)
é acessado pela indireção `+0xc4`, cujo valor inicial é `self`. A descrição
anterior de `this+0x31c` no construtor permanece correta.

Próximo trace estático: seguir `0x5725c4 → 0x5d8277/0x5d8ab4/0x5d8632`,
a população das raízes consumidas em `0x571753–0x57177d` e o caller
`0x5725ad`. Isso deve determinar ordenação, filtros e inclusão dinâmica dos
arquivos. Completar a análise da coleção `+0xac` e teardown para definir
retenção e descarte dos buffers; para excluir fontes alternativas do mapa,
rastrear todos os stores e aliases de `manager+0x168`, inclusive indiretos.
