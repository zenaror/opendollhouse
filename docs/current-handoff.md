# Continuidade atual

A meta integral do OpenDollhouse continua: primeira versão funcional e
verificável no Linux, com New 3DS, Vita e Android como etapas futuras e sem
validação funcional desses ports. Leia `AGENTS.md`, `docs/architecture.md` e
as notas de validação desta unidade antes de continuar.

A OMM voltou. O papel `project-base` e o handoff foram consultados; o remoto
`main` e o checkout publicados foram confirmados em `84a66b2`, e os hashes dos
13 arquivos cobertos pela validação continuam iguais. A evidência de retrigger
foi importada na OMM em
`sources/OpenDollhouse/checkpoints/hit-native-retrigger-evidence-84a66b2-2026-10-15.md`
(SHA256 `46531342d7d5ed4388bfd7295dc810bfce6941daaf33b38ede42376a9e4745cb`).
O handoff antigo foi preservado na OMM em
`sources/OpenDollhouse/checkpoints/handoff-before-native-retrigger-2026-10-15.md`
(SHA256 `6bbd63677c8a9e9645d4d3d547030c931d01a50077d5ff8091dbc5fdde821124`).
Antes de atualizar o handoff OMM, consultar seu `expected_sha256` vigente e
usá-lo na escrita condicional. Verificar separadamente a revisão Git publicada.
A revisão desta unidade é a do commit que contém este arquivo; conferir no
handoff OMM qual foi o último checkpoint sincronizado.

## Checkpoint de 2026-10-15

Base publicada `84a66b2`. Esta unidade amplia o builder fresh de controles
nativos e adiciona uma primitiva local para atualização de argumentos/controles
sem reset. A descrição completa, testes e limites estão em
[hit-native-retrigger-validation.md](hit-native-retrigger-validation.md); a
prova estática está em
[hit-native-retrigger-evidence.md](hit-native-retrigger-evidence.md).

`build_native_hit_controls` exige ArgStyle 2 e volume, ducking priority, patch
e Spl resolvidos. Instala defaults em 16–47 e 50–62; o segundo banco tem 52=120,
57=20 e zero nos demais IDs. Sentinelas raw de patch/Spl ficam no recibo e
mantêm valores efetivos 50=0/57=20; zero explícito continua zero. IDs 48/49 e
63–69 ficam desconhecidos. `HitPatchMode::raw` é o default compatível com o
perfil legado; factory/host escolhem `native_controller` para o perfil nativo.
A escolha inicial de hitlist não sobrescreve ID50.

`update_native_active_unpaused` requer uma thread nativa em execução e IDs 55 e
53 conhecidos e zero. O caller responde pelo estado e gates externos. A
primitiva limpa 45, atualiza R1–R3, 19/20 e flags zero/sign, preservando PC,
trace, notas/handles, orçamento, recibo, 47 e 50. Não representa `+0x1cf` ou
`+0xc8`, nem executa chooser, binding, provider, backend ou Clock. Ela não
implementa o manager persistente, admissão, lifecycle ou os outros ramos de
retrigger. Não deduplicar por nome nem criar ticket por admissão.

Opcode 69 continua com gate explícito no perfil `initial_controls`; o callback
legado permanece no perfil legado. A tabela de fontes `(TrackID, R1)` também
segue como fundação portátil, ainda sem integração ao manager/World/áudio. Os
contratos e históricos correspondentes estão em
[hit-native-controls-validation.md](hit-native-controls-validation.md),
[hit-source-contributions-validation.md](hit-source-contributions-validation.md)
e [hit-native-track-identity-evidence.md](hit-native-track-identity-evidence.md).

## Validação e limites

Checks diretos C++17 com warnings-as-errors passaram: 74 de controles, 965 de
seleção e 108 de thread; ASan/UBSan passou nos mesmos três executáveis. LSan
ficou desligado sob ptrace. Factory passou 76 checks diretos, XA 56; a suite
focada `sound_event_factory`, `xa_sound_factory` e `bhav_audio_cli` passou 3/3
(5,01 s). Logs da suite focada em
`reports/hit-native-retrigger-factory-{configure,build,ctest,repeat}.log`.

Os CTests oficiais configuraram 114 casos: Debug passou 113, um teste X11 foi
pulado e zero falharam (7,03 s). ASan/UBSan teve os mesmos 113 passes e um skip
(13,18 s); LSan permaneceu desligado. Sem SDL/X11 passaram 8/8 testes (0,62 s).
Logs em `reports/hit-native-retrigger-{build,build-asan,build-noaudio}-{configure,build,ctest}.log`.

O probe `chair_sit_metal` passou em Debug e ASan/UBSan: 94 dispatches, 662
ticks World, 664 polls de áudio e 30.208 frames PCM renderizados, submetidos
e consumidos; HIT terminou pendente/ativo, sem fault. A comparação JSON indica
VM, World, requisições e traces iguais ao baseline do gate; o relatório de
áudio também coincide, exceto pelo tempo de drain medido por relógio de parede.
Não é comparação byte a byte das amostras PCM. Ver
`reports/hit-native-retrigger-chair-{command,debug,asan}.json`,
`reports/hit-native-retrigger-chair-comparison.json` e o manifesto
`reports/hit-native-retrigger-validation.json`.

A fixture não exercita a primitiva de atualização ativa nem sentinelas no asset
real; esses casos têm regressões sintéticas. A auditoria estática conferiu 934
linhas, 2.917 bytes e 11 destinos de vtable em
`reports/hit-native-retrigger-verification.json`; o executável original não
foi executado. A revisão do core/factory e da nota estática terminou sem defeito
material. `git diff --check` passou.

## Próximas unidades

Preservar como pendentes saved frames 105/137 e a fila/Clock dormantes; cobrir
separadamente sem confundir polls de áudio com ticks de World.

Integrar um manager persistente indexado por HOTTrackID, separando geração de
World e binding PCM, e só então ligar a primitiva aos gates e estado reais do
controller. Fechar admissão, fonte/provider, remoção/reuso de ObjectID,
registro e teardown, pausa e restart com `interrupt55!=0`, Clock e os demais
caminhos antes de atribuir lifecycle completo. O builder fresh já cobre
50–62; não é uma pendência desta unidade.

A meta Linux mais ampla ainda inclui saved frames/queues/Clock (incluindo
105/137), continuação de Main sem reinit, câmera/picking, input, lote com SLOT/failure trees/needs/
inventory/save e SDK/CC; depois, ports com validação própria. Preservar dados
comerciais, saves, referências upstream, relatórios e builds existentes. Não
usar `git add -A` nem limpeza genérica.
