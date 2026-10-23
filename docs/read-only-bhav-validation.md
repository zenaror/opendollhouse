# Read-only BHAV evaluation in a disposable World

`evaluate_read_only_bhav` accepts an owned World snapshot, explicit default
`RoutineSet`, `RoutineOwners`, root `RoutineKey`, `VmContext`, arguments and
instruction budget. Private and semiglobal code resolve only within the supplied
owner; global code uses the default set while preserving the active owner.
No entity ID selects a resource owner. The API loads no files or network data.

A fresh scratch `World` restores the snapshot under `WorldLimits`. `VmMemory.read`
reads that scratch; every World write callback returns false. VM-local temps,
arguments, locals and StackObjectID remain writable. The only host primitive is
`object_type_primitive(scratch)` (opcode 32); other primitives, including WAIT
services, fail closed. No caller-supplied callback or wait resumption exists.

The result distinguishes true/false completion from unsupported evaluation, with
reason, executed instruction count and VM diagnostic. Invalid resources/snapshot,
missing root owner, faults and budget exhaustion do not become false. Nested calls
share one instruction budget and existing stack/frame/owner limits. Allocation
exceptions propagate. The budget counts VM instructions, not elapsed time or
resource-copy bytes; resource construction remains the caller's responsibility.

`tests/read_only_bhav.cpp` uses synthetic code and entity state only. It covers
private/global/semiglobal ownership, explicit arguments, exact signed World reads,
a snapshot differing from the live World, opcode 32 true/false, denied World
writes, rejected external/WAIT primitives, local VM writes, missing resources,
zero/loop/nested budgets, malformed root and World/VM capacity limits. Each normal
evaluation compares encoded input snapshot and original World bytes for mutation.

Validation: GNU C++ 13.3.0 Debug headless build in
`/tmp/odh-readonly-bhav-build`; `read_only_bhav`, `entry_point5_readonly`,
`vm_code_owners` and `world_snapshot` tests. This establishes only the synthetic
API boundary. It does not establish compatibility with arbitrary TS1 BHAVs,
original executable behavior, commercial game data or console hardware.

This milestone is standalone: `person.cpp`, real arrival routing and the existing
`entry_point5_readonly` API are unchanged.
