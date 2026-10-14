# Live World sound source binding

Checkpoint 2026-10-14. `SoundSourceBinding` turns the generation-bearing
PlaySound submission snapshot into an explicitly bound live source reader.
It retains World address, signed entity ID, generation, profile and field
budget, not an Entity pointer or PersonData/attribute-bank snapshot.

## Contract

`bind(World, SoundRequest, limit=256)` requires source_variables from the
submission, checks that the current World incarnation and profile match,
and validates a current bounded source bank. Missing receipt, wrong generation,
profile mismatch, incomplete bank or exceeded field limit returns nullopt.

`valid()` checks entity identity only. `refresh()` reacquires the entity,
checks generation, rebuilds the bounded Simitone profile and returns owned
values. `read(source,field)` requires the exact signed source ID and a valid
field, then uses refresh. It never narrows/wraps an int32 source to int16,
aliases another entity, adopts a new generation, truncates fields or falls
back to the old snapshot or zero. A legitimate field value zero remains valid.

Binding copies share only the borrowed stable World, which must outlive them.
Bank writes within the same generation are visible on subsequent reads;
earlier returned snapshots and the original SoundRequest remain unchanged.
Allocation exceptions propagate as in sound_source_variables.

For HIT integration, capture the binding by value in a HitSourceFieldReader
and call `binding.read(source,field)`. The persistent thread already copies
that callback. This changes field reads only; caller arguments still require
their own evidence and explicit initialization.

## Reference and scope

Local Simitone4906db3/FreeSOsubmodulef86bbd3 TS1 references:
`TSOClient/tso.simantics/Entities/VMEntity.cs` TickSounds329–408 refreshes
avatar HIT variables after accepted volume updates; SubmitHITVars420–427
builds the object attribute vector; entity deletion1389–1393 removes sound
owners. Prior source-profile validation identifies avatar PersonData mapping.

The generation guard and live getter are ODH integration mechanisms, not a
claim that the reference or original uses ODH's generation representation.
Live object-bank reads are also explicit ODH behavior; this does not establish
reference per-tick object refresh. It does not implement TickSounds'
volume/pan/zoom calculations, ownership collection or deletion cancellation.

A stale lookup faults a HIT thread when bytecode requests that field.
If it only waits for audio, no field lookup occurs: the scheduler must check
validity and explicitly cancel/remove its backend ownership. This binding
does not stop a sample, turn a missing owner into completion or resume Main.
There is no camera or offscreen filter in the reader.

## Validation

35 synthetic checks cover exact sources (including wide IDs that would wrap
under narrowing), signed field bounds, valid zero, missing/wrong receipt,
profile mismatch, avatar field budget, copied bindings and immutable snapshots,
live changes after WaitSamp, removed/reused IDs, new submissions for the new
generation, incomplete PersonData and bounded object attribute growth.

A persistent HIT fixture plays one note, waits, then reads field20 from the
actual World avatar bank after its value changes: the register receives the
current NeatPersonality/10 rather than the submission value. A second fixture
removes/recreates the owner during the wait; the subsequent field instruction
faults and does not read the replacement avatar.

Another fixture removes/recreates the source inside the resource resolver,
between source snapshot and sink submission. Binding rejects the stale receipt;
PlaySound retains its existing immediate true branch and reports
backend_rejected. No placeholder audio acceptance or Main bypass is added.

Debug suite:101 configured,100 pass,one X11 skip. ASan/UBSan full suite passed
the initial binding coverage with the same counts; the binding test passed
again after adding the resolver/submission race. No warnings. LSan disabled
with ASAN_OPTIONS=detect_leaks=0. No native audio, original behavior or hardware
claim is added by these tests.

See [sound-source-variable-validation.md](sound-source-variable-validation.md),
[hit-thread-validation.md](hit-thread-validation.md) and
[hit-source-callback-validation.md](hit-source-callback-validation.md).

