# Entity VM scheduler snapshots

`EntityVmScheduler::snapshot()` captures a value-only, in-memory continuation
between ticks. It retains the registered entity IDs and VM run-generation,
step-count and status stamps; hook-presence flags; entity capacity; the last
completed tick; the scarce-budget fairness cursor; the last instruction count;
and the terminal fault flag and diagnostic. Sorted dispatch and rotating grants
therefore continue from the same point after rebuilding the surrounding state.

The version-1 `EntityVmSchedulerSnapshot` codec stores those counters and
roster stamps using bounded little-endian fields. It rejects unsorted or
oversized rosters, unknown VM statuses, invalid cursor references, clocks
outside the scheduler range, malformed flags, truncation and trailing bytes.
The codec does not store VM programs or scheduler hooks.

Reconstruct the equivalent World, immutable programs/owners, VM snapshots and
controller snapshots first. Register those VMs with equivalent before/after and
interrupt hooks, then restore the scheduler before any scheduler runtime use.
The scheduler has no World reference: entity IDs and VM stamps verify the
registration correspondence, but cannot prove World entity generations, code
identity, arbitrary callback semantics or external state. Those remain host
preconditions. A World/VM/service/manifest session coordinator is still needed.
No VM pointer, callable, resource data or execution state is serialized here.

A destination's configured capacity is a ceiling; a successful restore adopts
the saved capacity. A second restore, runtime tick attempt, successful removal
or invoked interrupt callback consumes freshness. Registration alone does not.
A snapshot with the wrong sorted roster, VM generation/steps/status, hook shape,
capacity, missing cursor target or out-of-profile clock is rejected. Validation
and allocation failures leave all scheduler values and freshness unchanged.
The diagnostic copy is staged before publication; scalar publication and string
swap do not allocate. Capture and restore from inside `tick` throw, including
from its callbacks. The host must also avoid invoking them concurrently or from
an externally executing VM callback: all related state must be at a safe point.

`current_entity_` and the same-tick notification list are scratch used only
inside one tick. They do not cross the safe-point boundary, so are rebuilt at
the next tick instead of captured. Faults are retained as terminal faults:
restoring one never retries a partially applied tick or rolls back external
effects. Restore the corresponding external state as well if retaining a fault
for diagnosis. Invalid tick attempts can retain a diagnostic without faulting.

This is not end-to-end session persistence. It does not
establish original-game scheduling fidelity, expansion compatibility, graphical
playability or execution on handheld hardware.

## Evidence

`entity_vm_scheduler_snapshot` and `entity_vm_scheduler_snapshot_codec` use
synthetic programs and World entities. The scheduler test sends every valid
continuation through encode/decode before restoring and compares uninterrupted
execution with resumed execution. The codec test checks every truncated prefix,
invalid versions/statuses/flags, trailing bytes, size limits and malformed
rosters. The scheduler test additionally compares uninterrupted execution with
fresh reconstructed World/VM/controllers
and a restored scheduler, including scarce-budget rotations, instruction and
hook ordering, VM step/PC/status values and World writes. A waiting-thread case
continues via the rebuilt interrupt handlers. Rejections cover malformed roster,
generation/step/status, callback presence, capacity, fairness cursor and clock,
plus used destinations. Other checks cover empty scheduler continuation,
terminal fault preservation, staged-allocation rollback and in-tick guards.

Reproduce focused checks:

```sh
cmake -S . -B /tmp/odh-scheduler-snapshot-debug \
  -DCMAKE_BUILD_TYPE=Debug -DODH_X11=OFF -DBUILD_TESTING=ON
cmake --build /tmp/odh-scheduler-snapshot-debug --target \
  odh-entity-scheduler-snapshot-tests odh-entity-scheduler-snapshot-codec-tests \
  odh-entity-scheduler-tests -j4
ctest --test-dir /tmp/odh-scheduler-snapshot-debug \
  -R '^entity_vm_scheduler(_snapshot|_snapshot_codec)?$' --output-on-failure
```

Repeat in a separate build directory with `-DODH_SANITIZERS=ON` for host
AddressSanitizer and UndefinedBehaviorSanitizer checks. These tests use no
commercial data and write no session files.

Verified in fresh Debug and ASan/UBSan Debug build directories on 2026-10-22:
`entity_vm_scheduler` and `entity_vm_scheduler_snapshot` passed (2/2 in each).
The snapshot executable passed 534 assertions. Leak detection was enabled;
the sanitizer run required execution outside the sandbox because LeakSanitizer
cannot operate under its ptrace. These are host-only synthetic checks.
