# Family lifecycle validation

This work exercises the Simitone-derived UI with a copied game installation,
an isolated writable profile and a private Xvfb display. It does not establish
original-game equivalence, complete family management or console compatibility.
Commercial files and saves are not included in Git.

## Observed baseline

`reports/simitone-linux-evaluation/family-lifecycle-observation/` contains the
actions, captures, five process logs, profile inspections and archived save files.
The executable was the previously published `492c4f5` candidate; it did not yet
contain either correction below. `runner-at-start.py` matches the runner hash
recorded in `result.json`. The earlier discovery run stopped on a test-driver
mouse-motion wait; that driver issue was corrected before this observation.

The UI created one adult, Alex, in family ODHFlowA, reopened the family selector,
bought House09 for 3,500 and reduced the budget from 20,000 to 16,500. Alex obeyed
Go Here, moving from (136,328) to (184,312), and the action completed. An explicit
Save persisted the family, house assignment, budget and character identity.

Identity is joined through FAMI ChunkID 6, member GUID 1074646490, NBRS NID 273
and the character OBJD. FamilyNumber 10 and runtime PersistID 65537 are different
identifiers and are not used as substitutes for those keys. The saved avatar's
PersonData entries 31 and 61 contain 273 and 6 respectively.

The House09 FSOV payload hash agrees with the save diagnostics. On reopening,
family and avatar position, person data and motives were preserved. The only
save-to-deserialization state difference was the known SIMI version promotion
62 to 64. Later initialization changed 23 fields/digests in other entities and
threads; the complete initialized state is therefore not claimed identical.
The Hot Date welcome dialog also appeared again after reopening; its repetition
is recorded for later investigation.

Eviction was confirmed through the UI, followed by a process restart without
another save. The neighborhood file remained byte-identical and the family
still appeared in House09. This demonstrates the missing persistence call;
it is not merely a risk inferred from source inspection.

All five processes, the private display and temporary copied installation were
closed or removed. The protected original-file hashes remained unchanged.
See `closure-verification.json`, `after-move-save-evidence.json`,
`reopen-evidence.json` and `after-evict-before-any-save.json`.

## Corrections and bounded checks

The NBRS writer declared all entries but wrote only active neighbors. Creation
produced a file declaring 289 records with only 273 complete active records,
omitting 16 inactive markers. Its permissive reader hid this inconsistency.
The correction writes the entries, retaining known inactive markers. Eight
actual-source test groups cover this defect, identities, allocation and reload.
It does not reconstruct missing records from previously malformed files.
See [NBRS tests](../tools/simitone/save-tests/NbrsREADME.md).

An old malformed file can become 273/273 after being read and saved by the old
binary; that alone does not validate the new writer. A fresh-profile runtime
test must retain the original inactive markers to validate the correction.

## Corrected runtime

`reports/simitone-linux-evaluation/family-lifecycle-fixed/` uses a fresh copied
installation and the rebuilt candidate with both corrections. The saved source
patches, 11 runtime binaries and build log identify the tested implementation.
A new ODHFlowB/Alex family uses GUID 511304174 (different from the baseline),
FAMI ChunkID 6 and NBRS NID 273. Creation preserves all 16 inactive markers:
49 initial records become 289 records, containing 273 active neighbors. Both
creation and the later lot save decode strictly, with no incomplete-record
compatibility path.

The UI repeated the 3,500 purchase, 16,500 balance, Go Here and explicit Save.
After reopening, family/avatar identity, position and motives were retained;
21 initialization changes in other entities/threads remain separately recorded.
The Hot Date welcome dialog repeated here too.

`EvictLot` now calls `SaveNeighbourhood(false)` after changing the assignment and
before refreshing the UI. It writes the neighborhood without flushing unrelated
character resources. Four source-linked handler tests cover the old failure,
zero/positive architecture value and propagation of storage errors. These use
synthetic storage; the fresh UI restart supplies the real persistence evidence.
See [eviction tests](../tools/simitone/save-tests/EvictREADME.md).

After eviction and an immediate restart without another Save, the House09 panel
showed no family. A further fresh process listed ODHFlowB among unhoused families.
The saved FAMI contains HouseNumber 0, budget 16,500 and ValueInArch 0. The old
House09 FSOV remains unchanged. The empty-house panel now quotes 4,133 instead
of the initial 3,500; valuation fidelity remains unverified.

The selected household was then deleted. FAMI and FAMs ID 6 disappeared from
the persisted neighborhood, and a seventh fresh process no longer displayed
ODHFlowB in the family selector. NBRS NID 273, the character file and the old
House09 save remained: this proves household-record deletion, not erasure of
all related resources. All seven processes and the private Xvfb were stopped,
the temporary copy/profile removed and protected original hashes unchanged.

Following the user's subsequent direction, these fixes are a behavior checkpoint.
Further work prioritizes native shared-core migration rather than more C# game
features; see [architecture](architecture.md).

## Remaining coverage

Reusing an old lot must also check for former resident avatars in
its saved lot state; correct family binding alone is insufficient. Refund
calculation follows the existing ValueInArch field and has not been established
as equivalent to the original game. Family-record deletion does not imply
removal of every character or neighbor record.

Neighborhood, house and character saves are still separate writes rather than
one atomic transaction. Language, audio, build/buy breadth, expansion travel,
custom content and the final original-game UI remain part of the full goal.
