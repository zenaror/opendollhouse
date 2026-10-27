# Native household transfer, profile 1

This portable C++ component extracts a family from its authoritative immutable
checkpoint and applies personal state to freshly constructed residents of a
different occupancy. It never restores the source house's World or execution
graphs. Linux filesystem and purchase coordination remain in the host journal.
This is an incremental Simitone-based profile, not original TS1 equivalence.

## Authentication and publication

`prepare_household_transfer` validates the full archive codec, its exact SHA,
required FAML lease, original house/epoch/family/budget, and the complete
GUID/NBRS/OBJD/resource/fingerprint roster. Its privately constructed result
retains raw personal fields, motives, attributes and the source ticket. SIMS
fractional need state is required. Legacy person banks of 80 words are padded
to the 88-word TS1 projection; 256-word banks require a wholly zero extension
after word 87. A nonzero unknown extension is rejected, not truncated. The
immutable source archive retains every original byte in all cases.

`apply_household_transfer` stages every member by GUID with destination runtime
IDs, relationships and need fractions. Its privately constructed result binds
the source ticket, destination lease and complete materialized roster. The
journal rechecks this receipt and source archive before allowing the first
destination checkpoint. Source finances authenticate the source only; the new
household budget comes from the post-purchase overlay.

Purchases are now prepared, not immediately charged. The journal checks that
the proposal is exactly one legal move-in appended to its pinned overlay.
The runtime uses that proposed view to initialize the lot, characters and
execution services and validates a complete save/restore candidate, then
commits the purchase with CAS before starting play. This preflight publishes
no archive and advances neither the simulation clock nor its random state.
Preparation failure or a conflicting overlay leaves the family unhoused and
uncharged. Saving an uncommitted purchase is forbidden. Before a managed
move-out, a current checkpoint and a transferable complete family are required.
The source archive remains retained through move-out and subsequent purchases.

## Portable values and destination-owned state

The v1 personal projection includes personality, learned skills, autonomy,
interests, career/age/skin/gender, last work day, raw zodiac, TS1 preferences,
fame and magic progress. Use the explicit TS1 aliases for words 75–87; generic
TSO aliases have different meanings. Raw word 70 matters because the reference
accessor aliases it to SkillLock and does not faithfully round-trip zodiac.

Seven physiological needs retain their exact supported values and fractional
decay carries. The decay bucket is rebased to the destination clock. Room,
mood, sleep, motive-change controllers, positions, posture, IDs, queues and
old object references remain destination-owned. Neighbor IDs and family
control are explicitly rebound. Outbound family neighbor relationships and
inbound pairs targeting family members replace the corresponding destination
values; other neighbor pairs and destination object relationships remain.
Raw 32-bit neighbor target keys are not narrowed to runtime IDs.

Avatar attributes belong to fresh destination Init/EP8. Their old values remain
in the source archive/profile, but are not transplanted or required to match.
Pinned FreeSO `7e76c1960252ddd033d8937d5edb47d326f5b69c` demonstrates this
new-avatar behavior: `VMContext.cs:1463,1475,1484` constructs, initializes and
then inherits; `VMEntity.cs:237–256,494–495` creates the runtime attribute list
and runs Init/EP8; `VMAvatar.cs:957–969` inherits only the person bank.
`NBRS.cs:138–172` has no avatar attribute bank. This establishes the fresh
Simitone baseline, not the original executable's relocation rule.

The native host therefore requires each transferred member to have been
materialized and initialized during this launch. A standing actor left in an
old OBJM is not proof of freshness. The
[installed occupancy projection](native-occupancy-reconstruction.md) can retire
the exact original source-family actors only after replay proves their original
occupancy ended. It then constructs the incoming family afresh. A copied actor
or later visitor has no such authority; unsupported destinations still reject
before charging. Unrelated NPCs and visitors are retained.

## Explicit incomplete cases

- Ghost/living-ghost, scaling and toad transformations reject until their
  associated resources/controllers can be rebuilt, rather than moving flags alone.
- Bound appearances mix wardrobe accessories and transient animation props;
  they reject pending a provenance-aware appearance profile. Mutable wardrobe
  and default suits still need their own persistence work.
- Unclassified person words 9,16,19,20,24,29,54,55,66,67,77 and unknown motive
  slots must equal the fresh destination, otherwise transfer rejects. Their raw
  evidence is retained. They are not declared disposable. Before eviction, the
  host compares those person words with the effective saved NBRS baseline and
  requires unknown motives 0,1,2,4,10,12 to retain the proven fresh value100.
  A missing saved neighbor bank cannot pass this preflight. This prevents
  unsupported progress from leaving the family stranded unhoused. Fresh
  initialization repeats the compatibility check before charging a purchase.
- Inventories keep their stable neighbor IDs and are reread from the unchanged
  neighborhood NGBH. The current native inventory operation is read-only;
  future mutations, magic tokens and TATT changes must participate in the same
  neighborhood revision instead of becoming per-house copies.
- Full source-house saves, including old queues, belongings, runtime object
  matrices and chair/bed state, are not portable person state. Original TS1
  liquidation, building/buying persistence and general relocation remain
  separate incomplete requirements.
- Preparation currently uses the existing bounded complete-archive decoder.
  Peak allocation and a streaming projection need handheld-specific validation;
  portable C++ and fixed field arrays alone do not establish a console memory fit.

The diagnostic CLI uses the same production planners, resource preparation
and journal as the interface: `--headless-move-out-family ID` requires an
explicit house and zero ticks; `--headless-move-in-family ID` prepares a purchase
of the selected house and enters the normal runtime. These commands do not
make a headless result proof of interface clicks or original-game fidelity.

## Verified gate

an archived validation record (not distributed) records 12 passing checks. The core
uses two residents with changed runtime IDs and nondefault personal progress,
fractional needs, inbound/outbound relationships and protected destination
object relationships. The journal test exercises a real prepared/applied
receipt, deferred commit and first destination checkpoint, rejecting internally
valid resource-name and runtime-generation substitutions without changing
authority. The synthetic CLI refuses destination actors without retirement
authority, incomplete arrival resources, absent saves and changed character
resources without deducting a purchase price.

an archived validation record (not distributed) records nine completed native
processes with Complete Collection data and two privately created adult Sims.
The CAS helper creates the family only; the production launcher purchases
House01, saves at tick2, moves the family out, purchases House09, reopens it,
moves out again, purchases House01 and reopens it. Occupancy epochs are1/2/3.
The test grants an explicit initial budget44500 so all purchases are affordable;
balances become34000/30500/20000 after prices10500/3500/10500, with the retained
zero architecture refund for this created-family reference profile.

Both transfers preserve the compared personal projection, seven needs, decay
fractions, GUID/NBRS identity and character mappings/fingerprints. Both zero-tick
reopens retain the complete archive bytes and overlay. The original archive
remains present but cannot become a full restore of a later occupancy. The
binary stays SHA256 `8c3144862b53fa200a08663559e9ba776e4320e94c7a99ced6e59f35e35ac72f`.
All 114 protected original files and both private characters have matching
before/after hashes; processes and temporary profiles were closed/removed.
No display was used.

These earlier installed houses have no original selected-family actors, so returning
to House01 constructs residents afresh. This does not prove reconstruction of
an old inhabited OBJM, retention of player changes to a vacated property's
objects, graphical family management, long sessions, transformed residents,
original liquidation behavior or hardware support. The exploratory `real-1`
failure exposed the overly strict attribute equality check; its subsequent
correction follows the pinned constructor/inheritance behavior documented above.
The later occupied-House07 reconstruction gate and its narrower authorization
rules are documented separately in
[native-occupancy-reconstruction.md](native-occupancy-reconstruction.md).
