# HIT persistent note lifecycle

Checkpoint 2026-10-14. Portable `odh::HitThread` extends the existing bounded
selection instruction engine through NoteOn, WaitSamp and stackless End.
The offline `select_hit_note` contract remains unchanged; all 888 selection
checks pass. This is a Simitone submodule reference profile, with an explicit
optional Literal32 Jump mode, not original-runtime equivalence.

## Reference identity and semantics

Verified local revisions: Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`, its FreeSO submodule
`f86bbd34b2112296ed786741f640a0535886d180`. Consulted TS1 sound path:
`TSOClient/tso.sound/HITInterpreter.cs` NoteOn41–47, WaitSamp160–171,
End177–196; `HITThread.cs` LastNote45–48, Tick140–169,
NoteOn304–343 and NoteActive387–390. Third-party code is a reference;
its behavior is not evidence of original TS1 execution.

NoteOn writes the successful note's zero-based index to the operand variable,
without changing flags. A backend failed start returns nullopt: destination
becomes -1, and LastNote still refers to the most recent successful note.
Queued but unstarted notes count as active.

WaitSamp queries the last successful note. While active it rewinds PC to its
opcode and yields. When inactive, it advances PC but still yields that tick.
No successful note is inactive. End marks bytecode ended and yields; on a
subsequent tick the thread completes only when the last successful note is
inactive. Earlier notes are not implicitly stopped or included in that final
predicate, matching the consulted reference. Terminal completion is idempotent.

## Interface and bounds

HIT/HOT are borrowed immutable resources and must outlive the thread.
Object-variable array, limits, invocation arguments and callback objects are
copied. Referenced objects inside callbacks still need their own lifetime.
A single owner calls tick; callbacks cannot reenter it or mutate resources.

The backend starts a patch and returns a unique uint64 handle, kept queryable
for this thread's lifetime. Status distinguishes active, complete and failed;
invalid status and exceptions fault the thread. Successful handles map to
owned sequential indices. The backend owns the audio resources and their
cancellation, including cleanup after a fault or thread destruction; HitThread
does not own a Linux device or automatically stop samples.

Default limits: 4,096 cumulative instructions and trace records (including
repeated WaitSamp), 64 successful notes, existing group/choice budgets.
No retained callback or array borrows into temporary constructor arguments.
No copy of the full trace is made on each runtime tick. End polling does not
grow the instruction trace. These are logical bounds, not RSS or console proof.

Opcode69 requires an explicit kill_group callback; missing effects fail.
Looped patches, Call/Return and other unsupported instructions fail explicitly.
Any runtime error latches a fault: later ticks cannot replay effects or report
completion. Side effects already accepted by a backend are not rolled back.
Reentrancy remains a fault even if a callback catches the inner exception.

## Validation

52 portable lifecycle checks cover exact wait/End tick transitions, stable
note indices, failed starts and retained LastNote, active audio after End,
backend failure/invalid status, duplicate handles, cumulative instruction,
trace and note budgets, group effects, truncated operands, unsupported loops,
and swallowed reentrant callback failures.

A separate versioned SDL dummy test creates synthetic XA, decodes it when the
HIT note starts, enqueues it paused, proves WaitSamp still holds, starts output
and drains it before completion. No physical output is used by CTest.
Debug and ASan/UBSan each configure 100 tests: 99 pass, one X11 test skipped
due to the environment. No compiler warnings. LSan disabled
(`ASAN_OPTIONS=detect_leaks=0`), so leak absence is not claimed.

Private verifier an archived validation record (not distributed), SHA256
`9c4f06e718cbd032c04c17478ff2f7161899e8455c8810fe1d48417aeb634dce`,
loads the actual generated HSM/HOT/HIT and event hungry_sated_vox.
Explicit fixture arguments [1,1024,0], source1 fields20=30/0=0, choice
indices0/0 and Literal32 Jump select patch3212. Group1 is explicitly empty
in this isolated verifier before the first note; its effect is validated
against that registry. This is not a populated World's group policy.

The XA copy retains SHA256
`e68b39e613b2500e40796346fb6cef6343ae97dda5eea2ae0debc3f9f6dcbd64`:
mono22,050Hz,54,296frames. NoteOn at PC81 writes note index0; WaitSamp83
holds while the backend queue is active; End84 advances PC85; a later tick
completes the thread. Dummy Debug/ASan receipts observed175 instructions,
156 yielded ticks; PulseAudio observed176/157. These timing counts depend
on host scheduling and are not deterministic simulation or native timing.
All receipts show one successful note, two choices, one group effect and
54,296 delivered frames, PC85 and no Main resumed. Delivery does not prove
audibility or physical DAC completion.

Retained private receipts: an archived validation record (not distributed),
an archived validation record (not distributed), an archived validation record (not distributed).
Commercial resources and analysis artifacts remain external or ignored.

## Pending integration

BHAV sound submission remains nonblocking and separate. This does not
resume the resident Main or saved threads. Next work connects resolved events
to source/owner lifetime and real soundthread scheduling. Caller-derived
arguments, ArgStyle, shared defaults, controllers, volume/pan/loop/interrupts,
live variable refresh, Call/Return and all-note backend ownership still need
their own evidence. No silent fallback or artificial VM acceptance is added.
See [hit-invocation-validation.md](hit-invocation-validation.md),
[xa-play-validation.md](xa-play-validation.md) and
[linux-audio-validation.md](linux-audio-validation.md).

