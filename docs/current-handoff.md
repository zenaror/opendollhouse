# Continuidade atual

Retomada autorizada da meta integral OpenDollhouse. OMM não está disponível
nesta sessão; este checkpoint precisa ser sincronizado com ela quando o
conector voltar. Não declarar OMM atualizada. Ler AGENTS.md, este arquivo,
docs/architecture.md e as evidências recentes antes de continuar.

Base anterior:8a34ab03e2e09913fef4f06bbfdb3b1a2abaf6de (mixer PCM).
Novo checkpoint: PcmTransport portátil + LinuxPcmSink compartilhado;
identificar hash com git log. Evidência em pcm-transport-validation.md.
Catálogo/mixer anteriores seguem descritos nos seus documentos. Usuário
removeu OpenCode e descartou IA local; não invocar. Pediu coordenação com
subagentes/modelo/esforço conforme complexidade. Nesta unidade: implementação
Sol alto, revisão Luna alto e pesquisa TS1 Luna médio; integração pelo principal.

Catálogo recebe fontes FAR/loose explícitas; basename ASCII/slashes,
ambiguidades sem precedência adivinhada, limites globais de índice/nomes,
decoder bounded por entrada e proveniência. Sem extração/cache de PCM.
Allowances só reduzem orçamento do decoder; mixer cobra vector.capacity.

PcmTransport possui mixer e buffer fixo, empresta sink exclusivo estéreo.
Ctor recusa fila preenchida; tick não espera, preserva prefixos parciais,
commita só aceitos e acknowledge=submitted-queued. rendered ainda é HITactive.
Cancelamento: pause como barreira, acknowledge antes de clear, descartar sobra,
stop/release e rewind das sobreviventes; próximo tick reinicia saída.
Falha do sink trava transporte, pause/clear best effort, sem retomada implícita;
stop/release liberam alvo mesmo se flush lançar erro. Backend HIT futuro deve
observar failed; state ativo não pode virar sucesso artificial.

Testes finais:108 configurados,107pass/1X11skip em Debug e ASan/UBSan,
LSan off.110checks transport e teste Linux SDLdummy com remoção World da
foreground, flush de fila+retido, sobrevivente continua no dispositivo.
Build separado SDL_AUDIO/X11 OFF passa110checks do core.
Duas XA nativas2756/3212 com factory/World construído:109568frames,
FNV1a64le8bca194e939e6a8d igual nos dois builds e ao mixer anterior;
instância completa, stoponce, PCM/handles liberados. Args[1,1024,0],
Literal32, choices0/0 e grupo1 vazio explícitos. Sem audibilidade/original/
Main/hardware. Verificador/recibos reports/pcm-transport-native*, SHA no doc.
Logs reports/pcm-transport-build{,-asan}-{final,ctest}.log.
Revisão independente encontrou cleanup pós-falha de flush e confirmou correção.

Próximo: backend/lifetime HIT reutilizável por thread sobre PcmTransport,
registrando/liberando todas as notas sem fechar sink compartilhado; integrar
sound_primitive/coleção ao loopWorld sem bypass. Referência TS1 conferida:
VMPlaySound não aplica operand.Volume/SampleRate nesse caminho; HITVM cria
thread sem callerargs, R1=12/R2=R3=0 (ODH já tem default). Não usar fixture
[1,1024,0] como default real. Ver paths/linhas no doc de transporte.
Ainda gates: gain/pan espacial, callerargs/ArgStyle/unidades/controllers,
loops/CallReturn/owners/dedup/globalgroup69 e campos4/13/14.
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
