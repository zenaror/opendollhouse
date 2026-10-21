# Consulta RoomInfo reconstruída do World vivo

`query_world_room_info` enumera o `World` atual e deriva um snapshot limitado a
4096 entidades antes de cada consulta. Isso evita reusar uma fotografia depois
de movimentos, contenção, remoção, mudança de flags ou reclassificação entre as
consultas. Footprints são calculados com `object_collision_shape`, Definition
Index único por GUID, flags vivas em ObjectData[8] e `Entity::static_footprint`.
Entidades fora do mundo, contidas e objetos com `HasZeroExtent` não entram no
índice. Avatares usam `World::Entity::kill_timeout`, inicializado em -1 para
avatares recém-criados/restaurados e atualizável pelo controller de lifecycle.
Providers consultam por ID+generation; o snapshot também detecta mudanças desse
campo durante a query.

O chamador também fornece um resolver de sala para a posição viva. No host
`saved-world-main` sem edição de arquitetura, `SavedLotRenderer` mantém um mapa
congelado gerado de `PreparedArchitecture` desde que a identidade dos pisos
esteja resolvida. A conectividade usa os segmentos/estilos geométricos das
paredes; IDs de sprites de parede não alteram a conectividade das salas, e
aliases artísticos ausentes continuam sinalizados separadamente. Simitone
`VMContext.GetRoomAt` lê diretamente
`Architecture.Rooms[level-1].Map`; portal e janela constroem listas de
obstáculos/portais sem reescrever esse mapa. Portanto este resolver representa
pertencimento geométrico para o perfil de arquitetura imutável de duas alturas.
Uma mudança em pisos, segmentos, dimensões, aliases ou overlays requer preparar
e gerar o mapa novamente; ODH Main ainda não oferece mutação arquitetônica.
Isso não fornece `RoomObs`, `WallObs`, portais ou clearance de rota.

`SavedLotRenderer::runtime_room_at` usa `geometric_room_at_runtime`, que reproduz
o fallback de Simitone `VMContext.GetRoomAt`: coordenadas fora do lote e níveis
fora da quantidade de andares configurada retornam a sala 0. Isso permite
classificar entidades externas no mesmo bucket usado pela referência sem
invalidar uma consulta de outro cômodo. Mapa malformado e célula interna sem ID
válido continuam retornando ausência. A função geométrica estrita
`geometric_room_at` mantém ausência para coordenadas e níveis fora do mapa.

Definição ausente/ambígua, flag não inicializada,
sala não resolvida, `KillTimeout` desconhecido, geometria inválida ou excesso
de entidades torna a consulta `unsupported`, sem retornar interseções parciais.
Os resolvers recebem cópias dos dados, nunca referências a entidades. A API
valida novamente a projeção espacial depois dos callbacks e recusa o resultado
se o World tiver mudado durante a montagem.
O alvo da query é o avatar colocado: a lista estática o mantém; a dinâmica o
exclui. Isso é o contrato de montagem de GetAvatarPlace, não a política completa
de placement. EntryPoint 5, grupo multitile, flags Allow/Disallow, ghost,
EntryPoint 26, obstáculos de arquitetura e portais ainda não são avaliados.

Referência Simitone fixada em `4906db3156616bfaffc10ce0963cf08062975fef`,
submódulo FreeSO `f86bbd34b2112296ed786741f640a0535886d180`:
`VMGameObject.cs:355-382` calcula footprint por TileWidth/FootprintMask e
exclui `HasZeroExtent`/contenção; `VMAvatar.cs:985-1004` acrescenta KillTimeout
e radius 3; `VMEntity.cs:1436-1452` move o footprint entre coleções; e
`VMContext.cs:937-1032,1190-1235` registra posição e consulta as duas listas.
São regras da referência, sem confirmação contra o executável original.

`odh-world-room-info-tests` cobre fixture sintética com movimento entre salas,
contenção, zero extent, inclusão/exclusão do alvo estático/dinâmico, ordenação,
falha fechada para flags/definição/sala/KillTimeout desconhecidos e orçamento.
Também cobre mutação de posição, remoção e lifecycle disparadas por callbacks.
`odh-person` inclui `room_info_probe` no JSON do saved-world preview quando há
renderer com mapa preparado; consulta somente a caixa do avatar na posição
atual e lista interseções owner-keyed. Status unsupported é preservado. A saída
é diagnóstica e não bloqueia nem aprova interação. Ela comprova a consulta
espacial neste perfil, sem validar clearance, rota ou interação jogável no lote.

## House07 com Main vivo

Em 2026-10-21, um build Debug limpo executou House07 até o tick 214 com as 23
fontes loose `.flr`/`.wll` da instalação. `room_info_probe` retornou
`status=complete`, sala 3 e a interseção estática do próprio avatar 105 no box
`[389,437,395,443]`. O avatar é mantido na coleção estática nesse perfil; o
resultado não classifica EntryPoint 5/26 nem afirma que a posição passou por
`GetAvatarPlace`. O frame CPU produzido na mesma execução mostra Bob e o lote
compostos. Os 152 CTests passaram, incluindo `linux_window_backend` quando
executado com acesso autorizado ao display.

## Candidate placement diagnostic

`--placement-candidate X Y LEVEL DIRECTION` adds an independent query for a
candidate position against the prepared lot and the post-tick live World. It
reports the candidate room and radius-3 avatar box, `FloorChangeValid` inputs
from the unique avatar definition and live `ObjectData[8]`, plus the current
RoomInfo intersections. It does not move the avatar, execute a BHAV, remove the
avatar from its current static footprint, evaluate final placement policy, or
claim route clearance. An unavailable floor, room, definition, or live flags
remains unknown/unsupported rather than being inferred from the save snapshot.

House07 was queried at tick 214 for candidate `(392,440,1,3)`. It resolved room
3, precise floor 6, live placement flags 8 and avatar `LevelOffset` 0;
`FloorChangeValid` returned `allowed`. RoomInfo reported the current static
footprint of avatar 105 at `[389,437,395,443]`. This confirms the candidate
query plumbing on House07; because the probe deliberately keeps the avatar in
that static collection and does not evaluate EntryPoint 5/26, overlap policy,
ghost/allow flags, architecture obstacles, portals or route clearance, it is
not a placement approval.

Candidate `(400,440,1,3)` on the same run resolved room 3 and floor 6 with the
same live flags/LevelOffset; it returned no RoomInfo intersections. The empty
list describes only this avatar box against the current World snapshot. It does
not account for removing the avatar's old static footprint, architecture
obstacles, portals or movement between candidate and commit.
