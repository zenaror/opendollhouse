# Probe XA para saída Linux

Checkpoint de 2026-10-14. `odh-xa-play INPUT_XA [--play]` liga o decoder
Maxis XA existente à saída PCM Linux. Sem `--play`, somente valida,
decodifica e reporta; não inicializa SDL nem abre dispositivo.

## Contrato

O decoder preserva limites padrão de 16 MiB de input e 4 MiB de PCM.
A validação completa acontece antes da abertura de áudio. O arquivo de entrada
é aberto somente para leitura; não há arquivo de saída ou extração adicional.
PCM declarado conserva sample rate, canais e padding descartado pelo decoder.

Com `--play`, o backend usa sua fila limitada padrão de 256 KiB. A ferramenta
acompanha o cursor em frames, entrega prefixos parciais e tenta o restante
após o consumo, sem perder ou duplicar frames no avanço do cursor. Mantém a
amostra decodificada em memória durante o probe; não é um decoder incremental.
O prazo é duração da amostra mais dois segundos. Erro de dispositivo, decoder
ou timeout produz saída não zero e não imprime um recibo de sucesso. RAII fecha
o dispositivo também em falhas. A ferramenta é bloqueante e independente da
VM; não é o scheduler do jogo.

```sh
./build/odh-xa-play INPUT_XA
SDL_AUDIODRIVER=dummy ./build/odh-xa-play INPUT_XA --play
./build/odh-xa-play INPUT_XA --play
```

JSON identifica frames, canais, rate, bytes PCM, bytes restantes no input,
frames entregues, driver e `audibility_verified:false`. Fila consumida
significa entrega ao backend; não prova audibilidade ou término físico.

## Evidência

O teste CLI usa somente fixtures sintéticas e SDL dummy: mono de três frames,
stereo de 70.001 frames a 192 kHz (280.004 bytes, maior que a fila), declaração
vazia, trimming/padding e bytes finais. Confere cursor final, inspeção com
driver inválido sem abrir áudio, erro explícito ao reproduzir nesse driver,
input truncado, uso inválido e SHA do input preservado.

Suites Debug e ASan/UBSan: 98 configurados, 97 aprovados, um X11 ignorado;
sem warnings. `ASAN_OPTIONS=detect_leaks=0`; LSan não validado.
Testes específicos repetidos após adicionar a rejeição de driver inválido.

Input nativo previamente identificado como patch3212:
SHA256 `e68b39e613b2500e40796346fb6cef6343ae97dda5eea2ae0debc3f9f6dcbd64`.
O arquivo XA preservado em reports é cópia privada de recurso comercial,
não distribuída. Decode: mono 22.050 Hz, 54.296 frames, 108.592 bytes PCM,
zero bytes restantes. SDL dummy e PulseAudio fora do sandbox consumiram os
54.296 frames (aproximadamente 2,46 segundos). SHA input inalterado depois.
Recibo privado: registro arquivado não distribuído.

Não comprova áudio original, mixer/controllers/pan/volume, loop de notas,
soundthread/WaitSamp, retomada do Main ou ports. A seleção HIT ainda fornece
um plano separado; ligação evento->patch->reprodução e conclusão correta da
VM são próximas unidades, sem liberar a VM mediante sucesso artificial.

Dependências e fontes primárias SDL estão em
[linux-audio-validation.md](linux-audio-validation.md); a equivalência de
samples do decoder está em [xa-audio-validation.md](xa-audio-validation.md).

