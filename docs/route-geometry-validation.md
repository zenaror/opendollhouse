# Geometria de rota sobre arquitetura salva

`odh_route_geometry` acrescenta consultas puras para preparar a validação
espacial do lote:

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
- `prepared_precise_floor` aplica a mesma seleção espacial de piso aos grids
  preparados por `prepare_saved_architecture`, sem convertê-los de volta ao
  snapshot cru. Um alias não resolvido continua sendo consultável como valor
  retido; `floor_aliases_resolved()` mantém o diagnóstico estrito de misses,
  enquanto `floor_identity_unambiguous()` indica se há referência usada a alias
  FLRm declarado-sem-resolução. Nenhum resultado equivale a `FloorChangeValid`.

Proveniência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, FreeSO
submódulo `f86bbd34b2112296ed786741f640a0535886d180`;
`VMArchitecture.cs:GetPreciseFloor` e `RaycastWall`,
`tso.world/Model/WallTile.cs:TopLeftSolid/TopRightSolid`. Os valores de piso,
segmentos e padrões vêm de `SavedArchitecture` já interpretado.

`odh-route-geometry-tests` cobre grids crus e preparados, tiles e níveis,
limites estritos de seleção de piso em diagonais, pattern zero, bounds,
Bresenham em oito octantes, travessia reversa, aresta sólida, não sólida e
desconhecida, diagonais não consultadas pelo RaycastWall da referência e
extensões inconsistentes.

O módulo `odh_route_geometry` não valida sozinho `FloorChangeValid` com flags de
placement, terreno/piscina/água, inclinações, rooms, colisões dinâmicas,
overrides de interseção, multitile ou portas em estado vivo. O adapter separado
`odh-saved-floor-placement` cobre somente a regra de piso a partir dos dados
salvos e da arquitetura preparada; ele não está ligado ao runtime. Nenhum desses
resultados aceita um destino ou habilita Routing45. Ainda faltam montagem
autoritativa da solidez por portas e mapa de obstáculos de `GetAvatarPlace`.

A regra isolada `FloorChangeValid` está traduzida e testada. O adapter usa
`ObjectData[42]` e `LevelOffset` do OBJD, com o piso preciso da arquitetura
preparada, conforme [`floor-placement-validation.md`](floor-placement-validation.md).
