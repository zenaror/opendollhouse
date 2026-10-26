# Native saves during object interactions

Implementation in progress after `66007bf992290de948dafe3a6d533ee84657fd1f`.
This work extends the existing C++ session path. It does not use the managed
candidate or establish full Simitone/game parity.

## Execution graph and compatibility

An active interaction needs the resident VM, queue, pending services, animation,
object state and exact resource programs to survive together. The required,
versioned `GEXP` section describes this native execution graph while retaining
the existing value codecs. Old saves remain readable through their old profile.
Readers that do not understand the new required section must reject it.

Resource owners are independent of the current callee: two instances can use
the same program. Each manifest records the resource/definition, bank loading
kind, linked semiglobal and program fingerprint. Host-global owner IDs must
describe the same program wherever they occur and cannot conflict with implicit
resident or legacy owner identities. Process-allocated IDs and resource-GUID
IDs have explicit identities; the allocator cursor and exhausted state survive
the save. A high numeric GUID alone does not make an owner process-allocated.
Object threads also record their primary owner explicitly; loading must not
guess one when several instances share a resource GUID.

Generic resource keys are relative to the selected game root. They are matched
against the trusted definition catalog before runtime absolute paths are rebuilt.
Legacy profiles keep their previous path interpretation. Out-of-root resources
can still execute, but need a portable catalog mapping before this new save
profile can store them. Path normalization is a save/load concern, not a new
requirement for standalone object execution.

The graph includes residents and object VM/Sleep threads, resource origins for
created objects, removals from the original lot and completed deferred-removal
intents. Entity IDs always retain their generations. Removing an original
object and creating another with the same ID must not revive its old identity.
World values, slots and containment continue through the existing World codec.

Each resident and object thread records every resource bank installed in its VM,
including a bank that is idle at the save tick. This matters when
CreateObjectInstance gives the caller the new object's private code so a later
PushInteraction can run from the resident VM. On restore, both VMs rebuild that
same bank. The manifest still checks exact trusted resource identity and bank
fingerprints; a save cannot substitute an arbitrary path.

The loader prepares an unpublished candidate, resolves exact resources and
rebuilds program/callback bindings before restoring execution. Reconstructing a
created object's metadata must not rerun Construct, Init or Main. A validated
candidate World can replace the startup baseline before VM bindings exist;
this does not weaken live World publication rules. Failed candidate graphs
are discarded before gameplay resumes. Tick and RNG are restored only after
the complete candidate execution graph is valid.

## Remaining capability boundaries

The new profile targets household execution first. The single-resident legacy
loader rejects it explicitly until that loader receives the same graph support.
Pending Create or non-deferred Remove service requests still require explicit
support before they can be checkpointed; they must never disappear silently.
The current resident queue still implements inherited FIFO behavior. Full
priorities, nested queue execution and cancellation are a subsequent migration
described in [native-queue-migration.md](native-queue-migration.md).
Portal and other unsupported external services remain explicit limitations.
Do not describe the profile as saving at every possible game instruction.

## Runtime verification

The current working tree builds the native `odh-person` target successfully.
Nine focused CTests pass, including bundle validation, seated continuation,
household restore across processes and legacy single-resident restore. The
captured result is `reports/native-active-interaction/ctest-final-current.log`.

The real House07 test now passes from two active save points in
`reports/native-active-interaction/attempt-7/result.json`. Resident 105 starts
the fridge interaction with resident 47 selected; the control run completes at
tick 4096. At tick 256 the fridge action is active. At tick 512 that action has
returned true and the eating continuation is active. For each point, a fresh
process reopens the save at the same tick without advancing, then another fresh
process continues to tick 4096. Every saved section after reopen matches the
section before reopen, and every final section matches the uninterrupted
control. Actor, global values, RNG seed and final tick also match. The legacy
completed-action save still loads. All 25 protected game files kept their
original hashes; all test processes and temporary profiles were removed.

The first resumed continuation exposed a missing bank binding: the restored
food VM had its own code, but the resident VM did not have that code available
for PushInteraction. Tracking installed banks in each VM fixed the continuation.
The final save comparison then exposed an idle fridge bank that had not been
recorded by the uninterrupted path; tracking resource-backed interaction banks
made both final saves identical. Earlier failed attempts remain preserved as
diagnostic evidence and are not counted as passing runs.

After the attempt-6 run, lifecycle review found and fixed VM tracking cleanup on
runtime-object removal, failed creation rollback, and duplicate restore IDs.
The final C++ target was rebuilt after these fixes. Attempt 7 reran the full
process-split comparison: all 12 archive sections matched at reopen and at the
final tick, the legacy save loaded, all protected inputs stayed unchanged, and
temporary profiles and processes were removed. The independent source review
approved the installed-bank tracking and its cleanup paths. This review and
runtime gate cover this bounded save/restore path only.

Baseline observations with the published native executable are in
`reports/native-active-interaction/baseline-cuts/`. In House07, language 14,
resident 105 uses fridge 112 while resident 137 continues normal simulation:

- At tick 256, the action and get-food animation are active. Created object 180
  is attached to the fridge.
- At tick 512, the queued eating continuation and animation are active. That
  object is attached to the resident.

Both baseline runs ended without a host failure. Their 25 protected input
hashes remained unchanged and temporary profiles/processes were removed.
These observations select meaningful checkpoints; they do not prove saving or
restoring either state.

The first implementation run in `attempt-1/` exposed a regression before the
save: recording a complete resource bank reused a verifier intended for closed
seated-action call trees. Unused unresolved calls caused food creation to fail,
so the intended action was no longer active at either checkpoint. The processes
exited zero and their saved sections matched, but these were **not passing
active-action tests**. Raw results remain unchanged and `review-status.json`
records the failed precondition.

The correction separates `session_loaded_bank_fingerprint`, which identifies
the actual loaded bank, from the unchanged strict legacy seated verifier. It
does not add code to the bank or require unused calls to resolve. Test coverage
includes the old golden digest, native legacy BHAV forms and banks larger than
the seated profile. The runtime gate additionally requires a successful control
interaction, a live queue and a created object at each intermediate checkpoint.

This headless validation proves continuation of the tested House07 fridge and
eating interaction only. It does not prove visual behavior, complete save
coverage, full Simitone parity, all expansions, or console support.
