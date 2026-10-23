# VM runtime snapshots

`Vm::snapshot()` captures owned execution values: frames, locals, arguments,
temps, status, step count, pending and suspended waits, interaction/external
returns, ticket sequence, run generation, and diagnostic text.

`Vm::restore_snapshot()` accepts only a fresh VM, checks limits and internal
references, then installs the values atomically. The host must construct that
VM with the same immutable routines and owners and equivalent memory and
primitive bindings. These bindings are not stored or compared by the snapshot.
The separate `VmSnapshot` value codec now provides a version-1, bounded
little-endian byte representation for the owned values. It still does not
include programs, code owners, or host callbacks, so loading requires the same
resources and equivalent bindings to be rebuilt first. Neither the snapshot nor
its codec is a complete game save or save/continue feature.

## Checks

The synthetic test compares uninterrupted execution with execution resumed
from a fresh VM across ordinary waits, retries, nested external waits,
interaction returns, faults, and dormant activation. Each case passes through
the byte codec before restoration. It also checks invalid state rejection and
allocation-failure atomicity, including a snapshot with a pending wait and a
suspended parent wait. The dedicated codec test checks truncation at every byte
boundary, unsupported versions, invalid flags, trailing data, and configured
size limits.

```sh
cmake --build BUILD_DIR --target odh-vm-snapshot-tests odh-vm-snapshot-codec-tests
ctest --test-dir BUILD_DIR -R '^vm_snapshot(_codec)?$' --output-on-failure
```

Passing these tests establishes deterministic in-process VM restoration through
the codec for the synthetic fixtures only. It does not prove persistence across
process restart, validity of every saved game thread, full game-session
restoration, or gameplay.
