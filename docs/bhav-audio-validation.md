# Áudio no loop do probe BHAV

odh-bhav pode enviar suas solicitações reais de PlaySound ao pipeline
HOT/HIT -> catálogo XA -> owners por thread -> transporte -> saída Linux.
O modo silencioso anterior permanece disponível. Este novo modo é diagnóstico:
modo dos argumentos, controles e política de escolha são explícitos, sem afirmar que
representam a inicialização ou ganho espacial completos do jogo.

## Opções e agendamento

Acrescentar ao perfil existente --sound-probe/animation/world:

```text
--audio-probe HSM HOT HIT
--audio-far SOUND_DATA.far        (ou --audio-xa FILE; até16 fontes)
--audio-args R1 R2 R3
--audio-jump free-so|literal32
--audio-gain-q15 0..32768
--audio-pan-q15 -32768..32768
--audio-choose-first
[--audio-rate 8000..192000]
[--audio-drain-ms 1..5000]
```

Alternativa posterior: --audio-caller-args no lugar da tripla manual, com
resolução bruta da origem/HOT em hit-caller-resolution-validation.md. Controles
espaciais/ArgStyle continuam explícitos ou pendentes conforme esse documento.

Todos os seletores de perfil são obrigatórios, exceto rate (formato de saída
44100 padrão) e drain. A escolha entre tripla manual e caller/HOT é obrigatória. Não há
conversão de Volume/SampleRate ou flags pan/zoom do PlaySound para DSP. Esses campos continuam
nos recibos das solicitações. Jump e escolha determinística permanecem perfis
diagnósticos. Sem callback para grupos: opcode69 falha, sem registry vazio
inventado. Loops e demais perfis não implementados são rejeitados.

BhavAudio possui recursos imutáveis, catálogo, sink, transporte e coleção;
destrói a coleção antes do transporte, que precede sink/recursos. World vive
mais que o serviço. Submit é não bloqueante e captura erros na contagem e no
status do request; não altera o ramo true da primitiva BHAV.

A coleção e o transporte recebem um poll após a execução inicial de cada fase
e após cada world_tick existente (depois de playback/VM/Main). Não há tick de
áudio dentro de submit. scheduler_ticks conta esses polls, separadamente dos
ticks de simulação. Limites da VM/Main, seleção de fases e gates anteriores
permanecem ativos. O serviço não muda estado de entidades fora do pipeline de
som nem aceita silenciosamente instruções não suportadas.

## Saída pendente e erros

O drain opcional usa somente transporte e relógio do host: no máximo5000ms e
5001 polls, com pausas curtas. Não executa HIT, VM, World, rota, playback ou Main.
Assim, PCM esvaziado pode deixar HIT esperando o próximo tick de simulação.
JSON diferencia `pcm_drained_hit_pending` de `complete`; também distingue
`pending_output`, `drain_timeout` e `faulted`. Sem drain, saída pendente é
intencionalmente cancelada no encerramento, com requested=false e estado
pendente explícito. Exit0 significa execução válida do probe, não conclusão
de playback ou prova de audibilidade.

Rejeições, erros de submit/scheduler, faults HIT/sink e timeout solicitado
causam exit3, preservando o status BHAV original. Fechar a janela não encobre
falha de áudio já conhecida. Falhas de entrada continuam exit2. O recibo inclui
perfil, solicitações, completed/owner_invalid/faulted/active, frames PCM,
scheduler_errors, escopo pcm_only e tempo/polls do drain. Contagens de faults
HIT são explícitas, mas a coleção ainda não publica seu diagnóstico por thread.

O core continua opcional e sem SDL. Sem ODH_SDL_AUDIO, odh-bhav mantém a execução
silenciosa e recusa o modo PCM antes de abrir arquivos de recursos/dispositivo.
Orçamentos são os atuais de coleção64threads e transporte/mixer padrão
(4MiB PCM,64notas,1024frames/bloco,8192pendentes); catálogo16fontes e índices
limitados. Metadata/allocators/driver não são medidas de RSS ou hardware.

## Evidência

Novo teste CLI usa somente FAR/IFF/BHAV/HSM/HOT/HIT/XA sintéticos e SDL dummy.
Compara a mesma VM silenciosa/com áudio; prova emissão real no loop, perfil
explícito, conservação dos inputs, PCM consumido sem ticks extras de simulação,
HIT ainda pendente, enqueue-only, timeout e falhas de grupo/asset/loop. Perfis
incompletos/repetidos/inválidos falham antes de produzir JSON de execução.
Teste separado sem SDL verifica recusa e preservação do caminho silencioso.

Suítes finais Debug e ASan/UBSan:111 configurados,110 aprovados e um X11
ignorado. Builds observados sem warnings; LSan desativado. Build sem SDL/X11
passou bhav_audio_disabled_cli e bhav_cli. Logs em
reports/bhav-audio-build{,-asan}-{final,ctest}.log e
reports/bhav-audio-noaudio-{configure,build,tests}.log.

Cena nativa: comando da cadeira em stand-validation.md acrescido de
--main-probe --hold-ticks 120 e o perfil de áudio explicitamente configurado
com generated HSM/HOT/HIT, SoundData.far, args[2,0,0], Literal32, escolha0,
ganho unidade e pan central. Comando exato retido em
reports/bhav-audio-chair-command.json; trocar build-asan por build para Debug.
Comparação silenciosa/com áudio: mesma VM final true,94dispatches totais,
662ticks. PlaySound emite chair_sit_metal da origem2 para o pipeline real.
Debug e ASan/UBSan entregaram30208frames ao SDL dummy sem fault; PCM vazio
ao final, HIT ainda pendente e scheduler664polls. Não é término natural HIT.
Recibos reports/bhav-audio-chair-{silent,diagnostic,asan}.json e summary.json.
Resultados detalhados de PCM e estado final permanecem nesses recibos; tempos
de consumo dependem do host. Dados comerciais externos somente para leitura.

Não é retomada de saved Main, relógio fiel, ganho espacial, áudio original ou
hardware portátil. A derivação bruta do caller/evento está implementada como opção; próximas
camadas são ArgStyle/controles comprovados. Evidência estática em
[hit-native-caller-arguments.md](hit-native-caller-arguments.md).
