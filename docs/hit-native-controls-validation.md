# Validação dos controles HIT nativos antes do primeiro tick

Checkpoint iniciado em 2026-10-15 e ampliado sobre a base publicada `84a66b2`.
O perfil opcional de inicialização bruta ArgStyle 2 descreve valores visíveis
ao primeiro script; sua extensão recente cobre defaults fresh do banco 50–62 e
uma primitiva separada de atualização ativa sem reset. O estado bruto não
converte controles em ganho ou pan do backend. A base da interpretação é a [evidência estática da imagem
Sims.exe fornecida](hit-native-controls-evidence.md), sem execução do jogo
original e sem equivalência comprovada ao outro executável fornecido.

## API e ordem

`build_native_hit_controls(track, raw_arguments, spatial_external_volume,
spatial_pan, controllers)` é puro e devolve `HitNativeControls` próprio. As três
posições de `raw_arguments` são os valores assinados de R1/R2/R3; as duas
entradas espaciais são valores brutos fornecidos pelo caller. O builder exige
ArgStyle 2 e `Track.volume`, `Track.ducking_priority`, `Track.patch_id` e
`Track.Spl` resolvidos. A conversão das palavras de Track para int32 preserva o
padrão de bits. O banco opcional de controllers tem 31 posições para IDs
17–47; ausência ou `INT32_MIN` significa unset e não substitui o valor anterior.

A ordem implementada é:

1. ID 16 começa conhecido em zero. Os 31 IDs 17–47 recebem o banco completo
   de defaults evidenciado. Os valores não zero são 17=32, 18=1024, 19=1024,
   20=512, 21=3600, 28=100, 29=10000 e 39=1; os demais desse banco são zero.
2. IDs 50–62 recebem defaults fresh: todos zero, exceto ID 52=120 e ID 57=20.
   `Track.volume` e `Track.ducking_priority` escrevem IDs 18 e 25 quando
   diferentes de `INT32_MIN`; caso unset, seus defaults são mantidos.
   `Track.patch_id` e `Track.Spl` escrevem IDs 50 e 57 quando não sentinelas.
   O recibo conserva os bits raw mesmo quando são `INT32_MIN`, mas os locals
   mantêm os defaults fresh (50=0, 57=20). Zero explícito substitui o default.
   Overrides de controller não unset substituem IDs 17–47.
3. ArgStyle 2 escreve R2 em ID 19, R3 em ID 20, R1 em ID 47 e `isplaying=1`
   em ID 46. R1/R2/R3 continuam intactos nos registradores 1–3.
4. O caller fornece extvol espacial final em ID 19 e pan espacial final em
   ID 20. O último setter estabelece os flags iniciais `zero=(pan==0)` e
   `sign=(pan<0)` antes da primeira instrução. ODH não modela o flag positive.

O valor efetivo inicial de ID 50 é `Track.patch_id`, exceto que sua sentinela
`INT32_MIN` mantém o default zero; pode ser zero mesmo quando o chooser inicial
seleciona uma patch não zero. A escolha inicial continua sendo feita uma única
vez pela regra atual de SetTrack, sem sobrescrever o local 50. Instalar controles
não executa bytecode, NoteOn,
callbacks de backend nem efeitos de escrita de patch. `HitNotePlan` guarda
`initial_arguments` e `initial_controls` próprios; seus flags e locals já são
consultáveis em `HitThread::state()` antes do primeiro `tick()`.

`select_hit_note` e `HitThread` aceitam o perfil como último argumento opcional.
O construtor exige a tupla explícita de argumentos e Track ArgStyle 2
correspondentes antes de invocar o chooser. O snapshot público é verificado:
IDs 16–47 e 50–62 devem estar conhecidos; IDs 48/49 e 63–69 devem permanecer
desconhecidos. IDs 16, 19, 20, 46, 47, 50, 52 e 57 devem corresponder aos
defaults/overlays e stores finais. Os outros valores de 17–47 podem variar por
controller. Leitura de um local
nativo ainda desconhecido falha até que o script o escreva. Uma escrita nativa
em ID 18 altera volume, sem selecionar patch; a escrita em ID 50 conserva a
substituição de patch quando há correspondência no HOT. O estado inicial não
aciona esses efeitos de escrita.

Sem o argumento opcional, o perfil legado permanece: registradores/locals
zerados, exceto R1=12, flags zero/sign falsos, e o alias histórico de escrita
18/50 para patch. O novo perfil não altera limites de instruções, trace,
escolhas ou notas existentes. Seu estado adicional tem tamanho fixo:
54 locals opcionais e 31 entradas opcionais de controller; não há crescimento
por nota ou por tick nessa inicialização.

## Gate temporário do opcode 69

Com `initial_controls`, opcode 69 consome o byte de operando e falha
explicitamente antes do orçamento de solicitações, do callback legado e do
trace de efeito. Isso preserva o diagnóstico de bytecode truncado quando o
operando falta. `HitThread` fixa a falha e não tenta o efeito novamente em
ticks posteriores. Sem `initial_controls`, as solicitações offline e o
callback/trace legados continuam como antes. O gate não implementa a operação
nativa: o byte é uma localização de variável e a execução depende de registry
de controllers, fade/retry e Clock que ODH ainda não modela. A
[pesquisa estática do opcode 69](hit-native-group-kill-evidence.md) registra a
evidência e seus limites; não interpretar o nome legado `kill_group` como
semântica nativa comprovada.

## Verificação direta — unidade inicial

Os totais abaixo são da implementação inicial do perfil e do gate, preservados
como resultados históricos; os checks do retrigger estão separados mais abaixo.

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

O gate posterior foi testado diretamente em C++20 com warnings-as-errors:
949 checks de seleção e 68 de thread passaram. Operandos 0 e 1 sob o perfil
nativo rejeitam mesmo com callback presente, sem efeito, registro ou retry;
o cursor consome o operando e fica estável após a falha. O operando truncado
mantém o diagnóstico de bytecode incompleto. O perfil legado continua a
executar seu callback e a registrar o trace. ASan/UBSan também passou nos
mesmos dois testes com `detect_leaks=0` pela restrição de ptrace/LSan deste
runner. Os arquivos de seleção alterados foram recompilados diretamente; o
teste de seleção ligou bibliotecas estáticas existentes do checkout. A
validação oficial integrada do gate está registrada separadamente abaixo; as
contagens seguintes da primeira integração pertencem à base `2604e86`.

## Contrato ampliado — banco fresh e atualização sem reset

Auditoria estática posterior confirmou o banco fresh 50–62 e o ramo de
atualização de controller ativo, não pausado e com `interrupt`55=0. O builder
agora exige `Track.Spl` resolvido e inclui o campo raw no recibo. O banco 50–62
começa todo em zero, exceto 52=120 e 57=20; `Track.patch_id` e `Track.Spl`
substituem 50 e 57 quando não são `INT32_MIN`. Se a palavra raw é a sentinela,
ela continua no recibo, enquanto o valor efetivo conserva 50=0 ou 57=20. Um
valor explícito zero é preservado. A mask known atual é IDs 16–47 e 50–62;
48/49 e 63–69 permanecem desconhecidos. O initializer HOT que não resolve Spl
é recusado antes do chooser/backend.

`HitPatchMode::raw` continua sendo o modo padrão de `resolve_hit_event` e
preserva a interpretação legada literal de patch IDs. O factory escolhe
`native_controller` quando recebe `initial_controls`; nesse modo, patch
`INT32_MIN` é efetiva zero e a seleção inicial pode usar a hitlist sem alterar
metadata nem local50. Esse modo é explícito no factory e no host.

`HitThread::update_native_active_unpaused(raw, spatial_external_volume,
spatial_pan)` é uma primitiva de estado para um controller que o caller já
admitiu. O caller valida a fase e os gates externos; a primitiva só verifica
perfil nativo em execução, ID55 conhecido e zero, e ID53 conhecido e zero
(provider próprio no domínio representado). Ela limpa ID45, grava R1–R3 e os
controles espaciais em 19/20 e atualiza os flags zero/sign. Preserva PC, trace,
handles/notas, orçamento, recibo de inicialização, source47 e patch50. Não
representa o byte +`0x1cf` nem o ponteiro de provider +`0xc8` do original e não
é replay completo de `0x57dc59`. Não executa chooser, binding, provider ou DSP,
nem implementa o agendamento/Clock do controller. Pausa e restart
`interrupt55!=0` não são cobertos. Falhas de pré-condição rejeitam antes da
mutação. Reentrada durante `tick` marca falha; se SourceReader ou chooser
engolir a exceção, a verificação após o callback ainda aborta antes de novas
escritas ou início de nota.

Os checks diretos desta extensão passaram em C++17 com warnings-as-errors:
74 de controles, 965 de seleção e 108 de thread; ASan/UBSan passou nos mesmos
três executáveis. LSan permaneceu desativado por ptrace. A auditoria associada
conferiu 934 linhas, 2.917 bytes de instruções e 11 destinos de vtable; recibo
em registro arquivado não distribuído. O executável original
não foi executado. CTest global e fixture desta unidade estão registrados em
[hit-native-retrigger-validation.md](hit-native-retrigger-validation.md).
Contrato completo e lacunas em
[hit-native-retrigger-validation.md](hit-native-retrigger-validation.md) e
[evidência estática do retrigger](hit-native-retrigger-evidence.md).

## Integração Linux verificada — base `2604e86`

Build e CTest Debug configuraram 113 testes: 112 passaram, um teste de janela
X11 foi pulado pelo ambiente, sem falhas (14,42 s). ASan/UBSan teve o mesmo
resultado, 112/113 mais o skip X11, sem falhas (26,33 s); LSan permaneceu
desligado. A configuração sem SDL/X11 passou 7/7 testes selecionados (0,59 s).
Os registros estão em registro arquivado não distribuído,
registro arquivado não distribuído e
registro arquivado não distribuído.

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
registro arquivado não distribuído marca o trace/estado
anterior de VM e World iguais ao baseline do caller, excluindo o relatório de
áudio. Comando e recibos ficam em registro arquivado não distribuído
e registro arquivado não distribuído.

## Integração Linux verificada — gate posterior

O build e CTest oficiais do gate configuraram 113 testes em Debug: 112
passaram, o teste de janela X11 foi pulado pelo ambiente e nenhum falhou
(12,90 s). ASan/UBSan repetiu 112 passes e o mesmo skip sem falhas (25,56 s);
LSan permaneceu desligado. Sem SDL/X11, passaram os 7 testes selecionados
(0,63 s). Os logs são
registro arquivado não distribuído,
registro arquivado não distribuído e
registro arquivado não distribuído.

O probe `chair_sit_metal` foi repetido em Debug e ASan com o comando exato da
base `2604e86`. Ambos passaram; recibos de controles, requisições, estado e
trace VM/World ficaram iguais aos da base, conforme
registro arquivado não distribuído. Permaneceram 662 ticks
World, 664 polls de áudio, 30.208 frames PCM consumidos e
`pcm_drained_hit_pending`. Comando e recibos estão em
registro arquivado não distribuído e
registro arquivado não distribuído. Essa cena não executa
o opcode 69 nativo; confirma regressão da rota de áudio já exercitada, não
semântica implementada para o opcode.

Esses são resultados de fixture/host Linux, não execução nativa original nem
prova de áudio físico ou de fidelidade em 3DS/Vita/Android. Extvol e pan do
probe foram fornecidos explicitamente; esta unidade não deriva geometria
espacial, agregação de fontes, grupos/ducking, provider compartilhado,
conversão Q15, unidades finais do mixer nem efeitos DSP de escritas runtime.
Consulte também [os limites dos argumentos do caller](hit-native-caller-arguments.md).
