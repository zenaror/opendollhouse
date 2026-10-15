# Lifecycle do controller e áudio corrente no HIT nativo

Evidência estática de 2026-10-15, base ODH `ae5b19b`. End12 não equivale a
esperar a última nota e encerrar: com pilha de retorno vazia, solicita parada
gradual; se há áudio ativo, o bytecode pode continuar no mesmo poll.
WaitSamp11 consulta o objeto de áudio ligado ao controller. NoteOn2 não
armazena um handle na variável indicada pelo operando. Essas diferenças
precisam preceder a integração de um manager persistente ao host.

## Fonte e alcance

Imagem externa `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Base PE `0x400000`; nos trechos selecionados, offset de arquivo = VA −
`0x400000`. Os endereços `0x6f....` são globais de runtime. Foram lidos os
exports existentes `0057e3dc`, `0057dac6`, `0057dc59`, `0057df06` e os
documentos de [identidade](hit-native-track-identity-evidence.md),
[parada por opcode69](hit-native-group-kill-evidence.md) e
[retrigger](hit-native-retrigger-evidence.md), com confirmação em assembly.

`reports/hit-native-controller-lifecycle-selected.asm.txt` guarda bytes,
VAs, offsets, entradas da jump table e slots de vtable. Nenhum original foi
executado ou alterado; nenhum projeto Ghidra foi modificado. A leitura da
OMM confirmou o checkpoint, sem substituir os bytes como fonte da análise.

## Slots e identidade dos objetos

Nas vtables de controller `0x6719b4` e `0x671a74`:

| Slot | Destino VA / offset | Efeito demonstrado |
|---|---|---|
| +1c | `57dac6` / `17dac6` | Poll da execução/fade/deadlines |
| +30 | `57dc59` / `17dc59` | Atualização, retomada ou reinicialização |
| +34 | `57df9c` / `17df9c` | Solicitação de parada gradual ou imediata |
| +38 | `57e037` / `17e037` | Desativação e descarte do binding |
| +58 | `57df27` / `17df27` | Pausa e remoção da fila |
| +5c | `57df5c` / `17df5c` | Retomada/ativação e inserção na fila |
| +70 | `57d860` / `17d860` | Leitura do byte ativo +1c4 |
| +74 | `57d867` / `17d867` | Teste do DWORD de pausa +1c8 |

O controller guarda a referência de patch em +bc e o objeto de áudio corrente
em +1d0. O objeto concreto examinado tem vtable `0x671c84`, criada por
`0x5800bf`. Ele guarda sua referência de patch em +bc, obtém um objeto de
backend em +c0 via patch+98, e opcionalmente outro ponteiro em +c4 conforme
ID26 (`0x580229–0x580276`). Esses offsets pertencem a objetos diferentes.
Não são IDs de notas nem posições da coleção de handles PCM do ODH.

## NoteOn2, WaitSamp11 e End12

A jump table em `0x57f708` é indexada por opcode−1. Opcode2 aponta para
`0x57e478`, 11 para `0x57e761`, e **8 e12 para o mesmo `0x57e6e7`**.
Opcode27 aponta diretamente para `0x57f0c1`, a solicitação +34.

### NoteOn2 usa o binding existente

`0x57e478–0x57e486` lê um byte literal e passa esse valor a `0x57f88e`.
Não há writer de variável para o operando nem store de handle em registrador.
O helper exige +1d0 e +bc presentes; chama áudio+94 e devolve AL.
Pode propagar ID38 e reaplicar controles quando ID38 da patch exige isso.

Na vtable de áudio, +94 aponta para `0x58038e`. O trecho operacional em
`0x58045d–0x5804a7` obtém um valor por backend+c0/+74, chama esse backend
em +98 com argumento zero, consulta ID44 da patch e chama backend+20 com
o valor obtido, um booleano de ID44 e 1. O corpo não lê o argumento recebido
do opcode e não aloca um wrapper nem guarda uma nova referência por nota.
Não atribuímos nome semântico ao **slot +98 do backend** apenas pela posição.

Logo, no nível HIT demonstrado, cada NoteOn atua sobre o binding corrente.
Ainda não foi provado se backend+20 reinicia uma voz, pode criar vozes
internas ou compartilha buffers. A ausência de handles no wrapper não basta
para declarar monofonia global nem para implementar `NoteOn=criar handle`.

### WaitSamp consulta o áudio corrente e pode continuar imediatamente

`0x57e761–0x57e77e` consulta +1d0. Se nulo, continua. Se presente, chama
áudio+70; falso também continua no mesmo poll. Verdadeiro decrementa PC em
um byte, mantendo o opcode11 para retry, e escreve `manager.Clock+2` em
deadline+198 por `0x57e9b6–0x57e9c6`.

Áudio+70 é `0x5804f3`: prefere +c4 quando presente, senão +c0; chama
slot+24 desse objeto, ou retorna falso se ambos nulos. Não consulta histórico
de NoteOn, registrador de handle ou última nota. Não altera ativo/isplaying.
A unidade não traduz Clock+2 em milissegundos, frames PCM ou World ticks.

### End e Return compartilham a pilha; pilha vazia solicita +34

`0x57e6e7–0x57e721` verifica a lista +1a8. Com entrada, lê seu valor,
remove a entrada e restaura PC; não solicita parada. Call7 grava o endereço
de retorno nessa mesma lista (`0x57e6bf–0x57e6e2`).
Com lista vazia, o handler chama controller+34 em `0x57f0c1–0x57f0c8`.
Não estabelece um estado separado equivalente a `ended` do ODH.

`0x57df9c` tem três resultados:

| Condição | Resultado |
|---|---|
| Controller já inativo | Retorna sem iniciar fade nem chamar +38 |
| Controller ativo, áudio ausente ou áudio+70 falso | Chama +38 imediatamente |
| Controller ativo e áudio+70 verdadeiro | Escreve 40=0,41=19,42=100; liga byte+1cf; escreve45=1 |

As quatro escritas do último ramo passam pelo writer público; a última deixa
zero=false, positive=true, sign=false no provider próprio. O writer45 em
`0x57e352` só limpa45 se o controller não estiver ativo; não altera deadline.
No ramo fade, +34 não escreve PC/deadline, não desativa o controller e não
apaga binding, isplaying46 ou pilha.

Após o handler, `0x57dbfe–0x57dc20` ignora AL e testa novamente o deadline
já vencido. Portanto **End com fade pode executar o byte seguinte no mesmo
poll**, inclusive outro NoteOn, se o bytecode assim dispuser. Não há aqui
yield implícito, espera pela última nota ou espera automática pelo fade.
Isso é consequência do fluxo estático, não um teste executado no original.

## Quando o controller fica inativo

No início de cada poll elegível, `0x57dac6` avança o fade quando45!=0.
O alvo é40, a variável indicada por41 e o passo42. Ajusta o sinal do passo
na direção do alvo, soma e limita ao ultrapassar; igualdade exata não é
ultrapassagem. Zera45 quando o passo é zero ou há ultrapassagem e faz a
escrita pública da variável. Para End, a variável é19 e o passo tem magnitude
100, sem unidade DSP ou temporal inferida.

Em seguida, `0x57db67–0x57db86`, se byte+1cf está ligado e45==0, chama
+38 e sai do poll. Essa é a desativação posterior demonstrada para End com
fade. A conclusão física de áudio, isoladamente, não desativa o controller
por esse poll: a consulta de áudio+70 ocorre nos handlers descritos, não
como uma regra geral de remoção do controller. Callbacks internos ainda não
foram rastreados para provar uma afirmação global sobre todos os backends.

Stop+38 (`0x57e037–0x57e0dc`) executa:

1. Ativo+1c4=0; se há áudio, chama áudio+98(0).
2. Chama binding `0x57f91e(0)`. Este chama áudio+98(0) quando pertinente,
   patch+7c, áudio+0c e destrutor+00(1); limpa +bc/+1d0 e escreve ID50=0.
3. Zera45/+1cf e escreve46=0 pelo writer local+94. No provider próprio,
   essa última escrita deixa zero=true, positive=false, sign=false.
4. Zera provider+c8, restaura PC ao entry, zera deadline+198, remove da fila,
   limpa a pilha +1a8 e remove a contribuição de ducking.

O corpo não destrói o controller nem apaga sua chave do catálogo TrackID.
Não zera o banco R1–R16, nem refaz os bancos17–47/50–62. Não há store direto
de zero em pausa+1c8, loop-PC+1a0 ou deadline de NoteOn temporizado+194 nessa
rotina. Não substituir essas constatações por um reset de todo o objeto.
Os callbacks virtuais de áudio/backend ainda limitam qualquer alegação sobre
quantas vozes ou buffers físicos deixam de existir.

## Pausa, restart e persistência

Pausa+58 escreve +1c8=1. Se ativo, chama áudio+58 quando presente e remove
o controller da fila; preserva PC, bancos, binding e deadlines nesse corpo.
Retomada+5c escreve +1c8=0; se ativo, chama áudio+5c, **substitui deadline+198
pelo Clock corrente** e reinsere na fila. Não restaura o tempo restante de
um Wait anterior. Os wrappers de áudio+58/+5c (`0x580518/0x580538`) obtêm
patch+98 e chamam slots+2c/+30 do objeto retornado; não fazem novo binding.

O initializer+30 escreve R1–R3, limpa45/+1cf e restaura provider próprio
antes de decidir o caminho, conforme a evidência anterior. Ativo pausado
retoma sem reset, inclusive antes de consultar55. Ativo não pausado com55=0
mantém a execução. Inativo ou ativo não pausado com55!=0 reinicializa:

- Copia defaults17–47/50–62 e overlays; o banco1–16 é separado. R1–R3 já
  escritos permanecem e **R4–R16 preservam seus valores anteriores**.
- Chama `0x57f91e` com ID50 efetivo. Mesmo que seja a mesma patch, o helper
  libera o binding anterior antes de resolver/criar o novo wrapper. Não há
  teste de igualdade que preserve aquele objeto.
- Restaura PC e loop-PC; limpa listas+1b0/+1b4 e restaura seletor+1c0;
  escreve deadline+198=Clock,46=1,ativo=1 e chama pausa+58.
  O caller tipo1 aplica controles espaciais e chama+5c depois.

**Restart ativo não equivale a stop+start:** o initializer não chama+38 e
não limpa a pilha de retorno+1a8; suas duas chamadas `0x602b9f` recebem
+1b0 e+1b4, comprovadas em `0x57de7e–0x57dea0`. O corpo de `0x602b9f`
limpa apenas a lista recebida. A pilha+1a8 é limpa por stop, não por esse
trecho de restart. Também não há store direto em +194 durante o initializer.
Esse campo nasce zero no construtor (`0x57d9af`), recebe Clock+duração no
opcode1 (`0x57e470`) e é consumido/zerado pelo poll (`0x57dc22–0x57dc44`).
Não inventar reset de +194, ou continuidade de notas antigas, no novo start.

## Clock e próxima unidade integrável

O manager `0x57d230` só entra no poll com flag+60 ligada. Incrementa Clock+58
se flag+54 ligada, copia a fila e chama cada slot+1c. O poll do controller
exige ativo e não pausado, avança fade antes do bytecode e executa enquanto
deadline não zero e unsigned deadline<=Clock. Ativação em
`0x57df7e–0x57df87` copia Clock literalmente, sem +1 ou mínimo1. Se Clock
for0 na ativação, deadline fica0: subir Clock depois não basta para habilitar
esse bytecode; fade ainda pode ser processado. Isso é uma consequência
condicional, sem prova de que o startup/caller real permita tal estado.
A frequência dos callers do manager não foi fechada. Limites de instruções no ODH são política de
segurança; o original chama um diagnóstico após o contador ultrapassar100,
e o ramo mostrado retorna ao teste, não estabelece um yield por100.

Simitone `4906db3`/FreeSO submódulo `f86bbd3` (MPL2.0) continuam referência
comparativa, não autoridade nativa. `HITInterpreter.cs` usa LastNote em
WaitSamp e retorna HALT mesmo quando inativo; End retorna KILL com stack
vazio. O ODH em `ae5b19b`, `src/hit_selection.cpp`, igualmente consulta sua
última handle e marca `ended` no12 antes de aguardar a conclusão. O perfil
de controles iniciais não muda esses contratos por si só.

A próxima implementação coerente precisa unir banco persistente, segmento
de execução e **binding de áudio corrente** no caminho realmente usado pelo
host. Sua interface deve representar consulta de atividade do binding,
pedido de NoteOn sobre ele, substituição/teardown e pausa/retomada; deve
separar continuar no mesmo poll, retry por deadline e stop. O scheduler
precisa receber um Clock explicitamente definido, conservar R4–R16 e tratar
fade/stop antes da execução; não usar LastNote como substituto desse contrato.

Antes de prometer backend nativo completo faltam o corpo concreto dos slots
de backend+20/+24/+74/+98, multiplicidade de vozes/compartilhamento,
callbacks de término, cadência externa do Clock e conversão dos controles
raw para o áudio. Um adapter diagnóstico pode declarar essas políticas;
não deve apresentá-las como comportamento nativo comprovado. Construir o
manager sobre End/drain e handles atuais consolidaria uma divergência já
demonstrada. Esta unidade não altera código, testes ou recursos comerciais.
