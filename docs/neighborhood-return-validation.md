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

The scripted callback above validates only same-lot runtime preservation; it does not render or click the map, send native X11 events, or exercise a real lot change. The `linux_lot_travel_cli` CTest now drives the integrated runtime through synthetic occupied House07 and House08 data and confirms 7→8→7, including restoring House07 at tick 1. This checks the launcher and runtime together, but synthetic content cannot prove behavior on commercial lots.

An initial real-data attempt used the read-only Legacy Collection installation with all saves and user state isolated under `/tmp/odh-real-travel-validation-57fafad-family12-distance1`. House07 saved and the launcher entered House05. That run exposed and then passed through two real BHAV instructions: SetToNext FamilyMember (opcode 31 mode 12, comparing PersonData[61], the FAMI chunk ID) and GetDistanceTo (opcode 11, writing Temp1 from the caller's position). It then stopped before travel could be completed: the route snapshot for resident 16 returned `attempt_walk_snapshot_entry_point5_result_unknown`; no destination save or return to House07 was produced. The trace reached private BHAV 8534 at opcode 45 after the household routine had begun. This is a concrete runtime blocker, not a passing real-lot travel test.

Repeat a visual X11 test and the actual two-lot game-data journey after the EntryPoint5/AttemptWalk blocker is resolved. The current automated travel test proves integrated synthetic A→B→A behavior only. This step is progress toward the first playable build, not proof of a complete session or expansion coverage.
