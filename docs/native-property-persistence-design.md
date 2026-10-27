# Native property persistence design

## Status and scope

This is an implementation design, not evidence that persistent vacant properties
or original eviction liquidation are implemented. The current native checkpoint
already retains the occupied lot's World and generic execution state through
WRLD/GEXP and related session sections. Resuming the same occupancy must keep
using that complete family checkpoint. The missing boundary is preserving the
appropriate property state when a household leaves and a later household enters.

Use the existing C++ World, resource loaders, session validation, immutable archive
store and neighborhood overlay transaction. Do not introduce a parallel simulation
engine or overwrite installed commercial houses. See
[family checkpoints](native-family-checkpoints.md),
[portable family transfer](native-family-transfer.md), and
[family management window](native-family-window.md).

## Reference evidence and unresolved policy

The initial inspection for this design used upstream Simitone
`4906db3156616bfaffc10ce0963cf08062975fef` and its FreeSO submodule
`f86bbd34b2112296ed786741f640a0535886d180`. The Linux evaluation instead uses
the alexjyong Simitone revision `e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`
and FreeSO `7e76c1960252ddd033d8937d5edb47d326f5b69c`; these are distinct
provenances, recorded in [references](references.md).

Committed source was compared with `git show` in the separate upstream,
alexjyong and evaluation-submodule checkouts, without relying on modified
reference working trees. In both frontend revisions,
`Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs`, `Save` updates SIMI,
serializes `vm.Save()` into FSOV, saves the house and then the neighborhood. This
is whole-lot persistence, not evidence for an occupant-free property format.
The fork also adds a weather sidecar after this common save sequence; the two
complete Save methods are not claimed identical.
`EvictLot` adds retained `ValueInArch` to Budget, zeros ValueInArch and calls
`MoveOut`. That handler does not establish original object-sale or liquidation
rules. The observed historical persistence fix is documented separately in
[Simitone family lifecycle](simitone-family-lifecycle.md).

Both compared FreeSO revisions' `TSOClient/tso.simantics/Entities/VMEntity.cs` declare
`VMMovementFlags.StaysAfterEvict = 1 << 4`; `Model/VMStackObjectVariable.cs`
identifies MovementFlags as ObjectData word 43. These declarations alone do not
prove which eviction path tests the bit, which objects or multitile groups it
removes, how it values them, or how it updates family money. Do not assume that
all furnishings remain, or that all furnishings are sold.

The [property liquidation reference audit](property-liquidation-reference-audit.md)
records the owning investigation's original-game manual evidence: eviction
preserves family relationships and credits proceeds from selling the house and
belongings. This establishes high-level sale/proceeds behavior, but not the
Complete Collection formula, rounding, depreciation or retained-object exceptions.
The same audit records a bounded Ghidra inspection of the supplied Legacy
executable; it did not recover an eviction caller, sale loop or refund formula.
Import success and string addresses are not proof of those missing behaviors.
Source-linked policy evidence is required before enabling the new release
operation. The current retained-ValueInArch refund remains a documented partial
baseline; its zero refund for created families is not the original liquidation
policy established by the manual. An unchanged pre-eviction property archive may
remain recovery evidence without becoming the next buyer's activation state.

## Authority and commit boundary

The subsequent comparison-sample audit (registro arquivado, não distribuído)
supports a candidate disposition policy: retain build-mode and explicitly
retained objects, remove qualifying buy objects and type-2 actors, refresh the
cached asset value on neighborhood save, and refund it once on eviction. It
also reaches filtered-house serialization. This is static comparison evidence;
native movement/master mapping, live valuation and failure ordering remain
separate gates. In particular, source type-2 filtering is broader than the
native installed-occupancy projection's exact original-family roster. Do not
silently call those policies equivalent or lose unrelated neighbor state.

Keep one mutable authority: the neighborhood overlay. Extend its versioned replay
with per-house property state containing house number, installed-house SHA256,
property revision, immutable property archive SHA256, and format/policy version.
The identity also belongs to one exact neighborhood. A missing property head
means the installed baseline only when history explicitly permits that state;
an invalid or unreadable existing head must never trigger an installed-lot fallback.

For the first implementation, create a neutral property artifact only at move-out.
Normal occupied saves continue to publish the family archive and checkpoint as
they do now. The family checkpoint remains authoritative for all current lot
changes until release. This avoids maintaining two competing live representations.

Move-out is one compound operation:

1. Pin the complete expected overlay, current family lease, authoritative family
   checkpoint SHA, current budget and prior property revision/head. Read the exact
   immutable checkpoint and verify source-house and resource provenance.
2. Construct a private property candidate using the audited eviction policy.
   Validate its object identities, resources, references and reconstruction
   profile before exposing a prepared release receipt.
3. Publish the property artifact immutably under its encoded-byte SHA256 and
   complete the store's durability steps.
4. Commit one whole-overlay CAS that changes family vacancy/refund and property
   revision/head together. Retain the pre-release family checkpoint as the portable
   household source; it must no longer qualify as a full occupancy resume.

Do not implement this as a property-head rename followed by a family-head rename,
or as two separately committed overlay operations. A crash between either pair
would split property ownership from money. Replay must validate both expected
family state and expected property state before accepting the compound operation.
If later requirements update property heads on every occupied save, both heads
must still advance in one overlay CAS after both immutable artifacts exist.

Use the current journal's conflict rules: never silently rebase a stale expected
overlay; an ambiguous CAS error is successful only when rereading proves the exact
replacement. Keep unpublished immutable artifacts recoverable. Retry must not
refund or advance the property revision twice. Equal encoded payloads may reuse
the same immutable object, while logical revisions advance only on committed
state changes.

## New occupancy and stable baseline

A pending purchase pins the current property ticket and installed-house identity,
then reconstructs through existing World/resource APIs. Apply portable family
state, run supported initialization and complete the save/restore-profile
preflight before charging the purchase. The final overlay CAS must still compare
against the pinned authority; a property change during preparation rejects it.

Each new family session must record the immutable initial property-baseline
SHA/revision from which its `session_baseline` was constructed. Keep this pin
stable throughout that occupancy. It is not the current family archive hash and
must not become a self-reference when saving. Fresh-process restore rebuilds that
same baseline before validating the current family checkpoint against it.

Old sessions without a property-baseline pin remain readable using their existing
installed-baseline compatibility profile. Do not reinterpret them against a newer
property head. A new property-bearing profile must be explicitly versioned and
required so an older reader cannot silently discard its baseline dependency.

## Property payload and execution boundary

Reuse the existing bounded World and relationship representations. A prepared
release receipt should privately bind the exact source checkpoint, outgoing
household roster, lease, prior property ticket, source-house SHA and policy
version to its validated candidate. The journal must verify those bindings when
committing release, rather than accepting a caller-assembled head or refund.

The payload needs the surviving object state, exact created-object resource and
definition provenance, removals relative to its installed baseline, and the
relationship state permitted by the audited policy. Preserve positions,
attributes and other durable object data only according to that policy. Never
both preserve a saleable object and credit its sale unless the reference actually
requires that behavior. Validate complete multitile groups and containment;
held objects cannot silently disappear or retain dead parents.

Retired runtime IDs and the generation sequence must survive reconstruction.
The current World snapshot codec does not itself persist reserved IDs. A neutral
artifact therefore needs an explicit bounded reservation record or a provable
derivation from retained provenance; removed avatars are no longer present to
derive it from. Do not reuse numeric IDs while retained opaque or bank data can
still refer to them. Keep source evidence immutable and distinguish it from
activation data.

Retire only the exact departing household identities authorized by the source
lease. NPCs and visitors are not household members merely because they are
avatars. Their retained state and activation policy must be explicit and audited.
Do not replay old VM stacks, queues, scheduler state or opaque execution suffixes
as property continuations in a different occupancy. Use the existing supported
fresh-execution path for retained objects; unsupported actors or reference
cleanup must fail preparation before any financial commit. This boundary does
not claim that the original game resets every object's stack.

Architecture editing is not serialized by this design's current implementation
baseline. Binding unchanged installed architecture by SHA is sufficient only
while edits cannot occur. Persisting edited floors, walls, terrain and related
architecture requires a future bounded architecture payload and matching restore
validation; WRLD alone must not be described as storing those changes.

## Required verification before enabling

- Failure before CAS leaves family money, occupancy and property head unchanged;
  ambiguous publication and retries cannot apply a refund twice.
- Stale leases, revisions, source-house bytes, cross-house artifacts and altered
  resource definitions reject without falling back to commercial defaults.
- Same-occupancy resume remains unchanged; move-out, different-family purchase,
  return and fresh-process reopen preserve the policy-defined property contents.
- Created/removed objects, containment, NPC boundaries, full-width relationship
  targets, retired IDs and generation ceilings survive the intended lifecycle.
- An older checkpoint cannot overwrite or reactivate a released property. Map
  cancellation preserves committed edits; rollback requires current authority.
- Original-policy fixtures distinguish retained and liquidated objects and verify
  financial effects. Successful synthetic checks do not establish original-game
  fidelity or GUI completion without the corresponding source/runtime evidence.
