# Controller HIT nativo portátil

Implementação em C++ do subconjunto de execução HIT nativo, sobre a base
`f5ab084`. O controller mantém bancos próprios, recebe o Clock do caller e
opera sobre um único binding de áudio corrente fornecido por callbacks. A API
não está integrada ao manager do World nem ao host de áudio do probe.

A pesquisa estática que orientou o subconjunto está em
[hit-native-controller-execution-evidence.md](hit-native-controller-execution-evidence.md).
Ela cobre 535 linhas e 1.594 bytes de instruções, com oito entradas de opcode e
mapeamento PE conferido; o executável original não foi rodado. A nota tem SHA256
`635e6f322f12efe922b3143872c5c4f0da58ca8573129d0005f6c8749707154f`; o assembly
selecionado tem SHA256 `f0171402d4637498e9ada7449c7aec0303faa59e1d66eed97cd7c7f05048d9a8`;
recibo em `reports/hit-native-controller-execution-verification.json`. A
revisão da evidência e do core terminou sem defeitos materiais após correções
de ordem de ativação, diagnóstico, cleanup/reentrada e precondições.

## Estado e operações

`NativeHitController` recebe HIT/HOT/track, entry PC, argumentos brutos,
controles espaciais, callbacks de áudio, limites ODH e overrides. O perfil exige
ArgStyle 2, provider próprio e metadata já resolvida. `entry_pc` é o offset do bytecode já resolvido pelo caller; `hitlist_id` não zero
fornecido na HOT é aceito como metadata, sem executar chooser e sem substituir
ID50. O caller resolve TrackID
e gates de admissão/grupo, e mantém a vida dos objetos e callbacks. O construtor prepara
snapshot sem efeitos de áudio: ID46=0 e IDs19/20 contêm R2/R3. Isso não executa
o parser HOT nem o initializer nativo. Os bancos são separados: persistente
1–16, controles 17–47 e 50–62. R4–R16 permanecem no restart e no stop.

`activate(clock)` reconstrói os bancos fresh da metadata, inclusive após
`stop()` no snapshot preparado, e ordena binding, ID46/estado ativo, pausa do
binding, setters espaciais finais em 19/20 e retomada. `poll(clock)` recebe um
valor explícito de Clock a cada chamada; o controller não determina cadência
World. `pause()` e `resume(clock)` controlam o binding corrente. `retrigger(...)`
pressupõe que o caller já aprovou o evento: ativo e pausado retoma antes de
examinar ID55; ativo e não pausado com ID55=0 atualiza sem reset; inativo ou
ativo com ID55 diferente de zero reinicia. `stop()` desativa o binding, mas
preserva o controller e o estado associado ao TrackID.

Callbacks de áudio consultam atividade, recebem o byte literal de NoteOn,
aplicam controles raw e executam pausa, retomada e teardown/substituição do
binding corrente. NoteOn2 não é interpretado como handle nem ligado a
`LastNote`. `bind` deve ter garantia forte de exceção; o owner externo conserva
os objetos/capturas até o fim da vida do controller, é responsável pela limpeza
final e resolve qualquer teardown parcial quando `release_binding` falha.
Os callbacks de teardown precisam tolerar tentativas de limpeza repetidas
durante rollback e cleanup do owner externo.
Stop e restart tentam liberar binding mesmo quando `stop_current` falha ou
ocorre reentrada absorvida. Rollback excepcional para limpar um binding obtido
continua permitido depois de um fault; os faults de execução/callback ficam
fixados e impedem novos efeitos comuns. A API não define quantidade de vozes,
DSP, compartilhamento de buffers ou semântica interna do backend.

No perfil `global_catalog`, NoteOn2 agora usa a
[sequência global explícita](hit-global-note-on-validation.md): getter público
38 do controller, writer do wrapper se não zero, NoteOn do wrapper, getter
38 do Patch vivo depois do play e A/B separados se esse último valor não
for zero. O callback local `note_on` não serve de fallback. O pacote completo
de seis callbacks é exigido antes do primeiro efeito de um NoteOn com binding;
programas que não chegam a esse caminho não precisam inventar providers.
Getters, writer e efeitos A/B concretos continuam responsabilidade do provider.

## Bytecode e limites

O subconjunto implementa opcodes 2, 5, 7, 8, 9, 11, 12 e 27: NoteOn literal,
LoadL, chamada por chave, retorno/fim, Wait, WaitSamp e solicitação de stop.
Call7 exige um resolvedor explícito fornecido pelo caller e usa pilha limitada;
8 e 12 retiram o último retorno ou solicitam stop se a pilha estiver vazia.
A extensão com `HitEntryCatalog` e `NativeHitResourceResolver` conserva arquivo
e PC por alvo/frame; a ordem dos grupos é fornecida explicitamente. A origem
TrackData e os buffers independentes foram rastreados no original; a ordem
automática de descoberta ainda está pendente. Contrato e verificação atuais em
[hit-native-entry-map-validation.md](hit-native-entry-map-validation.md).
A extensão inicial,
incluindo preservação da pilha no restart ativo, está em
[hit-native-call-validation.md](hit-native-call-validation.md).
Opcode1 é recusado; assim,
o deadline nativo separado em +194 permanece zero. Escritas e efeitos de
variáveis ficam limitados aos locais comprovados; outros opcodes, destinos ou
efeitos não modelados falham explicitamente. Alguns IDs ainda recebem metadata
raw durante reset sem que isso implemente seus efeitos gerais no provider.

Clock é usado literalmente, incluindo zero e wrap uint32 das deadlines. Clock
com bit alto segue o diagnóstico/stop nativo depois do processamento de fade.
O diagnóstico nativo após a 102ª instrução interrompe o controller por caminho
próprio; limites menores configurados pelo ODH podem produzir fault de
segurança. Esses limites e estados são distintos.

## Verificação

No checkpoint inicial `0ce6cb2`, os checks diretos do core passaram 82 casos com `-Werror`, `-Wconversion` e
`-Wsign-conversion`; ASan/UBSan passou pelos mesmos checks. Os CTests oficiais
configuraram 115 casos: Debug passou 114, um teste X11 foi pulado e zero
falharam (6,97 s). ASan/UBSan teve os mesmos 114 passes e um skip (13,10 s),
com LSan desativado. Sem SDL/X11 passaram 9/9 testes (0,66 s). Logs de configure,
build e CTest em `reports/hit-native-controller-core-build-{configure,build,ctest}.log`,
`reports/hit-native-controller-core-build-asan-{configure,build,ctest}.log` e
`reports/hit-native-controller-core-build-noaudio-{configure,build,ctest}.log`.

Um oráculo Python independente conferiu novamente 6.331 casos em Debug e
6.331 em ASan/UBSan após a guarda de entry key, cobrindo ADD/NEG de 32 bits,
INT32_MIN, igualdade estrita e flags; seed `0x57dac6`. Scripts, driver e recibos estão em
`reports/hit-native-controller-fade-oracle.py`,
`reports/hit-native-controller-fade-oracle-driver.cpp` e
`reports/hit-native-controller-fade-oracle-{debug,asan}.json`. O manifesto de
hashes, CTests, oráculo e auditoria está em
`reports/hit-native-controller-core-validation.json`.

Não foi feito probe da cadeira nem teste de backend físico: esta classe não está
integrada ao host de áudio, e esse probe não exercitaria o core novo. A cobertura
é sintética/host. Não se reivindica comportamento físico de áudio, equivalência
ao executável original ou validação dos ports.

O controller ainda não implementa backend de vozes/DSP, cadência externa,
manager persistente por HOTTrackID, integração com World, provider espacial ou
gates de admissão. Esses são os próximos contratos de integração, não resultados
desta unidade.
