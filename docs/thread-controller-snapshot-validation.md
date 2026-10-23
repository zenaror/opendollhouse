# Entity thread controller snapshots

`InteractionQueue`, `Sleep`, and `EntityThreadControllers` expose owned runtime
snapshots. They copy continuation values without retaining World/VM pointers,
callbacks, closures, or resources. This is an in-memory foundation for session
persistence, not a session file format or a process-restart implementation.

The queue snapshot includes the complete FIFO (UIDs, target generations,
`VmCall` contexts, owners, arguments and return modes), active action and wait
tickets, deadlines, completion, actor/run generations, sequence, capacity,
clock/idle start/last tick/resume tick, interrupt state, and diagnostic text.
The Sleep snapshot includes actor identity and generation, job ticket/argument,
idle start/deadline, clock/last tick, local interrupt, Notify reexecution state,
and diagnostic text. The combined snapshot also retains the shared thread
interrupt latch, including a signal that one service has not consumed yet.

## Restoration contract

Restore the surrounding World and VM first, with equivalent immutable programs,
code owners, memory and primitive callbacks. Create fresh stable controllers,
recreate equivalent RNG and interrupt callback bindings, and bind the controllers
to that VM before calling `restore_snapshot`. Bind does not consume freshness;
runtime operations and a successful restore do. A second restore is rejected,
including after restoring empty controller state.

The saved queue capacity must fit the destination's constructor capacity and
becomes the restored runtime capacity. Validation checks FIFO UIDs, argument
budgets, live actor/target generations, run generation, pending/invocation and
completion tickets, instruction identity, deadlines, and consecutive-clock
state. A queued routine's immutable code must be recreated by the host; these
snapshots do not resolve resources or invent missing code. Capture and restore
must occur outside VM/controller callbacks at a quiescent host boundary.

Invalid state returns false. Allocation errors propagate. Queue and Sleep stage
their copied values before publication; the combined operation validates both
and stages both copies before publishing either, preserving both controllers
and the latch on failure. Restoration does not execute an instruction, consume
RNG, notify a thread, enqueue a new action, or advance a tick.

## Synthetic checks

`thread_controller_snapshot` compares uninterrupted execution with fresh
World/VM/controller reconstruction in the same process. It checks complete VM
and controller snapshots, instruction traces, fixture World flags/accounting,
and external RNG callback counts across Sleep and IdleForInput waits, queued
and active actions, action-owned Sleep, true/false completions, next-tick
resumption, local interrupts, Notify before and after waiting, shared interrupt
consumption by a later service, and retained diagnostics.

Rejections cover malformed identities, generations, FIFO/capacity/arguments,
tickets, deadlines and completion boundaries. Allocation-failure injection
sweeps the combined restore and confirms atomicity and retained freshness.
Capture and restore from VM callbacks are rejected. Existing Sleep, queue and
entity scheduler tests remain regression checks for runtime behavior.

Use a fresh build directory: caches copied from another checkout may still
contain that checkout's absolute source paths.

```sh
cmake -S . -B /tmp/odh-thread-snapshot-build -DCMAKE_BUILD_TYPE=Debug \
  -DODH_X11=OFF -DODH_SDL_AUDIO=OFF
cmake --build /tmp/odh-thread-snapshot-build --target \
  odh-thread-controller-snapshot-tests odh-sleep-tests \
  odh-interaction-tests odh-entity-scheduler-tests -j2
ctest --test-dir /tmp/odh-thread-snapshot-build \
  -R '^(thread_controller_snapshot|sleep|interaction_queue|entity_vm_scheduler)$' \
  --output-on-failure
```

On 2026-10-22 the fresh Debug build passed all four listed CTest cases; the
new test reported 1014 synthetic checks. A separate fresh build with
`ODH_SANITIZERS=ON` passed `thread_controller_snapshot` with
`ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer could not run in the traced tool
environment (its reported ptrace limitation), so that run establishes no leak
checking result. AddressSanitizer and UndefinedBehaviorSanitizer remained on.

Passing these checks establishes synthetic in-process controller continuation
only. File encoding, whole-session resource manifests, additional services,
integrated playable save/load, and persistence across process restart require
separate implementation and evidence.
