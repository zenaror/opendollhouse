# Durable session save design

This is a design checkpoint for saving a running OpenDollhouse session across a process restart. It does not claim that save/load exists yet.

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

This is not yet filesystem persistence: there is no atomic file writer, no serializer for `WorldSnapshot`, `RelationshipSnapshot`, `VmSnapshot`, scheduler or thread-controller snapshots, no resource identity check, and no host reconstruction. Those are the remaining steps before the House07 process-restart gate can run.
