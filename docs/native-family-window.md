# Native graphical family management

The Linux window flow now connects a live household to the existing launcher
family-management screens. It keeps simulation execution, save authority and
map actions on separate sides of a save-and-unwind boundary. This is an
incremental native C++ UI, not a claim of original TS1 interface equivalence.

## Live map requests

In a running lot, `M` opens the neighborhood resume map while the simulation
holds its current tick and execution state. Clicking the active house returns
a typed travel request for that same house. The runtime recognizes this as a
same-lot resume: it does not save, unwind, purchase or reconstruct the household.
Clicking a different lot retains the existing save-and-travel behavior.

The new `E` shortcut and visible management button return
`NeighborhoodMapRequest{manage_families, 0}`. The map screen only returns intent;
it does not evict a family, change an overlay or publish a save. Escape returns
no request and closes the runtime through its existing cancellation path.
It is not the same action as clicking the active house to resume.

`tools/neighborhood_request.hpp` defines the typed request. The X11 screen
consumer is `show_neighborhood_resume_picker` in `neighborhood_screen.cpp`.
The English label "E: Manage families" is provisional. Original-game UI and
functional localization remain requirements; the new label does not complete
either requirement.

The button chooses the first free corner among four bounded positions, avoiding
every lot-marker hit square. Lot clicks retain priority. If all four positions
overlap markers, the map keeps its unobscured lots and advertises E in the window
title without drawing a button. The button retains a visible background in
builds without the text renderer. This prevents the first fixed-position
prototype from hiding a selectable house near the lower-left edge.

## Save, unwind and reopen management

When `person.cpp` receives management intent, it first calls
`save_live_session()`. A failed save keeps the current runtime active and
reports the failure; management is not entered with an uncommitted live state.
A successful save produces a `NeighborhoodTransitionRequest` containing the
management kind and committed checkpoint path. The runtime then returns its
`PersonRunResult`, unwinding its World, VMs, controllers and services before
the launcher invokes management.

`linux_game_entry.cpp` reuses the extracted `run_management_map` flow. It
re-reads current player state and displays the existing map, CAS, lot details,
family picker, purchase confirmation and eviction confirmation screens. A new
destination rebuilds its resources, occupancy projection, household and save
services; it does not keep pointers into the previous runtime.

Before entering management the launcher clears its automatic rollback options
and per-house advisory save cache. Management can expire the previous family
lease, including by evicting the previously active household. That old
checkpoint remains retained evidence, but cannot authorize automatic rollback
to an expired occupancy after a later failure. Current managed checkpoint
selection continues to come from the journal, not the cleared advisory cache.

Canceling management closes the application and retains changes already
committed through the screens. It does not resume the earlier live runtime or
undo a completed eviction. If a subsequently selected destination is
unsupported, preparation fails safely without installing an automatic rollback
to the old occupancy. Deferred purchase publication still requires successful
destination initialization, portable carryover when required, and full save
preflight before charging the family.

## Existing controls and authority

- Map: click a house marker; `C` opens graphical CAS when that build supports it.
- Lot details: Enter continues; Escape returns to the management map.
- Occupied lot: Enter/click enters; `E` requests eviction if the managed-family
  checkpoint and transferable-state preflight permit it.
- Eviction confirmation: Enter/primary click confirms. The launcher revalidates
  current family, source and overlay before its compare-and-exchange write.
- Vacant-lot picker: Tab/Shift-Tab or a row click selects an unhoused family;
  Enter or the move-in button selects an affordable purchase. Confirmation
  creates a pending purchase, rather than immediately deducting funds.
- Live lot: `S` explicitly saves; `P` pauses; Escape closes. Automatic save on
  exit is not enabled by default. `M`, followed by `E` or the management button,
  is the new save-and-management transition.

The screens do not bypass the family journal, transfer receipt, occupancy
retirement authorization or current-checkpoint checks documented in
`native-family-transfer.md` and `native-occupancy-reconstruction.md`.

## Graphical gate setup and evidence boundary

Graphical CAS creates families with the normal initial budget20000. The
installed-data scenario uses House07 price17225 and House09 price3500. With
the retained zero refund of the supported created-family reference profile,
House07 leaves2775, which does not afford House09. A larger budget in a test
must therefore be identified as explicit setup, not presented as ordinary CAS
economics or proof of original liquidation behavior.

The isolated graphical gate uses a helper to create two private adult Sims,
seed an explicit initial budget57950, and evict the original source House07
family in its isolated overlay. These are setup operations, not graphical
coverage. The intended measured path performs the subsequent player-family
purchases and evictions through actual X11 input, including live map
management and managed checkpoint reopen. It must not use a headless move
command to substitute for one of those measured screen actions.

Final evidence is `reports/native-family-window/attempt-3/result.json`, with
eight captures and the replayable `validate_real.py` driver. It uses commit
base `9ec4546` plus this checkpoint's native UI changes. Host SHA256:
`fe7351297137816a56787e0983aa7fd5254b95dd7bbb605cdb53010537536f33`.
The unchanged CAS helper SHA256 is
`5d301fb38835b042b2acdded78ce28d5bb2921c40b025bb838e9123a94ec25d1`.

```sh
python3 reports/native-family-window/validate_real.py \
  --output reports/native-family-window/new-attempt
```

The driver refuses an existing output directory. It allocates its own Xvfb
display using `-displayfd`, isolates all XDG/log state in a temporary profile,
and uses xdotool events directed only at that display. Captures use explicit
`xwd -id`; no interactive pointer grab or user's desktop is involved.

One native process, PID261963, performs the complete measured lifecycle:
click House07 → select the created family → confirm purchase → choose resident
→ pause/save → M/E → confirm eviction → buy House09 → pause/save → M/E →
confirm eviction → buy House07 again → pause/save → close. Its three runtime
records finish at tick3; transfer receipts are applied on the second and third
entries. Retired installed IDs are105/137 in both House07 activations, and none
in House09. Occupancy epochs are1/2/3 and budgets40725/37225/20000.

All three saved complete rosters match the helper's GUID/NID/OBJD/resource
names and fingerprints. The compared persistent personal projection stays
equal, and the old House07 GUIDs are absent. Needs and fractional decay state
are checked and hashed but may advance during visible unpaused simulation;
this graphical gate does not claim identical need values across elapsed time.
Previously published source archives remain present and unchanged.

A fresh graphical process, PID262311, restores the authoritative House07
checkpoint at tick3, presents the lot and advances to tick4 before closing.
No additional save is requested, so the persisted overlay/head remain identical.
Root inspected the captured lot with both adults and the map with the button
in the upper-left corner, leaving the lower-edge markers visible.

All116 protected original files and both private character files retain their
hashes. Host/helper identities remain stable. Both game processes, helper and
Xvfb PID261960 were waited and absent afterward; the temporary profile was
removed. No virtual display remains from the gate.

`ctest-headless.log` records14 passing save/transfer/entry/travel regressions
after the management integration; `ctest-x11.log` records the two final passing
screen-action and move-out integration tests. Actual screen inputs cover E,
the button, same-house selection, cancellation and travel to a marker under
the first prototype's button position. Independent static review inspected
the save/unwind/authority boundary and final rectangle limits without finding
an outstanding blocker.

Attempt1 completed the three-visit graphical lifecycle but its reopen driver
used an old log offset after process-start log rotation and timed out; it is
retained as a test-driver failure. Attempt2 passed after correcting that
observation and strengthening roster/carry checks. Attempt3 includes the final
button placement fix. Screen tests remain distinct from installed-data GUI
execution; helper creation remains distinct from exercising graphical CAS.

## Remaining limits

This connection does not implement property persistence, original liquidation,
held-child placement or unsupported portable personal profiles. Unsupported
cases continue to reject rather than dropping belongings or progress to make
the graphical sequence continue. It does not establish long-session stability,
the original appearance/control scheme, full localization, or console hardware
operation. The new request type is host coordination; no World or session wire
format change is introduced by the graphical connection.
