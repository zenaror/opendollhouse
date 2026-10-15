# Continuidade atual

A meta integral OpenDollhouse permanece autorizada, com Linux funcional e
verificável primeiro; New 3DS, Vita e Android seguem como restrições e etapas
futuras, sem validação funcional nesta fase. Leia `AGENTS.md`,
`docs/architecture.md` e as evidências ligadas abaixo antes da próxima unidade.
OMM está indisponível nesta sessão: este checkpoint ainda precisa ser mesclado
com o handoff do escopo `OpenDollhouse` quando o conector voltar, usando o
`expected_sha256` atual. Não declarar OMM sincronizada.

## Checkpoint de 2026-10-15

Base publicada `a322f42` (gate de opcode 69 com `initial_controls`), sobre
`2604e86` (inicialização opcional ArgStyle 2 de controles HIT antes do primeiro
tick). A unidade atual é pesquisa estática da identidade/ciclo de TrackID,
sem mudanças em código ou testes. A evidência está em
[hit-native-track-identity-evidence.md](hit-native-track-identity-evidence.md).
O caminho silencioso e o perfil legado continuam preservados.
Revisão independente e conferência de 4.554 bytes em 1.619 linhas do assembly
selecionado passaram. Recibo em
`reports/hit-native-track-identity-verification.json`; validação de proveniência
estática, sem novo build ou teste de runtime nesta unidade.

`HotEvent.argument_overrides` mantém CSV 2/3/4 assinados; resolução por palavra
substitui só overrides não zero. `play_sound_arguments` usa a origem int16
resolvida pelo caller, formando `[source,0,0]`; não usa a antiga tripla manual
`[source,1024,0]`. O modo de probe `--audio-caller-args` é explícito e separado
em [hit-caller-resolution-validation.md](hit-caller-resolution-validation.md)
e [hit-native-caller-arguments.md](hit-native-caller-arguments.md).

O novo `build_native_hit_controls` recebe Track ArgStyle 2 resolvido, tripla
bruta e extvol/pan espaciais explícitos. Inicializa o banco comprovado 16–47,
Track.volume 18, ducking_priority 25 e patch bruto 50. `INT32_MIN` em volume ou
duckpri preserva o default do banco; não se extrapola essa regra a patch 50.
Depois instala R2/R3 em 19/20, R1 em 47, isplaying 46=1 e os controles
espaciais finais em 19/20. O último setter de pan determina zero/sign antes do
primeiro bytecode. O recibo próprio e a máscara de valores desconhecidos são
validados antes do chooser. Escrita nativa em 18 altera volume, não patch;
escrita em 50 conserva rebinding. Sem perfil opcional, o alias legado 18/50 e
flags iniciais falsos permanecem. Contrato, prova estática e limites em
[hit-native-controls-validation.md](hit-native-controls-validation.md) e
[hit-native-controls-evidence.md](hit-native-controls-evidence.md).

No perfil com `initial_controls`, opcode 69 agora consome o byte de operando e
falha antes de invocar o callback legado ou registrar um efeito. O operando
truncado ainda produz erro de bytecode; `HitThread` fixa a falha, sem retry.
Sem esse perfil, callback e trace legados permanecem. Esta recusa temporária
evita atribuir ao byte a semântica FreeSO de grupo no perfil nativo; **não**
implementa o opcode do jogo. A [pesquisa estática nativa](hit-native-group-kill-evidence.md)
aponta localização de variável, registry de controllers e caminhos de
fade/retry/Clock, que exigem contrato próprio antes de qualquer efeito.

A camada de áudio do probe continua opcional: uma fila/sink PCM compartilhada,
thread e owner por requisição, sem fechar o dispositivo por nota. HIT e PCM
avançam em polls explícitos nas fronteiras do probe; o drain final só bombeia
PCM e nunca simula ticks extras de World/Main. Não há conversão automática dos
controles raw em Q15, nem derivação geométrica, grupos globais ou provider
compartilhado. Os dados do jogo e o executable original permanecem externos;
nenhum original foi modificado.

## Validação e limites do resultado

Debug e ASan/UBSan oficiais configuraram 113 testes: 112 passaram, um teste
X11 foi pulado pelo ambiente, zero falhas (14,42 s e 26,33 s). LSan ficou
desativado. A configuração sem SDL/X11 passou 7/7 testes selecionados.
Core direto com warnings-as-errors passou 70 checks de controles, 944 de
seleção e 61 de thread; o factory possui 52 checks no build integrado.
Logs `reports/hit-native-controls-build-{configure,build,ctest}.log`,
`reports/hit-native-controls-build-asan-{configure,build,ctest}.log` e
`reports/hit-native-controls-build-noaudio-{configure,build,ctest}.log`.
Essas contagens oficiais pertencem à base `2604e86`. Para o gate posterior,
testes diretos C++20 com warnings-as-errors e ASan/UBSan passaram 949 checks
de seleção e 68 de thread; LSan ficou desligado sob ptrace. `git diff --check`
passou. Os builds oficiais do gate configuraram 113 testes: Debug passou 112,
com um skip X11 e zero falhas (12,90 s); ASan/UBSan teve os mesmos 112 passes,
um skip e zero falhas (25,56 s). Sem SDL/X11, 7/7 testes selecionados passaram
(0,63 s). Logs em `reports/hit-native-group-gate-build-{build,ctest}.log`,
`reports/hit-native-group-gate-build-asan-{build,ctest}.log` e
`reports/hit-native-group-gate-build-noaudio-{build,ctest}.log`.

Na fixture Linux `chair_sit_metal`, `--audio-caller-args` produziu `[2,0,0]`.
`--audio-native-controls 1024 512` mostrou IDs 17=32, 18=1024, 19=1024,
20=512, 21=3600, 25=10, 46=1, 47=2, 50=0 em Debug e ASan. Ambos registraram
94 dispatches BHAV, 662 ticks World, 664 polls de áudio e 30.208 frames PCM
renderizados/submetidos/consumidos, sem fault. O estado é
`pcm_drained_hit_pending`: PCM drenado e HIT ainda ativo; não é áudio físico
comprovado nem conclusão da thread. A comparação retida marca VM/World/trace
iguais ao baseline do caller, excluindo o relatório de áudio. Ver
`reports/hit-native-controls-chair-{debug,asan,comparison,command}.json` e
[hit-native-controls-validation.md](hit-native-controls-validation.md).
Esses resultados de fixture/host Linux não provam equivalência ao executável
original ou funcionamento dos ports.

O probe da mesma cadeira foi repetido em Debug e ASan com o comando exato da
base `2604e86`: recibos, requisições, estado e trace VM/World permaneceram
idênticos ao checkpoint anterior. Em ambos, foram 662 ticks World, 664 polls,
30.208 frames PCM consumidos e `pcm_drained_hit_pending`. A comparação está em
`reports/hit-native-group-gate-chair-comparison.json`; comando e recibos em
`reports/hit-native-group-gate-chair-{command,debug,asan}.json`. Essa fixture
não exerce opcode 69 nativo; o gate continua uma recusa explícita temporária,
não uma implementação da operação nativa.

## Próximas unidades

Pesquisa estática confirmou que o parser HOT cria e registra um controller
por TrackID; evento tipo 1 consulta/reutiliza esse objeto, sem alocar no lookup.
Um multimap separado mantém fontes `(TrackID, R1)` e agrega seus valores. A
admissão pode recusar antes de atualizar; o initializer pode escrever R1–R3
antes de escolher retomar, manter PC/bancos ou reinicializar. Os loops de reset
observados cobrem apenas IDs 17–47 e 50–62, preservando R1–R3 em 1–3. Remover
fontes não apaga o controller. Endereços, limites e lacunas estão em
[identidade/ciclo de TrackID](hit-native-track-identity-evidence.md), em conjunto
com a [pesquisa do opcode 69](hit-native-group-kill-evidence.md).

A unidade permanece estática. Antes de implementar, fechar os callers de
remoção de fontes, política para IDs especiais/reuso de ObjectID e destruição
World, registro/reload quando ativo, contador do controller e domínio/limpeza do
índice do opcode 69, além de teardown/restart e pausas. O desenho futuro requer
manager persistente indexado por HOTTrackID, separado da geração de World e do
binding PCM. Não introduzir registry de ticket por admissão nem deduplicação por
nome. Não conectar `kill_group(uint8_t)` nem substituir o método nativo por
cancelamento de notas: parar PCM não reproduz reset do controller/binding e
notificação ao manager. No Simitone pinado
(`4906db3`; submódulo FreeSO `f86bbd3`),
`TSOClient/tso.sound/HITInterpreter.cs:651-663` só trata operando zero com
`thread.KillVocals()`; outros IDs são um TODO de ObjectID, não um registro
funcional. `HITThread.cs:183-193` para notas da própria thread.
`HITVM.cs:159-180,207-211,254-280` faz reutilização global por evento no
ramo TS1; `tso.simantics/Primitives/VMPlaySound.cs:21-55` deduplica por nome no
owner, enquanto ODH ainda cria uma thread por submissão. `HITVM.cs:112-126`,
`HITThread.cs:98-102` e `Client/Simitone/Simitone.Client/SimitoneGame.cs:286-301`
mostram um domínio/ordem de ticks de áudio que não se deve confundir com ticks
World ou host drain. Tudo isso é comportamento da referência, não prova da
implementação nativa. O callback `kill_group(uint8_t)` de ODH segue explícito
apenas no perfil legado; no perfil com controles nativos, opcode 69 falha
mesmo se houver handler.

Investigar ainda os providers espaciais, agregação, grupo/ducking e escala real
do backend antes de converter vol/extvol/pan raw em DSP. A evidência estática
conhecida e suas lacunas estão em
[hit-native-controls-evidence.md](hit-native-controls-evidence.md). Outros
marcos da meta integral continuam: saved frames/queues/clock (incluindo
105/137), continuação Main sem reinit, câmera/picking, input e lote Linux,
SLOT/failure trees/needs/inventory/save, SDK/CC e depois os ports. Não usar
áudio para contornar gates de VM ou descartar simulação fora da tela.

Preservar originais externos, refs pinadas, reports, builds e artefatos antigos.
Não usar `git add -A` nem limpeza genérica. Verificar checkout/remote antes
de publicar. Quando OMM retornar, mesclar este checkpoint com a história lá
preservada, sem substituir evidência mais antiga por resultados de fixture.
