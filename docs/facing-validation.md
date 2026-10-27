# Segundo snap e retorno da rotina — 2026-10-13

O BHAV selecionado da cadeira agora retorna true no mundo ODH construído.
O avatar permanece no slot e alinha sua direção à cadeira; o loop sentado
permanece ativo. Esse resultado não é validação de um jogo Linux funcional,
thread principal do objeto, ação contínua de sentar/levantar ou original.

## Contrato do snap

Foram conferidos VMSnap, VMSlotParser e SLOT no FreeSO fixo. Contenção é
selecionada pelo `SnapTargetSlot` do SLOT; o bit4 dos operandos se chama
FootprintExtension, não contenção. O handler de referência consultado não usa
esse bit para decidir PlaceInSlot. Portanto flags0 e4 são aceitos neste perfil
Literal/contido; modos, outros flags e tail fora do perfil continuam recusados.

O segundo recurso é SLOT128, tipo3 índice6, record7, v10, offsetzero,
flags4096 (SnapToDirection), SnapTargetSlot0, height0, Facing-2, Resolution16.
VMSlotParser com SnapToDirection e sem compass bits adiciona NORTH quando
Facing é negativo, resultando na direção do objeto. Facing0..7 codifica um
offset relativo; -3/-2/-1 usam esse default. Esse caso não significa que
FaceTowardsObject sempre olha para o objeto: aqui o caminho SnapToDirection
precede a lógica geral de facing.

Snap ainda valida owner/generation, SLOT privado ligado ao OBJD, zero offsets,
storageheight7, nível igual e ocupação. Para flags4096, calcula a direção em
oito passos sem trigonometria. `World::place_in_slot` recebeu direção opcional;
valida-a antes de alterar relação/posição. Re-snap do mesmo ocupante é
idempotente e pode orientar um avatar já contido. Filhos mantêm sua própria
direção; posição física/contenção e rejeição de ciclos permanecem atômicas.
Flags0 de SLOT preservam a orientação anterior.

Não foi implementado o parser geométrico geral: offsets, transferências,
facing arbitrário, disponibilidade com raycast/rooms, shoo, footprints,
snaps livres e multitile continuam pendentes. O perfil não substitui esses
efeitos por sucesso automático.

## Notificação da thread do objeto

Opcode49 decimal é VMNotifyOutOfIdle. A referência, se houver StackObject com
Thread, reschedule interrupt e marca Interrupttrue; sempre retorna true,
inclusive com objeto/thread ausente. Operand bytes não são usados.

O adapter ODH fornece um sink para uma thread realmente registrada, retornando
true apenas quando ela foi interrompida/reschedulada. Sem backend registra
`no_thread`; objeto ausente registra `missing_object`; sink aceito registra
`notified`. O retorno normal do BHAV é true em todos esses casos, conforme o
handler consultado. O CLI ainda não tem scheduler/main thread da cadeira e
registra essa ausência, sem inventar uma notificação executada.

## Validação

CTest19/19 passou em Debug e ASan/UBSan. Snap/prefix tem97 checks sintéticos:
flags0/4, facing relativo/defaults/wrap, re-snap, descendentes, erros de
facing/direção, ocupação e falhas sem mutação. Notificação tem11 checks com
backend sintético, incluindo alvo ausente, sem thread, aceitação, operands
ignorados e retorno síncrono. CLI integra snap+notificação com fixtures próprias.
LSan continuou desativado pela limitação ptrace; não há prova de vazamentos.

Probe real usa o mesmo comando de [reset-validation.md](reset-validation.md),
incluindo `--post-ticks 240`. Originais externos são abertos apenas em leitura;
identidades de IFF/Global/BCFs permanecem nos checkpoints anteriores.

Resultado: saída0, status `returned_true`,29 instruções, stackdepth0,
169 ticks de execução +240 ticks de controlador =409. Última instrução:
BHAV1004 PC2 opcode49. Snap anterior preservava direção2; o segundo muda
para0, direção da cadeira. Posição final(64,64,1,direção0), recipiente2/slot0,
postura1 e33 movimentos. A notificação é `{entity:2,status:no_thread}`;
som metal continua `skipped_no_backend`. Loop `a2o-sit-loop` segue em
frame55.9998131 de116, looptrue/endfalse/fila vazia.

Saídas Debug/sanitizers JSON idênticas byte a byte, ambas saída0, stderr vazio.
Relatórios locais ignorados registro arquivado não distribuído e versão sanitizada.
Nenhum executável original foi executado, áudio reproduzido ou skeleton
renderizado. O returntrue prova somente este percurso BHAV no cenário ODH
construído, com políticas de rota/velocidade já declaradas provisórias.

## Referências e continuidade

FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2.0 declarada:
[VMSnap.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMSnap.cs),
[VMSlotParser.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMSlotParser.cs),
[SLOT.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/SLOT.cs),
[VMNotifyOutOfIdle.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMNotifyOutOfIdle.cs),
[VMContext.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/VMContext.cs).
Não se afirma equivalência original ou incorporação do runtime C#.

Continuar pela cadeia de levantar e pela thread principal/queue do objeto,
preservando mundo e animações entre ações. Seguem pendentes skeleton,
interpolação/renderização Linux, áudio/HIT, lote/tempo/necessidades/persistência,
comparação original e budgets reais de consoles. Meta completa permanece ativa.
