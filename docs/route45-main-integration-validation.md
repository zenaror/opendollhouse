# Route45 Main integration probe

The optional `tests/house07_route45_integration.py` probe checks the saved-world
Main host against a local copy of the base-game House07 data. It patches the
selected avatar's Main BHAV only in a temporary IFF file and replaces that
routine with one synthetic Route45 call. The source game files stay unchanged,
and no game data is copied into this repository.

Run it from the repository root after building `odh-person`:

```sh
cmake --build . --target odh-person -j2
python3 tests/house07_route45_integration.py \
  --binary ./odh-person \
  --game-root "../referencias/The Sims"
```

The default saved avatar is object 105, and the default StackObject is object
18. Both can be changed with `--avatar-id` and `--stack-object-id` when the
local House07 save uses different IDs. Success requires a returned-true VM,
at least one Route45 movement callback, a completed route ticket, and changed
avatar coordinates. The JSON output reports the exact route host state and the
number of live obstacle snapshots refreshed before movement ticks.

This is a synthetic BHAV integration test using real external lot, architecture,
global SLOT100, and object-definition resources. It verifies that the Main VM
dispatches Route45 through the host scheduler and that movement updates the
restored World. The runtime tracks a World spatial revision and rebuilds the
bounded same-room obstacle grid when relevant entity state changes; unchanged
route steps reuse the current grid. The probe reports refresh count and policy
alongside movement. It does not establish natural interaction behavior, portal
traversal, or full-gameplay compatibility.
