# Host de execução do avatar e gates de inicialização

## Sessão de janela contínua (2026-10-22)

`odh-person --render-window` sem `--ticks` renova o orçamento de 10.000
instruções por entidade em cada tick; o agregado permanece limitado a 64
entidades. Os tetos históricos de 4.096 ticks e 10.000 instruções acumuladas
deixam de congelar a janela. O relógio continua limitado a `uint32` pelo contrato
dos controladores. `--ticks N` explícito conserva o intervalo aceito 0..4096 e
interrompe o avanço no tick escolhido. Probes sem janela conservam 256 ticks
por padrão e orçamento total de 10.000 instruções, incluindo o bootstrap.

Os logs de trace (incluindo a segunda VM), RNG, globals, tuning, vizinhos,
enumeração de entidades, relacionamento e inventário guardam coleções
limitadas; a animação guarda eventos limitados também por bytes, podendo
descartar um evento grande e reter um posterior menor. Saturação descarta
somente evidência, sem lançar erro ou encerrar a VM. Sound, EntryPoint5 e
posições de movimento também contabilizam descartes.
Mantêm-se os limites anteriores de cada coleção: 20.000 leituras de
neighbor/global/tuning, 10.000 nos demais logs de execução, 4.096 em sound e
EntryPoint5, 128 posições de movimento e 256 KiB lógicos de eventos de animação.
O JSON expõe `diagnostics_truncated`, `diagnostics_dropped` por categoria,
`runtime_policy` e `runtime_tick_limit`; a última instrução observada permanece
disponível para contexto de falha mesmo depois que o trace enche.

A tecla P pausa e retoma; Escape fecha a janela pelo mesmo polling durante pausa ou
limite explícito. Os logs locais existentes recebem início da execução,
pausa/retomada, fechamento e falhas, sujeitos ao teto de arquivo do logger.
Exceder um slice de instruções pode ser retomado no próximo tick na janela;
faults reais de VM e falhas dos controladores continuam falhas. Não se
afrouxaram parsing, pilha, filas, recursos, orçamento por entidade/tick ou
limites semânticos do Playback. Caches de recursos continuam vinculados aos
IDs/owners limitados do World; contextos de portal mantêm prune/erase e teto16.

O gate `person_session` usa duas VMs sintéticas durante 6.200 ticks acelerados,
ultrapassando 10.000 instruções em cada VM e saturando o trace; cobre também
grants independentes de CPU por entidade/tick, limite explícito, horizonte do
relógio, descarte por contagem/bytes e fault real após truncamento. `person_cli`
continua verificando os resultados dos probes finitos. Esses gates dispensam
display; input e apresentação X11 são um gate separado.

Os três testes que abrem janelas (`linux_window_backend`,
`linux_neighborhood_screen_actions` e `linux_move_out_integration`) ficam
desabilitados por padrão no CTest. Isso impede que `ctest` herde o `DISPLAY`
normal do usuário e abra janelas no desktop. Para habilitá-los, reconfigure
com `-DODH_RUN_X11_TESTS=ON` somente quando `DISPLAY` apontar para um servidor
virtual isolado; não use a sessão gráfica normal. Sem a opção, CTest registra
esses gates como skipped e executa o restante normalmente.

Validação Debug em build nova `/tmp/odh-session-build` (GCC13.3,
`ODH_X11=ON`, `BUILD_TESTING=ON`): build completa sem warnings, quatro gates
focados (`person_session`, `linux_session_log`, `person_animation_cli`,
`person_cli`) passam e a suíte CTest registra 168 testes aprovados e um
`linux_window_backend` skipped (`X11 DISPLAY inaccessible`). O teste integrado
de animação entrega 1.000 eventos sintéticos válidos, excede a retenção de bytes
e ainda completa Main, com contagem exata do descarte no JSON. PangoCairo não
está disponível nesta configuração; overlay textual e input/pause/fechamento
reais de X11 não foram validados. Não houve execução de jogo original nem
teste desta unidade em hardware ou com ASan/UBSan.

```sh
cmake -S . -B /tmp/odh-session-build \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DODH_X11=ON
cmake --build /tmp/odh-session-build -j 4
ctest --test-dir /tmp/odh-session-build --output-on-failure \
  -R '^(person_session|linux_session_log|person_animation_cli|person_cli)$'
ctest --test-dir /tmp/odh-session-build --output-on-failure -j 4
ctest --test-dir /tmp/odh-session-build -V -R '^linux_window_backend$'
```

Esta unidade habilita continuidade do host/preview; não declara gameplay
completo ou equivalência ao original. A primeira build jogável continua exigindo
o ciclo de bairro/overworld, criar/excluir/mudar famílias, salvar/continuar,
lotes e expansões
(Magic Town, Studio Town, Vacation, Downtown, House Party, Livin' Large e
Unleashed), idioma selecionável e diagnóstico de crashes/bugs no nível esperado
da referência Simitone para The Sims Complete Collection. Esta unidade não
implementa esse conjunto nem restaura stacks/filas salvas dormentes.

## Checkpoint histórico inicial

Checkpoint de 2026-10-13. `odh-person` liga um avatar e seu semiglobal a uma VM
permanente, com Sleep e InteractionQueue no mesmo relógio. Executa o Main real
ou uma rotina privada selecionada explicitamente e preserva o primeiro fault.
O avanço posterior de scope26 está em [tuning-validation.md](tuning-validation.md).
O gate posterior de SIMI/current lot/zoning está em [lot-validation.md](lot-validation.md).
O gate posterior com RNG compartilhado está em [random-validation.md](random-validation.md).
A conclusão do Init privado com scopes30/31 está em
[person-indirect-validation.md](person-indirect-validation.md).
O modo posterior `--initialize` está em [avatar-startup-validation.md](avatar-startup-validation.md).
A enumeração posterior de vizinhos está em [neighborhood-validation.md](neighborhood-validation.md).
É um host de diagnóstico sem janela; não substitui o preview e não representa
um avatar/lote completamente inicializado.

## Recursos e uso

O host carrega owner1 para os BHAVs privados do avatar e seu semiglobal;
globais são compartilhados. Um alvo opcional usa owner2. As entradas FAR são
exatas, e GLOB128 Pascal deve corresponder ao semiglobal. Recursos com BHAVs
fora de seu papel, duplicações, OBJD ausente, master/multitile e OBJf do avatar
não são aceitos. O perfil do construtor exige OBJD138/ObjectType2 (campo+18).
A definição de uma ação não é uma factory completa do alvo: o World constrói
apenas seu tipo/GUID e identidade para a fila.

```sh
./build/odh-person --avatar '../referencias/The Sims/GameData/Objects/Objects.far' 'People\TemplatePerson.iff' 128 --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

`--routine 4097` inicia essa rotina privada em vez do Main da definição. Não
executa automaticamente Init/entry8/body strings. `--action FAR ENTRY OBJD ID`
oferece uma ação explícita à fila; não avalia TTAB/eligibility nem presume que
o Main a alcançou. `--flags WORD` e `--age YEARS` são overrides explícitos de
fixture, registrados no JSON; não são valores extraídos de uma pessoa/saved lot.

```sh
./build/odh-person --avatar '../referencias/The Sims/GameData/Objects/Objects.far' 'People\TemplatePerson.iff' 128 --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff --routine 4097
```

O estado registra initialização incompleta, arquivos/GUID/owners/root, trace,
contexto da stack, estado final selecionado, ticks/dispatches, fila e completion.
Sleep solicita ciclos RNG ao callback, mas o backend RNG continua ausente.
Somente primitivas0/17 e o kernel Expression/chamadas estão ligados neste host;
serviços ausentes permanecem unsupported, sem sucesso substituto.

O host chama observe antes de run e após serviço/execução, conforme o contrato
da fila. Avança tick uma vez, passa o mesmo relógio a Queue e Sleep, e retoma
execução. Não reinicia a VM para cada ação. Limites:10.000 dispatches por run
completo, ticks configuráveis0..4096 (padrão256), capacidade FIFO32 e limites de
frame/owners do kernel. Trace é limitado pelo orçamento de dispatches.

Exit0 significa probe terminado sem fault/budget/ação pendente; o resultado
booleano de uma ação ainda está no JSON e não comprova interação correta.
Exit3 indica runtime fault, falha do host, budget ou ação oferecida incompleta,
mesmo se a rotina Main selecionada tiver retornado. Exit2 indica uso/recurso
inválido. O JSON conserva o status real da VM; não troca waiting/yielded/fault
por um sucesso artificial para encerrar o probe.

## Construtor parcial e ordem da referência

Fonte fixa FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2:
[VMEntity](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMEntity.cs),
[VMAvatar](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs),
[OBJD](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/OBJD.cs).

A referência cria ObjectData80 zero e seta ChairFacing (Flags8=256). O avatar
começa com PersonData101 e MotiveData16 zero e Contained3; base.Init executa
entrada0 e entrada8 e empilha Main. Depois, Avatar.Init inicializa animações,
body strings, idade/gênero, motivos100 exceto SleepState, e NeatPersonality1000.
A BodySTR200 deste TemplatePerson indica idade27; ela é aplicada após base.Init
na referência, portanto não deve ser antecipada ao executar seu Init nativo.

O host reproduz somente um subconjunto antes dessa pipeline: bancos101/16/3,
raw ObjectData0..78 quando não são getters derivados, Flags256. ObjectId,
container/slot/count são resolvidos pelo World já existente. Direction1,
Lockout25, Parent26, Room29, CurrentValue41, UseCount62 e a extensão79 não recebem
zeros fabricados; dependem de contexto/semântica ainda não ligados. A revisão
identificou Room/CurrentValue/UseCount inicialmente declarados como raw0; isso
foi corrigido e os testes verificam faults para esses seis getters omitidos.

Também não há atributos dinâmicos da referência: GetAttribute fora do Count
retorna0 e SetAttribute expande, enquanto World possui bancos fixos e recusa
índices ausentes. Esse limite é explícito. O objetivo de reprodução fiel ainda
requer resolver esses acessos, os efeitos de SetValue e a pipeline completa,
sem tratar o perfil parcial como inicialização do original.

## Estrutura nativa e execução observada no ODH

TemplatePerson.iff:8568 bytes, SHA256
`3b602335406c772fc9ec2b081daf6e7248d0d07795658ec06be85322bb71ca2e`.
OBJD128/version138 contém Main em+10=`4096`, Init em+86=`4097`, Load em+94=`4098`,
BodyStringID+38=`200`, UsesFnTable+44=`0`. Init4097 chama8192 e depois4100/traits.
Executar8192 ou seu primeiro trecho não completa Init4097.

Com os recursos externos e o perfil acima, as três execuções abaixo produzem
JSON byte idêntico entre Debug e ASan/UBSan:

| Probe | Dispatches / ticks | Fronteira observada |
|---|---|---|
| Main4096 | 6 / 1 | Após primeiro Sleep, global320/pc0 lê scope6/index10: lote atual não fornecido |
| Init4097 | 10 / 0 | Person8192/pc8 lê scope26/index16516 (`0x4084`): tuning global não ligado |
| Main com Sit de DiningChairs oferecido | 6 / 1 | Mesmo fault de lote; queue1, ação não iniciada e completion ausente |

Init executa Person8192 nos PCs `0,3,2,1,6,4,5,7`, escrevendo:

| Campo | Estado após esse trecho |
|---|---:|
| Flags8 | 264 (ChairFacing256 + CanWalk8) |
| Weight27 / SupportStrength28 | 150 / 20 |
| RotationNotches23 | 1 |
| FlagField2/word40 | 32 |
| RoomPlacement30 | 0 |
| Autonomy/person36 | 50 |
| Hunger/motive7 | 50 |
| Age/person58 | 0 (body strings ainda não aplicadas) |

Esses são efeitos de BHAV nativo executado pelo ODH, não efeitos observados no
executável original. Lote/zoning não foram inventados para superar a fronteira.
A oferta de Sit não prova execução de Sit: o Main para antes do AllowPush.

## Próximas dependências identificadas

A leitura de referência e dos recursos localiza tuning necessário a Init:
`0x4084`→GlobalBCON257[4]=1 e `0x4087`→GlobalBCON257[7]=7. A referência divide
Tuning em faixas private/semi/global e consulta o recurso correspondente ao
owner; o resolver do probe anterior da cadeira só fornecia private BCON.
Esse era o limite deste checkpoint inicial. O avanço posterior liga resolver26
por owner com BCON explícitos e ausência diferente de zero; o Init agora alcança
o contexto de lote, conforme [tuning-validation.md](tuning-validation.md).

8325/init motives chama global321, que usa lote atual e GenericTS1Call/28 para
zoning. A área de lote/globals deve vir de contexto explícito. A árvore4100 de
traits precisa RNG8 e PersonDataByTemp30, além de locais/parâmetros. Main/idle
precisa ainda dos serviços e recursos alcançados por suas branches reais.

Não basta atribuir valores de lote, random ou atributos para fazer a execução
passar: cada ampliação deve ter contrato e prova separados. O rastreio anterior
de [Main/idle](person-lifecycle-validation.md) e o contrato de
[fila/retorno](interaction-validation.md) continuam válidos.

## Testes e limites

O teste `person_cli` constrói somente FAR/IFF/BHAV/OBJD/GLOB sintéticos e cobre
private→semiglobal→global, owner1 Main→owner2 ação→owner1 mesma instrução no
tick seguinte, resultado false ignorado pelo Main, Sleep/deadline/sentinela,
queue pendente/AllowPush0, budgets de ticks e dispatches, overrides e rejeições
de scopes/roles/tipo/nome/opções/recursos. Getters derivados desconhecidos falham
com PC/contexto conservados. Dados comerciais não são fixtures versionadas.

`--headless-second-main ID X Y LEVEL DIRECTION ROUTINE` cria um segundo avatar
de fixture e roda uma segunda VM no mesmo World. A VM usa o mesmo banco global
imutável, mais fila e Sleep próprios; se a fonte de animação estiver
carregada, também recebe seu próprio Playback. O RNG permanece um fluxo global
compartilhado. O `person_cli` prova dois owners em execução e a ordem crescente
de ID no despacho inicial. Notify guarda o sinal para o próximo Sleep ou
IdleForInput, conforme o perfil Simitone fixado (`VMNotifyOutOfIdle.cs:10–14`,
`VMSleep.cs:14–20`, `VMIdleForInput.cs:30–33`).

Por padrão, a segunda VM ainda recebe cópias das rotinas privadas/semi e do
tuning do primeiro avatar. A opção `--headless-second-avatar-file IFF OBJD`
seleciona um IFF separado para o segundo avatar da fixture. No código, seu
GUID, entry point, BHAVs/BCONs privados e tabelas de animação vêm desse recurso;
o host exige OBJD138 ObjectType2, GLOB apontando para o mesmo semiglobal
explícito e Main coincidente com o entry point do segundo OBJD. Os BHAVs e
BCONs do semiglobal continuam compartilhados. `person_cli` verifica o GUID do
segundo avatar e prova dois recursos sintéticos com os mesmos IDs privados de
Main/helper/BCON, mas valores e resultados distintos, cada trace atribuído ao
seu próprio owner; também rejeita tipo, semiglobal ou Main incompatíveis. O
teste ainda não verifica separadamente a tabela de entry points ou a busca de
animação do segundo IFF. Com `--headless-second-main`, é uma bancada sintética: não carrega
dois personagens de um save, não executa Init completo para eles nem prova que
os serviços bloqueados de Route45/Snap funcionem para a segunda VM.

`--headless-second-saved-main OBJECT_ID ROUTINE`, junto de
`--headless-second-avatar-file IFF OBJD` e `--headless-main`, inicia uma segunda
Main no mesmo World já restaurado e no mesmo relógio. Exige um avatar salvo
distinto do primeiro, com GUID correspondente ao IFF, posição presente e,
quando uma família está ativa, pertencente àquela família. Reutiliza a entidade
salva; não chama o construtor sintético nem reposiciona os moradores. Os
controladores de fila, Sleep e animação são separados; sons privados também
são procurados no recurso de cada dono. Route45/Snap do segundo morador
continuam retornando erro explícito por falta de controlador próprio.

Os dois Main reiniciados começam com StackObject vazio (ID0), como na referência
Simitone/FreeSO quando `MainStackOBJ` não foi definido. Isso reproduz somente
essa regra de início: não restaura o StackObject, as pilhas ou as filas antigas
que estavam salvas para cada morador.

`tests/house07_paired_main_smoke.py` é uma prova real limitada a 32 passos do
relógio: House07, moradores 105/137, User00000/01, LotZoning e recursos de cada
instalação. Exige trace de ambos, GUIDs exatos, estados waiting/yielded, nenhum
erro do host, quantidade de entidades e posições preservadas. Também verifica
a rejeição de um IFF que corresponde ao morador errado. O processo retorna 3
quando alcança o limite de tempo; o runner aceita esse resultado apenas junto
das condições anteriores. Reinicia Main sobre os bancos salvos; não retoma
pilhas/filas antigas, não valida autonomia ou interação simultânea, aparência
completa do segundo morador, viagem, salvamento ou jogabilidade plena.

Matriz executada neste checkpoint após alinhar esse início à referência:
instalação original e Legacy Collection, com 105 como principal e novamente
com `--reverse` (137 como principal).
As quatro combinações terminam waiting após 32 ticks: 235 instruções para 105
e 232 para 137, com 182 entidades antes/depois e posições originais preservadas
em ambos os moradores. O recurso trocado é rejeitado em cada combinação.
Build Debug e CTest focado (`person_cli`, `session_entry_resolution`,
`session_entry_cli`) passam, 3/3. Essa é evidência de partida conjunta limitada,
não de continuidade de uma sessão salva completa.

CTest Debug e ASan/UBSan:41 testes passam, um X11 é skipped neste sandbox. LSan
desligado (`detect_leaks=0`), sem prova de ausência de vazamentos. O preview,
renderer e o antigo host da cadeira não foram alterados por esta unidade.

Não há lote inicializado, Init completo, original executado, áudio/RNG backend,
avatar idle correto, render conectado a este host ou validação de consoles.
O desenvolvimento continua rumo ao Linux funcional, com essas fronteiras abertas.
