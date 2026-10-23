# Returning from a live lot to the neighborhood

Pressing **M** from the integrated Linux lot opens the existing neighborhood map in the same window. The live `odh_person_run` call remains on the stack while the map is open. Choosing the current lot resumes that same live state. Choosing another lot now asks the runtime to save at a safe point, ends that lot run, and lets the launcher construct the selected lot as a fresh runtime. The launcher keeps one save per visited lot so returning can restore its earlier state. If destination preparation or startup fails, it attempts to restore the lot the player just left. A failed save keeps the current lot running. Escape closes the application.

The first deterministic integration uses House07 and drives the real `WindowInput` dispatcher without a display. It selects the chair by a pixel in the actual lot pick buffer, runs Sit, requests neighborhood return, resumes the same lot, then runs Stand. The evidence compares the tick, Main VM step count, avatar generation/position/person data/attachment and relationship matrix counts immediately before and after the scripted callback. The Sit attachment survives the map step and the original Stand BHAV completes afterward. The test checks these values for the selected avatar and checks relationship counts; it does not deep-compare every World entity or every relationship value.

```sh
python3 tests/house07_interaction_integration.py \
  --binary /tmp/odh-acceleration-clean/odh-person \
  --game-root '../referencias/The Sims' \
  --window-input --window-neighborhood --window-stand

python3 tests/house07_interaction_integration.py \
  --binary /tmp/odh-acceleration-clean/odh-person \
  --game-root '../referencias/The Sims' \
  --window-input --window-neighborhood --neighborhood-house 8 \
  --neighborhood-expect-rejected --window-stand
```

The scripted callback above validates only same-lot runtime preservation; it does not render or click the map, send native X11 events, or exercise a real lot change. The launcher travel test uses a stub runtime and checks the coordinator's A→B→A cache behavior and rollback paths, but it does not prove that a real Sims lot can be entered and restored. Repeat a visual X11 test and an actual two-lot game-data test when available. This step is progress toward the first playable build, not proof of a complete session or expansion coverage.
