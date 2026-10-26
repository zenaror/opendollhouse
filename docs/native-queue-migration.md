# Native TS1 queue migration contract

Status: **partial native implementation in progress**, 2026-10-26. The current
working tree parses all seven TS1 `PushInteraction` priorities, maps them to
signed priorities/modes, carries TTAB action flags into the native queue, and
orders pending actions by the TS1 normal, ParentIdle/ParentExit, PushHead and
PushTail rules that fit its existing one-active-action model. It reevaluates the
active action when work is queued and at tick boundaries: a higher-priority
pending action, or active priority changing to zero, marks the action to notify.
The existing `IdleForInput` wait keeps its scheduled wake time; when it next runs,
it sets the avatar's notified flag. Starting and finishing the outer action
update PersonData[33] when that bank is present. The VM checks routine ownership,
frame limits and stack depth before the queue publishes an action as active.
Controller codec v2 persists priority, mode, flags, notification bit, public
queue UID and queue profile. It still reads v1 FIFO saves as an explicit legacy
profile; the first new TS1 push adopts priority ordering while keeping old
pending actions in their existing order and assigning unique queue UIDs.

This is a queue-ordering and notification milestone, **not full TS1 queue
fidelity**. The current native runtime still protects only one active action.
It does not execute check trees/eligibility, dispatch nested actions from an
action-tree `AllowPush`, cancel or skip followups, inject RunImmediately at a
tick boundary, or support nested protected action frames and their callbacks.
RunImmediately requests are rejected explicitly so they cannot masquerade as
ordinary queued work. Priority reevaluation is limited to the single active
action model; it runs on each tick instead of FreeSO's QueueDirty-only schedule,
and does nothing when the avatar lacks PersonData[33]. Setting the cancellation
flag also requires ObjectData[8]. Keep extending the existing `Vm`,
`InteractionQueue`, World and session codecs; keep one authoritative native
simulation. Do not present this slice as game parity.

Current gates: six focused CTests cover VM call-commit rollback, action
decoding/order, active-action notification, controller codec v1/v2 and legacy
migration; their captured output is in
`reports/native-queue-priority/attempt-1/ctest.log`. The current
House07 active-save/reopen rerun is in
`reports/native-active-interaction/attempt-15/result.json`; both cuts (ticks
256/512) reopened with all 12 save sections matching, and both continuations
matched the uninterrupted run through tick 4096. The legacy save loaded and 25
original game files retained their hashes. Repeat this gate after later queue
changes before treating it as current runtime evidence. This House07 case checks
save/reopen determinism for the fridge action; it does not exercise competing
queue priorities or prove game parity.

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
queues onto **StackObject's thread**. GetAction uses TTAB TTAIndex, requires both
action and nonzero check routines, and carries exact owner/resource identity.
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
existing action tree, unlike the current native adapter.

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
is a **static source hazard, not a demonstrated game failure**. Do not blindly
translate an unbounded loop into C++. Establish a bounded, documented policy and
fixtures for retained items before claiming parity for this case; do not silently
reinterpret it as ordinary deletion.

## State and codec coordination

Current native state has one active front/return event, no priority/mode/check/
NotifyIdle metadata, and explicitly rejects nested interaction frames. Completing
the queue requires extending these same structures with protected-prefix/action
frame associations, action/check provenance, priority/mode/TS1 flags, notification
and cancellation state, interaction number, arguments, callback descriptors and
any behaviorally used result state. Keep native lifetime/generation identities
separate from the source's wrapping public UID. Never persist raw callbacks or
resource pointers. Queue callbacks/checks/entry-point4 may alter World or enqueue
work; resolve identities afresh across those calls.

Coordinate changes to `InteractionQueueSnapshot`, `VmSnapshot`,
`entity_thread_controllers_snapshot_codec.cpp`, `vm_snapshot_codec.cpp`, and
`session_bundle.cpp::validate_controller_vm_links` with the active-save work.
Current controller codec is v1 and VM codec v2. Active-save validation is already
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
