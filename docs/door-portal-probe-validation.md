# Inventário estático de portas e Portal15

`odh-wall-appearance --portal-probe` amplia o snapshot de House07 com um
inventário das faces de porta e dos membros do grupo multitile. Para cada
entidade, o probe abre o asset explícito que forneceu sua OBJD, escolhe a tabela
OBJf quando `UsesFnTable` é não zero ou gera as entradas da OBJD no perfil
legado, e registra a ação da entrada 15. Também indica se o ID dessa ação está
presente como chunk BHAV no mesmo asset. A propriedade `Portal` do Simitone é
`EntryPoints[15].ActionFunction != 0`; por isso `portal_property_authored`
descreve a função salva, não uma BHAV executada.

O probe constrói o mapa de salas a partir da arquitetura preparada quando
recebe todos os arquivos `.flr`/`.wll` como fontes explícitas. Sem essas fontes,
usa o perfil cru e identifica essa limitação no JSON. As posições dos membros
são consultadas no mapa para expor os IDs de sala associados ao grupo. Isso
permite conferir os candidatos a ligação de sala que
`VMRoomMap.GenerateRoomObs` usa ao procurar uma entidade Portal no grupo da
porta. O probe também produz os candidatos geométricos de obstáculo por sala,
usando pares explícitos tile/room derivados dos membros posicionados com
Portal15 não zero. O contador estrito de preparo mantém os misses de
`TryGetValue`, inclusive quando o ID não está declarado em FLRm e o fallback
preserva o byte. O room map preparado só fica incompleto quando uma referência
usa um ID FLRm declarado sem resolução final; o probe publica
`floor_map_entries` e `unresolved_floor_alias_references` para rastrear essa
distinção. Um fallback sem alias não é reinterpretado como espaço livre. O
probe também emite `collision_candidates`, juntando geometrias salvas no OBJM a
posições iguais no World restaurado, e `room_boundary_audit` compara cada caixa
expandida com RoomObs e os limites externos de `AttemptWalk`. É só uma auditoria
de interseção estática por entidade, não pathfinding ou clearance; estados
incompletos de avatar e porta permanecem `unsupported`. O inventário do portal fica incompleto se uma
porta ou membro Portal15 não tiver posição/sala resolvida, sair do lote ou se
grupos distintos de portas disputarem o mesmo tile; o campo
`overlapping_portal_door_tiles` expõe essa última ambiguidade. O probe não
calcula clearance/pathfinding completo.
`portal_bhav_executed`,
`runtime_portal_state` (runtime não simulado) e `routing_clearance` continuam
false.

Opcionalmente, `--route-grid-diagnostic ACTOR_ID TARGET_ID` acrescenta uma
busca diagnóstica para duas entidades na mesma sala, mas agora exige também
`--route-slot-resource ARCHIVE ENTRY CHUNK_ID TYPE INDEX`. O SLOT é lido de uma
entrada FAR e chunk SLOT exatos; seleção ausente ou ambígua falha fechada. O
diagnóstico compõe o mapa preparado, RoomObs, candidatos WallObs do andar e
caixas de colisão salvas na grade limitada, gera oito posições circulares do
perfil ODH de raio exato e reporta posição, `entry_flags`, aceitação da máscara,
limites/sala e caminhabilidade da grade para cada uma. `find_route` considera
somente candidatos aceitos pela máscara e caminháveis. O perfil geométrico é
restrito a SLOT v10 type-3 isotrópico com resolução 16; o arredondamento
diagonal é uma regra de candidato ODH, não equivalência comprovada com o jogo.
O modelo falha fechado fora do perfil observado House07 (`flags=0x55`, offsets
zero, postura em pé, facing=-2 e demais campos v10 verificados).
Cada candidato também reporta o `FloorChangeValid` do avatar e `RaycastWall`
entre os tiles do destino e do alvo, usando a fotografia inicial salva das
arestas; a grade caminhável atua como teste conservador de ocupação salva.
`static_goal_gates_passed` agrega esses filtros e a máscara. Isso ainda não é
uma chamada completa a `PositionValid`/`GetAvatarPlace`: ocupação dinâmica,
flags `AllowPersonIntersection`/`DisallowPersonIntersection`, EntryPoint 5
bilateral e estado vivo de portas permanecem desconhecidos. `found` significa
apenas caminho na grade candidata ODH. Salas diferentes retornam
`cross_room_unmodeled`; entradas incompletas falham fechadas.
O bloco `goal_selection` classifica separadamente os footprints do snapshot
para o perfil estreito v10 standing=1/sitting=0: `snapshot_clear`, objeto
sólido sobreposto rejeitado, avatar conhecido sobreposto com penalidade de
score, ou `unsupported`. Isso não calcula o score nem os flags vivos. Um
destino só entra na busca se passar os gates estáticos e tiver elegibilidade
positiva conhecida pelo snapshot; células ainda bloqueadas na grade continuam
fora do A*. Candidatos multipartes sobrepostos e colisões incertas ficam
`unsupported`.
`route_clearance` e o `routing_clearance` geral continuam false.

Referências locais pinadas: Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`, FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`, `VMEntity.cs` (propriedade
`Portal`) e `Model/VMRoomMap.cs:390-446` (abertura de obstáculos por grupo e
Portal15). A consulta à presença do chunk BHAV é restrita ao asset de definição;
ela não procura dependências Global/SemiGlobal nem valida/executa o bytecode.
Também não prova equivalência com o executável original.

## Evidência local House07

Os arquivos comerciais permanecem em `../referencias/The Sims`, fora do Git.
Execução reproduzível a partir da raiz do checkout:

```sh
mapfile -d '' architecture_sources < <(
  find '../referencias/The Sims' -type f \
    \( -iname '*.flr' -o -iname '*.wll' \) -print0 | sort -z
)
architecture_args=()
for source in "${architecture_sources[@]}"; do
  architecture_args+=(--architecture-resource "$source")
done
build/odh-wall-appearance '../referencias/The Sims/UserData/Houses/House07.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' \
  --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff' \
  "${architecture_args[@]}" --route-edges --route-grid-diagnostic 105 128 \
  --route-slot-resource '../referencias/The Sims/GameData/Global/Global.far' Global.iff 100 3 1
```

Com as 23 fontes loose `.flr`/`.wll` presentes nesta instalação, o probe
preparou a arquitetura e gerou seis salas. House07 reporta 115 misses estritos,
zero entradas FLRm e zero referências a alias declarado-sem-resolução; portanto
as seis salas têm `generation_complete=true` quando o inventário de portais
está completo. Isso só completa os candidatos geométricos de obstáculo dentro
do perfil atual; `routing_clearance` permanece false. Foram localizadas seis faces de
porta. Cada grupo de duas entidades aponta para `OBJf` action `4105`, e esse
ID existe como BHAV no respectivo asset `Doors.iff`. Os pares de IDs de sala
consultados para os seis grupos foram `(2,3)`, `(3,4)`, `(4,2)`, `(1,3)`,
`(1,2)` e `(3,1)`. A expansão por peças de porta produziu 24 pares
tile/room-alvo. Os candidatos de obstáculo emitiram 8, 7, 8, 6 e 0 retângulos
para as salas 1–5; com inventário completo, todos têm
`generation_complete=true`; o inventário por sala não executa movimento e
`routing_clearance` segue false. Isso valida leitura e junção estática da
instalação/save, não abertura runtime, colisões completas ou movimento de Sims.

### Árvore BHAV do Portal15 e binding semiglobal (2026-10-21)

`odh-bhav Objects.far Doors.iff 4105` decodifica estaticamente a ação Portal15
presente nos membros. A rotina chama os BHAVs privados 4112, 4113, 8197, 4162,
4106 e 4107 em ramos distintos. `4113`, que é chamada pela instrução 2 de
4105, contém duas instruções de opcode 45 (Route45 no mapeamento ODH). Os
BHAVs privados diretamente referenciados (4112, 4113, 4162, 4106 e 4107)
existem no asset `Doors.iff`; a inspeção de 4113 confirma o código dos dois
Route45.

O `GLOB128` Pascal de `Doors.iff` declara `DoorGlobals`; a entrada correspondente
é `DoorGlobals.iff` em `Global.far`. O vínculo pode ser conferido sem executar
o código:

```sh
./odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' \
  Doors.iff 4105 --semiglobal \
  '../referencias/The Sims/GameData/Global/Global.far' DoorGlobals.iff
./odh-bhav '../referencias/The Sims/GameData/Global/Global.far' \
  DoorGlobals.iff 8197
```

No asset ligado, 8197 tem versão `0x8001`, sem metadata serializada, e uma
expressão (`opcode=2`, `op=5`) com escopo 7/literal 0 como origem e escopo
9/argumento 0 como destino; op 5 atribui zero ao argumento 0 e retorna true.
O VM agora aceita o perfil legado limitado de quatro argumentos e zero locais,
conforme `SimsLib/IFF/Old/BHAV.cs` no checkout Simitone/FreeSO pinado. Isso
permite executar este BHAV; não prova a equivalência geral dos formatos 8000/
8001 nem a semântica do executável original.

O probe construído de `Doors.iff:4105`, OBJD15089, caller words `[9,0]` e
`attribute1=1` agora avança até 135 instruções e termina `returned_false`.
Em `Doors.iff:4162`, opcode 28 resolve `STR#303` para “do idle”; nenhum BHAV
privado ou semiglobal carregado tem esse nome, então o ramo falso é tomado.
Depois `DoorGlobals.iff:8197` executa, seguido de `Doors.iff:4105` pc7. O
resultado é evidência deste percurso no VM ODH, não uma travessia de portal ou
execução em gameplay. A grade segue construída e não verifica clearance
dinâmico do avatar.

O BHAV privado 4115 foi decodificado separadamente com `odh-bhav`: versão
`0x8002`, metadata sem argumentos/locais, uma expressão (`opcode=2`) com
operandos `[1,0,0,0,0,2,1,7]`, ramo verdadeiro `0xff` e falso `0xfe`. Pela
codificação de `VMExpressionOperand.Read` e `VMVariableScope` no submódulo
FreeSO pinado pelo Simitone, isso representa `StackObject` atributo 1 igual ao
literal 0. Os destinos `0xff` e `0xfe` correspondem, respectivamente, a retorno
false e true no interpretador de referência; portanto 4115 retorna true quando
o atributo 1 difere de zero.

`Doors.iff:4113` começa chamando 4115 e, conforme o retorno, seleciona uma de
duas instruções Route45. Seus operandos são `[0,0,1,0,1,0,0,0]` e
`[1,0,1,0,1,0,0,0]`. `VMGotoRoutingSlotOperand.Read` no mesmo submódulo lê os
campos como `Data=0/1`, `Type=1` (`Literal`) e `Flags=1` (`NoFailureTrees`).
`VMMemory.GetSlot` resolve `Literal` no SLOT type-3 do StackObject. Assim, no
interpretador de referência, 4113 escolhe o slot literal 0 quando o atributo 1
é diferente de zero e o slot literal 1 quando é zero.

O teste `world` usa uma sequência BHAV sintética com os bytes observados de
4113/4115 e confirma a ramificação, a identidade do StackObject, o índice,
escopo e flag que chegam ao serviço Route45; a ausência do StackObject falha
fechado. Ele não carrega os SLOTs comerciais nem move uma entidade. O `Routing`
atual exige escopo global 2 e flags zero, então rejeita explicitamente essa
forma de operandos. É uma leitura estática e uma validação sintética à luz da
referência, não execução dos BHAVs comerciais nem prova da semântica do
executável original.

O FreeSO pinado em `f86bbd34b2112296ed786741f640a0535886d180`, arquivo
`VMRoutingFrame.cs`, documenta a sequência de sala: calcular a cadeia de
portais, chamar EP15 do próximo portal e aguardar seu retorno; retorno falso
descarta aquele portal e recalcula a rota. A rotina `Doors.iff:4113` conter dois
Route45 é compatível com essa função de portal. Os efeitos dos demais BHAVs
chamados por 4105 seguem sem interpretação confirmada; nenhum deles foi
executado aqui. Isso não valida travessia, estado de porta, mutação de sala ou
equivalência com o jogo.

Na amostra 2026-10-20, `--route-grid-diagnostic 105 128` colocou Bob (105) e o
alvo salvo 128 na sala geométrica 3. A grade candidata ficou `ready` com
10.240 células, 4.529 caminháveis e start caminhável. Essa medição anterior
usava aproximações pela caixa expandida e não deve ser confundida com o novo
diagnóstico de candidatos SLOT. Mesmo com candidatos SLOT, `found` será apenas
um resultado de `find_route` 8-vizinhos na grade ODH, não rota aprovada pelo
jogo nem prova de que a interação Sit funcione.

Na consulta validada de 2026-10-21, oito candidatos do House07 tiveram seis
classificados `snapshot_clear` e dois `unsupported` por sobreposição com
multipartes (IDs 106 e 125). Três passaram os demais gates e entraram no A*;
o resultado foi um ponto/custo zero porque Bob já ocupa um deles. Os testes
focais de seleção passaram 32 checks, e CTest passou 145/145 com o display
X11 acessível, sem skips. Isso não executa a escolha do destino nem o caminho
na simulação original.

`tests/entry_points.cpp` cobre o reader de OBJf/OBJD. O teste manual acima
valida a junção com arquivos comerciais reais; nenhum asset ou relatório com
conteúdo comercial é versionado.

### Host-scheduled Portal15 diagnostic

`odh-person --headless-portal ID TICK` resolves EntryPoint15 from the exact
restored World record, loads the explicitly linked semiglobal and
`Global.iff`, validates their BHAV closures, installs a resource CodeOwner and
queues that BHAV through the existing Main/interaction host. The JSON
`portal_runtime` result is labeled diagnostic: the host schedules the action
directly; this is not the natural portal scheduler or cross-room routing.

A primeira execução de `tests/house07_route45_integration.py --portal-id 25
--ticks 2`, antes do limite de parada após a ação, excedeu 90 segundos; a Main
sem portal também excedeu esse limite. O probe foi depois alterado para parar
no resultado da ação, permitindo os traces House07 descritos abaixo. Isso não
converte a chamada host-scheduled em navegação natural.

### Comparação de posição antes e depois do Portal15 (2026-10-22)

O diagnóstico agora registra a posição do Sim e do portal quando a chamada é
agendada, além das posições finais. Em House07, a chamada direta de Portal15 no
objeto26 terminou `returned_true`: o Sim saiu de `[392,440,1,3]` e chegou a
`[408,408,1,0]`; a peça ficou em `[408,424,1,0]`. `actor_level_changed` foi
`false`. O Route45 interno fez 32 movimentos no mesmo piso. Isso confirma a
medição com uma porta comum, mas não testa escadas nem navegação natural.

House03 tem 267 entidades salvas e nenhum avatar. Por isso, o probe atual não
consegue selecionar um Sim dessa casa para chamar EP15 em um patamar. O próximo
teste precisa fornecer um avatar de fixture apenas na memória, junto a uma
escada real da House03, sem alterar os saves comerciais.


### Trace Portal26 até a recusa de Route45 (2026-10-21)

O probe headless foi ajustado para encerrar assim que Portal15 termina e para
expor as instruções do CodeOwner da ação, mantendo também a cauda de 24 itens
compatível com o relatório anterior. Com a instalação externa House07 e
`--portal-id 26 --ticks 214`, EntryPoint15 resolveu `Doors.iff:4105` no owner
`2147483648`; o trace filtrado contém 49 instruções e termina em `4105 pc7`,
opcode2, destinos `on_true=0xff` e `on_false=0xfd`. Antes disso, `4105 pc2`
chama `4113`; `4113 pc1` despacha Route45 literal com operandos
`[0,0,1,0,1,0,0,0]` e retorna pelo ramo falso a `4105 pc3`. O runtime registra
`candidate room 1 differs from prepared route room 3` e
`host goal validator rejected every direction-compatible SLOT candidate`.
Assim, a recusa observada vem do Route45 interno tentando validar um destino da
sala1 contra a malha ativa da sala3. A ação termina `returned_false`, sem fault;
o ator fica em `[408,424,1,0]`, e nenhuma travessia é executada. O processo
encerra com código 0 após a ação completar; a Main permanece `yielded` no tick
119 porque o probe para antes de continuar seu relógio.

O script `tests/house07_route45_integration.py --portal-id 26 --ticks 214`
produz o trace até 256 itens por owner (49 neste caso), informa contagem e
truncamento, e mantém `portal_trace_tail`. Isso identifica o limite do host para
este trace; não estabelece que o executável original falharia. A próxima etapa
é ligar o planner entre salas ao lifecycle do Route45 e do portal, preservando
a EntryPoint15, o owner e a reconstrução da malha ao mudar de sala.


### Portal25 e Portal26 no mesmo lote

Com o mesmo host e `--ticks 214`, a peça25 da Door (posição salva
`[408,408,1,0]`) falha antes de mover: GotoRelativePosition não encontra uma
entrada compatível com a grade ativa, o VM termina `fault` com
`routing start or pre-ranked goal input invalid`, e o ator permanece em
`[392,440,1,3]`. A peça26 executa GotoRelativePosition até `[408,424,1,0]`, mas
seu Route45 interno é rejeitado por pedir destinos da sala1 contra a grade da
sala3, conforme o trace anterior. São limites observados do adaptador ODH com
ação agendada. Não representam faults atribuídos ao jogo original. A aresta do
planner estático Room3→Room1 segue identificada via Portal26; a ligação desse
planner ao Route45/EntryPoint15 continua pendente.
