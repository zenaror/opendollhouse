# Returning from a live lot to the neighborhood

Pressing **M** from the integrated Linux lot opens the existing neighborhood map in the same window. The live `odh_person_run` call remains on the stack, so its World, effective relationship data, Main VM, controllers, scheduler, interaction queue, animation playback and random state stay owned by the same session. No simulation tick runs while the map picker waits. Choosing the currently loaded lot restores the lot window and resumes that same state; choosing another lot is refused with an explanation. Escape closes the application.

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

The scripted callback validates runtime preservation and the unsupported-lot guard. It does not render or click the neighborhood map, send native X11 events, save to disk, change lots, or establish a complete playable session. Repeat a visual smoke test when an accessible X11 display is available. Durable saves, household editing and switching to a second lot remain required parts of the first playable build.
