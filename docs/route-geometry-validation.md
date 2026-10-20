# Geometria de rota sobre arquitetura salva

`odh_route_geometry` acrescenta duas consultas puras para preparar a validação
de destinos do lote:

- `saved_precise_floor` reproduz a seleção de piso de
  `VMArchitecture.GetPreciseFloor` para coordenadas em 1/16 tile. Em tiles
  comuns retorna o Pattern da grade; diagonais vertical/horizontal escolhem
  `TopLeftPattern` ou `TopLeftStyle` pelas mesmas comparações locais. Pattern
  zero continua sendo um resultado válido. Posição, nível ou snapshots com
  grades incompletas retornam ausência.
- `saved_wall_raycast` traduz o Bresenham de `VMArchitecture.RaycastWall`
  sobre tiles inteiros. Ele consulta apenas segmentos TopLeft/TopRight, na
  ordem de travessia da referência. A fonte de save não contém os campos
  runtime `TopLeftDoor`/`TopRightDoor`; portanto, a chamada exige uma entrada
  de solidez explícita por tile. Uma aresta presente sem solidez conhecida
  retorna `unsupported`, nunca caminho livre. `non_solid` representa
  `WallTile.TopLeftSolid/TopRightSolid == false`, como na regra de porta da
  referência; não modela abrir/fechar nem a navegação do cômodo.

Proveniência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, FreeSO
submódulo `f86bbd34b2112296ed786741f640a0535886d180`;
`VMArchitecture.cs:GetPreciseFloor` e `RaycastWall`,
`tso.world/Model/WallTile.cs:TopLeftSolid/TopRightSolid`. Os valores de piso,
segmentos e padrões vêm de `SavedArchitecture` já interpretado.

`odh-route-geometry-tests` cobre tiles e níveis, diagonais de piso, bounds,
Bresenham em oito octantes, travessia reversa, aresta sólida, não sólida e
desconhecida, diagonais não consultadas pelo RaycastWall da referência e
extensões inconsistentes.

Este é apenas um incremento verificável do adapter. Não valida `FloorChangeValid`
com flags de placement, terreno/piscina/água, inclinações, rooms, colisões
dinâmicas, overrides de interseção, multitile ou portas em estado vivo. O
resultado não deve ser usado para aceitar um destino nem ligado ao Routing45;
faltam também a montagem autoritativa da solidez por portas e o mapa de
obstáculos de `GetAvatarPlace`.
