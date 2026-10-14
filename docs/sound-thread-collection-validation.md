# World sound thread collection

Checkpoint 2026-10-14. Portable `SoundThreadCollection` connects nonblocking
SoundRequest submissions, generation-guarded World sources and persistent
HIT threads. The collection is a sound tick service; it does not advance the
World clock or replace the game's overall scheduler.

## Lifecycle and ownership

A caller supplies an explicit factory receiving the complete request and
SoundSourceBinding. The factory resolves resources/arguments and returns an
owned SoundThreadInstance, containing a HitThread and a SoundThreadLifetime
backend owner. The collection itself never executes HIT instructions during
submit; factories must construct/defer playback rather than tick internally.

Submission rejects loop requests, missing/stale source identity and exceeded
capacity before calling the factory. Factory null is rejection, exceptions
propagate. After construction, generation is checked again: if the factory
removed/replaced the source, the instance is destroyed without publication.
Metadata is passed intact; no volume/pan/rate or ArgStyle conversion is invented.

Each tick checks owner identity before touching the thread, ticks every valid
entry once, then checks identity again for removals inside backend callbacks.
An invalid owner removes the entry and stops its audio immediately, even if
the thread is waiting without further source-field reads. Reused IDs are never
adopted. Completion, invalid owner and fault have distinct report counters.
A backend/interpreter exception removes the affected instance and counts a
fault; it is not counted as completion. Other entries continue.

All entry removals and collection destruction invoke backend stop and release
the thread and owner. stop must cancel all notes owned by that instance, be
idempotent/nonthrowing and not reenter the collection. Thread callbacks are
destroyed before their lifetime owner. A factory's owner must also use RAII
for exceptions before an instance has been constructed.

Single-owner operations reject callback reentrancy before mutating the
collection. World, HIT/HOT and borrowed factory inputs must outlive it.
No visibility/camera filtering is used. Entries keep only source binding and
instance ownership, not a copied commercial resource or SoundRequest name.

## Bounds and current profile

Defaults:64 active threads,256 source fields; configured limits must be
1..4096 threads and1..32769 fields. Metadata capacity is reserved before audio
acquisition. Per-thread instruction/trace/note bounds remain HitThread's.
The collection does not enforce aggregate PCM/cache memory or mix samples:
factory/backends must supply their own budgets. These bounds are not evidence
of New3DS feasibility or measured RSS.

Current profile is one source per instance, nonlooping, no implicit event
deduplication or shared owner lists. Resolution of native events, caller-derived
arguments, global sequence-group effects, controllers, spatial volume/pan,
mixed output, ownerless UI audio and connection to the resident Main remain
pending. A factory is an explicit extension point, not a claim these are
implemented. Existing PlaySound retains its immediate true branch.

## Reference and validation

Simitone4906db3 with FreeSO submodulef86bbd3 TS1 paths:
HITSound.cs AddOwner88–92/RemoveOwner94–97; HITThread.cs103–108 stops vocals
when an owned thread loses all owners; VMEntity.cs deletion removes sound
owners, and TickSounds removes dead entries. ODH generation identity and the
single-source collection are integration policy, not original-runtime proof.

33 synthetic collection checks cover capacity-before-factory, deferred starts,
two threads, removal/IDreuse during WaitSamp without another backend query,
completion's separate wait/End ticks, failures and destruction cleanup, loop
rejection, factory owner race/rejection/exception, RAII, reentrancy and owner
removal inside a backend callback.

A synthetic BHAV PlaySound sink submits to this collection: VM runs its next
instruction while the sound remains active, without a hidden sound tick or
pending BHAV service. This preserves the primitive's nonblocking behavior and
does not resume the original resident Main.

The SDL dummy integration additionally enqueues56 decoded synthetic XA frames
paused; the collection ticks to WaitSamp, the owner is removed, and cancellation
verifies56 queued frames before stop, zero after clear, one stop and zero
completion/fault counters. Dummy permits one open output in this environment;
the earlier completed probe is closed before this cancellation fixture opens.

Final Debug and ASan/UBSan suites:102 configured,101 pass,one X11 skipped due
to environment. No warnings. LSan disabled via ASAN_OPTIONS=detect_leaks=0.
No physical audio, new native-resource compatibility or console claim is added.

See [sound-source-binding-validation.md](sound-source-binding-validation.md),
[hit-thread-validation.md](hit-thread-validation.md) and
[linux-audio-validation.md](linux-audio-validation.md).

