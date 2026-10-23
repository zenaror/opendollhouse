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

The initial Linux neighborhood flow now lets the player click a vacant lot,
choose an unhoused family that can afford it, review the price and remaining
budget, and confirm before saving. It reloads the neighborhood and player state
after confirmation, verifies the selected family and lot are still available,
and refreshes the map before opening the lot. Move-in saves use a persistent
private sibling lock and compare the expected overlay before atomic replacement;
simultaneous launchers cannot silently erase each other's purchases. The lock
file remains beside the saved overlay in player state.

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
  Portanto, o reembolso de uma casa adquirida pelo novo fluxo não pode ser
  deduzido com segurança do preço SIMI. Para uma família originalmente alojada,
  o valor observado no FAMI pode ser devolvido segundo o comportamento do
  Simitone; para uma família recém-mudada, ainda falta provar como obter o valor
  de arquitetura atual. O overlay do OpenDollhouse continua sem despejo e sem
  reembolso até essa fonte ser identificada.

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
- `linux_family_overlay_store` verifies stale compare-and-write requests leave
  the current save unchanged, and matching snapshots publish successfully.
- `linux_family_overlay_store`: private atomic file replacement, refusal to
  follow file/directory symlinks, unchanged parent permissions, lookup by the
  source neighborhood hash, rejection of stale or mismatched identities, and
  compare-and-write conflict handling.
- `family_overlay`: assignment validation, source identity, roster identity,
  vacant destination, one-time purchase accounting, and stale/conflict cases.
- `session_entry_resolution`: the effective moved family resolves to the same
  lot and resident GUIDs.
- Linux build completed; 211 CTest cases passed and the display-dependent
  `linux_window_backend` case was skipped because this run had no display.
- Read-only `odh-person --plan-only` on the installed game data resolved the
  original occupied House05 with three residents and seven expansion packs.
  Its player-state directory was isolated under `/tmp`; this does not verify
  launching the live simulation or using a saved move-in overlay.

## Still missing

The visual family move-in flow compiled but could not be clicked through in this
headless test run; `linux_window_backend` was skipped because no display was
available. Synthetic checks cover the planner and persistent store separately,
not the entire UI-to-runtime path. Move-out and refund accounting remain
unsupported. The runtime reloads the overlay when it starts; a future hardening
step should verify the selected family identity at that boundary.
