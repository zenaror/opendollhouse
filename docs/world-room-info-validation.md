# Consulta RoomInfo reconstruída do World vivo

`snapshot_world_room_info` enumera o `World` atual e deriva o inventário
completo de RoomInfo limitado a 4096 entidades para uma sala. A resposta
preserva coleção estática/dinâmica e ordena por owner ID. A query
`query_world_room_info` usa esse mesmo snapshot antes de filtrar a caixa
candidata e aplica a regra GetAvatarPlace de reter o alvo estático e excluir o
alvo dinâmico. Isso evita reusar uma fotografia depois de movimentos,
contenção, remoção, mudança de flags ou reclassificação entre as consultas.
Footprints são calculados com `object_collision_shape`, Definition
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

`odh-world-room-info-tests` cobre fixture sintética com inventário completo por
sala, coleção estática/dinâmica, movimento entre salas, contenção, zero extent,
inclusão/exclusão do alvo estático/dinâmico, ordenação, falha fechada para
flags/definição/sala/KillTimeout desconhecidos e orçamento.
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

Com a API de inventário, a mesma execução House07/tick 214 foi repetida pelo
`odh-person --saved-world-main` após adicionar as 23 fontes arquitetônicas ao
renderer. `room_inventory` retornou complete para a sala 3 com 19 footprints,
todos na coleção estática nessa amostra. IDs: 18, 19, 26, 57, 105, 106, 111,
112, 113, 114, 115, 124, 125, 126, 127, 128, 130, 135 e 137. Isso confirma a
enumeração da sala a partir do World construído no Main e das posições atuais;
não é ainda uma tentativa de rota. Os footprints não incluem portal/wall
obstacles, filtro AttemptWalk, EntryPoint5 ou movimentação do avatar.

## Candidate placement diagnostic

`--placement-candidate X Y LEVEL DIRECTION` adds an independent query for a
candidate position against the prepared lot and the post-tick live World. It
reports the candidate room and radius-3 avatar box, `FloorChangeValid` inputs
from the unique avatar definition and live `ObjectData[42]` (`PlacementFlags`), plus the current
RoomInfo intersections. A later `entity_overlap_policy_audit` field composes
that list with the live World flags and restored EntryPoint IDs. The probe does
not move the avatar, execute a BHAV, remove the avatar from its current static
footprint, evaluate final placement policy, or claim route clearance. An
unavailable floor, room, definition, or live flags remains unknown/unsupported
rather than being inferred from the save snapshot.

An earlier version accidentally read general `ObjectData[8]` flags for
`FloorChangeValid`; Simitone reads the distinct `ObjectData[42]` placement
flags. The reader and the saved-World regression test now use/check word 42.
After the fix, House07 was rerun at tick 214. Candidates `(392,440,1,3)` and
`(400,440,1,3)` both resolved to room 3, floor 6, `ObjectData[42]=2307`,
`LevelOffset=0`, and `FloorChangeValid=allowed`; RoomInfo was complete. The
first query included only avatar 105's current static footprint, which the
overlap audit skipped as self. The second had no intersections. Both overlap
audits returned `allowed` with zero blocker pairs.

The probe now also reports `architecture_walkability_audit`. It builds the
candidate room's live AttemptWalk snapshot from complete Portal15, RoomObs,
WallObs and RoomInfo/entity inputs, resolves EntryPoint 5 through the bounded
read-only evaluator, then checks only the candidate cell in a bounded local
grid. This is a walkability check for that cell, not a route to it or final
placement approval. With 23 loose FLR/WLL sources, `(400,440,1,3)` returned
`clear`, `candidate_walkable=true`, complete architecture/portal inventories,
154 obstacle rectangles, one other avatar in the room and zero EP5 calls.
Candidate `(456,504,1,3)` returned `blocked` in room 1; its complete snapshot
had 190 rectangles, two EP5 calls, and `start_intersects=true`. The separate
entity audit also returned blocked with ObjectID 35 as its final blocker.
Neither query moved the avatar or executes a route.

These results cover the floor rule and the entity-overlap part of
`GetAvatarPlace` for those positions. They do not move the avatar or prove a
route to the position. The top-level `placement_decision` remains
`not_evaluated` until the remaining position-validity inputs and integration
are explicit.

`candidate_cell_preflight` now combines four separately reported gates:
`FloorChangeValid`, RoomInfo entity-overlap policy, availability of a complete
AttemptWalk obstacle snapshot, and the candidate cell's walkability result.
The cell gate also rejects coordinates outside the prepared lot bounds.
The answer is `clear` only when all four are known and allowed; any known
blocker yields `blocked`, and otherwise an unknown yields `unsupported`.
The result applies to that cell only. It does not mean the Sim can reach it,
pass through a changing door, or complete a room/lot transition, so the
top-level `placement_decision` remains `not_evaluated`.

After binding EntryPoint tables to the restored entities, both candidate probes
also report `entity_overlap_policy_audit`. For `(392,440,1,3)`, RoomInfo returned
only avatar 105's static footprint; the audit skipped the actor's own ID and
classified zero blocker pairs. For `(400,440,1,3)`, the intersection list was
empty and the audit also classified zero pairs. Both report `allowed` only for
the entity-overlap subset with Default `AllAvatarsSolid=false`; top-level
`placement_decision` remains `not_evaluated`. The group relation comes from the
restored save snapshot; the World does not yet model runtime group mutation.
The complete contract and synthetic coverage are in
[`world-avatar-place-audit-validation.md`](world-avatar-place-audit-validation.md).

A further House07 candidate `(456,504,1,3)` intersected entities 35 and 101;
the audit returned blocked by 35 after two classifications, with EP26 known
false. Candidate `(280,376,1,3)` intersected flamingo 155 and exercised its
authored BHAV4108 through the bounded read-only EP5 evaluator; it returned
false after one TestObjectType instruction because Bob's GUID did not match.
These remain hypothetical candidate queries, not avatar movement or original
engine placement evidence.

The synthetic World test now also runs repeated queries through live flag toggles,
container attach/detach, ID removal/reuse with a new generation, and avatar
kill-timeout death/revival. It checks that each next query rebuild reflects the
mutation; it does not add a persistent obstacle cache. `world_room_info` reports
38 checks. This validates the query adapter's snapshot contract only, not VM
placement decisions or live movement on the original engine.

## Combined preflight in House07 at tick 214

The rebuilt Linux CLI reran the two positions above after 214 ticks, with the
23 loose architecture resources and no display. `(400,440,1,3)` reported all
four preflight gates allowed and status `clear`; `(456,504,1,3)` reported two
known blockers (entity overlap and candidate cell) and status `blocked`, with
the floor and obstacle snapshot allowed. Both runs ended in the host's existing
`waiting` result (exit code 3), kept `placement_decision=not_evaluated`, and
produced the same 320x240 CPU frame (SHA-256
`8e61cf69fac5b4235cd3f8736a4b93e8755d694863abe0347585f58d2408dd25`). The
frames are `/tmp/odh-candidate-preflight-house07-400-440.ppm` and
`/tmp/odh-candidate-preflight-house07-456-504.ppm`; their equal image hash is
expected because the probe does not move Bob or alter the displayed scene.
This confirms headless rendering and diagnostic composition for those
positions; it does not validate a visible window, route, or final placement.

## Same-room route preview from the live actor position

The candidate CLI now builds a second obstacle snapshot from Bob's actual
position before asking the ODH route grid for a path to the candidate. This is
kept separate from the candidate-cell snapshot: EntryPoint 5 can depend on the
actor's position, so the destination probe cannot stand in for the route's
starting state. The route preview reports its start, goal, grid scope, path
cost, and point count. It is read-only; `route_clearance` remains false and
`placement_decision` remains `not_evaluated`.

On House07, a headless 32-tick Main run from the real save found a same-room
route from the live actor position `[392,440]` to candidate `[400,440]` in the
complete room grid (cost 80, 9 path points). Candidate `[456,504]` is in a
different room, so the route status is `not_evaluated` with reason
`different_room_requires_portal_transition`; the existing cell and entity
preflight still report its blockers. Neither query moved the avatar.

`found` means only that ODH's bounded, 1/16-tile obstacle grid found a path in
this snapshot. It does not establish equivalence with Simitone's `VMRectRouter`,
prove door opening/portal behavior, or permit movement. If a full room grid
exceeds its budget, the preview may use a bounded window; `bounded_no_path`
then means only that no path was found inside that window.
