# Native state and Expression integration checkpoint

This records the bounded native C++ checkpoint of 2026-10-26, based on
`c7966c100bc97555cbbc71f3cba6455182228291` plus the working-tree changes.
The intended product remains the complete TS1 game ported from the Simitone
reference to C/C++, with no C#/.NET dependency. The whole game is incomplete.
The previously pending managed bridge is preserved outside this work and was
not used for the checks described here.

## Existing C++ VM integration

The existing `Vm::expression` now calls `odh_expression_execute_ts1_v1` through
native memory and level callbacks. The main CMake build statically links the
same C++ implementation used by the standalone ABI. There is one operator body
for TS1 Expression operators 0 through 20. Exceptions from native VM callbacks
are caught before returning through the C ABI and rethrown on the VM side.

The VM still owns its existing TEMP and TempXL arrays. Both those arrays and
snapshot storage use `Ts1Temps` and `Ts1TempXL` from
`include/odh/temporary_registers.hpp`: 20 signed 16-bit values and two signed
32-bit values. This shares storage definitions; it does not create another
runtime state or mirror authoritative registers. Scope 8, scope 11 indirection,
and scope 42 use those existing arrays in the real VM.

`VmMemory::expression_write` distinguishes an unsupported setter (`nullopt`)
from a supported rejection (`false`), while legacy write callbacks keep their
old unsupported-on-false contract. `expression_levels` supplies the exact
MyObject.Room/1024 special case lazily after both reads. World/importer changes
preserve the required callee OBJD LevelOffset and use actual caller/callee
levels; absence remains unavailable. The preexisting read-only `has_routine`
helper and its tests are incorporated as a dependency of the resident action
resolver described below. The header contains more than an include/type-alias change.

## Separately tested reference-counted state API

`native/simulation/STATE.md` describes the new `odh_thread_state` holder and
independent reference-counted TEMP/TempXL banks. Its state-aware Expression
entrypoint uses the same operator body and directly resolves scopes 8, 11, 13
and 42, including per-access stack-holder resolution and bank replacement
visible after host callbacks.

The new holder/bank API has **not** replaced storage in the existing `Vm` or its
save codec. Its successful sharing, clone, replacement, lifetime and scope 13
tests are component evidence. Real entity-thread ownership, StackObject.Thread
resolution across the complete VM/world, disposal/replacement lifecycle, and
serialization of shared versus cloned banks still need integration and runtime
validation. The real VM routes other scopes through its supplied memory
backend; the isolated scope 13 resolver suite does not establish that full-game
path. Do not copy arrays into a second authoritative bank as a migration shortcut.

## Recorded host and ARM checks

The standalone Linux module passed all six CTests in fresh Release and
AddressSanitizer + UndefinedBehaviorSanitizer builds. The shared state suite
passed 76 checks; the original Expression suite passed 792. Plain C header
consumption and strict warning builds passed. Independent review additionally
ran 123 native-VM checks under sanitizers, exercising actual BHAV instruction
branching, existing storage, callback failures and snapshots.

Local evidence is in `reports/portability/native-state/manifest.json`,
`release-ctest.log`, `sanitized-ctest.log` and `independent-review.json`.
These are synthetic component/VM checks, not original-game equivalence.

The same 76-check state suite built as ARM EABI5 ELF and 3DSX, then emitted the
exact guest marker `ODH_3DS_NATIVE_STATE_V1:PASS` in the emulator. The independent
artifact review also checked the visible result, source/binary/log hashes and
cleanup; see `reports/portability/3ds-native-state-execution/final-review.json`.
The raw runner result deliberately remains unmodified: its initialized
`emulated_component_verified: false` is not promoted by that runner. The
independent review supplies the bounded emulated-component approval. This does
not validate the game, saves on 3DS, physical hardware, or another console.

## Actual native House07 save and reopen

The parent-owned run used the native `build/linux-checkpoint/odh-person`, mode
1, language 14, House07, resident 47, and a scheduled actor 105 interaction with
target 112. It ran 768 ticks, wrote an ODH session, reopened that session at
zero additional ticks, then separately resumed for 2,048 ticks to tick 2,816 and
saved again. The recorded processes exited successfully; the household's three
scheduled entities remained in a bounded waiting state without VM diagnostics.
The zero-tick reopen reported `session_loaded: true` and tick 768.

These results establish execution and this native session save/reopen path.
The requested fridge interaction returned false; it did not establish successful
snack preparation or a fully playable household. Reopening after that completed
false interaction does not prove persistence of an active interaction, routing,
new bank sharing, every continuation, or unrestricted long-running play.
Protected commercial-data hashes remained equal and temporary profiles/processes
were cleaned up. Evidence: `reports/native-vm-house07/result.json`,
`first.stdout`, `zero-reopen.json`, `zero-reopen.stdout` and `second.stdout`.

## Causal comparison and native language lookup correction

The paired comparison restored only the previous `Vm::expression` operator
switch; it retained the current memory adapters, `VmMemory` layout and remaining
libraries. With the same initial RNG seeds, old-switch and shared-executor runs
produced identical 1,713-entry traces and the same false interaction result.
Only profile/session paths differed in observable top-level JSON. This provides
bounded evidence that replacing the operator switch preserved this observed run;
it is not a comparison against the complete previous HEAD or the original
executable.

The recorded false branch is RunTreeByName, routine 4121 PC 2, STR table 303,
string ID 1, requested language 14. That table contains only language 1;
`tools/person.cpp` returned no name before catalog lookup, while the pinned
reference falls back to English US for an empty requested language set. The
following cleanup removed newly created object 180. See
`reports/native-vm-expression-comparison/result.json` and `cause.json`.

The subsequent native correction adds `string_in_ts1_language_set` for both
RunTreeByName and ChangeSuit STR304 lookup. It selects English only when the
requested language group is absent; a present group retains its own missing
indexes and empty strings. The existing presentation fallback remains distinct.
The animation suite passed 234 checks and nine focused CTests passed. Pinned
STR, RunTreeByName and ChangeSuit source contracts were reviewed independently.

The corrected native run entered the previously skipped named routine, then
stopped at tick 392 on a separate existing limitation: PushInteraction accepted
only the primary avatar's queue, whereas this request targeted another resident.
`reports/native-vm-language-fallback/diagnostic/stdout.json` retains the fault and
trace. The first attempt with save-on-exit rejected the failed session before
writing the JSON report; the diagnostic rerun omitted save-on-exit and preserved
the log. Original hashes remained unchanged; own profiles and processes ended.
This is progress past the language lookup, not a successful complete snack or
a new successful save. ChangeSuit was not executed in this diagnostic run.
The following resident queue integration closes that observed execution gap.

## Resident PushInteraction and successful native continuation

The native `ts1_push_interaction_primitive` lives beside `InteractionQueue` and
resolves the StackObject recipient to that resident's existing VM and queue.
The host registers the primary, optional second and all household residents.
It never substitutes the primary avatar for an unavailable recipient or creates
a parallel VM. Resource provenance and exact installed action ownership are
validated before enqueueing. Source TTAB lookup uses TTAIndex (`label_index`),
not serialized row order, including inherited object interaction resources.
The former private-Food-only restriction is removed. The underlying queue now
inserts PushHead before an inactive queue or immediately after its active action.

Tests execute a cross-resident push through real native VMs and shared World,
including return to Main, source/recipient ownership, operand rejection, sparse
TTAB indexes and both head orderings. The interaction suite passed 105 checks;
the final application build and ten focused CTests passed. Independent source
review approved the final ordering fix. This remains an inherited FIFO adapter:
full priority propagation, special queue modes and eligibility/check execution
remain incomplete. Custom icon selection is decoded for host presentation; the
native queue does not yet render it. Do not claim full PushInteraction parity.

`reports/native-vm-resident-continuation/` records the final native executable,
SHA256 `2425bf514ae00260e2499315b9af88922aa533117a02da4a6c4dfe75068bc9bd`.
Mode 1, language 14, House07, selected resident 47: the request ran on the other
resident (entity 105), targeting fridge 112/action 4103. At tick 4,096 the
interaction reported true, its queue was empty and no created-object runtime
remained. The native process saved and exited zero. A second process reopened at
the same tick without advancing; a third reopened, advanced to tick 5,120 and
saved again. All three exited zero with no host failure. Protected original-data
hashes remained unchanged; own profiles and processes ended. No display opened.

Zero-tick JSON comparisons include selected actor state, globals, RNG and tick;
the independent artifact review records additional resident and needs checks.
The save is after interaction completion, not an in-flight action save. The
string `entity_population` is a policy description, not a whole-world equality
proof. Neither this headless run nor its logs prove visual fidelity, all saved
fields, original-executable equivalence or all households/expansions.
Source-only provenance comparison confirms the consulted STR, RunTreeByName
and PushInteraction files are identical to adopted FreeSO commit
`7e76c1960252ddd033d8937d5edb47d326f5b69c`, excluding preserved managed WIP.
See `reports/native-vm-language-fallback/reference-provenance.json`.

Further migrations still include full thread/entity state, all needed simulation
primitives and data services, save compatibility, graphics, audio, input and the
original TS1 interface. Linux remains the first runtime gate; host and emulator
component success does not complete the other platform ports.
