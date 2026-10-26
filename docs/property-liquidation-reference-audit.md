# Property liquidation reference audit

Original-game documentation establishes that eviction sells the household's
house and belongings and transfers the proceeds to the family. The current
created-family Simitone profile, whose unchanged ValueInArch produces a zero
refund, is therefore an incomplete reference behavior rather than an original
TS1 liquidation rule. The exact Complete Collection calculation and retained
object exceptions remain unresolved.

## Original-game manual evidence

The owning investigation reviewed the EA *The Sims Deluxe Edition* manual,
printed page 5, in the section about eviction, destruction and rebuilding.
The manual describes retention of the family's relationships and receipt of
proceeds from selling its house and belongings as part of the family's assets.
Source: [EA manual, hosted copy](https://www.manuallib.com/download/2023-10-18/The%20Sims%E2%84%A2%20Deluxe%20Edition%20Manual.pdf#page=6).

This is the root investigation's web/manual evidence, distinct from the pinned
source inspection and binary audit below. No manual wording is reproduced
verbatim here. It establishes the high-level sale/proceeds behavior; it does
not establish the precise Complete Collection/EP8 implementation, depreciation,
rounding, lot versus architecture components, special inventory handling, or
the physical disposition of every object category.

In particular, the manual does not authorize either preserving all furnishings
for the next buyer or deleting every non-person object. It also does not supply
a formula that can replace a missing ValueInArch update with the original
purchase price.

## Pinned Simitone and FreeSO implementation

The later [supplied comparison-sample audit](../reports/ghidra/property-liquidation-supplied/audit-summary.md)
adds static evidence beyond the inconclusive Legacy image: normal eviction
filters ordinary buy objects/type-2 actors while excluding a retained flag and
build-mode objects, adds cached assets once, clears that cache and serializes
the filtered lot. Neighborhood saving refreshes the cache. Its configurable
architecture multiplier defaults to approximately0.7.

These findings corroborate the manual and identify concrete omissions in the
Simitone method below. They are from a separately identified comparison sample,
not an observed pristine-original run. Exact accumulator meaning/rounding,
effective master-object classification, containment/deletion and writer failure
ordering still need verification before original-parity claims.

The inspected revisions are Simitone
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` and its FreeSO revision
`7e76c1960252ddd033d8937d5edb47d326f5b69c`. These findings come from committed
sources, not the modified reference working trees.

Simitone `Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs:279-284`
adds the existing FAMI.ValueInArch to Budget, clears ValueInArch and calls the
neighborhood provider's MoveOut. It does not sell, filter or remove objects,
rewrite the house, or save the neighborhood in that committed method.
FreeSO `TSOClient/tso.content/TS1/TS1NeighbourProvider.cs:256-260` only clears
the house assignment and occupied-house map entry.

The normal save path is separate. Simitone `TS1GameScreen.cs:959-998` calls
UpdateSIMI and `vm.Save()` and writes a house IFF containing FSOV, SIMI and
thumbnails, followed by neighborhood saving. FreeSO `VM.cs:699-744` saves all
current entities, threads, groups, globals and context for this default
`user=false` call. `VMContext.cs:1563-1570` includes architecture. Consequently,
the last saved house contains its furnishings and architecture, and the
committed eviction method leaves those saved bytes alone. That retention is
an implementation omission, not proof of the original sale policy.

The separate exporter at Simitone `TS1GameScreen.cs:890-904` writes
LocalHouse/cas.fsov; its call at line 444 is commented. The legacy LocalHouse
load/reset path must not be confused with normal house IFF saving or with
liquidation.

FreeSO `VMTS1LotState.cs:88-96` updates SIMI architecture and object values.
`VMArchitectureStats.cs:76-93` classifies current multitile prices using
BuildModeType. `SIMI.cs:23-28` computes purchase value as lot value plus object
value plus seven tenths of architecture value. These are demonstrated
purchase/statistics rules, not an eviction formula.

Repository-wide inspection found no runtime update of FAMI.ValueInArch in this
FreeSO revision. Its occurrences are the FAMI field/read/write and a read
during family asset merging. New Simitone families initialize Budget to20000
and leave ValueInArch at its default zero (`SimitoneNeighbourGenerator.cs:107-120`).
That explains the native incremental profile's observed zero refund without
validating it as the original behavior described by the manual.

`VMEntity.cs:1806-1813` declares MovementFlags.StaysAfterEvict as bit4, stored
through ObjectData[43]. The pinned repository contains no consumer of this
flag. Its name indicates a distinction that deserves investigation, but does
not prove the original executable's mask test, eligible object categories,
deletion order, or interaction with refund calculation. PlayersCanMove and
PlayersCanDelete are separate bits and cannot substitute for it.

Individual buy/delete transactions also do not establish eviction behavior.
`VMNetBuyObjectCmd.cs:150-177` charges a catalog purchase; a user deletion
credits current group price (`VMNetDeleteObjectCmd.cs:24-34`). Their existence
does not prove that original eviction loops through those same operations.
TSO per-object OwnerID assignment is guarded by `!vm.TS1`
(`VMNetBuyObjectCmd.cs:99-113`); it is not a TS1 household ownership rule.

## Supplied Legacy executable: bounded static audit

A separate static Ghidra audit imported the supplied Legacy Collection
Sims.exe into a new private project. The executable was not executed, patched
or unpacked. No old Ghidra project was opened. Local metadata and logs are in
`reports/ghidra/property-liquidation-legacy/`.

- SHA256 before and after:
  `b7c8765ed2383cbda8c047694d54a21d97895c8dae57dcc87716ca6e78b342e3`.
- PE machine0x014c; Ghidra language x86:LE:32:default; image base0x00400000.
- Entry RVA0x006d4310, VA0x00ad4310, in .bind rather than .text.
- The eviction-related ASCII strings were found at VA0x0098f914 and
  VA0x0098817c. Neither had a discovered Ghidra reference or a direct
  little-endian 32-bit address occurrence in the file.
- Raw .text entropy was approximately7.999969 bits/byte. Together with the
  .bind entry this is consistent with protected/encrypted code, an inference
  rather than a recovered protection specification.

Ghidra 12.1.3 completed the bounded import/analysis with two CPUs, a2GiB heap
and a240-second per-file analysis limit. Import success did not recover an
eviction caller, sale loop, object flag test or refund calculation. The strings
alone are not code evidence. This binary audit does not resolve the manual's
remaining implementation questions and does not justify a native formula.
No executable payload or copied decompilation is included in this document.

Settings were redirected into the audit directory. Ghidra nevertheless logged
a shared packed database cache under /var/tmp/rafael-ghidra; no subsequent
cleanup or modification of that shared cache was performed.

## Native semantics and next evidence

Same-occupancy property persistence is distinct from eviction. Retaining a
household's property mutations while its occupancy remains authoritative is
supported by the normal save path. After eviction, an immutable property
archive can remain recovery evidence without automatically becoming the next
buyer's activation state.

Until the original transformation is understood, neither universal furnishing
preservation nor blanket liquidation should be introduced as original-game
semantics. The current zero-refund profile must remain labeled partial, and
native architecture must allow a later validated sale transformation to update
property state, values and family funds atomically without double credit.

A proposed next experiment uses an isolated copy of original-game data and
new disposable saves, never production families, installed originals or shared
saves. No original-game execution for that experiment has occurred under this
audit. Before/after captures should cover:

1. A furnished untouched house and a family with a known budget.
2. One identifiable newly bought item, one item sold before eviction, and one
   moved item, with catalog/current prices recorded separately.
3. Architecture edits and build-mode objects, including explicit cases with
   and without the candidate StaysAfterEvict bit.
4. Eviction followed by inspection of FAMI budget/ValueInArch, SIMI values,
   remaining object GUIDs/flags/prices and architecture, then a different
   family's purchase of the vacant property.
5. Personal inventory, relationships, special/controller objects and saved
   attachments as separate cases rather than inferred furniture categories.

Record the exact edition, expansion level, executable hash, initial state,
actions, save hashes and typed differences. Only those observations can
establish which assets remain, how proceeds are calculated, when ValueInArch
changes and what the subsequent buyer pays. Any unsupported original runtime
or unsafe save import remains a pending gate, not a reason to test on real
player state.
