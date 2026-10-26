# Returning from a live lot to the neighborhood

Pressing **M** from the integrated Linux lot opens the existing neighborhood map in the same window. The live `odh_person_run` call remains on the stack while the map is open. Choosing the current lot resumes that same live state. Choosing another lot now asks the runtime to save at a safe point, ends that lot run, and lets the launcher construct the selected lot as a fresh runtime. The launcher keeps one save per visited lot so returning can restore its earlier state. If destination preparation or startup fails, it attempts to restore the lot the player just left. A failed save keeps the current lot running. Escape closes the application.

The live map also exposes **E: Manage families** and a clickable button. This
returns an explicit management request; it cannot change family state while
the runtime is alive. The runtime saves and unwinds before the launcher opens
the complete neighborhood management screen. That screen reloads persisted
family authority and supports the existing CAS, purchase and eviction flows.
Management clears the old travel rollback/cache because eviction can invalidate
the old occupancy. Cancelling closes the application with confirmed changes
retained; a later preparation failure does not restore an expired occupancy.
See [native-family-window.md](native-family-window.md) for the graphical gate
and its evidence limits. This is distinct from ordinary residential travel,
whose rollback behavior above remains unchanged.

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

That Legacy Collection result remains historical evidence for those inputs. The later commercial gate below uses the Complete Collection installation and demonstrates a native residential round trip; it does not rerun the exact Legacy case. A visual X11 map test, visiting another household with the original family, and expansion trips remain separate gates.


## Delayed residential switching with complete households (2026-10-26)

The integrated diagnostic schedule now accepts `--headless-travel-tick N`,
positive and below the per-visit tick limit. Default remains1. It schedules the
existing neighborhood request after N additional ticks in each visit, including
a previously saved lot. The raw absolute tick interface remains available;
the launcher explicitly requests relative scheduling and overflow is checked
before adding the restored clock. Audio enable/disable/drain preferences now
survive destination construction and rollback; they were previously dropped.

The synthetic CLI gate retains its original short7→8→7 test and adds
7→8→7→8 with delay3 and four ticks per visit. Departures are at absolute
3,3,6, with the last House08 finishing at7 after restoring tick3. Every saved
section equals a fresh continuous House08 run to7, both residents execute, and
SDL dummy audio remains selected across all four visits. Invalid zero, missing
route and tick-at-limit schedules fail before publishing a save. The fixture's
Sleep loop was corrected to reset its argument on the next cycle; increasing
its instruction budget would only have hidden a synthetic busy loop.

The complete commercial gate is
`reports/native-residential-switch/real-1/result.json`, using
`../referencias/The Sims` (Complete Collection), executable SHA256
`de878b8d77057b820a4d3f37153c10dc66f130245ef48be723123c37a56e7b56`.
Five OS processes execute eight native lot sessions:

- House07 Sim105 sits in chair128. The first departure occurs at300, after
  the completed Sit. House05 then simulates300 ticks with its own household.
  Returning to House07 restores tick300 and advances another512 ticks to812;
  Sim105 remains attached to chair128 and the complete household is restored.
- Every one of the13 final save sections matches an uninterrupted House07 run
  to812, together with actor, globals, gameplay RNG and tick JSON fields.
  A separate fresh zero-tick reopen also matches all13 sections.
- A fresh continuation requests Stand105 at813 and completes with posture0 and
  no attachment, demonstrating use of the seat after the residential round trip.
- An unavailable House98 destination is rejected because it has no unique
  LotLocations entry. The launcher restores the saved House07 once and resumes
  play. Its13 final sections equal the uninterrupted control; the failure is
  retained in stderr rather than presented as a successful destination visit.
- Audio remains enabled with SDL dummy in both successful switches and rollback.
  All five processes exit0, all temporary profiles are removed, and30 protected
  original file hashes remain unchanged. No virtual display is created.

Ten focused CTests passed in
`reports/native-residential-switch/attempt-1/final-expanded-ctest.log` after the
host build in `build.log`. A broader regression initially found an obsolete
expected error text for a resident seated without a completed Sit; its exact
negative assertion now names the supported-boundary error, without relaxing the
rejection or previous-save preservation. Independent review inspected the code,
fixtures, reports and documentation without repeating test execution.

These are whole-household residential-session switches through the existing
neighborhood request dispatcher. They open the destination's resident family;
they do not transport the originating family as visitors or implement a trip to
Downtown, Vacation, Studio or Magic Town. Headless input does not prove native
mouse/keyboard interaction with the rendered neighborhood map. Full playable
parity, original interface, construction/buying and expansion activities remain
incomplete.
