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

## Verified

- `linux_family_overlay_store`: private atomic file replacement, refusal to
  follow file/directory symlinks, unchanged parent permissions, lookup by the
  source neighborhood hash, and rejection of stale or mismatched identities.
- `family_overlay`: assignment validation, source identity, roster identity,
  vacant destination, one-time purchase accounting, and stale/conflict cases.
- `session_entry_resolution`: the effective moved family resolves to the same
  lot and resident GUIDs.
- Linux build completed; CTest passed 212/212, including the X11 window resize
  check on the available display.
- Read-only `odh-person --plan-only` on the installed game data resolved the
  original occupied House05 with three residents and seven expansion packs.
  Its player-state directory was isolated under `/tmp`; this does not verify
  launching the live simulation or using a saved move-in overlay.

## Still missing

The game does not yet present a move-in action that writes this overlay, so
family relocation is not playable yet. Move-out, refund accounting, transaction
rollback, and a full synthetic end-to-end move-in test are also pending. The
runtime reloads the overlay when it starts; future write support should pass a
single immutable snapshot through the launcher and runtime, or verify the
selected family identity at the runtime boundary.
