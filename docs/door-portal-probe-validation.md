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
