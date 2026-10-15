# Continuidade atual

Retomada autorizada da meta integral OpenDollhouse. OMM não está disponível
nesta sessão; este checkpoint precisa ser sincronizado com ela quando o
conector voltar. Não declarar OMM atualizada. Ler AGENTS.md, este arquivo,
docs/architecture.md e as evidências recentes antes de continuar.

Base anterior:cae292fe12a86543fbead39c133e2b9e37c24021 (áudio no loop BHAV).
Novo checkpoint: HOT overrides signed e derivação bruta caller/evento;
identificar hash com git log. Evidências em hit-caller-resolution-validation.md,
hit-native-caller-arguments.md e novo hit-native-controls-evidence.md.
Core/parser Sol alto, revisão Luna alto, pesquisa estática difícil Astra alto;
principal integrou probe/testes/receipts/docs. OpenCode/IA local removidos;
usuário quer delegação por complexidade. OMM indisponível, não declarar sync.

HotEvent.argument_overrides (apósreason) guarda CSV2/3/4 como optional<int32>.
Omitido final/zero explícito são0conhecido; vazio/símbolo/overflow sãoausentes
com reason, não zero adivinhado. Sextocampo sem interpretação. Contagens e
budgets preservados; metadatafixa extra não éRSS. resolve_hit_arguments aplica
cada override nãozero à sua posição; play_sound_arguments usa [source16,0,0].
Não implementa wrappercache-2, ArgStyle ou DSP.

--audio-caller-args é alternativa mutuamenteexclusiva a --audio-args R1 R2 R3.
Modo ainda opt-in, com Jump/chooser/Q15 explícitos, sem gainespacial automático.
Factory recebe request.source selecionado pelo BHAV e validado pelo binding;
resolução bounded do event precede copia/lookup. JSON profile.argument_mode
play_sound_caller,argsnull; cada sound_request.hit_arguments guarda tripla da
submissão ou nullse não chegou àresolução. Sem stale em recursofaltante.
OverrideR1 não transfereowner/binding; queryde sourcefora do perfil falha, semalias.
O caminho silencioso não muda. PcmHitOwner/transporte compartilhado mantêm
lifetime/refusals/cleanup. Opcode69 continua unsupported semhandler, semfakegroup.

Testes finais:112 configurados,111pass/1X11skip Debug e ASan/UBSan, LSan off.
75checks recursos,11resolvedor + CLIcom caller/stacksource, signedoverrides,
origem sobrescrita/guard, unresolved, semstale e exclusãoopções. CLI repetido
nos doisbuilds após ampliar o caso de overrideR1. SemSDL/X11 passam4checks
registrados recursos/argumentos/CLI silencioso/recusaáudio. Logs
reports/hit-caller-{build,build-asan}-ctest.log, cli{,-asan}-final.log e noaudio-*.
Cena nativa cadeira usa --audio-caller-args (semtriplamanual), deriva[2,0,0]
de chair_sit_metal/source2 e HOTzerooverrides. Debug/ASAN:30208frames consumidos,
94dispatches/662ticks, semfault. PCMdrained/HITpending explícito, semticksfake.
Controles unidade/centro, Literal32 e escolha0 continuamdiagnósticos.
Recibos/comando em reports/hit-caller-chair-*.json.

Pesquisa nova: HSM IDs17priority/18vol/19extvol/20pan/21pitch; ID16h4 nãoéArgStyle.
TrackVolume->ID18. Defaults32/1024/1024/512/3600 e controllerunset0x80000000.
ArgStyle2 instala rawargs+defaults/controllers, guardaR2/R3 em19/20, pausa;
caller sobrescreve19/20 com dadosespaciais e sóentão ativa/enfileira. Primeira
instrução deveria enxergar controles póssetters, R2/R3raw continuam0. Issoainda
NÃO estáimplementado automaticamente. Composição observada extvol*group/1024,
vol*external/1024, duckhalfcondicional (IMUL32/IDIV), semprovaescalaQ15.
VAs/identidades/limites/provider/visão em hit-native-controls-evidence.md.

Próximo: inicialização raw de controles antes do primeirotick, com controller/
defaults e recibos; resolver contexto espacial, sharedprovider e escala backend
semconfundir rawargs comDSP. Depois grupos/owners/dedup/relógiogeral do lote,
loops/CallReturn e campos4/13/14. Não alias source12, não bypassMain ou reInit.
Diagnóstico de drain ainda só bombeia PCM, não HIT/World; JSON separa
pcm_drained_hit_pending de complete/pending_output/faulted/drain_timeout.
Savedframes105/137/queues/clock ainda dormentes, sem reInit; câmera/picking,
input/lote mínimo, SLOT8/failuretrees/needs/inventory/save, SDK/CC e ports
continuam na meta integral. Linux primeiro, Simitone/submoduleTS1 principal.

Originais externos e refs/reports/builds preservados. Artefatos de build na
raiz e antigos runners não versionados continuam sem integração. Mixer antigo
inválido preservado em reports/mixer-before-takeover; substituído pelo core
validado, não é referência de arquitetura;
não git add -A, não cleanup genérico. Checkout/remote devem ser revalidados.

Quando OMM voltar: get_handoff no scope OpenDollhouse, mesclar novo checkpoint
com fontes existentes preservando questões/história e usar expected_sha256
atual. Histórico anterior está na OMM e no handoff datado de2026-10-14,
que é um snapshot anterior e não substitui este estado mais recente.
