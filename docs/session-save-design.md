# Durable session save design

This is a design checkpoint for saving a running OpenDollhouse session across a process restart. The Linux host now has an experimental single-resident save/load path; the playable restart gate below remains incomplete.

## Storage boundary

- Read the commercial game installation as source data. Never write a save back into the installation or the `referencias` copies.
- Store OpenDollhouse saves in the platform's user data directory. On Linux, use the OpenDollhouse directory under `XDG_DATA_HOME` (or the standard home data directory when unset). Keep the location behind a small platform service so later ports can choose their normal save directory.
- A save belongs to a selected family and active lot. It must also record which game data, language, and enabled expansion resources were used. Refuse to load it against a different resource set until an explicit compatibility rule exists.

## What must survive

The current runtime is assembled from several owners. Saving only the `World` would lose the actions that are in progress. The first complete session record must cover:

- The selected family, neighborhood, active lot, active resident, and logical game tick.
- `WorldSnapshot`: entity banks, positions, containment, and runtime values.
- `RelationshipSnapshot`: neighbor and object relationship matrices, including empty sources and retained tails.
- The main and resident `VmSnapshot` values, restored against the same code owners and equivalent host bindings.
- `EntityVmSchedulerSnapshot`: which residents are active, their run generations and statuses, budget cursor, and last logical tick.
- `EntityThreadControllersSnapshot`: sleep timers, queued and active interactions, waits, and interrupts.
- Runtime random state and any other state that can change the next simulation result. Camera position and zoom can be restored as presentation state; one-shot sound playback can restart silently.

The value-only snapshots already in the code are building blocks, not a file format. In particular, callbacks, pointers, loaded resources, render objects, audio handles, and input queues must be rebuilt by the host rather than copied into the file.

## File and safety rules

Use a versioned OpenDollhouse container with a small header and bounded typed sections. Encode integer values explicitly in little-endian form; never dump C++ structs or rely on compiler layout. Each section carries its size and integrity check. Reject unknown required sections, duplicate sections, truncation, oversized counts, invalid cross-references, and unsupported versions before changing the live session.

Capture only at a safe point between simulation ticks, after VM/controller callbacks have returned. Write to a temporary file in the destination directory, flush it, then atomically rename it over the previous save. A failed write must leave the previous save usable.

Load into a separate candidate session: verify the file and game-resource identity; rebuild immutable programs, resource catalogs, world, relationships, VMs, host bindings, controllers, scheduler, and random state in dependency order; validate all links; then replace the live session in one step. If any stage fails, keep the current session and report a useful log entry.

## First verification gate

Use the real House07 data and an integration test that starts a session, performs Sit, reaches a safe tick boundary, saves, exits the process, starts a fresh process, loads the file, and checks that the resident is still seated with the same lot, position, relationships, and pending runtime state. Then execute Stand and verify that the original action can continue. Include cases for a truncated file, changed game-resource identity, and an interrupted write; each must fail without damaging the prior save.

Passing this gate proves only restart persistence for that tested session and build. It does not prove that every lot, family, expansion, or running BHAV can be saved. Those remain separate coverage gates in [the playable feature matrix](playable-feature-matrix.md).

## Current implementation boundary

The version-1 archive codec now encodes and decodes byte buffers. Its 28-byte header contains the eight-byte signature `ODHSES\r\n`, version, zeroed header flags, section count, payload length, and CRC-32. Each section has a four-byte uppercase/digit ID, a required bit, a 64-bit payload length, a CRC-32, and its payload. Defaults cap a file at 128 MiB, a section at 64 MiB, and the archive at 128 sections. The codec rejects duplicate IDs, unknown flag bits, unsupported versions, truncation, unclaimed bytes, checksum errors, and limit violations.

The Linux file store now writes an encoded archive to a private same-directory temporary file, flushes the contents, and atomically renames it into place. It reads only regular non-symlink files and checks the file-size cap before allocation. The parent directory is not fsynced yet, so power-loss durability of the rename still needs a follow-up. The live game now calls this store for the experimental S-key save; the launch-time `--load-session PATH` restores supported single-resident sessions.

A version-1 value codec now serializes `WorldSnapshot` with explicit little-endian fields, bounded banks/counts, canonical entity order, and validation through `World::restore_snapshot`. It preserves entity data, saved banks, definitions, entry points, containment, positions, flags, counters, and radian direction. The synthetic fixture round-trips through both the codec and a fresh `World`. The live capture places this value in the required `WRLD` section; the reusable candidate-restore helper now rebuilds a fresh World, and the live host transfers that candidate into its lot container before binding callbacks or renderers.

The version-1 `RelationshipSnapshot` codec now stores its source/pair/value budgets and counts, matrix kind, signed source ID, object generation, signed 32-bit target, and each signed value. It uses canonical source and pair ordering and validates the decoded data by restoring it into a fresh `RelationshipState`. Empty sources/pairs, old object generations, negative or wide neighbor targets, and value tails are retained. Its synthetic test includes a 300-value tail and verifies a query after restore. The live capture includes it in the required `RELA` section; the candidate-restore helper now restores these values into a fresh relationship state, before VM callbacks are bound.

The version-1 `VmSnapshot` codec now stores frames, arguments, locals, contexts, temporary registers, waits, suspended waits, return values, counters, and diagnostics with explicit little-endian fields. It bounds bytes, stack depth, frame variables, and diagnostic length; rejects invalid enum values, routine keys, booleans, truncation, and trailing bytes. The broad VM snapshot test now encodes and decodes each scenario before restoring it, then compares the resumed execution trace with uninterrupted execution. A separate codec test checks malformed inputs and limits. Code owners and equivalent memory/primitive callbacks still have to be rebuilt by the host; they are not in the bytes.

A version-1 `EntityVmSchedulerSnapshot` codec stores the roster's entity IDs, VM generation/step/status stamps, hook-presence flags, entity limit, tick, instruction count, fairness cursor, and terminal diagnostic. The existing fairness/continuation test now round-trips valid scheduler states through this codec before restoration. Callables and VM pointers remain host bindings and are not serialized.

The version-1 `EntityThreadControllersSnapshot` codec now stores Sleep state,
interaction queues and calls, waits, completions, and interrupt latches using
bounded explicit little-endian fields. The continuation test exercises
encode/decode before restore, and a dedicated codec test checks truncation,
limits, malformed values, and noncanonical booleans. These bytes still depend
on the host rebuilding the matching World, VMs, code resources, and callbacks.

The value-only `SessionBundle` now assembles required `META`, `HOST`, `WRLD`,
`RELA`, `VM  `, `SCHD`, `CTRL`, and `PLAY` sections. It stores launch identity, selected
family/lot/resident, tick and RNG state, bounded mutable globals, World and
relationship snapshots, and keyed VM/controller/Playback rosters. Playback
stores animation resource keys and cursor, pending opcode-44 wait, carry key,
hand gestures, and last playback tick as bounded values; it contains no loaded
animation resources or resource fingerprint of its own. The session-wide
`META` fingerprint remains the authority for game-data identity. Decoding checks the
resource identity when the caller supplies the expected value, validates
cross-links between world actors, generations, VM states, waits, queued actions,
and Playback's animation wait, and rejects malformed or unsupported states. Real Sleep and
IdleForInput waits round-trip through the bundle tests. The Linux live host now
has an experimental S-key capture path between ticks. It records the current
bundle values, hashes the selected resource manifest only on request, and stores
the archive atomically in the user data directory. Saves are rejected while
a route is active, Go Here is queued, any dynamic object/portal code owner exists,
or a seated action requires an unserialized binding. The experimental loader supports only the same restricted single-resident profile.
A public save selector and visual X11 validation are still absent; do not treat
this limited persistence support as a playable build.

The Linux runtime receives the canonical game root and resolved resource paths
without hashing them on a fresh launch; loading an ODH archive first hashes and
compares the selected manifest, language and expansion mask. When the experimental save key is pressed, it
computes the version-1 `META` identity from that manifest and checks/encodes the
complete bundle before writing. The current plan still needs auditing for
resources opened dynamically on other lots and expansion sidecars. Synthetic
coverage and one real installation plan are recorded in
[session-resource-manifest-validation.md](session-resource-manifest-validation.md).

The reusable `SingleResidentSessionRestore` helper now decodes a bounded bundle
against an explicitly supplied resource identity and selected lot/family/resident.
It owns an unpublished candidate World, relationship state, lot globals and
bootstrap globals. The caller must rebuild equivalent code, memory/primitive
callbacks, controllers, Playback resolver and scheduler hooks against this
candidate, then restore execution before starting Main. A failure invalidates
that candidate: discard it and its execution objects; the live session must not
be replaced until every restore step succeeds. Tick and RNG are published only
after VM, controllers, Playback and scheduler accept their state.

This helper currently accepts one resident thread and the unchanged original lot
entity roster and generations, bank layouts, definitions, entry points and saved
baseline metadata. Resource fingerprint, language, expansions, lot/family/resident
selection and the exact supported global roster must match. Identity globals 9
(family) and 10 (house) must match the selected household and original lot;
other supported mutable globals are restored without dropping zero values.
Routes, UI destination ghosts, external portal frames, dynamic code owners,
non-standing posture and seated-action bindings remain unsupported. The original
live-save guards remain; before filesystem mutation, the host also applies the
loader profile against its retained original-lot baseline. Unsupported posture,
attachment, roster, metadata, globals or runtime state therefore cannot replace
a previously usable save. The integrated Linux launcher now accepts
`--load-session PATH`; archive selection supplies mode, house, family, resident
and language, while conflicting explicit options, changed resources and
unsupported runtime state fail before Main is started. It rebuilds the original
lot metadata, transfers the validated candidate World into the host lot container,
binds fresh VM/controllers/Playback/scheduler callbacks, restores execution and
continues at the saved logical tick plus one. Renderer and routing references
use that same World. A failed launch does not rewrite the archive or user settings.

Synthetic tests restore a real Sleep wait into a fresh VM/controller/scheduler
graph without starting Main, continue six ticks, and compare the recaptured
World, nonempty relationships, globals (including a mutable value of 321), VM,
controllers, Playback, scheduler, RNG and tick with uninterrupted execution.
Two CTest processes separately write and resume the synthetic archive. Rejection
cases cover changed resources/selection/metadata/layout, missing or extra globals,
altered family/house identity, routing footprint, seated posture, dynamic owners,
truncation, archive limits, missing VM routines/hooks and missing Playback resource
resolution. This is synthetic restart evidence only; it does not establish
House07, Sit-to-Stand, expansion or live-window save/load behavior.

The same integrated path supports `--headless-ticks N` for a bounded run without
opening a display. This is a count of additional ticks when loading. With
`--save-session-on-exit`, a successful safe boundary uses the same atomic save
operation and user-data directory as the S key; the JSON result includes
`saved_session_path`. Finishing the requested tick count with a waiting or yielded
VM is normal for this explicit bounded mode; VM faults, instruction exhaustion
and incomplete requested actions still fail. `--plan-only` cannot be combined
with `--load-session`, since a resource plan cannot prove restoration.

The synthetic `linux_session_resume_cli` test runs the real executable in three
processes: save after one tick; load and run one more tick; run two ticks without
a restart. Its Main increments one value before Sleep and another after Sleep.
The resumed value is not incremented a second time and the recaptured archive
is byte-identical to uninterrupted execution. This exercises the integrated
launcher, original-lot reconstruction, resource fingerprint, callback graph and
production atomic file path without commercial files or a display.

Remaining for the House07 restart gate: serialize/rebind dynamic code owners
and the seated action needed to continue Stand; support routing persistence or
retain its explicit save refusal. Then run Sit → save → new process → load
seated → Stand against real House07 data, including the actual window and
animation resources. The synthetic CLI result does not establish this gate,
expansion coverage, multi-resident simulation or full game compatibility.
