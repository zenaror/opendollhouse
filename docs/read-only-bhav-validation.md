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

The EP5 adapter also has synthetic tests. It preserves Simitone's frame shape:
the object whose EP5 is checked is both Caller and Callee, the tested obstacle
is StackObject, and arguments are `[stack object ID, ghost flag, 0, 0]` (as in
`VMEntity.ExecuteGenericEntryPoint` and `VMRoutingFrame.AttemptWalk`). If the
caller cannot provide the ghost flag, the adapter evaluates both 0 and 1 and
accepts only matching true/false results. The Linux diagnostic now assembles a
private call closure from the selected object asset, follows only its exact
GLOB-linked semiglobal in the selected Global FAR, and uses the selected Global
resource as the shared bank. It limits each evaluation to 256 instructions,
the observed program cache to 256 entries, the semiglobal cache to 64 entries,
and total resolver evaluations to 1024. Unsupported resources, writes,
services, faults, and budget exhaustion remain unknown; this does not commit a
position or implement EP9/placement side effects.

Validation: GNU C++ 13.3.0 Debug headless build in
`/tmp/odh-readonly-bhav-build`; `read_only_bhav`, `entry_point5_readonly`,
`vm_code_owners`, `world_snapshot`, `saved_attempt_walk_obstacles`,
`world_attempt_walk`, and avatar-placement audit tests. This establishes the
synthetic API and adapter boundary only. It does not establish compatibility
with arbitrary TS1 BHAVs, original executable behavior, commercial game data
or console hardware.

The Linux diagnostic uses the adapter, but this is still not real arrival
execution or a complete placement decision.

With commercial files kept external and read-only, a House07 spot check used
candidate positions `(280,376,1,3)` and `(456,504,1,3)`. The first ran
`flamingo.iff` EP5 action 4108 against avatar 105, returned false in two total
instructions (the same one-instruction result for both ghost possibilities),
and kept object 155 as the blocker. The second kept object 35 as the blocker
without opening a BHAV EP5 run. Both probes
reported `host_failed=false`; their one-tick Main remained `waiting`, so this
is diagnostic evidence rather than a completed game session. SHA-256 hashes of
House07, Neighborhood, Global.far, Objects.far and both character files were
unchanged before and after the probes.
