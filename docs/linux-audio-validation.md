# Saída PCM Linux inicial

Checkpoint de 2026-10-14. `odh_linux_audio` é um backend opcional de host;
não altera a VM, a seleção HIT ou os recursos comerciais. A entrada é PCM16
signed nativo, mono ou stereo intercalado, entre 8.000 e 192.000 Hz.

## Contrato e dependência

A implementação carrega `libSDL2-2.0.so.0` em tempo de execução, exige SDL2
2.0.4 ou posterior e mantém o ABI utilizado isolado em `tools/linux_audio.cpp`.
Não exige headers SDL para compilar. Ausência da biblioteca, símbolos, driver
ou dispositivo produz erro explícito. `-DODH_SDL_AUDIO=OFF` exclui os targets.

O dispositivo abre pausado. `queue` copia apenas o prefixo em frames que cabe
na fila, sem bloquear; capacidade padrão 256 KiB, configurável, alinhada ao
frame e limitada a uint32. O chamador mantém a propriedade do restante.
`start`, `pause`, `clear` e encerramento têm efeitos explícitos. Uma única
thread deve controlar a instância. A destruição fecha o dispositivo, equilibra
somente sua inicialização do subsistema e descarrega a biblioteca. SDL pode
converter internamente para o dispositivo; o formato entregue pela aplicação
permanece o solicitado. O limite cobre PCM enfileirado pela aplicação, não
memória interna de SDL, servidor ou hardware.

A ferramenta independente abre uma saída somente com `--play`:

```sh
./build/odh-audio-probe --help
SDL_AUDIODRIVER=dummy ./build/odh-audio-probe --play
./build/odh-audio-probe --play
```

Gera 100 ms locais de PCM mono 44.100 Hz, amplitude máxima 1024, bordas
atenuadas; prazo de consumo da fila de dois segundos. Respeita
`SDL_AUDIODRIVER`. Não muda volume, configurações ou arquivos do usuário.

## Validação e limites

65 checks com SDL dummy cobrem parâmetros, overflow, capacidade, prefixos
parciais, fila pausada exata, clear, start/drain, pause, reabertura e preservação
de outro owner do subsistema. O teste recusa executar sem
`SDL_AUDIODRIVER=dummy`. Debug e ASan/UBSan: 97 testes configurados,
96 aprovados, um X11 ignorado por limitação do ambiente; sem warnings.
Sanitizadores com `ASAN_OPTIONS=detect_leaks=0`; LSan não validado.

Runtime observado SDL 2.30.0. Probe dummy e probe Linux fora do sandbox,
driver `pulseaudio`, aceitaram e consumiram 4.410 frames sintéticos. Consumo da
fila confirma entrega ao backend, não audibilidade, término no DAC, equivalência
ao original ou desempenho nos consoles.

Ainda faltam ligar XA decodificado e seleção HIT à reprodução, lifecycle de
soundthread/WaitSamp, refresh da origem, mixer/controllers/volume/pan e avanço
correto do Main. Esta ferramenta não substitui esses mecanismos nem libera
uma VM mediante sucesso artificial.

Fontes primárias para ABI e semântica:
[SDL_audio.h](https://github.com/libsdl-org/SDL/blob/SDL2/include/SDL_audio.h),
[SDL.h](https://github.com/libsdl-org/SDL/blob/SDL2/include/SDL.h),
[SDL_version.h](https://github.com/libsdl-org/SDL/blob/SDL2/include/SDL_version.h),
[OpenAudioDevice](https://wiki.libsdl.org/SDL2/SDL_OpenAudioDevice),
[QueueAudio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio),
[GetQueuedAudioSize](https://wiki.libsdl.org/SDL2/SDL_GetQueuedAudioSize),
[ClearQueuedAudio](https://wiki.libsdl.org/SDL2/SDL_ClearQueuedAudio).

