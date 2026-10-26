# Native player order queue

The C++ preview accepts another TTAB request while its selected Sim already has
queued or active work. Menus can reopen, the selected Sim can change, and the
provisional order list shows that Sim's pending player requests in native queue
order. Clicking a row cancels its exact request. Backspace retains the existing
menu/Go Here precedence, then selects an outstanding request of the selected Sim.
The list has bounded pages and uses game labels converted from their declared
legacy encoding to UTF-8. Scene, labels and input remain native C++.

## Presentation contract

`UserInteractionTracker` is shared, header-only C++ and observes value snapshots
at external safe points. The simulation queue is authoritative. The identity is
(actor ID, actor generation, Main run generation, native 64-bit request UID).
The wrapping public queue UID and the global latest-request display are not
used to attribute completion. Only `check_permissions` actions are rehydrated as
player orders, including protected ancestors in `suspended_active`.

A matching completion supplies true/false. Another Sim's completion or a child
interaction's return does not finish the request. An unexplained disappearance
is dropped, without a fabricated false result. Actor/Main replacement invalidates
outstanding identities; existing terminal outcomes remain historical. Explicit
cancellation intent comes only from an accepted `cancel()` result. NotifyIdle
alone also represents priority competition and does not prove a human cancel.

The host uses a budget of 256 live records (eight residents × 32 native entries)
and 64 terminal history records. Pending work is never evicted to make room.
Terminal retention follows terminal-transition order, so an old request that
finally completes remains available even after newer requests have filled the
history. Stand bindings are updated from every successful request and its actor's
actual posture/attachment, independently of current menu selection.

Save data continues to use controller codec v5: queue identity, resources and
execution already persist there. A fresh process reconstructs outstanding
requests from those queues. Terminal history, submission errors, labels and the
currently selected Sim are presentation state and are not added to the save.
An explicitly stopping order may therefore reopen with its native active/pending
state; the native cancel flags still govern execution. This list covers player
requests; autonomous continuation icons and the final original interface remain
future work. Completed household Sit boundaries are covered separately in
[native-household-seated.md](native-household-seated.md).

## Input and diagnostics

The integrated CLI accepts up to 32 repeated
`--headless-resident-interaction ACTOR OBJECT TICK` arguments. ACTOR is a native
loaded household entity ID, including the primary resident. Ticks are relative
to the restored session, nondecreasing, and stable in CLI order when equal.
The option still requires the bounded whole-household profile. Both parser
rejections and runtime preflight failures have explicit diagnostics.

`player_requests` reports each known identity, target, action, native position,
state and real result. `player_submissions` records every scheduled attempt,
including invalid actors/targets that never reach enqueue. Accepted attempts
include actor/Main generations for joining records. History is bounded and is
not replayed after load. The old `interaction_runtime` object projects the latest
known request for compatibility; it does not own the queue or its results.

The host does not force a sleeping Main awake merely because a request arrives.
Pinned Simitone/FreeSO `VMNetInteractionCmd` and `PushUserInteraction` enqueue;
`NotifyOutOfIdle` is the explicit interrupt path. Pending work waits for the
thread's appropriate dispatch point. Do not reinterpret a short pending interval
as failed submission or inject an unsupported wakeup.

## Verified checkpoint

Build: `reports/native-player-queue/attempt-1/final-build.log`.
Nineteen focused CTests passed in `final-ctest.log`, including the portable tracker
matrix, controller/VM save roundtrips, actor/run reuse, child completion, native
order, cancellation and late completion with full terminal history.

Final binary SHA256:
`6d2a3f3c76ec2d045afd3ade2dc4dbff71d61f1f9b0ee9b2bbf0ad76ac229217`.

- `reports/native-player-queue/window-final/`: isolated Xvfb/X11 mouse and keyboard
  input added two House07 snack requests, cancelled only UID2 by its row, selected
  Sim137, and pressed Backspace without cancelling Sim105's UID1. S wrote the save;
  Escape ended the game. The game, Xvfb and temporary profile were closed. The
  screenshot `two-orders.png` shows the provisional queue. Language14 game labels
  and HUD were rendered. Work was still pending at the saved tick10.
- `window-zero-final/result.json`: that window-written save reopened at tick10
  with the outstanding UID1 reconstructed and all 12 save sections identical.
  Backward schedule ticks and a 33rd scripted order were rejected explicitly.
- `roundtrips-final/result.json`: twelve fresh processes compared uninterrupted
  execution with save/reopen at tick256 and continuation through tick4096. Two
  requests of Sim105 both completed true; rejected target32767 and actor32766
  were retained as rejected attempts. Requests of Sims105/137 both used native
  UID1; cancelling Sim137's request preserved Sim105's successful result. A third
  case continued the window-written save and compared it with a split run. Every
  case matched all 12 sections and actor/global/RNG/tick fields. Twenty-five
  original files retained their hashes; processes and profiles were cleaned up.

Static review and report inspection were independent; the reviewer did not repeat
the test runs. The first visual/roundtrip reports before the terminal-history fix
remain intermediate evidence. These gates establish the exercised native player
queue paths. Full game parity, the original interface and other platform builds
remain incomplete.
