# Continuidade atual

Retomada autorizada da meta integral OpenDollhouse. OMM não está disponível
nesta sessão; este checkpoint precisa ser sincronizado com ela quando o
conector voltar. Não declarar OMM atualizada. Ler AGENTS.md, este arquivo,
docs/architecture.md e as evidências recentes antes de continuar.

Base anterior:9e3e2a368c6078a3a1fbaf0a2638cbc932b6690d (catálogo XA).
Novo checkpoint: mixer PCM portátil com orçamento agregado e consumo por
nota; identificar hash com git log. Evidência em pcm-mixer-validation.md.
Catálogo/CLI e evidência anterior em xa-patch-catalog-validation.md. Não houve
novas tarefas para OpenCode; o usuário informou sua remoção e descartou IA local.

Catálogo recebe fontes FAR/loose explícitas; basename ASCII/slashes,
ambiguidades sem precedência adivinhada, limites globais de índice/nomes,
decoder bounded por entrada e proveniência. Sem extração ou cache de PCM.
Paths ficam absolutos; detectar tamanho diferente não é checksum de conteúdo.
PCM retornado owned; allowance opcional limita decoder à capacidade disponível
do mixer, sem aumentar orçamento configurado. Admissão cobra vector.capacity.
Mixer separa render/commit/acknowledge; rendered ainda é HITactive. Stop não
retira buffers já gerados. Após acknowledge antes do clear, descartar buffers
pendentes e rewind_pending restaura sobreviventes no frame consumido. Ainda
falta transport/backend compartilhado que coordene isso no runtime.

Testes finais:106 configurados,105pass/1X11skip em Debug e ASan/UBSan,
LSan off.70checks mixer,35catálogo + wrapperCLI e integração sintética
mixer->filaSDLparcial->ack->HIT. Duas XA nativas2756/3212 no mixer44100stereo
com factory/World construído:109568frames, FNV1a64le8bca194e939e6a8d igual
nos dois builds, instância completa, stoponce, PCM/handles liberados. Args e
choices explícitos, sem audibilidade/original/Main/hardware. Verificador e
recibos reports/pcm-mixer-native*, hash no documento de validação.
Logs reports/pcm-mixer-build{,-asan}-{final,ctest}.log.

Próximo: transport/backend Linux reutilizável com dispositivo compartilhado,
prefixos parciais, consumo e cancelamento/flush/rewind por nota; integrar
serviço de som ao loopWorld real sem bypass. Depois callerargs/ArgStyle/units,
controllers, loops/CallReturn/owners/dedup/globalgroup69 e campos4/13/14.
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
