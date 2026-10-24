# Durable session save design

This is a design checkpoint for saving a running OpenDollhouse session across a process restart. The Linux host now has experimental save/load paths for a single resident and for the supported whole-household profile. House07 confirms short headless continuation for two present residents and one live X11 save/resume; player-driven interactions and long-session gates remain incomplete.

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

## Initial single-resident verification gate

The real-data gates now cover two single-resident seat paths: House05 neighbor
34 on bench 173 and House07 neighbor 48 on chair 128. Each performs Sit, saves
at a safe tick boundary, exits, loads in a fresh process, verifies the seated
state, then executes Stand and saves again. Their specific evidence and
resource fingerprints are recorded in [the playable feature matrix](playable-feature-matrix.md)
and the current handoff; the whole-household extension is documented below.

Passing this gate proves only restart persistence for that tested session and build. It does not prove that every lot, family, expansion, or running BHAV can be saved. Those remain separate coverage gates in [the playable feature matrix](playable-feature-matrix.md).

## Current implementation boundary

The version-1 archive codec now encodes and decodes byte buffers. Its 28-byte header contains the eight-byte signature `ODHSES\r\n`, version, zeroed header flags, section count, payload length, and CRC-32. Each section has a four-byte uppercase/digit ID, a required bit, a 64-bit payload length, a CRC-32, and its payload. Defaults cap a file at 128 MiB, a section at 64 MiB, and the archive at 128 sections. The codec rejects duplicate IDs, unknown flag bits, unsupported versions, truncation, unclaimed bytes, checksum errors, and limit violations.

The Linux file store now writes an encoded archive to a private same-directory temporary file, flushes the contents, and atomically renames it into place. It reads only regular non-symlink files and checks the file-size cap before allocation. The parent directory is not fsynced yet, so power-loss durability of the rename still needs a follow-up. The live game now calls this store for the experimental S-key save; `--load-session PATH` restores supported single-resident or whole-household sessions according to their profile.

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
or a seated action requires an unserialized binding. The loader applies the corresponding supported profile before publishing a candidate session.

The seated continuation profile now supports a completed Sit whose EntryPoint26
and EntryPoint27 actions are in the object's exact GLOB-linked semiglobal bank.
SEAT v2 records the flat GLOB name, canonical Global FAR path relative to the
selected game root, exact FAR entry and bounded fingerprints of the private
and linked semiglobal owner routines. Restore resolves the object's GLOB again,
requires the same provenance and compares both action banks before installing
the owner. Multiple GLOB chunks are rejected specifically for this seated
save/restore path. SEAT v1 private-action saves remain readable. This profile still
represents only the stable completed Sit → Stand boundary; it does not save an
in-flight interaction or route.

The opt-in real-data gate `tests/house05_seated_session_resume_cli.py` runs the
integrated Linux host in two processes against House05, resident neighbor 34
and saved bench 173. Process one completed Sit 8213 and saved at tick 512.
Process two loaded that archive at the same logical tick, completed Stand
8216, and left the resident standing and detached. The test hashes House05,
Neighborhood, User00009, Animation.far, Global.far and Objects.far before and
after; all were unchanged. Session files and XDG state are isolated in a
temporary directory. This proves only the tested headless House05 seat path.

The opt-in real-data gate `tests/house07_session_resume_cli.py` repeats this
boundary on House07, neighbor 48 and chair 128. Sit and Stand complete in
separate processes across ticks 512 to 1024. The gate fingerprints House07,
Neighborhood, User00000, Animation.far, Global.far and Objects.far; their hashes
remain unchanged. Its save and XDG state are temporary. This extends the
evidence to a second real lot and chair, not to visual interaction or expansion
parity.

Seats beyond these two cases, a visual Stand, natural click dispatch, full
House05 household save/resume, expansion coverage and full Simitone parity
remain unverified; this is not yet a playable build.

### Whole-household continuation

The integrated House07 gate now saves the two residents present in the lot,
starts a fresh process, restores both resident VMs/controllers and continues
the household. For each of the two resident selections, the resumed run's
saved archive is byte-for-byte equal to the archive from an uninterrupted run
with the same total tick count. The focused test set passed 11/11 in Debug and
11/11 in Release. Commercial files covered by the before/after fingerprints
retained their hashes. This is bounded headless continuation evidence for the
tested House07 state and build, not a complete gameplay result.

The household loader pins the character IFF resources used by the resident
roster under a cumulative 64 MiB byte budget. If the total exceeds that budget
or a file changes while being read, the launch/save path fails before writing
the session archive. This limit covers the bounded resident-resource snapshot;
it is not a measurement of total process memory.

The supported profile requires the household roster and resident resources to
match the saved World. Additional-resident posture/accessory state, an active
route, live dynamic object-code references or portal code owners, and residents
absent from the saved World remain unsupported. Installed action banks without
references in the captured VM/controller/Playback state no longer block saving.
The complete household restore validator still runs before writing, including
checks on Playback carry. The real House07 gate verifies additional-resident
Sit/Stand, save at tick512 and fresh-process continuation to tick1024 against
uninterrupted execution, for both resident selections. UI/X11 interaction and a long-running
household session still need validation.

The Linux runtime receives the canonical game root and resolved resource paths
without hashing them on a fresh launch; loading an ODH archive first hashes and
compares the selected manifest, language and expansion mask. When the experimental save key is pressed, it
computes the version-1 `META` identity from that manifest and checks/encodes the
complete bundle before writing. The current plan still needs auditing for
resources opened dynamically on other lots and expansion sidecars. Synthetic
coverage and one real installation plan are recorded in
[session-resource-manifest-validation.md](session-resource-manifest-validation.md).

The `SingleResidentSessionRestore` helper remains the bounded single-resident
candidate-restore path. It decodes a bundle against an explicitly supplied
resource identity and selected lot/family/resident. It owns an unpublished
candidate World, relationship state, lot globals and bootstrap globals. The
caller rebuilds equivalent code, memory/primitive callbacks, controllers,
Playback resolver and scheduler hooks against this candidate, then restores
execution before starting Main. A failure invalidates that candidate; the live
session is replaced only after every restore step succeeds. Tick and RNG are
published only after VM, controllers, Playback and scheduler accept their state.

That helper accepts one resident thread and the unchanged original lot entity
roster and generations, bank layouts, definitions, entry points and saved
baseline metadata. Resource fingerprint, language, expansions, lot/family/resident
selection and the exact supported global roster must match. Identity globals 9
(family) and 10 (house) must match the selected household and original lot;
other supported mutable globals are restored without dropping zero values.
Routes, UI destination ghosts, external portal frames, dynamic code owners,
non-standing posture and seated-action bindings remain unsupported. The original
live-save guards remain; before filesystem mutation, the host also applies the
loader profile against its retained original-lot baseline. Unsupported posture,
attachment, roster, metadata, globals or runtime state therefore cannot replace
a previously usable save. The integrated Linux launcher also has a
whole-household restore path for the profile described above.
`--load-session PATH` selects mode, house, family, resident and language from
the archive; conflicting explicit options, changed resources and unsupported
runtime state fail before Main is started. It rebuilds the original
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
resolution. This synthetic restart evidence alone does not establish House07,
Sit-to-Stand, expansion or live-window save/load behavior; see the separate
real-data gate below.

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

## Real-data X11 checkpoint (2026-10-23)

Using the integrated Linux launcher, the original game data in mode 1 and
House07 loaded as family 5 with residents 48 and 47. The 800x600 X11 lot window shows a localized loading card before resource
preparation. In the original smoke it appeared within three seconds; the house,
furniture, both residents and HUD appeared after about 75 seconds. Pressing S
wrote a 116895-byte experimental save at tick 81 to a temporary save folder,
outside the game installation. Escape closed the first process with exit code 0.

A new process loaded that `.odhs` through
`tools/play_linux.py --game-root GAME --load-session PATH`, without `--mode` or
`--language`, using the default `build/linux-checkpoint` executable. The JSON
result reported `session_loaded=true` and `resumed_tick=81`. The restored X11
window showed the house and both residents; simulation continued through tick
138 before Escape closed the process with exit code 0. Captures of the loading
card and the running lot were `/tmp/odh-loading-card.png` and
`/tmp/odh-loading-later.png` during validation. The archive
fingerprint matched the same 132 game resources, language and expansion mask
before restore. Captures: `/tmp/odh-loading-card.png`,
`/tmp/odh-loading-later.png`, `/tmp/odh-final-loading.png` and
`/tmp/odh-final-lot.png`.

### Startup fingerprint speed checkpoint (2026-10-23)

A bounded `--plan-only` launch took 46.134 seconds while fingerprinting 132
resources totaling about 1.0 GiB. The native build now uses OpenSSL SHA-256 when
CMake finds it; cross builds, builds without OpenSSL and the explicit
`ODH_USE_OPENSSL_SHA256=OFF` option keep the built-in implementation. A repeated
plan took 0.719 seconds and produced the identical fingerprint
`b4842d0534f9962992e4f1d977913c5b689eb7a74c0e5bda3761c66485bdc511`. The X11
resume then showed the lot about 10 seconds after the loading card, resumed from
tick 81, and closed cleanly at tick 90. The “Loading lot...” text is now centered
inside its card.

The full native build and CTest suite passed 222/222 with OpenSSL enabled; the
6 focused hash/UI/launcher tests also passed. The built-in hash-only build passed
its 2/2 CTests. This proves faster verification on this installation with the
same resource identity;
it does not prove long-session stability, every action or lot, all expansions,
full game compatibility or original-game visual parity.

Remaining: validate live Sit/Stand and other player actions through X11, a
long-running session, and expansion-specific data. Add profiles for
additional-resident posture/accessory state, in-progress routing, dynamic code
owners and residents absent from the saved World before claiming those cases.
This checkpoint does not establish expansion coverage or full game compatibility.
