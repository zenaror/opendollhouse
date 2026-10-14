# Bounded HIT resources and offline note selection

ODH now reads explicitly supplied HOT/HSM/HIT groups and executes a bounded
subset through the first NoteOn request. The result is an offline note plan.
It does not create a sound thread, execute audio effects or advance Main past
opcode23. Actual voice selection still needs an original-semantics investigation
because the native event exposes a Jump ambiguity in the pinned reference.

## Resource contract

`read_hsm` owns ASCII-folded constants and their first source lines. First
duplicate occurrence wins, as in pinned FreeSO; duplicate rows are counted.
`read_hot` owns events, tracks, patches, hitlists, equates and TrackData. Its
dictionary rows overwrite within that file and are counted; duplicate TrackData
rejects. No cross-group/source precedence is inferred. Unresolved symbols remain
optional with reasons rather than becoming numeric zero. Symbolic hitlists keep
their raw spelling and are unsupported; numeric ranges expand within an
aggregate bound. Missing TrackData is not replaced by an HSM address guess.
HOT @symbol lookup uses ASCII folding explicitly, extending the reference's
case-sensitive lookup into its lowercased HSM map; this is not original proof.

`read_hit` retains owned bytes for HIT!/TRAX major1 minor0/8 and inspects the
first ENTP table when present. Entries require distinct track IDs, complete
records/EENT termination and addresses before the table and after the header.
Native minor8 without ENTP is supported. Other profiles reject.

Default per-resource bounds:4 MiB input,2 MiB serialized text,16384 bytes/line,
32768 rows,262144 aggregate hitlist IDs,1 MiB HIT bytes and8192 entry points.
`logical_bytes` reports serialized text extent, including line endings/BOM and
duplicates; it is not retained allocation size or RSS. Row and expansion bounds
separately constrain containers. Raw filenames preserve backslashes.

## Selection contract

`select_hit_note` takes an already resolved start PC, the owned resource group,
29 explicit object variables and a chooser returning an index into a hitlist.
It starts registers/locals at zero except register1=12, matching the consulted
FreeSO constructor. Initial SetTrack list/patch selection is observable, including
the initial choice that can later be replaced by bytecode. No hidden RNG or
BHAV RNG consumption is introduced.

Supported operations include LoadB/LoadL/Set/Test/Cmp, conditional branches,
Jump, SmartSetList/SmartChoose, GetSrcDataField, requested sequence-group kills
and the first NoteOn. Integer comparison wraps as C# int32 using defined C++
arithmetic. SmartChoose preserves flags. Field20 reads object_vars[20] directly,
not slot14/age. Patch-variable writes preserve the prior patch when a replacement
does not exist, as in the pinned reference. Branches resolve through TrackData;
untaken conditional targets are consumed but not resolved. A present ENTP mapping
must agree. Unsupported/global variable locations and unknown opcodes reject.

Sequence-group kills are recorded requests, not performed effects. Selection
stops before NoteOn's backend action or destination write. WaitSamp, End or
unsupported instructions before that point fail explicitly. Defaults bound
instructions/trace to4096, group requests to64, and choice list snapshots to
262144 IDs. Missing resources, callbacks, invalid choice indices, code-region
violations, truncations and exhausted budgets raise diagnostics. This is not a
complete HIT scheduler, mixer or audio backend.

The CLI is `odh-hit-note HSM HOT HIT EVENT [--field INDEX INT32]
[--choice INDEX]...`. Unspecified object fields are a zero-valued host fixture.
Choices are an explicit sequence, including initialization; unused choices fail.
JSON includes group counts, fixture values, choices, a diagnostic or successful
note plan/trace. `audio_submitted` is always false. Exit3 is selection failure;
exit2 is an invalid resource/CLI setup.

## Validation on 2026-10-14

Debug and ASAN/UBSAN pass63 resource checks and502 selection checks, plus the
synthetic CLI integration. Coverage includes byte/row/range/choice/trace budgets,
symbols, duplicates, line endings, malformed/truncated resources, ENTP addresses,
fields, flag preservation, wrapping comparison, patch replacement, all conditional
outcomes, untaken unresolved targets, both Jump paths and unknown instructions.
The CLI checks explicit choices, planned effects, diagnostics and unchanged inputs.
The full suite configured96 tests:95 passed and X11 skipped inside the sandbox
in both builds. Build logs are warning-free. Leak detection is disabled here.

The real selected group parses1476 HSM rows,2 duplicate names/1474 constants,
5764 HOT rows,38 unsupported records, no dictionary duplicates or unknown rows.
It resolves `hungry_sated_vox`→track3979→HSM901→HIT13699, and the previously
validated list1291 and its five patch filenames. HIT has13836 bytes, minor8,
no ENTP. Counts alone do not prove full event support; the unsupported records
and the runtime diagnostic remain explicit.

## Native Jump divergence

In the native low branch at HIT PC13759, bytes `0d 03 01 00 00` are a Jump
followed by four bytes that encode259 (`0x103`) when read as little-endian uint32.
HOT maps259 to generic subroutine PC36. However, pinned FreeSO's Jump handler
at `tso.sound/HITInterpreter.cs:193–210` chooses the operand interpretation from
its first byte. Values<=15 become a register ID. It therefore reads register3,
initially zero; missing TrackData0 becomes zero in that reference.

ODH's explicit bounded reference profile rejects the missing mapping instead
of executing header bytes. With field20=30, field0=0 and initial choice index0,
both builds produce:

`HIT selection at PC 13759 (cursor 13761): missing HOT TrackData branch ID 0`

Initialization selected list901 index0/patch3372. No later choice occurred and
no NoteOn was planned. Native JSON outputs are byte-identical. There is no
automatic literal fallback or modified native bytecode. This shows a limitation
of the consulted interpretation, not proof that original TS1 has a broken event.
Register initialization, operand rules and original field meanings require
verification against the original runtime/binary before adopting a workaround.

Primary references are local FreeSO revision
`4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:
`tso.files/HIT/{Hot,HSM,HITFile,Patch,Hitlist}.cs`,
`tso.sound/{HITInterpreter,HITThread,HITVM}.cs`. Resource and C++ test evidence
is not a comparison with executing that C# runtime or the original game.

The original HOT/HSM/HIT hashes match the preceding checkpoint. Ignored evidence:
`reports/hit-native-{debug,asan}.json`, `reports/hit-input-identities.json`,
`reports/hit-full-*.log` and build logs. The user's reported original executable
Sims.exe.bak was inventoried read-only: PE32 Intel80386,9 sections,4422663 bytes,
SHA256 `32857c25e00594a9bc5d700f7b373b6df59ab76ec51b36aa1d30f3dd973eff58`.
Sims.exe is a distinct7-section3043328-byte binary, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Neither was imported into Ghidra or executed in this unit. File naming/operator
report is not independent release provenance.

Follow-up: [the dedicated binary investigation](hit-jump-binary-validation.md)
finds a four-byte literal Jump operand in the distinct supplied `Sims.exe`.
An explicitly selected probe mode now reaches the first native-resource NoteOn;
other semantics remain the FreeSO profile and `.bak` equivalence is unproven.
The findings above describe the default heuristic mode and its preserved gate.

Next: resolve the native field interpretation using a dedicated original
binary investigation; connect sound-thread execution and the verified XA codec
to the real Linux backend. Saved frame/queue/clock continuation and the interactive
Linux lot remain required; no saved avatar is reinitialized to bypass those gates.
