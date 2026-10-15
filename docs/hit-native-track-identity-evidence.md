# Identidade e ciclo de TrackID no HIT nativo

Evidência estática de 2026-10-15, sobre a base ODH `a322f42`. O caminho
ordinário de evento tipo 1 usa o controller já catalogado por TrackID e pode
reutilizar seu estado de execução. Ele mantém contribuições de várias fontes
em outra estrutura. Isso não sustenta criar uma thread nativa independente
por pedido, nem deduplicar nativamente apenas pelo nome do evento.

## Fonte e alcance

Imagem externa `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Base PE `0x400000`; nos trechos `.text`/`.rdata` selecionados, offset de
arquivo = VA − `0x400000`. Endereços de globais de runtime não são offsets.
Não foi executado o original, alterado projeto Ghidra ou recurso comercial,
nem estabelecida equivalência com outra imagem executável.

Foram consultados primeiro os exports existentes de `0057348c`, `0057376b`,
`0057d6f8` e `0057dc59` em `reports/ghidra/supplied-functions/`. A confirmação
pontual em assembly, com bytes, VAs, offsets e destinos da vtable
`0x6719b4`, está em `reports/hit-native-track-identity-selected.asm.txt`.
O documento complementa [opcode 69](hit-native-group-kill-evidence.md) e
[controles iniciais](hit-native-controls-evidence.md); não implementa lifecycle.

Uma conferência independente comparou as 1.619 linhas de bytes selecionadas
com a imagem identificada acima: 4.554 bytes conferidos, sem divergência.
O recibo `reports/hit-native-track-identity-verification.json` guarda também
o SHA256 do relatório. Essa checagem confirma a proveniência dos trechos,
sem converter a interpretação estática em validação de execução.

## Três estruturas distintas

| Estrutura | Conteúdo demonstrado | Evidência VA / offset |
|---|---|---|
| `manager+0x80` | Chave numérica → objeto; no caminho Track, chave é TrackID | `0x573466` / `0x173466`; `0x57d0ce` / `0x17d0ce` |
| Serviço de eventos `this+0xd4` | Pares TrackID → fonte; várias fontes por TrackID | `0x57451a` / `0x17451a`; `0x574581` / `0x174581` |
| `manager+0x1a4+4*índice` | TrackID escolhido por um valor de variável do bytecode 69 | `0x57f215` / `0x17f215`, evidência anterior |

O manager vem de `DAT_006f6a78`. O `ESI` do dispatcher `0x57376b` é o
serviço de eventos, não esse manager: `0x573877` usa `lea ecx,[esi+0xd4]`,
enquanto `0x57384f` carrega explicitamente o global para consultar o catálogo.
Nenhuma dessas chaves demonstra uma geração de entidade World ou ownership
do sink PCM. O catálogo também aceita outros tipos de objeto; não tratá-lo
como uma coleção exclusivamente de threads HIT.

## Criação, registro e lookup

O parser de Track aloca `0x324` bytes em `0x57bb02–0x57bb09`, chama o
construtor `0x57d6f8` em `0x57bb32`, inicializa pela vtable +`0x08` e registra
o objeto com o TrackID em `0x57bb52`. O construtor guarda esse ID em
`this+8` (`0x57d77e`) e faz `this+0xc4=this` (`0x57d7b4`). Esse objeto possui
os bancos e o estado de execução usados depois pelo dispatcher.

`0x57d0ce` procura a chave no hash `manager+0x80`. Se existente, chama
vtable +`0x0c` no objeto antigo, depois +`0x00` com argumento 1, e substitui
`node+8` (`0x57d10c–0x57d128`). Se ausente, insere um par chave/valor via
`0x5816d2` e grava o objeto (`0x57d133–0x57d14b`). Ambos os caminhos chamam
+`0x10`, setter do ID, em `0x57d153`. Há um objeto por chave nesse catálogo;
a substituição durante carregamento não é a criação de uma nova instância
por PlaySound. Sua segurança quando o objeto antigo está ativo não foi
estabelecida: essa rotina não executa explicitamente a parada +`0x38`.

`0x573466` apenas consulta o hash através de `0x575b11`, que compara a chave
em `node+4` e retorna o nó existente; o wrapper retorna `node+8` ou zero.
Não há criação no lookup. No evento tipo 1, uma chave ausente encerra esse
caminho (`0x573859–0x573863`). O caminho analisado não aloca uma thread extra
para cada fonte. Isso não prova ausência de outros mecanismos de instâncias
em todos os tipos de evento ou ArgStyles.

## Evento tipo 1: admissão e reutilização

`0x573706–0x573746` resolve o registro de evento: tipo em +`0x10`, TrackID
em +`0x14` e overrides não zero de R1/R2/R3 em +`0x18/+0x1c/+0x20`.
Nomes distintos que chegam ao mesmo TrackID encontram o mesmo controller
nesse caminho. Não existe uma chave de lookup `(nome,fonte)` nesse trecho.

O dispatcher segue esta ordem (`0x573842–0x573920`):

1. Busca o controller. Se inativo, apaga **todas** as contribuições de fontes
   daquele TrackID no serviço +`0xd4`, por `0x5764a8` em `0x57387e`.
2. Chama admissão `0x574bdb`. O gate também se aplica a retrigger ativo.
3. Para ArgStyle 2, exige R1 não zero, insere o par `(TrackID,R1)` se novo,
   agrega as fontes por `0x57488f`, chama inicializador +`0x30` com R1–R3,
   aplica os setters espaciais +`0x40/+0x48` e ativa com +`0x5c`.
4. Para os demais ArgStyles, chama inicializador e ativação diretamente;
   não extrapolar a eles o contrato de fontes do ramo 2.

`0x574bdb` conta a fila de execução (`manager+0x2c/+0x30`, helper `0x574d13`).
Com menos de 16 entradas permite; com 16 ou mais lê ID25 do controller e
recusa se zero. Com prioridade não zero permite; acima de 16 ainda pode
selecionar outro objeto e chamar sua parada gradual +`0x34`. Portanto 16 não
é um limite rígido nem esse helper um mecanismo de criação/deduplicação.
Uma recusa ocorre antes de inserir a nova fonte e antes de escrever R1–R3.

### O inicializador não equivale sempre a um restart

Em `0x57dc59`, um gate de grupo pode retornar cedo. Passado esse gate,
`0x57dca5–0x57dce4` limpa o marcador +`0x1cf` e fade_on (ID45), aponta
`this+0xc8` para o próprio objeto e escreve R1–R3 pelo writer público.
Isso ocorre **antes** de decidir se reinicia:

| Estado | Caminho demonstrado |
|---|---|
| Ativo e pausado | +`0x74` detecta pausa; +`0x5c` retoma sem passar pelos loops de reset (`0x57dcf3–0x57dd0b`). |
| Ativo, não pausado, ID55 (`interrupt`) zero | Retorna sem reset dos bancos/PC (`0x57dd1f–0x57dd2d`), mas os efeitos anteriores já ocorreram. |
| Inativo, ou ativo não pausado com ID55 não zero | Segue a reinicialização a partir de `0x57dd33`. |

A reinicialização copia defaults dos bancos 50–62 e 17–47, reaplica valores
do controller não sentinela, aplica os argumentos conforme ArgStyle, faz
binding de patch, restaura PC/loop, prepara listas e define isplaying46=1 e
ativo. Ao fim chama pausa +`0x58`; o dispatcher depois chama +`0x5c`.

O dispatcher não ramifica conforme o retorno do inicializador. Mesmo no
ramo ativo que conserva PC, ele aplica os novos valores espaciais e ativa.
Ativação `0x57df5c` limpa pausa, atualiza deadline com o Clock corrente e
chama `0x57d310` se ativo. Isso não foi convertido em ticks World ou ms.
R1 pode mudar sem o caminho completo que atualiza `source`47; logo não é
seguro reduzir o controller a um único owner fixo, nem descrever todo
retrigger como chamada sem efeito.

### R1–R3 não são apagados pelos dois loops de reset

O layout comprovado por `0x57da1a–0x57da76` distingue banco de registradores
dos dois bancos copiados pelo inicializador:

| Banco no objeto | Inicialização | IDs | Payload |
|---|---|---|---|
| `this+0x1c` | `0x57da20–0x57da29`: count31/start17 | 17–47 | ponteiro em `this+0x28` |
| `this+0xcc` | `0x57da34–0x57da3a`: count13/start50 | 50–62 | ponteiro em `this+0xd8` |
| `this+0x178` | `0x57da45–0x57da4b`: count16/start1 | 1–16 | ponteiro em `this+0x184` |

O primeiro banco encadeia para o segundo e este para o terceiro via +`0x10`.
O setter `0x5805fa` segue essa cadeia quando o ID está fora do intervalo;
assim as escritas públicas 1,2,3 alcançam o terceiro banco. Os loops de
`0x57dd33–0x57dd80` copiam apenas os dois primeiros, preservando R1–R3.
Não há contradição com o perfil inicial já implementado no ODH.

O banco 1–16 é zerado na alocação por `0x580558`, não novamente nesses
loops: não concluir que R4–R16 voltam a zero a cada retrigger. O getter
`0x5805ba` ainda tem fallback +`0x14` quando encontra `INT32_MIN`; leitura
efetiva não é necessariamente a palavra bruta de metadata. O reset de
50–62 está provado, mas os defaults completos 51–62 não foram rastreados
nesta unidade e continuam fora do contrato do builder atual.

## Fontes e remoção

`0x57451a` faz equal-range por TrackID no serviço +`0xd4` e compara a fonte
em `node+8`. `0x574581` só insere quando esse par ainda não existe. Duas
fontes diferentes coexistem como contribuintes do mesmo controller; repetir
o mesmo par não cria uma segunda entrada. A fonte é o R1 após overrides do
evento, não necessariamente o source original recebido do caller.

`0x57488f` agrega as entradas do TrackID e consulta dados por fonte via
`0x57320a`; ausência produz volume0/pan512. Esta unidade não define a escala
DSP nem reimplementa geometria. `0x57462e` percorre o multimap, apaga cada
entrada cuja fonte coincide e chama `0x5747dc` para o TrackID afetado.
Se não restam contribuições, este chama +`0x34` (parada gradual); se restam,
reagrega e reaplica os setters espaciais por `0x57482d`. Isso remove a fonte
através dos tracks, sem apagar os controllers do catálogo.

Parada imediata +`0x38` (`0x57e037`, evidência anterior) desativa e reseta
estado/áudio, mas não deleta o controller nem remove diretamente sua chave
do catálogo ou a entrada do registry de opcode69. Na próxima chamada tipo1
inativa, o dispatcher limpa as contribuições antigas antes da admissão.
Teardown do manager `0x57ccc8` chama a rotina de parada geral, percorre
objetos do catálogo, chama +`0x0c` e +`0x00(1)`, zera os valores e limpa o
map em `0x57cd6f`. Não demonstramos que importação/reload e remoção de
entidades World usem exatamente essa ordem.

Há contador em `this+0x14`: +`0x78` (`0x57d6db`) inicializa via +`0x08`
quando zero e incrementa; +`0x7c` (`0x57d6f1`) decrementa. Nenhuma dessas
rotinas associa o contador às fontes do multimap ou destrói ao chegar a zero.
Não usá-lo como prova de ownership/refcount por entidade sem seus callers.

## Comparação com Simitone e próxima fronteira

Referências locais fixadas: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`
e seu FreeSO `f86bbd34b2112296ed786741f640a0535886d180`, conforme
[references.md](references.md); o `FreeSO/LICENSE.md` identifica MPL 2.0.
Em `TSOClient/tso.sound/HITVM.cs:159–180`,
`PlaySoundEvent` normaliza o nome e consulta `ActiveEvents` por nome; pode
retornar o som vivo ou preparar interrupção. O ramo TS1 em 207–211 resolve
TrackID/subrotina; 255–280 cria outra `HITThread` quando necessário.
`tso.simantics/Primitives/VMPlaySound.cs:21–55` escolhe owner caller/stack,
procura nome em `owner.SoundThreads` e adiciona owner ao som compartilhado.
Esses são contratos de terceiros e não equivalem automaticamente ao
catálogo TrackID e ao multimap nativos demonstrados aqui.

A evidência sustenta separar catálogo, execução reutilizável e contribuições
de fontes. Ainda **não** sustenta substituir a coleção ODH por um registry
nativo completo. Antes disso, faltam os callers de remoção `0x57462e`, a
política de fontes especiais/reuso de ObjectID e destruição World, invariantes
de registro/reload enquanto ativo, callers do contador +`0x78/+0x7c`, e o
domínio/limpeza do índice usado pelo opcode69. Também faltam os caminhos
eventuais de MultipleInstances/clone fora do evento tipo1 examinado.

No ODH atual, `HitThread` não expõe retrigger e conserva callbacks copiados;
`make_hit_sound` captura um `SoundSourceBinding` específico, enquanto
`PcmHitOwner` limita notas por toda a sua vida. Reutilização futura precisa de
API própria para argumentos/estado e de limites por fase de execução, sem
substituir silenciosamente o owner PCM ainda usado. A geração World usada
para validar binding é um contrato ODH: não demonstra uma identidade nativa.
Remover um contribuinte não autoriza encerrar o áudio compartilhado enquanto
outras fontes continuam registradas. O contrato de remoção e refresh dessas
fontes deve preceder a integração desse manager ao World.

Um futuro contrato de implementação precisa representar explicitamente
admissão recusada, retomada, atualização sem reset e reinicialização, com
remoção de contribuições separada da vida do controller. O owner por pedido
do probe continua sendo política diagnóstica do host. O gate do opcode69 e o
escopo fresh ArgStyle2 permanecem necessários; esta unidade acrescenta
evidência e lacunas verificáveis, sem implementar um lifecycle presumido.
