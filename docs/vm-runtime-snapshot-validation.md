# VM runtime snapshots

`Vm::snapshot()` captures owned execution values: frames, locals, arguments,
temps, status, step count, pending and suspended waits, interaction/external
returns, ticket sequence, run generation, and diagnostic text.

`Vm::restore_snapshot()` accepts only a fresh VM, checks limits and internal
references, then installs the values atomically. The host must construct that
VM with the same immutable routines and owners and equivalent memory and
primitive bindings. These bindings are not stored or compared by the snapshot.
This is an in-memory runtime copy, not a file format and not save/continue.

## Checks

The synthetic test compares uninterrupted execution with execution resumed
from a fresh VM across ordinary waits, retries, nested external waits,
interaction returns, faults, and dormant activation. It also checks invalid
state rejection and allocation-failure atomicity, including a snapshot with a
pending wait and a suspended parent wait.

```sh
cmake --build BUILD_DIR --target odh-vm-snapshot-tests
ctest --test-dir BUILD_DIR -R '^vm_snapshot$' --output-on-failure
```

Passing this test establishes deterministic in-process VM restoration for the
synthetic fixtures only. It does not prove persistence across process restart,
validity of every saved game thread, full game-session restoration, or gameplay.
