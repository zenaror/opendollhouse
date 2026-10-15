# Continuidade atual

Retomada autorizada da meta integral OpenDollhouse. OMM não está disponível
nesta sessão; este checkpoint precisa ser sincronizado com ela quando o
conector voltar. Não declarar OMM atualizada. Ler AGENTS.md, este arquivo,
docs/architecture.md e as evidências recentes antes de continuar.

Base anterior:a2267da0a2fc1c194c4053bdefd8d71be672291d (transporte PCM).
Novo checkpoint: PcmHitOwner e make_xa_hit_sound; identificar hash com git log.
Evidência em pcm-hit-owner-validation.md. Core por subagente Sol alto,
revisão independente por Luna alto; principal integrou factory, BHAV e testes.
Usuário pediu delegação por complexidade/modelo/esforço. OpenCode foi removido,
IA local descartada; não invocar. OMM ainda indisponível.

PcmHitOwner é lifetime por thread HIT; transport, catálogo/HOT/HIT/World e
capturas vivem mais que ele. Backend captura owner estável por ponteiro;
callbacks nunca podem sobreviver ao owner. Lista de handles prealocada1..256,
limite lifetime (não recicla completos). Loader recebe allowance PCM corrente;
helper host usa catálogo no NoteOn, sem decode/áudio durante construção.
Somente handles próprios são consultáveis; rendered ainda é HITactive,
consumido ou explicitamente parado vira inativo/complete enquantoowner vivo.
Ownerstop ou sinkfailed vira HITfailed. Grupo só por callback explícito;
nenhum efeito após falha do sink. Stop noexcept libera todas as notas mesmo
se um flush falhar; não fecha dispositivo compartilhado.

Transporte mantém prefixos, consumo e cancelamento pause/ack/clear/rewind;
falha trava saída, sem recuperação implícita, cleanup por alvo ainda funciona.
Catálogo FAR/loose explícito, basename ASCII/slashes e rejeição de ambiguidades;
allowance só reduz decoder, mixer cobra vector.capacity. Sem cache/precedência
adivinhada; fontes externas imutáveis e sizecheck não é checksum de conteúdo.

Testes finais:110 configurados,109pass/1X11skip Debug e ASan/UBSan, LSan off.
44checks owner e45integração host: VM BHAVopcode23->FWAVcallback->coleção->
HOT/HIT->catálogoXA->PCM, duas origens, partialqueue, remoção e sobrevivente,
cleanup de conclusão, asset ausente e budget insuficiente. Dados sintéticos.
Novos testes passam também com SDL_AUDIO/X11 OFF. Revisão achou grupoexecutado
após sinkfailed; corrigido, testado e relido independentemente.
Verificador nativo usa helper/owner reais com XA2756/3212,44100stereoSDLdummy;
109568frames/FNV1a64le8bca194e939e6a8d igual em Debug/ASAN e checkpointanterior.
Foregroundhandle liberado com instância; background sobrevive até cleanup.
Args[1,1024,0], Literal32, choices0/0 e grupo1vazio explícitos. Worldconstruído,
sem audibilidade/original/Main/hardware. Verificador/recibos reports/pcm-owner-native*,
SHA no doc. Logs reports/pcm-owner-build{,-asan}-{final,ctest}.log.

Próximo: serviço de som com ticks no loopWorld, gain/pan espacial e política
comprovada de owner/grupo/dedup; depois demais profiles/HITinstructions.
Ainda não ligar Main com sucesso artificial ou adotar argumentos de fixture
como callerreal. VMPlaySound referência não aplica operand.Volume/SampleRate;
HITVM inicia R1=12,R2=R3=0, ODH já tem default. Callback no binário analisado usa sourceID, ao contrário do
GetSrcDataField que ignora src no Simitone: preservar evidência
RE existente em hit-source-callback/hit-invocation-validation, não alias source12.
Ainda gates: callerargs/ArgStyle/unidades/controllers, loops/CallReturn,
ownerscompartilhados/globalgroup69 e campos4/13/14.
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
