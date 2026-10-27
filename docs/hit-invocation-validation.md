# Explicit HIT invocation arguments and World source queries

The offline selector now accepts an owned signed int32 argument triplet, seeded
into registers1,2,3. This connects the explicit source-field reader to actual
World source IDs without an alias to the reference constructor's fixed12.
Selection still stops before NoteOn audio and destination effects.

## Contract and evidence

`HitArguments` is a fixed three-word array. `select_hit_note(..., initial_args)`
copies it into the plan's `initial_arguments` and starting registers1–3.
Explicit zero is valid. Without a tuple, register1 remains12 and registers2/3
remain zero, preserving the previously tested reference profile. Other initial
registers, locals and zero/sign flags are unchanged. Bytecode can later overwrite
the seeded registers, while the recorded invocation tuple stays intact.

The inspected initialization entry at VA `0x0057dc59` forwards its three stack
arguments through the variable writer at VAs `0x0057dcc4`, `0x0057dcd4` and
`0x0057dce4`, with destination IDs1,2,3. It sets the variable-provider pointer
at instance+`0xc8` to itself before those writes. Instruction exports were
checked against objdump. Input identity and registered field-query path remain
in [hit-source-callback-validation.md](hit-source-callback-validation.md).

The surrounding initializer also copies defaults, applies controller overrides,
branches on argument style, starts timing and establishes other state. Those
operations are not implemented by the raw tuple option. It does not derive the
arguments from a native caller, map units, emulate shared variable providers,
apply volume/pan or claim complete original thread initialization.

Simitone remains the primary integration reference. Its pinned submodule's
`tso.sound/HITThread.cs:194–212` initializes register1 to12. The new option is
explicit; the legacy constructor profile is retained and not silently replaced.
The tuple and source callback are separately selectable from literal32 Jump.

## CLI and limits

`--args INT32 INT32 INT32` supplies the entire triplet. Missing, repeated,
nondecimal or out-of-range values fail before resource selection. JSON records
`initial_args_fixture` (or null) and the explicit-argument evidence label.
With `--field-source`, only explicitly supplied fields of that source are valid;
argument initialization does not introduce a fallback for missing fields.

Storage adds a fixed12-byte argument payload plus optional/container overhead,
independent of resource sizes. Existing instruction/trace/group/chooser budgets
remain enforced. No callback or World pointer is retained in the returned plan.

## Validation

Selection now passes888 checks (previously822), including bytecode reads of27
signed argument combinations, exact extremes, unrelated zero state, omitted
defaults, explicit zero and owned invocation receipts. The World integration
fixture creates avatar1 and object2, queries their different banks through the
same bytecode, observes later PersonData changes, and guards the expected entity
generation so that ID reuse rejects the lookup. No bank/entity is changed by
selection itself. Only the test executable links the sound/World module; the
production selector's library dependencies remain unchanged.

CLI checks cover tuple arity, duplicate/overflow rejection, signed extremes and
an explicit source1 binding. Full Debug and ASAN/UBSAN suites each configure96
tests:95 pass and one X11 backend skips in the sandbox. Builds have no warnings.
LSan remains disabled here; this does not establish leak validation.

A separate ignored verifier uses the prior constructed-actor bank receipt
identified in [sound-source-variable-validation.md](sound-source-variable-validation.md)
and the real Global.far FWAV355/generated HSM/HOT/HIT resources. Its PlaySound
request supplies source1 to a generation-checked World lookup, with explicit
fixture arguments `[1,1024,0]`. The second/third values are raw fixture words;
their native caller derivation and unit interpretation are not asserted.
The reader obtains field20=30 and field0=0 from the World source itself, without
rebinding source12. It plans patch3212, NoteOnPC81,19 instructions/two choices.
The callback returns false and audio_submitted=false; the test does not run a
Linux audio device, advance Main or restore saved threads.

Debug/ASAN JSON is byte-identical, SHA256
`827bdc69b659270c5c3860c8d3477e8139340a55a7ea300048a62b63e35ace25`.
Ignored evidence: an archived validation record (not distributed), verifier binaries and
`hit-args-world-native-{debug,asan}.json`; `hit-args-*` build/full-test logs;
Ghidra `thread-init`, `role-vars`, `role-construction`, `argument-storage` logs
and selected exports. Original inputs and prior receipts remain preserved.

Next: derive invocation arguments from the real host call context, resolve
argument-style/controller/shared-variable setup and verified field contexts,
then implement sound-thread lifecycle, WaitSamp and Linux playback of decoded
XA. Saved thread/clock continuation and continuously interactive lot simulation
remain required. Runtime asset inputs remain independent of original executables
and physical media.
