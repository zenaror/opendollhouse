# Controles HIT nativos e atualização de retrigger

Unidade de implementação sobre a base publicada `84a66b2`. O builder de
controles fresh agora inclui o banco 50–62, e `HitThread` expõe uma primitiva
de atualização de estado para o ramo nativo ativo sem reset. Isso ainda não
integra um manager persistente de controllers com World ou áudio. A base
estática, os limites de domínio e o caminho nativo examinado estão em
[hit-native-retrigger-evidence.md](hit-native-retrigger-evidence.md).

## Banco fresh e sentinelas HOT

O builder requer ArgStyle 2 e metadata resolvida de volume, ducking priority,
patch e Spl. Ele preserva os quatro campos raw no recibo. IDs 50–62 começam
em zero, exceto ID 52=120 e ID 57=20; `Track.patch_id` e `Track.Spl` substituem
50 e 57 quando não são `INT32_MIN`. A sentinela raw fica registrada, mas o
valor efetivo mantém o default: patch em ID 50 vira zero, Spl em ID 57 fica
20. Zero explícito substitui o default. Se Spl está ausente, o perfil nativo
é recusado antes de chooser ou backend. A máscara known inclui 16–47 e
50–62; 48/49 e 63–69 continuam desconhecidos.

`HitPatchMode::raw` permanece o modo padrão de `resolve_hit_event` e conserva
a interpretação literal do perfil legado. `native_controller` é explícito;
factory/host escolhe esse modo quando constrói com controles nativos. Com ele,
patch `INT32_MIN` é tratada como zero efetivo, podendo acionar a hitlist inicial
sem alterar a metadata HOT nem o recibo raw.

## Atualização de estado sem reset

`HitThread::update_native_active_unpaused` exige uma thread nativa ainda em
execução, ID 55 conhecido e zero, e ID 53 conhecido e zero. A API só representa
estado local da thread: cabe ao caller garantir o ramo ativo e os gates de
admissão/grupo. A chamada não altera estado de pausa, ativação do controller externo ou gates.

Quando aceita a chamada, a primitiva limpa fade ID 45, grava R1–R3, atualiza
os controles espaciais 19/20 e recalcula os flags zero/sign do pan. Preserva
PC, trace, handles/notas, orçamento, recibo de inicialização, source47 e patch50.
Não chama chooser, binding, provider, backend ou Clock e não aplica DSP. O
modelo local também não representa o byte `+0x1cf` nem o ponteiro de provider
`+0xc8` do executável; a chamada não é reprodução completa de `0x57dc59`.
O restart `interrupt55!=0`, gates e lifecycle do controller continuam fora
desta unidade.

Reentrada da atualização durante um `tick` fixa a falha da thread. Se
SourceReader ou chooser engolir a exceção da reentrada, a verificação depois
do callback ainda aborta antes de escritas de bytecode ou início de nota subsequente.

## Verificação

Testes diretos C++17 com warnings-as-errors passaram 74 checks de controles,
965 de seleção e 108 de thread. ASan/UBSan passou nos mesmos três executáveis;
LSan ficou desativado sob ptrace. A suite focada de factory passou
`sound_event_factory`, `xa_sound_factory` e `bhav_audio_cli` (3/3, 5,01 s);
os checks diretos associados passaram 76 casos de factory e 56 de XA. Logs:
registro arquivado não distribuído.

Os CTests oficiais configuraram 114 casos em Debug: 113 passaram, um teste X11
foi pulado, zero falhas (7,03 s). ASan/UBSan teve os mesmos 113 passes e um
skip (13,18 s); LSan permaneceu desligado. Sem SDL/X11, passaram 8/8 testes
(0,62 s). Logs de configure/build/CTest em
registro arquivado não distribuído.

O probe Linux da cadeira `chair_sit_metal` passou em Debug e ASan/UBSan:
94 dispatches, 662 ticks World, 664 polls de áudio e 30.208 frames PCM
renderizados, submetidos e consumidos, sem fault. O estado final foi
`pcm_drained_hit_pending`, com HIT ativo. A comparação indica VM, World,
requisições e traces iguais ao baseline do gate anterior; o relatório de áudio também coincide em contadores e estado, com diferença no tempo de drain medido
por relógio de parede. Não é comparação byte a byte de amostras PCM.
Comando, recibos e comparação estão em
registro arquivado não distribuído e
registro arquivado não distribuído.
O manifesto da unidade cobre hashes e contadores de 13 arquivos em
registro arquivado não distribuído.

Essa fixture não exercita `update_native_active_unpaused` nem a sentinela no
asset real da cadeira; os ramos correspondentes são cobertos pelas regressões
sintéticas. Os CTests e a fixture são evidência Linux/host, não equivalência ao
executável original, implementação completa de retrigger, áudio físico ou
validação dos ports. A auditoria estática conferiu 934 linhas e 2.917 bytes de
instruções com 11 destinos de vtable (registro arquivado não distribuído);
o executável original não foi executado. A revisão do core/API e do texto da
evidência estática terminou sem defeito material.
