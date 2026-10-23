# Linux family overlay validation

This checkpoint wires the saved family overlay into the Linux neighborhood
map, selected-lot resolution, and the lot runtime's family lookup. The overlay
is kept in player state, keyed by the SHA-256 of the exact `Neighborhood.iff`
bytes. The commercial neighborhood file remains read-only. A missing overlay
keeps the original FAMI records; a corrupt, mismatched, stale, or conflicting
overlay fails closed.

The Linux launcher reads one overlay for the map and selected-lot resolver.
Those paths compare its source identity against the file they parse. The person
runtime loads the same identity-keyed overlay before selecting the active FAMI.
This keeps the visible lot marker and the household lookup based on the same
family assignment during normal play.

The initial Linux neighborhood flow lets the player click a vacant lot,
choose an unhoused family that can afford it, review the price and remaining
budget, and confirm before saving. It reloads the neighborhood and player state
after confirmation, verifies the selected family and lot are still available,
and refreshes the map before opening the lot. Move-in saves use a persistent
private sibling lock and compare the expected overlay before atomic replacement;
simultaneous launchers cannot silently erase each other's purchases. The lock
file remains beside the saved overlay in player state.

The local overlay is now a version 2 operation log; version 1 purchase-only
saves remain readable. Its core can evict a household that was already in a
house in the original neighborhood data, refund that source FAMI's
`ValueInArch`, clear the value, and make the lot vacant. It can then replay a
move-in to another house. A household that was first moved in by the player
cannot be evicted again: the current Simitone source review does not establish
how `ValueInArch` changes after purchase, so a second refund would be a guess.

When the player selects an occupied lot, the Linux neighborhood window offers
Enter/click to open it and `E` to move the family out only when the refund is
supported by the original family data. The confirmation screen shows the
refund and resulting budget. Confirming rereads the neighborhood and overlay,
checks the family and lot identity, saves with compare-and-write, refreshes the
map, and returns to lot selection. Families without a verified refund can
still enter their house.

## Verified

- A referência do Simitone distingue compra e despejo: `SetFamilyForHouse(...,
  buy: true)` define a casa e desconta `SIMI.PurchaseValue`; `TS1GameScreen.EvictLot`
  soma `FAMI.ValueInArch` ao orçamento, zera esse campo e chama `MoveOut`, que
  define `HouseNumber` como zero. A tela de confirmação mostra orçamento mais
  `ValueInArch`. Localizadores: `refs/upstream/Simitone/FreeSO/TSOClient/tso.content/TS1/TS1NeighbourProvider.cs:221-234`,
  `refs/upstream/Simitone/Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs:249-254`
  e `.../UI/Panels/UIHouseSelectPanel.cs:351-365`, Simitone
  `4906db3156616bfaffc10ce0963cf08062975fef` e FreeSO do submódulo
  `f86bbd34b2112296ed786741f640a0535886d180`. Isso é leitura estática do código,
  não teste contra o executável original.
- A leitura/escrita de FAMI nessa revisão apenas serializa `ValueInArch`; não
  encontrei nesses caminhos TS1 um ponto que o atualize após compra ou construção.
  Portanto, o reembolso depois de uma compra nova não pode ser deduzido com
  segurança do preço SIMI. O planejador permite devolver o valor observado no
  FAMI somente para família que já estava alojada nos dados originais. Depois
  que essa família é mudada, também bloqueia novo despejo até provar como o
  campo de arquitetura muda. É o comportamento do núcleo local, não validação
  contra o jogo original.
- Uma busca estática inicial com Ghidra 12.1.3 no executável original PE32/x86
  (SHA-256 `32857c25e00594a9bc5d700f7b373b6df59ab76ec51b36aa1d30f3dd973eff58`)
  não localizou os marcadores FAMI/IMAF nem confirmou gravações dos campos de
  FAMI. Uma heurística por deslocamentos encontrou 25 funções candidatas nas
  áreas de código adicionais `stxt774`/`stxt371`; as funções mais fortes
  examinadas tratam de strings codificadas e acesso a arquivos, sem vínculo
  demonstrado com famílias. Isso descarta essa heurística como prova, não prova
  que o executável nunca grava FAMI. Projeto, scripts e saídas completos ficam
  em `reports/ghidra/` (ignorado pelo Git).

- The SIMI purchase price is now calculated as
  `LotValue + ObjectsValue + (ArchitectureValue * 7) / 10`, using integer
  division. This matches the Simitone TS1 SIMI property and its household
  move-in budget deduction (`refs/upstream/Simitone/FreeSO/TSOClient/tso.files/Formats/IFF/Chunks/SIMI.cs`
  and `refs/upstream/Simitone/FreeSO/TSOClient/tso.content/TS1/TS1NeighbourProvider.cs`).
  OpenDollhouse computes with a wide intermediate and rejects results outside
  the saved signed 32-bit range. Synthetic checks cover truncation, negative
  integer division, and overflow. Read-only parsing of original House01,
  House05, and House07 yielded purchase values 10,500, 32,353, and 17,225;
  House05 used 7,000 + 14,660 + 70% of 15,277. These values have not yet been
  compared against a running Simitone build.
- `plan_family_move_in_overlay` preserves prior purchases and rejects occupied
  lots, already housed families, stale identities, and unaffordable purchases.
- `family_overlay` replays move-out and rehome operations, checks exact budget
  refunds, clears `ValueInArch`, rejects new-family refunds, negative values,
  overflow, and stale source data, and reads version 1 saves as move-ins.
- `linux_family_overlay_store` verifies stale compare-and-write requests leave
  the current save unchanged, and matching snapshots publish successfully.
- `linux_family_overlay_store`: private atomic file replacement, refusal to
  follow file/directory symlinks, unchanged parent permissions, lookup by the
  source neighborhood hash, rejection of stale or mismatched identities, and
  compare-and-write conflict handling.
- `family_overlay`: assignment validation, source identity, roster identity,
  vacant destination, one-time purchase accounting, and stale/conflict cases.
- `linux_neighborhood_screen_actions`: synthetic X11 key events verify occupied
  lot entry, supported move-out selection, confirmation/cancel, and refusal of
  move-out when the refund is unverified. This covers screen decisions, not the
  entire UI-to-save-to-map path or a manual visual play session.
- An isolated interactive run of `odh-person` against the local `../referencias/The Sims`
  data completed House05 → move-out → refreshed neighborhood → House01 family
  picker. The confirmation reported a 33,116 refund and 35,658 resulting
  budget; House01 then showed a 9,500 budget after purchase. The move-in was
  canceled before writing. Reopening the app with the same temporary player
  profile showed House05's center marker in the vacant-lot color
  `srgb(40,220,120)`. `Neighborhood.iff` kept SHA-256
  `e585615fed1f9ddfb15ff8f5ebe7a09ea48a1369e9a3106f8331d3d9f5dde323` before
  and after. Xephyr and X11-generated events isolated the run; its overlay and
  screenshots stayed in `/tmp`, and no commercial input file changed. This is
  an integrated Linux run on this installation, not proof against the original
  executable or a human-input play test.
- `session_entry_resolution`: the effective moved family resolves to the same
  lot and resident GUIDs.
- Full Linux build completed; all 213 CTest cases passed in this run.
- Read-only `odh-person --plan-only` on the installed game data resolved the
  original occupied House05 with three residents and seven expansion packs.
  Its player-state directory was isolated under `/tmp`; this does not verify
  launching the live simulation or using a saved move-in overlay.

## Still missing

An automated end-to-end test of UI → compare-and-write → refreshed map →
choosing another lot is still missing; the one-off Xephyr run above covered
that path using this local installation. This build was configured without
PangoCairo, so the map rendered but choice/confirmation text was absent from
the canvas; the window title still showed the active choice and amounts. A
human-input play test and a visible text-rendering build remain pending. The
runtime reloads the overlay when it starts; a future hardening step should
verify the selected family identity at that boundary.
