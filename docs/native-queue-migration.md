# Native TS1 queue migration contract

Status: **partial native implementation in progress**, 2026-10-26. The current
working tree parses all seven TS1 `PushInteraction` priorities, maps them to
signed priorities/modes, carries TTAB action flags into the native queue, and
orders pending actions by the TS1 normal, ParentIdle/ParentExit, PushHead and
PushTail rules around a protected prefix of active actions. It reevaluates the
active action when work is queued and at tick boundaries: a higher-priority
pending action, or active priority changing to zero, marks the action to notify.
The existing `IdleForInput` wait keeps its scheduled wake time; when it next runs,
it sets the avatar's notified flag. Starting and finishing the outer action
update PersonData[33] when that bank is present. The VM checks routine ownership,
frame limits and stack depth before the queue publishes an action as active.
Controller codec v5 also persists the suspended parent prefix. It preserves v4
permission metadata and reads v1–v4 with an empty parent prefix. It persists
priority, mode, flags, notification bit, public
queue UID, queue profile, optional check routine identity and whether a
player request requires permission checks. It reads v3 without adding that
permission requirement to older actions, and reads v2
priority saves without inventing checks, and still reads v1 FIFO saves as an explicit legacy
profile; the first new TS1 push adopts priority ordering while keeping old
pending actions in their existing order and assigning unique queue UIDs.

This is a queue-ordering and notification milestone, **not full TS1 queue
fidelity**. An action-tree AllowPush can now start a higher-priority child while
retaining its parents in the protected prefix.
PushInteraction now retains its TTAB check routine under the same code owner.
At AllowPush dispatch, the check runs synchronously with four zero arguments and
shared TEMP/TempXL and World state. False removes that candidate and tries the
next one in the same attempt; effects produced before false remain. Missing code,
unsupported services, faults and the 10000-instruction check limit produce an
explicit error, never approval or a fabricated false result. The check retains
the suspended Main frame and uses a separate, synchronous-only primitive handler.
The host uses an explicit synchronous handler set, including the operations
accepted by its menu probe; asynchronous services remain explicit gaps. PushInteraction sets
SkipPermissions, which bypasses permissions but not the check. Player-selected TTAB actions retain their flags, priority 50, check identity and
permission requirement. Menu selection and live dispatch use the same TS1 rule
for age, visitors, pet flags and debug restrictions. Dispatch repeats permission
and BHAV checks against the current World; a check-time change can reject an
action that was available at click time. Other direct host enqueues and legacy
saves retain their old semantics. Check primitive coverage remains incomplete.

Targeted cancellation now removes pending skippable actions or signals retained
actions; ordinary Enqueue also cancels the following suffix under the pinned
priority-pivot rule. See below for the bounded retained-action policy and
synchronous EntryPoint4 limits. RunImmediately tick injection and general action
callbacks remain pending.
RunImmediately requests are rejected explicitly so they cannot masquerade as
ordinary queued work. Priority reevaluation protects the entire active prefix; it runs on each tick
and when a nested action completes instead of FreeSO's QueueDirty-only schedule,
and does nothing when the avatar lacks PersonData[33]. Setting the cancellation
flag also requires ObjectData[8]. Keep extending the existing `Vm`,
`InteractionQueue`, World and session codecs; keep one authoritative native
simulation. Do not present this slice as game parity.

Current gates: 18 focused CTests passed for VM execution/snapshots, queue,
controller codec v1–v5, session bundle, seated continuation and resident restore
(including cross-process fixtures). Output:
`reports/native-auto-cancel/attempt-1/final-ctest.log`; build:
`reports/native-auto-cancel/attempt-1/final-build.log`.
The earlier cross-resident PushInteraction assertion remains covered.
Fixtures cover true/false checks, retained World/TEMP/TempXL effects, owner/context,
zero check arguments, missing/unsupported checks, independent instruction budgets,
queue save/reopen, rejected missing-owner code, priority threshold captured before
the attempt, cancellation flag clearing, and cancelled ParentIdle removal.
Player fixtures additionally cover permission changes after enqueue and save/reopen,
unknown avatar data, pet/visitor/age flags, and rejected requests without fabricated
completion. These are synthetic interpreter/World fixtures, not original-executable evidence.

House07 active-save/reopen rerun:
`reports/native-active-interaction/attempt-24/result.json`, binary SHA256
`0469a48014ff9cfdb3c59a342398b21a627d92a4bfd080d0a9902c1717c3a4fc`.
Both cuts (ticks 256/512) reopened with all 12 sections matching, and both
continuations matched the uninterrupted run through tick 4096. The legacy save
loaded and 25 original game files retained their hashes. Processes ended and
isolated profiles were removed. This verifies continuity of the fridge action;
it does not establish coverage of all checks, competing priorities or game parity.

## Nested AllowPush execution

A child keeps its parent's PC and arguments. True and false child returns are
recorded as completion events without branching the parent; the parent retries
its own IdleForInput instruction on the next tick. TEMP/TempXL and World are
thread-shared, so child effects remain. Completion restores parent priority;
NonInterruptable and motive changes clear only when the outermost action ends.
StackObjectID may change inside an action and is not its identity: routine,
invocation ticket, caller, callee and code owner identify each frame.

New AllowPush=1 waits poll every tick. Restore still accepts the older full
countdown deadline for a Main wait and preserves that deadline. AllowPush=0
keeps its full countdown. Captures before return observation, after observation,
and after a clock advance before VM execution preserve the same continuation.
Nested dispatch and targeted cancellation require the TS1 priority profile;
legacy FIFO snapshots cannot fabricate suspended ancestors or gain new cancellation
semantics. RunImmediately and general action callbacks remain explicit gaps. The synthetic integration test covers one parent and one
child for both return values, comparing uninterrupted and repeatedly reopened
VM/controller/World state; this is not evidence of a complete game flow.

## Automatic suffix cancellation on enqueue

Only the ordinary backward priority scan triggers this rule: if an insertion
follows a pivot whose priority is at least the new priority, cancel entries after
the newly inserted request in their original order. PushHead, PushTail, empty
queues, highest-priority insertion without a pivot, and a lower-priority
ParentExit-only boundary do not trigger the pass. The native implementation
uses existing cancel(native UID), preserving its MustRun, mode, protected-prefix
and queue-skipped EP4 semantics.

The source's repeated-index loop can fail to terminate on a retained action.
Native policy is to snapshot the original suffix's native identities and visit
each still-present identity at most once. ParentIdle may remove later identities,
which are skipped; retained entries remain signalled and the pass continues.
Work appended by callbacks is not included (same-queue callback reentry remains
unsupported). This is a deliberate termination policy for a static source hazard,
not proof of exact finite original-executable behavior for that broken loop.

Native/source identities and any legacy-to-TS1 adoption are published before
callbacks. If cancellation fails after effects have begun, remove only the new
pending request and report enqueue failure. Earlier removals, World/register
effects and consumed identities remain; no completion or identity reuse is
fabricated. Revalidate actor/run generation and the new request's target after
callbacks. Callers may use last_enqueued_uid for request tracking only after a
successful enqueue. Successful states and retained notification metadata keep
controller codec v5, with no asynchronous callback job to serialize.

The synthetic scheduler gate executes automatic suffix cancellation during a
registered avatar VM's tick. Its nonzero EP4 returns false while updating callee
TEMP/TempXL and World; those effects persist, its waiting Main does not advance,
and the callback runs once under its own instruction bound. Separate VM fixtures
cover external clone mode, true/false/exception/limit writeback and rejected
reentry leaving both banks unchanged. Queue fixtures cover original suffix
order, mode boundaries, retained items, callback failure, consumed identities,
new-target invalidation and controller/VM snapshot restoration. Real fridge
runs below are continuity regressions; they do not prove automatic suffix or
nonzero EP4 in an original game interaction.

## Targeted cancellation and queue-skipped entry points

The host cancels by the native 64-bit action identity, independently of the
wrapping ushort source UID. Pending Normal or DirectControl entries can be
removed unless MustRun is set. Active/protected entries and other retained
modes instead receive NotifyIdle, avatar InteractionCanceled and current priority
zero; their game BHAV performs cleanup. Cancelling ParentIdle removes following
pending ParentIdle entries and marks following ParentExit entries notified with
priority zero. If that would remove a protected descendant, the native queue
reports unsupported before mutation rather than corrupting its active prefix.
Legacy FIFO cancel is also explicitly unsupported.

Removal invokes a configured synchronous queue-skipped handler **after** removal,
as in the pinned source. The host resolves callee EntryPoint4 from the exact
live definition/resource, runs Caller=Callee=object and StackObject=avatar with
four zero arguments, and accepts either true or false completion. A known zero
entry is a no-op. An external host-boundary call clones callee TEMP/XL (or starts
zero if no callee thread exists); it never imports the avatar's registers.
During simulation dispatch, EP4 register effects are published back to that
callee before returning, including false, fault and budget-limit effects. The
scheduler exposes its active phase; a host phase guard covers initial Main
slices before the first numbered tick. Rejected nested/freshness checks validate
before touching either register bank. The disposable VM shares exact code/memory and only explicit synchronous
handlers. Waiting, asynchronous service calls, unavailable code and the
10000-instruction limit are explicit errors. Effects before failure remain;
an errored callback does not resurrect the removed action, fabricate completion,
or run again on a retry for that identity.

The callback itself is not serialized: it finishes at the host boundary, and
only the resulting queue/World state is saved using existing v5 metadata. Hosts
rebind their callbacks on reopen. Active cancellation state, including priority,
flags and NotifyIdle, is compared across VM/controller save/reopen in synthetic
execution. Callback reentry into the same queue is rejected. Arbitrary callbacks
that enqueue work, RunImmediately, and the source exception for skipping a
protected action outside an action tree remain pending. Automatic Enqueue uses
the bounded suffix pass below rather than reproducing the source-loop hazard. No asynchronous EP4 job
is silently dropped or represented as a successful cancellation.

Cancelling the real fridge action exposed a separate cleanup-order bug:
RemoveObjectInstance used to wait, erase object180, then resume code that still
read that object's flags. Non-self flags0 removal now continues the tree immediately and
queues deletion for a scheduler boundary, matching the source's deferred delete.
Foreign VM frame/service references retain the object's data across an instruction
budget cut or save/reopen; the object's own Main does not pin its own deletion.
The existing deferred-removal identity/generation section stores this work.
This is still a native lifecycle limitation: retained objects remain in World
until readers release them, whereas the source can remove the entity from lookup
while existing frames retain a VMEntity reference. General dead-object/tombstone
visibility and the remaining removal flags require a later lifecycle migration.
Self-removal also retains the previous native immediate branch, whereas the pinned
source uses GOTO_TRUE_NEXT_TICK; that timing path is not validated by object180.
Repeated deletion requests are deduplicated by ID/generation, and stale intents
are discarded before reference checks so they cannot pin or erase a replacement;
no unknown or removed object read is fabricated as zero.

Backspace uses the existing menu/Go Here precedence, then cancels the tracked
selected interaction. `--headless-cancel-interaction TICK` schedules that same
host operation after enqueue and before the tick's interpreter dispatch; equal
enqueue/cancel ticks exercise pending cancellation. JSON and session logs retain
cancel result/diagnostic and EP4 context/routine/steps/result.

Cancellation runtime evidence: `reports/native-action-cancel/auto-regression-1/result.json`,
using the same final binary as the normal fridge regression above. Tick1 removes
the pending fridge action without starting it (its known EP4 is zero). Tick256
signals the active action; the save at tick256 still has that action active.
It then finishes its cleanup, returns false and leaves an empty queue. Both
scenarios reopen all 12 sections identically and their final states match their
uninterrupted controls (ticks512 and4096). Actor/global/RNG/tick fields match;
25 originals are unchanged and all eight processes/profiles were cleaned up.
Nonzero EP4 execution and true/false effects are interpreter fixtures, not a
claim that every object's EP4 has been exercised. HOST deferred jobs have fixture
roundtrips; these real cuts do not prove saving during pending removal itself.
The earlier `real-1` failure at tick395 documents the cleanup-order bug fixed
here. Static independent review and report inspection found no remaining blocker
for these exercised paths; no graphics, original-EXE or hardware proof is claimed.

## Player requests and code ownership

The UI tracks the request's native 64-bit identity, independently of the wrapping
public UID. Losing a pending request without a completion means it was rejected
before execution; a later automatic action must not supply its result.
Menu actions keep checks for every TTAB row sharing an action root under the
same resource owner, including private and linked code needed by those checks.
New complete banks use existing resource-bank descriptors. Legacy dynamic-owner
restoration accepts only a bank whose fingerprint exactly matches the save;
old action closures remain readable. Unknown code/permission state is an explicit
error, not a fabricated eligibility result. The pinned TS1 permission rule has
quirks (including humans passing the cat/dog flag branch); preserve its observed
source semantics rather than imposing new game rules.

Legacy single-resident regression probe: House05 bench173 and House07 chair128
both pass Sit/save/fresh reopen/Stand in
`reports/native-menu-checks/seated-9/result.json`, including byte-identical
zero-tick reopen. The earlier House05 failures are retained as historical
evidence. The host now reuses the target's exact installed resource bank for
RunTreeByName destination 2 and loads its linked BCON constants. Dedicated SEAT
validation recognizes an ordinary Main subroutine and matching animation wait
under its declared Sit owner. It verifies all owner references and restores
resource metadata before animation resolution. Pending interaction queues and
unknown owners remain unsupported in this legacy profile. Whole-household
code-owner capture is a separate path. See `session-save-design.md` for evidence.

## Pinned authority

Authority is Simitone's adopted FreeSO commit
`7e76c1960252ddd033d8937d5edb47d326f5b69c`, read with `git show` from
`refs/work/Simitone-linux/FreeSO`, never its modified working files. Paths below
are relative to that repository. SHA-256 values cover complete file bytes.

| Source | SHA-256 |
| --- | --- |
| `TSOClient/tso.simantics/Engine/VMThread.cs` | `294616ef1f4a67283657744f6e2970e6ee9fc42161135c709bb5029040a49692` |
| `TSOClient/tso.simantics/Engine/VMQueuedAction.cs` | `9c279ab025211767b4c3cef6a97455783af2a89586f15aab4a88789f1d720492` |
| `TSOClient/tso.simantics/Primitives/VMPushInteraction.cs` | `e58aa2e51b93200e6018def2011b7b3fe6485fac3aa9cd63e6de1a34ae7aff50` |
| `TSOClient/tso.simantics/Primitives/VMIdleForInput.cs` | `21aad442e80397592c4ea0b3ae82703361ce121ceb6b0677b7f5349921dbceb4` |
| `TSOClient/tso.simantics/Engine/VMActionCallback.cs` | `6bbeb74b20a2c3247731a1d550b7e34ca2488939b82caa83df0f2a7b03c703f3` |
| `TSOClient/tso.simantics/Entities/VMAvatar.cs` | `0d4dac3a3781e8a369a148872a0d8c3d09a21a272815d8246b7ddfa08d2d7faf` |
| `TSOClient/tso.files/Formats/IFF/Chunks/TTAB.cs` | `b7b3069944cb4be62589047b32aff7f223487b92510e08f3c9021c2b9f5a5d58` |

The corresponding readonly upstream files are byte-identical except
`VMAvatar.cs`; its relevant Priority setter was checked directly in the pinned
commit, lines 891–895. Do not infer whole-file equality for VMAvatar.

## Exact execution behavior to retain

`VMPushInteraction` resolves the source from the selected local/argument and
queues onto **StackObject's thread**. GetAction uses TTAB TTAIndex, requires the
action and any referenced nonzero check routine, and carries exact owner/resource identity.
Push adds SkipPermissions; this bypasses permission filtering, **not the check
routine**. Another avatar recipient also receives InteractionResult = 0.

Priorities are signed16: Maximum 100, Autonomous 2, UserDriven 50, ParentIdle 40,
ParentExit 30, Idle 0. Modes are Normal, ParentIdle, ParentExit and Idle. Inherited
uses max(1, Caller PersonData[33]) inside an action tree, otherwise 1. Mode remains
Normal for inherited/maximum/autonomous/user-driven pushes.

`ActiveQueueBlock` is -1 when inactive; entries through that index form a protected
prefix. Its last entry is ActiveAction. It is not equivalent to a single active
front. Enqueue assigns a wrapping ushort UID and marks QueueDirty, then:

- RunImmediately outside check execution appends with Idle mode and returns;
  tick processing performs its injection. It does not execute in the enqueue call.
- Empty queues append. PushHead inserts at ActiveQueueBlock + 1, ignoring priority;
  head wins when head and tail are both set.
- PushTail, except ParentExit mode, inserts before the first pending entry with
  strictly lower priority, otherwise appends. TS1 disables TSO Leapfrog.
- Ordinary insertion scans pending entries backward. ParentIdle starts searching
  only after crossing a ParentExit boundary; other modes start immediately. A
  priority greater than or equal to the new priority, or a ParentExit boundary,
  selects insertion immediately after that entry. If no pivot qualifies, insert
  after the protected prefix. The TS1 branch may cancel following entries.

AttemptPush examines the next entry after the protected prefix. Its threshold is
PersonData[33], decremented when <= 2. A candidate must be strictly above this
threshold; equal autonomous or inherited-low-priority actions can therefore nest.
An unsuitable ParentIdle/check-failed candidate is removed and the next examined.
A valid action clears InteractionCanceled, writes its priority, pushes an action
frame and extends the protected prefix. TS1 AllowPush=1 permits this from an
existing action tree, as now implemented by the native adapter.

QueueDirty is set by queue changes and by writes to PersonData[33]. At the tick
boundary EvaluateQueuePriorities runs, then TryRunImmediately. Evaluation treats
current Autonomous priority as 1; it marks active NotifyIdle when an originally
nonzero-priority action has current priority zero, or when a pending action has
higher priority. Pending null/dead callees are removed. It sets ObjectData[8]
InteractionCanceled (bit7); IdleForInput reports notification through bit6. The
cancel bit is not unconditionally cleared on every reevaluation. Routing checks
it only when PersonData[71] NonInterruptable is zero.

Cancellation finds the first matching UID and marks NotifyIdle. For ParentIdle,
following ParentIdle entries are removed and following ParentExit entries remain
with NotifyIdle and priority zero. A non-MustRun action can be removed when pending
(or the current frame is outside an action tree), provided its mode is Normal or
it is DirectControl. TS1 removal invokes callee entry point4 with the actor. Other
cases remain queued and set InteractionCanceled/current priority zero. This is
not unconditional deletion of the active action.

Finishing an action invokes its callback before removing that protected entry.
Non-ParentIdle completion clears InteractionCanceled. Only outermost completion
resets NonInterruptable and clears motive changes. The previous protected action's
priority is restored, or zero if none remains. Ordinary interaction returns resume
the suspended instruction next tick; RunImmediately resumes it in the same tick.
Check evaluation shares TEMP/XL banks while the scheduler is running and clones
those banks outside a tick; a failed check does not imply rollback of prior effects.

### Static source-loop hazard

Pinned VMThread.cs lines 807–812 repeatedly call CancelAction on the same queue
index without advancing it. Cancellation can retain a MustRun, non-Normal or
protected item. For such a queue state the loop can fail to make progress. This
is a **static source hazard, not a demonstrated game failure**. The bounded
original-UID suffix policy above preserves retained items and makes progress;
it does not silently reinterpret those items as ordinary deletion or claim
original-executable parity for the source's nonterminating state.

## State and codec coordination

Current native state has one innermost active action, an ordered vector of
suspended ancestors, one return event and priority/mode/check/NotifyIdle metadata.
Entries through the active action form a protected prefix. Completing the queue
requires extending these same structures with full cancellation, interaction
number, callback descriptors and any behaviorally used result state. Keep native lifetime/generation identities
separate from the source's wrapping public UID. Never persist raw callbacks or
resource pointers. Queue callbacks/checks/entry-point4 may alter World or enqueue
work; resolve identities afresh across those calls.

Coordinate changes to `InteractionQueueSnapshot`, `VmSnapshot`,
`entity_thread_controllers_snapshot_codec.cpp`, `vm_snapshot_codec.cpp`, and
`session_bundle.cpp::validate_controller_vm_links` with the active-save work.
Current controller codec is v5 (reads v1/v2/v3/v4) and VM codec v2. Active-save validation is already
changing the same active/wait/frame invariants: do not independently replace its
schema. Add versioned fields only after agreeing the shared shape; old FIFO saves
must not silently acquire invented TS1 priority metadata. Upstream load rebuilds
references and marks QueueDirty=true. Native restore must rebuild exact owner and
object manifests, validate before publication, preserve identity/aliasing, and
resume existing frames without rerunning Init.

## Meaningful integrated gates

Use actual native BHAV/World/controller execution and save/reopen boundaries:

1. Mixed priorities/modes with active and inactive queues: exact order for ordinary,
   head and tail insertion, ties, ParentIdle/ParentExit boundaries and both flags.
2. An action-tree AllowPush starts a higher-priority child, preserves its parent's
   PC/arguments/TEMP/XL, and restores priority and timing on both true/false return.
   Include low inherited/autonomous equal-priority nesting and RunImmediately.
3. Priority33 changes from BHAVs trigger notification; IdleForInput and routing
   respect flags/NonInterruptable. Test cancellation during routing and idle.
4. Pending Normal cancellation runs entry point4 once; MustRun/protected/ParentExit
   actions remain consistent; parent-chain cancellation and the static-loop hazard
   terminate under the documented policy. Exercise callbacks that enqueue work.
5. Checks pass/fail with exact owner context, SkipPermissions, bank side effects,
   dead/recycled targets and missing code. No fabricated success or ownership fallback.
6. Save/reopen with nested protected actions, active waits, queued continuations,
   cancellation/notification and pending immediate work. Compare an uninterrupted
   continuation against restored execution: event order, frames, banks, World,
   needs/fractions and callback count. Include malformed identity/provenance failure
   leaving the live destination unchanged, plus wrapping-UID cancellation identity.

These gates prove only their exercised native paths. Linux success is not original
executable equivalence, complete UI/game parity or console/hardware validation.
