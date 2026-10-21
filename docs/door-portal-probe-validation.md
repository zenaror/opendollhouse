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
busca diagnóstica para duas entidades na mesma sala. Ela compõe o mapa
preparado, RoomObs, candidatos WallObs do andar e caixas de colisão salvas na
grade limitada, depois chama `find_route` até qualquer célula caminhável logo
fora da caixa do alvo expandida em três unidades. `approach_model` identifica
essa aproximação geométrica. Ela não resolve SLOT, direção, `WallRaycast`, flags
de ocupação, portas runtime, EntryPoint 5, portais ou `VMRectRouter`; `found`
significa apenas caminho na grade candidata ODH. Salas diferentes retornam
`cross_room_unmodeled`; entradas incompletas falham fechadas.
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
  "${architecture_args[@]}" --route-edges --route-grid-diagnostic 105 128
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

Na amostra 2026-10-20, `--route-grid-diagnostic 105 128` colocou Bob (105) e o
alvo salvo 128 na sala geométrica 3. A grade candidata ficou `ready` com
10.240 células, 4.529 caminháveis e start caminhável; a busca ODH encontrou um
caminho de 9 pontos, custo 80, até `[392,448]`, entre 40 pontos geométricos
candidatos ao redor da caixa expandida. É apenas um resultado de `find_route`
8-vizinhos, não uma rota aprovada pelo jogo, seleção real de SLOT ou prova de
que a interação Sit funcione.

`tests/entry_points.cpp` cobre o reader de OBJf/OBJD. O teste manual acima
valida a junção com arquivos comerciais reais; nenhum asset ou relatório com
conteúdo comercial é versionado.
