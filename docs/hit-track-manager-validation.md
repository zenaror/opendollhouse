# Persistent HIT TrackID manager

This increment connects the immutable global object catalog, explicitly loaded
controllers and the independent source-pair collection. It implements only the
ordinary type1/ArgStyle2 dispatcher boundary demonstrated in
[track identity](hit-native-track-identity-evidence.md) and
[source contributions](hit-native-source-contributions-evidence.md).

## Coverage matrix, recorded before execution

| Criterion | Synthetic cases | Configurations |
|---|---|---|
| Global event -> winning Track -> global entry | replacement in another HOT, separate entry HIT; missing event/Track; wrong kind; unregistered; alias | Debug; ASan/UBSan |
| Effective arguments and admission order | three overrides; zero R1; active refusal; inactive clear observed inside admission | Debug; ASan/UBSan |
| Contributions and lifetime | duplicate; weighted two-source aggregate; source47/PC preserved; negative special source; positive missing; pair/controller capacities | Debug; ASan/UBSan |
| Initializer group gate inside caller | active/inactive skipped initializer; caller spatial writes and activation still occur; later admitted initialization | Debug; ASan/UBSan |
| Removal effects | interleaved erase/effect across two tracks; remaining-source controls; last-source fade/immediate stop; exact negative source; no controller deletion | Debug; ASan/UBSan |
| Initial Play vs refresh service adjustment | volume0/1: initial and active Play conserve0/1; refresh condition true gives1/0, false conserves raw | Debug; ASan/UBSan |
| Pre-effect refusal recovery | valid -> absent event/globally absent TrackID/unregistered existing Track/wrong kind -> valid/poll/stop; duplicate/capacity registration -> prior track poll/play/stop, factory unchanged | Debug; ASan/UBSan |
| Post-callback error policy | factory/admission/source/group exceptions; swallowed reentry at factory/admission/source/group; partial removal on unresolved remaining source | Debug; ASan/UBSan |
| Regression boundary | existing controller, global NoteOn, arguments, source contributions, object catalog | Debug; ASan/UBSan |

Case-level output and CTest totals are retained separately by gate. There is
no commercial fixture or physical-audio test in this matrix. Full-suite and
ports are not part of this bounded gate.

## Explicit interfaces and limits

`register_track` resolves the winning Track/entry once and gives that metadata
to an explicit factory. The manager owns the resulting inactive controller;
Play never calls the factory, replaces a controller or creates one per request.
The caller must configure the returned controller from the provided metadata,
including its owning HOT, global Patch callbacks, resource resolver and budgets.
The immutable catalog and all referenced resources/callback captures must
outlive the manager. Providers must not mutate controllers/catalog/resources
through retained aliases or reenter manager mutation. This does not emulate
native loading or active replacement.

The manager reserves its Track vector and source/scratch arrays at construction.
Controller and pair capacities must be in 1..65536 and are ODH limits. Default
values for the limits struct are 64 controllers/256 pairs, but the constructor
requires explicit limits. Each controller's trace/call/budget allocation and
all callback allocations remain additional factory responsibilities; this does
not close the persistent-runtime diagnostic budget problem.

Each Play uses `resolve_global_track_event`, applies event overrides, finds an
already registered TrackID and clears all contributions when that controller is
inactive. Admission is queried for both active and inactive requests. ArgStyle2
requires nonzero effective R1. A new pair is idempotent; complete samples are
resolved and aggregated before insertion. Source<=0 explicitly contributes
(1024,512); unresolved positive data refuses the request before insertion or
controller mutation. Admission and prior inactive clearing can already have
occurred. That preflight refusal is an ODH policy, not emulation of native
uninitialized/residual outputs.

After insertion, the explicit initializer-group provider is called. A false
gate skips only initializer effects: caller volume/pan writes and activation
still run. A true gate reuses the controller's existing active/paused/ID55
contract or initializes/restarts it. `dispatch_type1` is distinct from the
existing `retrigger` API, whose prior-activation precondition remains intact.
The manager accepts only ArgStyle2; other argument styles require another
contract.

Immutable Play lookup, argument, Track registration and ArgStyle preconditions
run before source clearing or callbacks. Their refusal releases the manager's
busy guard and preserves a usable nonfaulted manager. Registration similarly
refuses duplicate/capacity/lookup/empty-factory preconditions before its factory
begins. Exceptions or reentry after a callback/execution operation begins still
latch fault and retain completed effects. No rollback is promised.

Source removal erases one pair, applies the affected track's remaining controls
or gradual stop, then searches again. Deterministic ODH insertion order is not
the original hash-bucket order. A missing remaining positive sample or callback
exception latches a manager fault after any already completed erases/effects;
there is no rollback claim. Swallowed reentrant mutation faults are detected
after each manager-owned provider call and delegated controller operation.
Effects inside a delegated controller operation may already have occurred before
the manager observes a swallowed manager-reentry error; the controller retains
its separate own-reentry policy. Faulted manager mutation is refused; snapshots remain
readable. Audio teardown still follows the existing controller/provider contract,
including caller responsibility when cleanup fails or objects are destroyed.

Initial type1 Play uses the raw aggregate. The service+0x114 decrement is proved
in refresh helper `0x57482d`, whereas initial dispatch calls
`0x5738d2–0x5738ee` apply the raw aggregate volume, with pan through
`0x5738f8`. Play therefore has no adjustment parameter.
Only refresh/removal takes the explicit condition; its arithmetic is decrement
modulo32 then negative->1, so 0->1 and 1->0.
The service field's own lifecycle and refresh cadence are not implemented.
`poll_track` takes an explicit native Clock; there is no implicit World tick or
queue scheduling. No World generations, object reuse protection, faithful
projection/source geometry, physical audio, streaming/DSP, native admission
queue, native group registry, persistence/load/save or playable Linux build is
proved by these fixtures.

## Results

Revision before independent review passed 32 cases per gate but encoded an
incorrect Play adjustment and faulted on pre-effect refusals. Those green results
are superseded; the exact prior sources, logs, script and receipt are retained
under `reports/hit-track-manager-20261020/pre-fix/`.

The previous corrected Debug and ASan/UBSan gates each passed 6 focused CTests
and 37 manager cases (94 checks). That receipt and its sources/logs are retained
under `before-final-evidence-fix/`. It lacked an executed recovery sequence for
a globally absent TrackID (distinct from a registered-catalog Track that has
no controller). The expanded pre-test matrix above adds that recovery case.

Final Debug and ASan/UBSan gates each execute only `hit_track_manager`:
1/1 CTest and 38/38 named cases (98 checks), zero failure/skip/blocked/not-run.
The additional `pre-effect-refusal-recovers-kind-3` case executes the `missing`
event mapping to globally absent TrackID99; kinds0/1/2 separately cover absent
event, existing-but-unregistered Track8 and wrong-kind Patch1000. Every recovery
sequence verifies no provider/runtime effects or fault on refusal, followed by
valid dispatch, poll and stop.

The 5 companion CTests per configuration have unchanged common/test sources and
are explicitly reused from `before-final-evidence-fix/validation.json`, whose
SHA256 is recorded in the new receipt. They were not rerun; executed and reused
totals are separate in `validation.json`.
ASan uses `-fsanitize=address,undefined -fno-omit-frame-pointer`; LSan is disabled,
and UBSan halts on error. The expanded matrix above was recorded before rerun.
The final focused logs use `*-evidence-fix-*`; `validation.json` supersedes the
prior corrected receipt, while earlier stages remain preserved. Its script verifies production/test source hashes before/after the two
gates and records individual CTest/case results. Independent final review remains
pending. No full-suite, SDL/X11/ports or playable-game claim is made.
