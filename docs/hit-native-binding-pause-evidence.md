# Dispatch do binding de áudio: pausa, retomada e NoteOn

Investigação estática de 2026-10-17, imagem externa
`../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Nenhum executável original foi executado ou modificado. Complementa a evidência
de [lifecycle](hit-native-controller-lifecycle-evidence.md).

## Caminho concreto observado

A vtable da patch `0x671b2c`, slot `+0x98`, contém `91 80 5f 00` em
VA `0x671bc4` e aponta a `0x5f8091`. Esse getter lê `patch+0xe0` e retorna.
O initializer `0x57fe8f` cria o objeto de backend pelo slot `+0x70` do engine
quando ID26 não é zero, ou pelo slot `+0x5c` quando é zero. No primeiro ramo,
`patch+0xe4` e `patch+0xe0` recebem o mesmo ponteiro; no segundo, `+0xe4`
fica nulo. Não foram excluídas alterações posteriores desses campos.

O initializer do wrapper, `0x580229`, copia o retorno de patch `+0x98` para
`wrapper+0xc0`; pode também copiar `patch+0xe4` para `wrapper+0xc4`.
Os caminhos de pausa/retomada `0x580518`/`0x580538` consultam novamente
patch `+0x98` e chamam os slots `+0x2c`/`+0x30` do objeto retornado,
sem substituir o binding. Os campos e as chamadas não demonstram o que
acontece com PCM que já tenha sido enviado ao dispositivo.

NoteOn2 passa seu byte literal pelo handler `0x57e478` e wrapper `+0x94`.
O corpo concreto `0x58038e` não lê esse argumento. Ele consulta backend
`+0x74`, chama `+0x98(0)` e depois `+0x20`, usando também o valor de ID44
da patch. Isso não justifica transformar o literal em índice de handle.
O status em `0x5804f3` prefere `wrapper+0xc4`, depois `+0xc0`, e chama
backend `+0x24`. A preferência permanece relevante mesmo que esses campos
nasçam iguais ou um deles nulo nos ramos observados.

O controller retira/insere a si mesmo na fila do manager durante pausa e
retomada. Essa é uma fila de agendamento HIT; não foi identificada como fila
de amostras PCM. Confundir as duas produziria uma política de áudio inventada.

## Evidência preservada e próximos destinos

`reports/hit-native-binding-pause/selected.asm.txt` contém bytes, VAs,
offsets e entradas de vtables; `findings.txt` registra a cadeia completa.
Uma auditoria independente comparou 263 linhas de instrução e os slots
impressos com os bytes da imagem, calculando offsets pelo cabeçalho PE.
O recibo está em `reports/hit-native-entry-map/root-static-audit.json`.
Nos trechos escolhidos `.text`/`.rdata`, offset = VA − `0x400000` para esta
imagem; essa igualdade não é uma regra geral para arquivos PE.

Faltam identificar as vtables dos objetos retornados pelo engine e os corpos
de backend `+0x20`, `+0x24`, `+0x2c`, `+0x30`, `+0x74`, `+0x98`, seus
callbacks e efeitos sobre buffers/vozes. A entrada seguinte é o engine
instalado no manager por `0x57ca49`, via provider `0x6fe8bc+0x14`.

`PcmTransport` ainda não oferece pausa por binding/nota. `PcmSink::pause`
afeta o sink compartilhado, portanto encaminhar essa chamada diretamente
pausaria fontes não relacionadas. Os bytes acima não provam monofonia,
quantidade de vozes, escala raw→Q15, silêncio físico ou equivalência com o
backend PCM ODH. Essas lacunas continuam impedindo uma alegação de adapter
nativo completo, sem impedir o avanço dos demais contratos do runtime.
