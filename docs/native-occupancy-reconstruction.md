# Native installed-lot occupancy reconstruction

This C++ activation projection removes the exact installed residents of an
expired source occupancy before constructing the current household. It is a
derived view of an immutable installed lot, not a rewrite of House IFF/OBJM and
not a complete property-save system. Personal carryover remains governed by
`native-family-transfer.md` and the managed family journal.

## Authority and identity

`prepare_installed_occupancy_retirement` accepts the original
`NeighborhoodFamilySource`, the proposed runtime `FamilyOverlay`, the selected
house, its SHA256, and the current destination `SessionFamilyLease`.
It replays the complete overlay and requires the destination family, house,
identity and occupancy epoch to agree with that replay.

Only the original source FAMI assigned to this installed house authorizes
retirement. Its installed occupancy is epoch one. If that occupancy is still
current, there is no retirement plan. If full replay proves that it ended, the
plan contains precisely its original member GUIDs. A later family visiting or
occupying this house never broadens the installed-lot deletion roster. In
particular, a visitor whose family originally lived in another house is not an
expired resident of this house merely because it appears in later history.

The privately constructed `InstalledOccupancyRetirement` binds neighborhood
identity, house SHA256, encoded-overlay SHA256, destination lease, original FAMI
chunk ID, house number and sorted original GUID roster. Its domain-separated
plan digest includes those values. The host checks the runtime overlay against
the authorization and rechecks installed house bytes before activation.
This is source-occupancy authority, not a heuristic based on PersonType, current
family membership, position, or every non-selected avatar.

## Private projection and retirement

`project_installed_occupancy_retirement` accepts a validated `SavedWorld` and
the matching observed house SHA256. It bounds retained provenance before
copying and requires complete, unique source-record coverage of the World.
Every matching GUID must identify one plain saved avatar with the matching
record type; duplicates and multitile membership reject the projection.

Work occurs in a private candidate. The projection removes all matching
avatars, detaches them from containers and clears their occupied chair slots.
It removes their OBJECT relationship rows and exact inbound OBJECT targets.
Raw 32-bit target keys are compared without narrowing them to a different
runtime ID. NEIGHBOR relationships remain intact because their IDs identify
people rather than retired runtime instances. Unrelated furniture, visitors,
attributes and unrelated relationship pairs remain present.

The effective records mirror structural changes to slots, container/slot
ObjectData, positions and typed relationship links. Typed stale stack-object,
interaction and ObjectUses links are pruned. Arbitrary numeric params, locals,
temps, attributes and opaque bytes are not guessed to be object references.
The original records remain separately retained as diagnostic provenance.
The derived record offsets still describe source bytes; the records are not a
re-encoded OBJM stream.

If any retired avatar carries a surviving child object, the core requires an
`OccupancyDropHandler`. The handler must preserve that object and supply
reference-backed placement and visibility semantics. The current production
host supplies no handler and therefore rejects this case without publishing
the candidate. It does not delete the child or silently place it on a guessed
tile. General held-object placement is not implemented. The current callback
runs after all expired actors have been removed; this is also different from
the reference's per-avatar deletion/drop sequence.

## Retired IDs and fresh household construction

Every retired runtime ID becomes a `WorldLimits::reserved_entity_ids` entry.
Reservations do not enumerate as entities and cannot be allocated by ordinary
creation, runtime-object creation, saved-entity creation or snapshot
publication. Existing reservations survive removal and snapshot publication.
The launch policy is propagated through household construction, CreateObject
and temporary Go Here allocation.

These IDs cannot immediately be reused for the new residents. In the reference,
queued actions retain references to the old dead instance even when its number
is reused; a numeric native reference must not accidentally become a reference
to a different live person.

Reservations are derived again from installed-lot identity and authoritative
history on each launch, including a full-session resume. They are deliberately
not serialized in World or session wire formats. After restoring a current
checkpoint, `World::install_reserved_entity_ids` unions the exclusions and
rejects overlap with a live entity. It preserves the World address and existing
entity pointers, counters and bound VM callbacks. It cannot clear exclusions.

The host clears the old numeric selected-actor ID and resolves the selected
GUID after fresh household materialization. Each new resident runs the existing
Init/EP8 and NBRS inheritance path before portable personal state is applied.
An unexplained existing actor matching the incoming family GUID is still
rejected; retirement authority is not extended to arbitrary visitors to make
materialization succeed.

Review found that clearing the old numeric selection exposed an unchecked
dereference of the optional headless fixture actor. The host now uses that
fixture only when present; a GUID-selected launch defers runtime ID resolution
until materialization. A launch with neither selection form rejects. This fix
is included in the final binary's integration gate below.

## Execution boundary and save preflight

Retiring an installed actor invalidates the old installed saved execution for
this activation. This is an explicit native safety policy. It is not a claim
that the reference resets every object's stack when a family moves out.
Old stacks, queues, temporary banks and opaque execution suffixes remain
diagnostic evidence and must not be reactivated as continuations.

The audited House07 PhoneLine path is the existing special consumer of an
installed saved stack. After occupancy invalidation, its saved record supplies
code provenance only. The host starts fresh Main 4096 with fresh arguments and
binds a ready Sleep snapshot to the PhoneLine ID, generation, VM run generation
and current tick. At a tick-zero boundary this is a ready controller at T0,
not a resumed old wait. A subsequently saved current full checkpoint can
restore its validated PhoneLine execution and Sleep state normally.

Before charging a pending purchase, the host rejects failed household VMs,
failed preparation and unfinished object services, validates the live complete
roster, and rechecks neighborhood and house identity. It then invokes the full
live-session save preflight without publication. That preflight must encode
and validate the complete candidate, including scheduler/controller state,
before `FamilySessionJournal::commit_move_in` publishes the purchase with CAS.
An initialized World alone is insufficient evidence that the first save can
be produced. Portable carry receipts and the journal's first-destination-head
checks still apply.

## Pinned reference behavior and limits

The reference revisions are FreeSO
`7e76c1960252ddd033d8937d5edb47d326f5b69c` and Simitone
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`. References below refer to committed
sources, not the modified reference working trees.

- `TS1NeighbourProvider.cs:256-260` only clears the family's house/map entry.
  Simitone `TS1GameScreen.cs:279-285` changes finances and calls that provider;
  neither operation removes actors from the installed house.
- Normal IFF activation, `VMBlueprintRestoreCmd.cs:43-66`, loads embedded FSOV
  or OBJM, executes EP2 for loaded entities, then calls VerifyFamily.
  `VMTS1ActivatorNew.cs:500-605` loads valid saved actors without a family filter.
  `VMTS1LotState.cs:39-43` recognizes existing family members by GUID.
- The separate legacy `LocalHouse/<house>.fsov` path in Simitone
  `TS1GameScreen.cs:811-821` calls `vm.Load` followed by `vm.Reset`.
  `VM.Reset`, `VM.cs:241-254`, deletes all avatars, clears remaining objects'
  blocking/EOD state and removes reserved-tile objects. The native projection
  intentionally does not apply that all-avatar purge to unrelated visitors.
- `VMEntity.Reset:525-550` clears stack/queue state, runs EP3 only for game
  objects or pets, and restarts Main. It does not generically zero ObjectData,
  attributes, MyList, slots or relationships. BHAV effects remain significant.
- `VMEntity.Delete:1352-1431` detaches, removes short-term object references,
  removes the instance and drops carried children. `UseCount` is derived from
  current avatar queues (`VMEntity.cs:804-807,1130-1164`), not an arbitrary
  saved ObjectData counter that can safely be zeroed.

These findings support explicit structural cleanup and fresh person activation.
They do not establish original-executable relocation fidelity, general EP2/EP3
behavior or universal cleanup of object-specific attributes. The projection
does not erase arbitrary object banks to imitate unknown BHAV effects.

## Evidence and remaining work

`reports/native-occupancy-reconstruction/ctest.log` records 19 passing checks
after the final selection fix: projection, reservation policy, household
transfer/spawn, World loading, Sleep, session codecs/restorers, journal, stores,
overlay, managed relocation and entry/resume/travel CLIs. The synthetic
projection includes unrelated visitors, a later occupancy whose roster must not
authorize deletion, original-family return, typed references, raw32 keys,
held-child rejection and rollback. The synthetic rehome fixture rejects
unproven copied actors and incomplete arrival resources without charging.

Final installed-data evidence is
`reports/native-occupancy-reconstruction/real-5/result.json`, produced by:

```sh
python3 reports/native-occupancy-reconstruction/validate_real.py \
  --output reports/native-occupancy-reconstruction/real-5/result.json
```

The runner refuses to overwrite evidence. Use a new output path when repeating
it. Its 11 processes create two private adults, explicitly vacate the original
source family in test setup, buy House07 and run two ticks, reopen with zero
additional ticks, move out, buy vacant House09 at T0, reopen, move out, buy
House07 again at T0, reopen, then reject the earlier full archive as authority.
The production launcher performs all purchases of the created family.

House07's original IDs105/137 (GUIDs850010198/844195932) disappear from the
activation World. New residents use IDs180/181. All180 other original records
retain their GUID/ID, chair128's old occupied slots become empty, and the old
neighbor identity relations remain. Both transfers retain the compared personal
projection, seven needs, fractions, GUID/NID and character resource identities.
All zero-tick reopens preserve complete archive bytes and overlay. The explicit
test budget57950 becomes40725/37225/20000 after prices17225/3500/17225;
occupancy epochs are1/2/3, with the existing zero architecture-refund profile.

The host binary SHA256 is
`f7bb2e56f4258a40ca6a9e0784d11baffa6cffca22f535f76c483d3a273143e6`;
the helper SHA256 is
`5d301fb38835b042b2acdded78ce28d5bb2921c40b025bb838e9123a94ec25d1`.
Both remain stable before/after every process. All114 protected original files
and both private character files retain matching hashes. Every child was
waited and its PID was absent afterward; temporary profiles were removed.
No display was created. This is native headless evidence, not graphical,
original-game or hardware validation.

Earlier reports remain diagnostic history: REAL1 exposed missing old-resident
definitions; REAL2 exposed the catalog-directory/asset path join; REAL3 reached
the return but failed a T0 PhoneLine save after a purchase had already committed.
The ready controller and full save preflight address that failure. REAL4 passed
before the final optional-selection fix; REAL5 is the final-binary gate.

Property persistence is not implemented by this feature. A family can carry
the supported personal checkpoint, but the projection does not retain arbitrary
player changes to a vacated property's objects, building, object values or
execution as a reusable property checkpoint. It also does not establish the
original liquidation/refund rule. Held children, unsupported personal profiles
and general object reset/placement semantics remain explicit incomplete cases.

The projection and reservation policy are platform-independent C++; filesystem
hashing, overlay CAS and Linux launch coordination remain host services. There
are no new session or World wire fields. This separation does not prove memory
fit, toolchain availability, graphical operation or hardware support on the
planned console and handheld targets.
