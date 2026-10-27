# Transporte PCM compartilhado

PcmTransport coordena PcmMixer e uma única fila PcmSink, com ticks explícitos.
LinuxPcmSink adapta o backend SDL2 existente. Um dispositivo atende várias
notas; remover uma nota não fecha o dispositivo nem reinicia as sobreviventes.
Esta unidade fornece transporte reutilizável, ainda não o serviço de áudio do
loop principal nem uma implementação completa de HIT.

## Contrato

O transporte possui mixer e um buffer estéreo prealocado de block_frames*2
samples. O sink é emprestado, deve viver mais que o transporte e ter formato
PCM16 estéreo fixo. O transporte tem acesso exclusivo à fila, start/pause/clear.
Construtor valida formato/limites e recusa fila previamente preenchida sem
descartá-la. Dispositivo começa pausado; o primeiro prefixo aceito inicia saída.

Cada tick consulta consumo, submete uma sobra retida ou gera um bloco e tenta
submetê-lo. Não espera nem cria threads; reutiliza seu buffer durante tick. O sink/SDL
pode alocar internamente ao copiar prefixes para sua fila.
Somente o prefixo aceito vira commit; zero aceito preserva cursor e samples.
Não gera silêncio continuamente após as notas: termina de enviar o padding já
gerado e observa o consumo restante. Uma nota rendered permanece ativa para
WaitSamp até seu endpoint ser reconhecido como consumido.

Limites são os do mixer: padrão4MiB PCM/64registros,1024frames por bloco e
8192frames pendentes, somando fila e buffer retido. Além do PCM há buffer de
saída de4096bytes padrão e metadata do mixer. LinuxPcmSink tem seu limite
adicional de fila,256KiB padrão. Metadata/alocadores e buffers internos de SDL,
conversão e driver não são medidos como RSS. Sem cache ou prova de console.

stop/release de uma nota pendente pausam a saída antes de consultar consumo,
reconhecem o prefixo consumido, limpam a fila, descartam o buffer retido,
retiram a nota e fazem rewind das sobreviventes. O próximo tick regenera e
reinicia a saída. Notas paradas/completas não causam novo flush ao serem
paradas; liberar um handle o invalida. controls valida primeiro e regenera
pendentes com os novos controles; não muda PCM já entregue ao backend.

PcmSink::pause deve ser uma barreira de consumo. A adaptação Linux usa
[SDL_PauseAudioDevice](https://wiki.libsdl.org/SDL2/SDL_PauseAudioDevice):
a documentação garante callback suspenso; o
[código SDL2 consultado](https://github.com/libsdl-org/SDL/blob/SDL2/src/audio/SDL_audio.c)
protege a mudança de paused com LockDevice/UnlockDevice e usa callback para
esvaziar a fila. Consulta em2026-10-15. Esta barreira evita repetir frames que
saíssem da fila entre a medida e clear. Frames já entregues ao backend não
podem ser retirados nem equivalem a conclusão audível no DAC.

## Falhas e ownership

Erro de sink ou violação de prefixo/contagem trava o transporte em failed,
tenta pause/clear e impede novos add/controls/tick. Não há recuperação implícita.
Se o próprio sink recusar pause/clear, o bloqueio de playback é best effort;
nenhum método do transporte tenta reiniciar a saída depois da falha.
stop/release continuam disponíveis para cleanup lógico. Se o flush dessas
operações falhar, ainda liberam o PCM/handle do alvo antes de propagar o erro.
Isso permite cleanup de SoundThreadLifetime::stop noexcept sem depender de
uma segunda tentativa. O chamador deve observar failed e tratar HIT como falha,
não promover estado de nota ainda ativo a conclusão bem-sucedida.

Destrutor tenta pause/clear antes de liberar o mixer, sem lançar. Owners de
notas não devem possuir/destruir o dispositivo compartilhado. A integração
ao loop do jogo permanece pendente. A classe posterior reutilizável de backend/lifetime HIT
está em [pcm-hit-owner-validation.md](pcm-hit-owner-validation.md).

## Evidência

110 checks portáteis com sink sintético: prefixos parciais/zero, endpoint versus
padding, pausa com consumo na fronteira, rewind racional, buffer retido e fila
preenchida durante cancelamento, sobreviventes, handles, controles inválidos,
fila alheia e falhas de queue/start/query/pause/clear. A revisão independente
encontrou e motivou a correção do cleanup quando flush lança erro; a versão
final tem testes de liberação na primeira tentativa e foi relida pelo revisor.

Teste Linux sintético registrado em CTest: duas notas de taxas diferentes,
blocos32frames/fila16frames, SoundThreadCollection com binding World, remoção
da origem da foreground e cleanup idempotente. Confere descarte da fila e do
prefixo retido, PCM regenerado somente da sobrevivente, handle antigo inválido,
conclusão da sobrevivente no mesmo dispositivo e PCM/registros liberados.
O output é exclusivamente SDL dummy, sem audibilidade ou teste físico.

Suítes finais Debug e ASan/UBSan:108 configurados,107 aprovados e um X11
ignorado. Builds sem warnings; LSan desativado. Configuração separada com
ODH_SDL_AUDIO=OFF e ODH_X11=OFF compila/roda os110checks do core. Logs em
registro arquivado não distribuído,-asan}-{final,ctest}.log e
registro arquivado não distribuído,build,tests}.log.

Verificador nativo local registro arquivado não distribuído, SHA256
f23816de79bfb5b39918c061c16a6a9a03f7df258d648db37273bdf9d27d6aaf:
repete as duas XA2756/3212 e a factory HOT/HIT do checkpoint do mixer,
agora via PcmTransport/LinuxPcmSink, bloco1024 e fila512frames. Debug e
ASan/UBSan:109568frames, FNV1a64 PCM little-endian8bca194e939e6a8d, igual
ao mixer anterior. Instância termina, owner para uma vez, PCM/handles zerados.
Recibos registro arquivado não distribuído,asan}.json. Fontes e SHA encoded
em [pcm-mixer-validation.md](pcm-mixer-validation.md). Sem cancelamento nesse
verificador nativo; cancelamento coberto pelos testes sintéticos acima.

Argumentos[1,1024,0], Literal32, choices0/0 e grupo1 inicialmente vazio seguem
fixtures explícitas; World é construído. Não há inferência de unidades,
audibilidade, Main retomado, original ou hardware portátil.

## Próximo vínculo com o jogo

Pesquisa somente de leitura nos refs fixados: Simitone4906db3 e submodule
FreeSO f86bbd3 (revisões completas em references.md). No submodule:
TSOClient/tso.simantics/Primitives/VMPlaySound.cs13–60 resolve FWAV e origem,
chama HITVM.PlaySoundEvent(nome), submete variáveis e registra owner. Volume e
SampleRate são lidos em63–89, mas não aplicados nesse caminho. Não derivar
unidades de gain/pan/resampling desses campos sem evidência adicional.

TSOClient/tso.sound/HITVM.cs207–212 seleciona track/subroutine no branch TS1;
254–268 cria HITThread sem argumentos de caller. HITThread.cs195–211 inicia
R1=12 e demais registros zero; SetTrack260–285 não os substitui. ODH já tem esse
default em hit_selection.cpp. HitArguments[1,1024,0] sobrescreve R1..R3 e
permanece fixture, não default de caller inferido da referência.

TSOClient/tso.simantics/Entities/VMEntity.cs329–427 calcula volume/pan espacial
e submete variáveis. Dedup/eventos compartilhados, múltiplos owners,
globalgroup69, controllers, loops/CallReturn, unidades e escolha do relógio
continuam pendentes; não suprimir flags ou liberar Main por sucesso artificial.
