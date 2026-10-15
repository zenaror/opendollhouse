# Validação dos controles HIT nativos antes do primeiro tick

Checkpoint local de 2026-10-15. Esta unidade adiciona um perfil opcional de
inicialização bruta ArgStyle 2 ao seletor e à thread HIT. O perfil instalado
descreve valores visíveis ao primeiro script; não converte controles em ganho
ou pan do backend. A base da interpretação é a [evidência estática da imagem
Sims.exe fornecida](hit-native-controls-evidence.md), sem execução do jogo
original e sem equivalência comprovada ao outro executável fornecido.

## API e ordem

`build_native_hit_controls(track, raw_arguments, spatial_external_volume,
spatial_pan, controllers)` é puro e devolve `HitNativeControls` próprio. As três
posições de `raw_arguments` são os valores assinados de R1/R2/R3; as duas
entradas espaciais são valores brutos fornecidos pelo caller. O builder exige
ArgStyle 2 e `Track.volume`, `Track.ducking_priority` e `Track.patch_id`
resolvidos. A conversão das palavras de Track para int32 preserva o padrão de
bits sem depender de conversão C++ fora da faixa assinada. O banco opcional de
controllers tem 31 posições para IDs 17–47; ausência ou `INT32_MIN` significa
unset e não substitui o valor anterior.

A ordem implementada é:

1. ID 16 começa conhecido em zero. Os 31 IDs 17–47 recebem o banco completo
   de defaults evidenciado. Os valores não zero são 17=32, 18=1024, 19=1024,
   20=512, 21=3600, 28=100, 29=10000 e 39=1; os demais desse banco são zero.
2. `Track.volume` escreve ID 18 e `Track.ducking_priority` escreve ID 25 quando
   diferem de `INT32_MIN`. Se unset, o recibo conserva a palavra bruta mas ID 18
   mantém o default 1024 e ID 25 mantém zero. `Track.patch_id` bruto escreve
   ID 50 sem aplicar essa regra de sentinel. Overrides de controller não unset
   podem substituir IDs 17–47, inclusive 18 e 25.
3. ArgStyle 2 escreve R2 em ID 19, R3 em ID 20, R1 em ID 47 e `isplaying=1`
   em ID 46. R1/R2/R3 continuam intactos nos registradores 1–3.
4. O caller fornece extvol espacial final em ID 19 e pan espacial final em
   ID 20. O último setter estabelece os flags iniciais `zero=(pan==0)` e
   `sign=(pan<0)` antes da primeira instrução. ODH não modela o flag positive.

O valor inicial de ID 50 continua sendo o `Track.patch_id` bruto: pode ser zero
mesmo quando o chooser inicial seleciona uma patch não zero. A escolha inicial
continua sendo feita uma única vez pela regra atual de SetTrack, em variável
separada do local 50. Instalar controles não executa bytecode, NoteOn,
callbacks de backend nem efeitos de escrita de patch. `HitNotePlan` guarda
`initial_arguments` e `initial_controls` próprios; seus flags e locals já são
consultáveis em `HitThread::state()` antes do primeiro `tick()`.

`select_hit_note` e `HitThread` aceitam o perfil como último argumento opcional.
O construtor exige a tupla explícita de argumentos e Track ArgStyle 2
correspondentes antes de invocar o chooser. O snapshot público é verificado:
IDs 16–47 e 50 devem estar conhecidos; IDs 48/49 e 51–69 devem permanecer
desconhecidos; IDs 16, 19, 20, 46, 47 e 50 devem corresponder aos stores finais.
Os outros valores de 17–47 podem variar por controller. Leitura de um local
nativo ainda desconhecido falha até que o script o escreva. Uma escrita nativa
em ID 18 altera volume, sem selecionar patch; a escrita em ID 50 conserva a
substituição de patch quando há correspondência no HOT. O estado inicial não
aciona esses efeitos de escrita.

Sem o argumento opcional, o perfil legado permanece: registradores/locals
zerados, exceto R1=12, flags zero/sign falsos, e o alias histórico de escrita
18/50 para patch. O novo perfil não altera limites de instruções, trace,
escolhas, notas ou grupos existentes. Seu estado adicional tem tamanho fixo:
54 locals opcionais e 31 entradas opcionais de controller; não há crescimento
por nota ou por tick nessa inicialização.

## Verificação direta

Compilação separada em C++17 com `-Wall -Wextra -Wpedantic -Wconversion
-Werror` passou nos testes sintéticos: 70 checks de builder,
944 de seleção e 61 de thread. Os testes cobrem defaults e zeros conhecidos,
unset de Track/controller, overrides válidos 18/25, bits assinados de Track,
recibos próprios, distinção entre ID 50 bruto e patch escolhida, rejeição de
snapshot alterado/desconhecido, escrita 18 versus 50, e preservação do legado.
Pan -512, 0 e 512 foi verificado nos flags antes do primeiro tick e nos seis
branches condicionais como primeira instrução, sem escrita prévia de flags.

Os três executáveis de teste também passaram com ASan/UBSan. A instrumentação
de leak ficou desativada (`ASAN_OPTIONS=detect_leaks=0`) porque LSan não opera
sob o ptrace deste ambiente; não se afirma ausência de vazamentos. O teste de
seleção foi ligado às bibliotecas estáticas já existentes do checkout, enquanto
os arquivos alterados foram recompilados diretamente. `git diff --check`
passou. Essas checagens diretas precederam a integração; nenhuma executou o
jogo original.

Com o diretório do repositório como cwd, os comandos diretos de compilação e
execução usados para os três testes foram:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Werror -Iinclude src/hit_controls.cpp tests/hit_controls.cpp -o /tmp/odh-hit-controls-test && /tmp/odh-hit-controls-test
g++ -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Werror -Iinclude src/hit_selection.cpp src/hit_controls.cpp tests/hit_thread.cpp -o /tmp/odh-hit-thread-test && /tmp/odh-hit-thread-test
g++ -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Werror -Iinclude src/hit_selection.cpp src/hit_controls.cpp tests/hit_selection.cpp libodh_sound.a libodh_hit_resources.a libodh_world.a libodh_vm.a libodh_entry_points.a libodh_formats.a -o /tmp/odh-hit-selection-test && /tmp/odh-hit-selection-test
```

O último comando depende dos arquivos `.a` pré-existentes nesse checkout; os
builds integrados abaixo recompilaram o código atualizado.

## Integração Linux verificada

Build e CTest Debug configuraram 113 testes: 112 passaram, um teste de janela
X11 foi pulado pelo ambiente, sem falhas (14,42 s). ASan/UBSan teve o mesmo
resultado, 112/113 mais o skip X11, sem falhas (26,33 s); LSan permaneceu
desligado. A configuração sem SDL/X11 passou 7/7 testes selecionados (0,59 s).
Os registros estão em `reports/hit-native-controls-build-{configure,build,ctest}.log`,
`reports/hit-native-controls-build-asan-{configure,build,ctest}.log` e
`reports/hit-native-controls-build-noaudio-{configure,build,ctest}.log`.

O probe Linux da cena `chair_sit_metal` usou recursos do jogo fornecidos
externamente, `--audio-caller-args`, Jump Literal32, chooser índice zero e
`--audio-native-controls 1024 512`. Debug e ASan/UBSan passaram. A requisição
guardou argumentos brutos `[2,0,0]` e o recibo de controles foi idêntico nos
dois: ID 17=32, 18=1024, 19=1024, 20=512, 21=3600, 25=10, 46=1, 47=2,
50=0. O valor zero bruto de ID 50 não foi substituído pela patch escolhida.
O probe registrou 94 dispatches BHAV, 662 ticks de simulação, 664 polls do
scheduler de áudio, uma thread ainda ativa e 30.208 frames PCM renderizados,
submetidos e consumidos, sem fault. O drain de host bombeou apenas PCM:
`pcm_drained_hit_pending` descreve a thread HIT ainda pendente, não conclusão
do som ou progresso adicional da VM. A comparação retida em
`reports/hit-native-controls-chair-comparison.json` marca o trace/estado
anterior de VM e World iguais ao baseline do caller, excluindo o relatório de
áudio. Comando e recibos ficam em `reports/hit-native-controls-chair-command.json`
e `reports/hit-native-controls-chair-{debug,asan}.json`.

Esses são resultados de fixture/host Linux, não execução nativa original nem
prova de áudio físico ou de fidelidade em 3DS/Vita/Android. Extvol e pan do
probe foram fornecidos explicitamente; esta unidade não deriva geometria
espacial, agregação de fontes, grupos/ducking, provider compartilhado,
conversão Q15, unidades finais do mixer nem efeitos DSP de escritas runtime.
Consulte também [os limites dos argumentos do caller](hit-native-caller-arguments.md).
