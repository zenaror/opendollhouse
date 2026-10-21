# Segmentação geométrica de salas salvas

`odh_room_map` expõe `generate_saved_room_map` em `include/odh/room_map.hpp`.
Recebe somente os grids tipados de `SavedArchitecture`; não abre recursos ou
saves comerciais. A biblioteca permanece independente de aceitação de alvos,
roteamento e estado de entidades.

O resultado é um **candidato geométrico sobre IDs de piso retidos**, não o mapa
restaurado do runtime TS1. A resolução de nomes FLRm e o remapeamento de IDs
de piso condicionado por flags em `RemapFloors` não são implementados; `floor_resources_resolved` é
sempre falso. A segmentação pode mudar quando esse preparo for resolvido.
O caminho separado `generate_prepared_room_map` consome grids já preparados;
seu contrato está em [prepared-architecture-validation.md](prepared-architecture-validation.md).

## Referência e perfil implementado

Referência consultada: FreeSO
`4c6b3e8f5835b228723caea3c9f683c62f244f73`,
`TSOClient/tso.simantics/Model/VMRoomMap.cs` (`GenerateMap`, `SpreadOnto`,
`MakeOutside`) e `TSOClient/tso.simantics/VMArchitecture.cs`
(`RegenRoomMap`). FreeSO declara MPL 2.0; créditos e revisões estão em
[references.md](references.md). A implementação C++ não depende do runtime C#.
O preparo dos estilos salvos segue `VMTS1Activator.DecodeWalls` e
`DecodeAdvWalls` no submódulo Simitone descrito abaixo.

Também foi comparado o mesmo arquivo no FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`, submódulo do Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`. Nesses métodos, as diferenças
observadas são imports, reutilização da pilha e um comentário; a segmentação
consultada é igual. Isso é referência de implementação de terceiros, sem prova
de comportamento do jogo original.

O perfil preserva:

- Duas histórias salvas, IDs globais começando em 1 e registro reservado 0.
  O campo `level` da sala começa em 0, como `VMRoom.Floor`.
- Busca de sementes em ordem row-major e expansão por pilha LIFO. A ordem de
  inserção dos vizinhos é +X, -X, +Y, -Y. Todos os passos envolvem as bordas
  com módulo; o grid é toroidal, inclusive em dimensões 1.
- Dois IDs por tile: bits 0..15 e 16..30. Bit 31 identifica diagonal horizontal.
  Na horizontal, primeiro ID corresponde à metade superior (`x+y<=15` em
  coordenadas locais de subtile); na vertical, à metade direita (`x>y`).
  Tiles normais repetem seu ID nas duas posições.
- `area` conta itens retirados da pilha: 1 para tile normal, 1 para cada metade
  diagonal. Os limites abrangem coordenadas inteiras dos tiles visitados.
- Antes do flood fill, uma cópia local aplica a normalização dos dois métodos
  DecodeWalls: em tiles sem diagonal, estilos presentes fora de {2,12,13,14}
  tornam-se 1; estilos de segmentos ausentes tornam-se 0. Assim, estilos crus
  0/3/15/22/255 também representam parede normal quando o segmento está presente.
  Nos tiles diagonais, a normalização de estilos não é aplicada. Os grids de
  entrada nunca são alterados.
- O fallback determinístico de `RemapWalls` é aplicado na cópia: quando
  `TopLeftPattern` e `TopLeftStyle` são ambos zero em tile diagonal, os dois
  recebem o padrão cru do piso desse tile. Se um deles é diferente de zero,
  os campos crus são preservados, sem inventar remapeamento de recursos.
- Depois desse fallback, pools e water opcionais com extents exatos substituem
  o padrão do piso da primeira história por 65535 e 65534, respectivamente.
  Bytes 0 e 255 não substituem o piso; water ganha quando ambos estão ativos.
  Esta ordem preserva o preparo consultado: o fallback diagonal ocorre antes
  dessas sobreposições. A segunda história não recebe essas sobreposições.
- Parede com segmento TopLeft/TopRight e estilo normalizado 1 bloqueia a expansão e não
  cria adjacência. Se o estilo difere de 1, `SpreadOnto` é chamado com
  `forceAdj`: o segmento separa salas adjacentes, sem unir seus IDs. O valor
  255 em tile diagonal segue essa regra; em tile comum, é normalizado para 1.
- `noFloorBad` é calculado uma vez por história a partir da lista global:
  falso no térreo, verdadeiro na segunda história. No térreo, zero e padrões
  comuns se unem; acima, zero separa-se dos padrões comuns. Padrões 65534 e
  65535 formam grupos separados por valor e marcam `pool`.
- A primeira sala de cada história nasce exterior. As adjacências recebem
  links recíprocos e a propagação exterior segue a ordem e o momento do
  algoritmo de referência, incluindo a visita transitiva de `MakeOutside`.
  `adjacent` mantém a ordem de descoberta, equivalente à enumeração por
  inserção do HashSet usado pela referência; a linguagem C# não especifica
  uma ordem universal para HashSet.

## Consulta por posição

`geometric_room_at` consulta a metade do tile para posições em unidades de
1/16 tile. Para tile comum retorna o ID repetido; em diagonal vertical, escolhe
o primeiro ID quando `local_x > local_y` e o segundo também na igualdade; em
diagonal horizontal, escolhe o segundo somente quando `local_x + local_y > 15`.
O mapa e os registros de salas precisam ter extents e ownership de nível
coerentes. As duas metades packed são validadas antes da seleção; coordenadas
inválidas, IDs não atribuídos ou salas de outra história retornam ausência,
mesmo quando a metade escolhida seria válida. O ID zero explícito permanece
distinguível da ausência.

Essas comparações seguem `GetRoomAt` em Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`,
`FreeSO/TSOClient/tso.simantics/VMContext.cs:1314-1343`. A API consulta o mapa
geométrico de ODH; não cria salas do runtime, não resolve portais e não autoriza
transições entre salas. `floor_resources_resolved` continua sendo apenas uma
medida de cobertura dos aliases, não validade de piso.

## Limites explícitos

Grids devem ter dimensões positivas de no máximo 64x64 e extents exatos nas
duas histórias. Bits desconhecidos de segmento e as duas orientações diagonais
no mesmo tile são rejeitados, pois ficam fora do perfil válido suportado.
`RoomMapLimits` permite reduzir dimensões e número de salas. IDs acima de 32767
são rejeitados antes do retorno; o bit de orientação ocupa o bit alto do segundo
ID. Extents incorretos dos grids opcionais de pool/water também são rejeitados.
Os outros metadados opcionais e arrays retidos não participam da segmentação.

Ficam fora do resultado: `GenerateRoomObs`, objetos e ocupação, portas e
portais dinâmicos, obstáculos de parede/sala e sua otimização, linhas de parede
ou cerca, `LightBaseRoom`, iluminação, suporte entre histórias, estado vivo da
VM e consultas de rota. A normalização DecodeWalls é aplicada aos estilos
salvos, sem estilos temporários de objetos. O remapeamento de nomes e IDs de
recursos de `RemapFloors`/`RemapWalls` (incluindo a seleção por flags), grass, road flip e
resize ficam fora desta API. O fallback diagonal usa o piso cru, enquanto o
runtime usa o piso já remapeado. Não é o mapa de iluminação transformado por
`RegenRoomMap`, nem uma implementação completa de salas ou rotas do TS1.

## Validação sintética

`tests/room_map.cpp`, registrado no CTest como `room_map`, cobre salas comuns,
wrap nas bordas, divisão por parede sólida, adjacência de cercas nos dois eixos
com estilos retidos 2/12/13/14, normalização de estilos crus 0/1/3/15/22/255/65535,
preservação dos dados de entrada e dos estilos diagonais, propagação exterior,
`noFloorBad`, padrões de piscina, sobreposições pool/water e sua precedência,
fallback diagonal e sua ordem em relação às sobreposições,
ambas diagonais e suas quatro entradas cardinais, IDs globais entre histórias,
área e limites geométricos, dimensão máxima, rejeições de grids/limites e
consultas posicionais para tiles comuns/diagonais, room zero, bounds, ownership
de nível e extents inválidos.
Os valores esperados são fixtures sintéticas derivadas do contrato acima.
Não foi executado um comparador diferencial com o runtime C#.

Comandos de validação:

```sh
cmake -S . -B build-room-map -DODH_X11=OFF -DODH_SDL_AUDIO=OFF -DBUILD_TESTING=ON
cmake --build build-room-map --target odh-room-map-tests -j4
ctest --test-dir build-room-map -R '^room_map$' --output-on-failure
```
