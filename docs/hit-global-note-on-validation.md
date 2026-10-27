# Sequência global de NoteOn no controller HIT

Unidade baseada em `bcc0ed2a8d9d760c4908406a0bde7e60baa2113d`, preparada em
2026-10-20. O objetivo é representar a ordem externa do NoteOn global
demonstrada em `0057f88e`, mantendo as responsabilidades dos providers
explícitas. Isso avança a ligação entre controller e áudio; o adapter físico
e a primeira versão Linux jogável continuam pendentes.

## Contrato e origem

A [pesquisa do backend comum](hit-native-common-backend-evidence.md) identifica
getter público do controller em `0057f8c9`, writer do wrapper em `0057f8dd`,
NoteOn do wrapper em `0057f8ec`, recarga do Patch corrente em `0057f8f2`,
getter do Patch em `0057f8fe` e A/B em `0057f90a`/`0057f911`.
Essa evidência estática pertence à imagem identificada naquele documento.
Esta unidade reutiliza a pesquisa existente e não executa o jogo original.

`NativeHitAudio::global_note_on` contém seis callbacks:

| Callback | Responsabilidade |
| --- | --- |
| `controller_id38(snapshot)` | Resolver o getter público efetivo do controller antes do play, inclusive fallback de provider quando necessário. O banco cru do snapshot não é automaticamente o valor efetivo. |
| `wrapper_write(38,1,0)` | Aplicar essa operação exata ao wrapper quando o primeiro getter não for zero. Não é uma escrita no banco do controller nem uma interpretação automática como streaming. |
| `wrapper_note_on(literal)` | Executar o NoteOn do wrapper e retornar seu resultado booleano. As operações internas do wrapper pertencem ao provider. |
| `patch_id38()` | Consultar o Patch corrente vivo depois de NoteOn. Pode ser uma fonte diferente da que existia antes da chamada. |
| `controller_a(snapshot)` | Executar A se o getter pós-play do Patch for não zero. |
| `controller_b(snapshot)` | Executar B depois de A, sob o mesmo gate. |

Opcode2 chama o pacote nessa ordem. `false` retornado pelo wrapper não evita
o getter do Patch nem A/B; o opcode não grava esse resultado ou o literal
em registradores/flags. Valores assinados negativos também satisfazem
`!= 0`. O callback local `note_on` continua próprio do perfil `local_hot`
e nunca supre um provider global ausente.

O pacote inteiro é validado antes do primeiro getter de um NoteOn global
com binding. Se algum callback faltar, a execução falha explicitamente.
Programas globais sem esse opcode, ou sem binding adquirido, mantêm o
comportamento anterior. O caminho sem binding não reproduz a mensagem
diagnóstica do helper original; ele permanece sem efeitos de áudio.

Cada callback é envolvido pela checagem de reentrada já usada pelo controller.
Exceção ou tentativa de mutação reentrante, mesmo absorvida pelo callback,
fixa o fault e impede as chamadas seguintes. Uma falha durante `poll`
mantém a responsabilidade de teardown no owner externo, como antes;
não cria um novo rollback de binding. Getters/writer/A/B podem alterar
estado externo de providers, mas não podem reentrar mutações do controller
ou modificar os HIT/HOT/catálogos imutáveis. Snapshots são cópias válidas
apenas durante sua chamada.

## Matriz de aceitação

Os casos de `tests/hit_global_note_on.cpp` são executados em um único lote
parametrizado, com resultado por ID e total. Uma falha não interrompe a
execução dos demais casos.

| Critério | Casos | Gate |
| --- | --- | --- |
| Ordem e fontes distintas; A/B independem do resultado de play | 8 combinações controller38 `{0,-3}` × Patch38 pós-play `{0,-5}` × play `{false,true}`; em todos, a fonte do Patch é trocada durante NoteOn para um valor com condição zero/não-zero oposta | Debug; ASan/UBSan; sem SDL/X11 |
| Getter efetivo distinto de banco cru | Nas 8 combinações, banco cru é 9 com getter efetivo zero, ou banco cru zero com getter efetivo não zero | Mesmos gates |
| Predicado não-zero assinado e literal integral | `INT32_MIN` em ambos os getters; literais 0 e 255 | Mesmos gates |
| Providers incompletos falham antes de qualquer efeito | Cada um dos 6 callbacks ausente, incluindo callbacks de ramo que o valor zero evitaria | Mesmos gates |
| Falha/reentrada interrompe sequência e fixa fault | Exceção e reentrada absorvida em cada um dos 6 estágios | Mesmos gates |
| Limites de perfil | Binding zero, Patch ausente, opcode não alcançado, perfil local completo e perfil local sem seu callback | Mesmos gates |

Total: 34 casos. A matriz também confere os argumentos do writer,
preservação dos bancos/flags, passagem do literal, trace e deadline do Wait
seguinte, ausência de fallback local e o efeito de A observado por B.

As integrações existentes `hit_native_controller`, `hit_global_patch_binding`
e `native_patch_pcm` declaram providers sintéticos zero/zero para os dois
getters. Os ramos writer/A/B dessas fixtures rejeitam chamadas inesperadas.
O resultado de play `true` é uma decisão da fixture; não prova a política
do backend original. `hit_entry_catalog` e `hit_resource_bank` cobrem
consumidores do contrato de controller que continuam no perfil local.

## Evidência e limites

O runner local registro arquivado não distribuído registra
comandos, configuração, ambiente, SHA256 das fontes antes/depois, resultado
por caso, resultado por teste CTest e agregado de cada gate. Cada configuração
tem build e logs próprios. Nenhum PASS de Debug supre os demais gates.

| Gate final | CTests selecionados | Matriz parametrizada | Duração do comando CTest |
| --- | --- | --- | --- |
| Debug, SDL/X11 habilitados | 6 PASS, 0 FAIL, 0 não executados | 34 PASS, 0 FAIL, 0 bloqueados/não executados | 0,096 s |
| Debug com ASan/UBSan | 6 PASS, 0 FAIL, 0 não executados | 34 PASS, 0 FAIL, 0 bloqueados/não executados | 0,146 s |
| Debug sem SDL/X11 | 6 PASS, 0 FAIL, 0 não executados | 34 PASS, 0 FAIL, 0 bloqueados/não executados | 0,117 s |

Sanitizers usam `ASAN_OPTIONS=detect_leaks=0` e
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`; LSan não é coberto.
Os receipts `debug-final.json`, `asan-ubsan-final.json` e
`no-sdl-x11-final.json` confirmam os mesmos hashes das sete fontes de
produção/teste/CMake antes/depois. São seis testes focados por configuração,
não a suíte completa. Os alvos afetados e os consumidores de controller
selecionados passaram; as baterias antigas não foram repetidas retroativamente.

A primeira execução Debug teve quatro falhas de fixture: o teste tentava
injetar `INT32_MIN` pelo override de inicialização, que esse caminho já
normaliza. A fixture final usa banco cru 9/getter zero e cru zero/getter não
zero; a implementação de inicialização não foi alterada. Essa tentativa,
com 30/34 casos PASS e 5/6 CTests PASS, permanece em `debug.json`,
logs sem sufixo `final` e `initial-matrix-test.cpp`. Seu resultado não foi
promovido a PASS nem combinado com os gates finais.

Esta unidade implementa a sequência externa e testa providers sintéticos.
Não implementa getters efetivos de produção, a semântica interna do writer
38, A/B completos, o backend comum, os comandos internos de seek/gain/play,
streaming, dirty/preload, loop latch, DSP ou seleção de fonte física/virtual.
Também não demonstra áudio físico, execução do original, compatibilidade
integral, scheduler persistente ou hardware dos ports. As referências
comerciais e os relatórios anteriores permanecem preservados.
