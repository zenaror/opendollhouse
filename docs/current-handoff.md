# Continuidade atual

Retomada autorizada da meta integral OpenDollhouse. OMM não está disponível
nesta sessão; este checkpoint precisa ser sincronizado com ela quando o
conector voltar. Não declarar OMM atualizada. Ler AGENTS.md, este arquivo,
docs/architecture.md e as evidências recentes antes de continuar.

Base anterior:eba5bd3f6f6f93af9c29332975a4d47b1e4714c9 (PCM HIT owners).
Novo checkpoint: áudio diagnóstico integrado ao loop odh-bhav e evidência
estática dos argumentos de caller. Identificar hash com git log. Ler
bhav-audio-validation.md e hit-native-caller-arguments.md antes de continuar.
Implementação Sol alto, revisão Luna alto, pesquisa difícil Astra alto;
principal integrou testes/CMake/receipts/docs. Usuário quer essa delegação por
complexidade. OpenCode/IA local removidos; não invocar. OMM indisponível.

--audio-probe HSM HOT HIT exige --sound-probe, --audio-args tripla,
--audio-jump, --audio-gain-q15, --audio-pan-q15, --audio-choose-first e fontes
--audio-far/--audio-xa explícitas. Rate44100 padrão. Sem derivação automática
ou conversão volume/rate/pan/zoom; perfil é diagnóstico. Não fornece handler
fake de grupo69. Eventos unsupported falham e ficam nas contagens/exit3.
Silent--sound-probe mantido; semSDL a opção é recusada antes de abrir dados.

BhavAudio possui recursos/catálogo/sink/transporte/coleção nessa ordem, World
emprestado. Poll após fase inicial e world_tick, sem tick dentro de submit.
Host drain opcional1..5000ms/5001polls só bombeia transporte, nunca HIT/VM/World.
JSON separa scheduler/worldticks e playback_state pending_output,
pcm_drained_hit_pending, complete, faulted, drain_timeout. Exit0 é sucesso
do probe, não playback completo; fila semdrain é cancelada no teardown.
Erros áudio não são escondidos por viewclose. Owners continuam liberando todas
as notas e só o transporte controla dispositivo compartilhado.

Testes finais:111 configurados,110pass/1X11skip Debug e ASan/UBSan, LSan off.
Novo CLI testa emissãoreal do loop com assets sintéticos/SDLdummy, perfil,
pending e timeout explícitos, nenhumtickWorldextra, preservação de inputs,
falhasgrupo/asset/loop. SemSDL/X11 passam bhav_audio_disabled_cli e bhav_cli.
Logs reports/bhav-audio-build{,-asan}-{final,ctest}.log e noaudio-*.
Cena nativa cadeiraMain/hold120/post240: chair_sit_metal da origem2 chega à
saídaSDLdummy;30208frames gerados/submetidos/consumidos nos doisbuilds, semfault.
94dispatches/662ticks preservados; PCMdrained mas HIT aindaactive, não contar
como threadconcluída. Args[2,0,0], Literal32, escolha0, ganho1/pancentro explícitos.
Comando/receipts em reports/bhav-audio-chair-*.json. Não audibilidade/original/
savedMain/hardware; cena é construída e controles ainda diagnósticos.

Pesquisa estática nova: no input com hash6e598c2... (identidade completa no doc),
BHAV23->handler452862->wrapper57a7f7->resolver57348c->dispatch57376b->init57dc59.
Caller passa [source16,0,0]; EventMapping camposCSV2/3/4 substituem argumentos
individualmente se nãozero. HOT hungry_sated_vox tem todoszero;1024 dafixture
é TrackVolume, não R2 dessecaller. Node offsets18/1c/20 e parser/copy confirmados.
Ainda NÃO implementado automaticamente: próximo preservar/resolver essescampos
em HotEvent e derivar tripla daorigem real, com testes contra essecontrato.
ArgStyle2 computa controleespacial separado e writesvars19/20 após init;
R2zero não significa ganhozero. Ler doc para VAs/offsets/hashes/limites.
Callback no binário analisado usa sourceID, diferente de GetSrcDataField
Simitone que ignora src; preservar RE anterior, não alias source12.
Depois: ArgStyle/controllers/ganho espacial, group/owners/dedup e scheduler
realtime geral do lote, loops/CallReturn e campos4/13/14. Nenhum bypassMain.
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
