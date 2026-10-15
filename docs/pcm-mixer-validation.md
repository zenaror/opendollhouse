# Mixer PCM portátil

PcmMixer possui PCM por nota, handles monotônicos e limites agregados. Opera
sem SDL, filesystem, callbacks ou relógio de parede. O chamador fornece os
buffers de saída; render não aloca. O objeto tem um único proprietário/thread.

## Perfil e limites

Saída PCM16 estéreo entre 8kHz e 192kHz. Fontes mono/estéreo no mesmo intervalo;
mono é duplicado. Resampling linear com fase racional inteira, último sample
mantido até ceil(framesFonte*rateSaída/rateFonte). Gain Q15 de0 a32768 e pan
de-32768 a32768, como balance linear explícito. Divisões signed truncam para
zero. Soma em int64 e uma saturação final, sem clipping entre vozes.

Este é um perfil DSP determinístico próprio, não prova do resampler/pan law
do original. Simitone4906db3/FreeSO submodulef86bbd3, HITThread.cs313–326,
encaminha Volume/Pan ao backend; isso não determina a lei DSP daquele backend.
Não há conversão implícita das unidades HIT para estes controles.

Padrões:4MiB de capacidade PCM agregada,64 registros de notas,1024 frames por
bloco e8192 frames gerados ainda não consumidos. Máximos:256 registros,
8192 frames por bloco e65536 pendentes. Recusa explícita antes de mover a
entrada se capacidade, formato ou registros não couberem. Contabiliza
vector.capacity()*sizeof(int16_t), não apenas tamanho. Metadata prealocada;
buffers do chamador/SDL e overhead de allocator ficam fora dessa contabilidade.
Não é limite de RSS nem evidência de viabilidade em console.

XaPatchCatalog::load aceita allowance opcional de PCM, usado antes da alocação
do decoder e limitado pelo orçamento configurado do catálogo. O chamador pode
passar mixer.available_pcm_bytes(); a admissão final ainda verifica capacidade
real do vector. Sem concorrência de admissão, cache ou reserva entre chamadas.

## Consumo e cancelamento

render gera frames; commit registra apenas o prefixo aceito pelo backend;
acknowledge recebe o frame absoluto consumido, monotônico e até submitted.
Uma nota rendered continua ativa para HIT/WaitSamp até acknowledge alcançar
seu endpoint. Só então libera PCM e passa a complete. Registros continuam
válidos até release; handles não são reutilizados. stop é idempotente e libera
PCM, mas não remove samples que já estejam misturados/enfileirados.

Para retirar uma nota dos buffers pendentes: atualizar acknowledge antes de
limpar a fila do backend, parar/liberar a nota, descartar buffers do chamador e
da saída e chamar rewind_pending. As sobreviventes retomam a fase no último
frame consumido, preservando seus timestamps de início. Dados já consumidos
pela saída não podem ser retirados. O chamador deve descartar todos os buffers
anteriores ao rewind; a API não fornece tokens de época. Controles novos afetam
renderizações futuras e podem ser aplicados aos pendentes mediante esse flush.

Ainda falta um transport/backend reutilizável que faça essa coordenação e
possua o dispositivo compartilhado, separado dos lifetimes individuais HIT.

## Evidência

70 checks sintéticos: PCM exato, mono/estéreo, taxas racionais, signed rounding,
partição de blocos, ganho/pan, soma/saturação, limites e recusa sem mover entrada,
handles antigos, consumo parcial, cancelamento/rewind/fase fracionária,
notas futuras e256 vozes. Teste SDL dummy usa duas fontes sintéticas de taxas
distintas e fila de16 frames para blocos de32, preservando prefixos não aceitos;
HIT NoteOn/WaitSamp/End só termina após consumo.

Suítes finais Debug e ASan/UBSan:106 configurados,105 aprovados e um teste X11
ignorado pelo ambiente. Sem warnings nos builds; LSan desativado, sem afirmação
de ausência de leaks. Catálogo passou35 checks e wrapper CLI, incluindo
allowance zero, menor/exato e tentativa de ampliar o limite configurado.
Logs reports/pcm-mixer-build{,-asan}-final.log e
reports/pcm-mixer-build{,-asan}-ctest.log.

Verificador nativo local reports/pcm-mixer-native.cpp, SHA256
89fb18f331c69ab863305364554ec6a997ee31bb6539c7c86fee6fb6128b461f:
factory HOT/HIT -> binding World -> catálogo FAR -> duas XA -> mixer -> SDL
dummy,44100Hz estéreo, unidade/centro. Background patch2756 começa no frame0
em grupo2 explícito; hungry_sated_vox escolhe3212, track3979/PC13699. Group1
vazio antes da foreground, argumentos[1,1024,0], Literal32 e choices0/0 são
fixtures explícitas. Sem inferência de global group ownership.

Entradas nativas de SoundData.far:

| Patch | Fonte | Frames mono22050 | SHA256 encoded |
| --- | --- | --- | --- |
|2756|Voice\\interactions\\VOX_HUGGEE_GOODM1.XA|42740|cc7b1f7816b017d788ff712ed7d8fb132264872c721e1f801ed7832f832eaf98|
|3212|Voice\\mood\\VOX_COMFORT_SIGHM4.XA|54296|e68b39e613b2500e40796346fb6cef6343ae97dda5eea2ae0debc3f9f6dcbd64|

Debug e ASan/UBSan produziram109568 frames, incluindo padding do último bloco;
FNV1a64 do PCM little-endian:8bca194e939e6a8d em ambos. A instância concluiu,
o owner foi parado uma vez e nenhum registro/PCM restou. Recibos
reports/pcm-mixer-native-{dummy,asan}.json. Hash coincidente demonstra
determinismo entre esses builds, não comparação com áudio do original.

Sem audibilidade/DAC, Main retomado, ciclo World real ou hardware portátil.
Cancelamento no dispositivo ainda só tem contrato e verificação do core;
o verificador nativo esgota as duas notas, sem flush durante reprodução.
