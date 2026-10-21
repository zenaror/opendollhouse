# Candidatos geométricos de obstáculos por sala

`odh_room_obstacles` expõe `generate_room_obstacle_candidates` sobre o mapa
retornado por `generate_saved_room_map`. Ele reproduz a parte puramente
geométrica de `VMRoomMap.GenerateRoomObs`: escolhe a caixa da sala (ou o
interior expandido para a sala 0), examina uma borda de um tile, considera
bloqueado todo tile que não contém o ID da sala em nenhuma das metades
compactadas, cria retângulos em coordenadas de 1/16 tile com margem de 3
unidades e executa o merge ordenado de `OptimizeObstacles`.

Referência: Simitone submódulo FreeSO `4906db3156616bfaffc10ce0963cf08062975fef`,
FreeSO `f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.simantics/Model/VMRoomMap.cs` (`GenerateRoomObs`,
`OptimizeObstacles`). A função original consulta objetos de porta nos tiles e
seus grupos multitile; se um membro do grupo pertencer ao quarto alvo e seu
Portal15 for não zero, a execução encerra a linha de obstáculo e omite o tile.

A sobrecarga simples não recebe essa evidência e mantém todos esses tiles
bloqueados, com `complete=false`. A sobrecarga com `RoomPortalOpening` aceita
os pares explícitos (sala alvo, tile e andar); só omite os pares fornecidos e
valida seus limites. `build_room_portal_inventory` deriva esses pares dos
membros salvos do grupo multitile. Porta sem posição, porta fora dos limites,
membro Portal15 sem sala geométrica, identidade duplicada ou tile reivindicado
por grupos de portas diferentes torna o inventário incompleto. Grupos de portas
distintos sobrepostos são especialmente ambíguos: Simitone escolhe a primeira
porta arquitetônica consultada no tile. O probe informa a contagem
`overlapping_portal_door_tiles` e mantém a geração incompleta nesses casos.
O caller passa `portal_inventory_complete` à geração.
`complete` nessa sobrecarga descreve apenas a geração dos obstáculos de sala e
fica verdadeiro somente quando o mapa preparado não tem identidade ambígua de
piso entre as referências usadas e o inventário de portais está completo. O
contador estrito de floor lookup misses pode continuar positivo para IDs sem
declaração FLRm, cujo byte é preservado pelo fallback Simitone. Isso não significa clearance total para
roteamento ou placement: colisão de entidades continua separada.

`DynamicRoomPortalInventory` acrescenta uma camada bounded para o host
reconstruir esse inventário em registro, movimento e remoção de membros. Cada
operação válida monta o conjunto e resultado candidatos antes de publicar o
novo estado no thread chamador; a classe não é thread-safe. Geração e membros
permanecem intactos quando identidade/capacidade rejeitam a operação. Uma
atualização aceita com posição ausente publica um inventário incompleto,
removendo aberturas potencialmente obsoletas. O limite padrão é
4.096 membros e o host pode reduzi-lo. `replace_members` aceita um snapshot
completo em lote, sem repetir uma reconstrução por membro. A classe guarda uma
cópia do mapa geométrico e depende de o host enviar todas as alterações
relevantes. Ela não recebe eventos de `World` e ainda não cria adjacency usada
pelo `Routing`. No Simitone consultado, `Portal15` é estrutural e o cálculo de
`RoomObs` não consulta animação/estado aberto-fechado
(`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Model/VMRoomMap.cs`,
`GenerateRoomObs`). Assim, esta camada representa a atualização de membership,
posição e vínculos do grupo, sem alegar uma máquina de estado de portas. Os
testes exercitam registro em lote/individual, movimento entre salas, posição
desconhecida, remoção e limite com mapas sintéticos.

`tools/runtime_attempt_walk_probe.cpp` alimenta o índice em lote com as
posições atuais do `World` e a classificação de portas/grupos extraída do save.
Esse probe ainda é uma reconstrução pontual: não mantém o índice entre ticks
nem captura alterações posteriores.

Quando recebe o mapa cru de `generate_saved_room_map`, o resultado também herda
a ausência do remapeamento de pisos/flags. O caminho de
[arquitetura preparada](prepared-architecture-validation.md) trata essa etapa
com aliases explícitos; ainda assim, os retângulos são candidatos geométricos,
não obstáculos runtime completos, colisão autoritativa, espaço de rota livre ou
aceitação de destino. Esta API não está conectada a Routing45 nem a uma consulta
de movimento.

## Candidatos geométricos de WallObs

`generate_wall_obstacle_candidates` emite candidatos geométricos de cada
segmento de parede no andar selecionado, usando as mesmas fórmulas de
`VMRoomMap.GenerateMap`. Inclui as quatro peças diagonais e as caixas dos
segmentos TopLeft, TopRight, BottomLeft e BottomRight, preservando o wrap de
vizinhos da fonte. Uma porta só remove a caixa correspondente quando
`ResolvedWallEdges` traz evidência `non_solid` para a face do mesmo snapshot;
aresta presente sem face resolvida fica sólida. O limite padrão é 65.536
retângulos; erro ou estouro descarta a saída parcial e mantém `complete=false`.

O resultado é um inventário global, não mesclado, por andar. Simitone coleta
`wallObs` enquanto percorre cada região flood-filled e chama
`OptimizeObstacles` separadamente por sala. Portanto, a ordem e a lista de
retângulos não são byte a byte equivalentes às listas de cada sala. O candidato
global é consumido apenas depois de recortar os retângulos aos limites da sala;
isso ainda é geometria estática, não comportamento runtime.

Proveniência: Simitone submódulo FreeSO na revisão acima,
`VMRoomMap.cs:129-180`, comparado a `resolve_initial_wall_edges`. `non_solid`
significa que o snapshot de aparência marcou a face como porta; não significa
porta aberta nem passagem aceita em runtime. `tests/room_obstacles.cpp` cobre
as duas fórmulas diagonais, retângulos cardinais, faces ausentes como parede
sólida, portas reconhecidas, vizinhos normais e com wrap, bits inválidos,
extent inválido e teto de saída. O `SavedRoomRouteGrid` consome esse conjunto,
mas Main ainda não monta nem usa o resultado para Routing45.

`tests/room_obstacles.cpp` cobre bounds expandidos, coordenadas/margens de
tile, sala 0, ambos os IDs de tile diagonal (incluindo a máscara do bit de
orientação), ordem/merge de retângulos, portal que interrompe um run como na
referência, completude do inventário (posição ausente/fora do lote, membros
portal sem sala, identidades duplicadas e sobreposição entre grupos), estado
incompleto e entradas inválidas. Os testes são sintéticos;
não são um diferencial executável com FreeSO nem prova do comportamento do TS1
original.

`audit_saved_room_boundary` verifica a sala exata do centro de cada candidato e
compara a caixa de colisão expandida em ±3 unidades com os RoomObs completos
desse andar. Também testa, separadamente, as quatro faixas de um tile além dos
limites de `VMRoom.Bounds` que `VMRoutingFrame.AttemptWalk` acrescenta. O
resultado `no_intersection` significa somente que a caixa não cruzou nenhum
desses dois grupos de retângulos. Dados ausentes, sala 0, sala/andar inválido,
bounds de sala inválidos, conjunto de RoomObs ausente/ambíguo/incompleto,
shape inválida e candidatos de colisão `unsupported` não produzem resultado
negativo. A auditoria ainda exclui `wallObs`, seleção dinâmica de entidades,
`AllowPersonIntersection`/`DisallowPersonIntersection`, EntryPoint 5 e a busca
`VMRectRouter`; portanto nenhum estado equivale a rota livre ou clearance.
O candidato carrega sua posição validada junto da caixa; avatares recebem a
shape centro±3 e a expansão do roteador, totalizando raio 6. O teste sintético
confirma esse caso de borda.
`tests/saved_room_boundary_audit.cpp` cobre interseção, caixa interna, toque sem
interseção, as quatro faixas externas, toque estrito e bounds inválidos, além de
casos ausentes/incompletos e estados sem suporte. A fórmula segue
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Engine/VMRoutingFrame.cs:346-357`.
No House07, entre as entidades com caixa candidata, 50 não cruzaram RoomObs e
41 cruzaram; nenhuma cruzou as faixas externas. Outros 22 resultados
permaneceram `unsupported` e 69 foram `not_applicable`.
