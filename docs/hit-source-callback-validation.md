# HIT source callback: supplied binary evidence and explicit lookup

The distinct supplied `Sims.exe` registers a source/field callback for HIT.
ODH now supports an explicit source-field reader in offline selection. This
separates source-dependent queries from the existing Simitone ObjectVar profile;
it does not claim original-runtime equivalence or implement native sound effects.

## Binary identity and registered call path

Input: external `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`, PE32/x86,
image base `0x00400000`. Original-reported `Sims.exe.bak` remains a distinct
self-modifying input; equivalence and its post-transformation image are unproven.
Both remain immutable and neither Windows executable has been run.

Ghidra12.1.3 read-only exports were checked against objdump/raw file bytes:

| Step | VA |
|---|---|
| registration pushes callback address | `0x005717a5`: callback `0x0057127b` |
| registration calls setter | `0x005717aa` -> `0x0057c0d8` |
| setter stores function pointer | `0x0057c0dc`: instance+`0x100` |
| opcode72 reads source/field register values | `0x0057f608` / `0x0057f615` |
| engine getter | `0x0057f854`: global pointer at `0x006f6a78` |
| wrapper invokes registered function | `0x0057c0f3`: instance+`0x100` |
| callback invokes source-field provider | `0x00571294` -> `0x00570fb5` |

At callback `0x0057127b`, field>=21 returns false without writing output.
Lower signed values pass to the provider; negative values then take its unsigned
switch default and return zero. Successful results are written through the
third argument's output pointer and true is returned. Registration bytes at
VA `0x005717a5` are `68 7b 12 57 00 e8 29 a9 00 00`.
In this PE's `.text`, file offsets and RVAs equal VA minus image base; e.g.
registration file/RVA `0x001717a5`, callback `0x0017127b`, provider `0x00170fb5`.

The provider resolves its source argument using `0x00407498`, then dynamic-casts
the result from RTTI `cXObject` to `cXPerson`. It does not discard the source ID.
It accesses the person's signed16-bit bank based at instance+`0x58c`:
the serialization path `0x0045d099` passes that address to `0x00464973`, and
indexed bank accesses include `0x0042152e` (`base+index*2`). Index names below
are reference interpretations, while the offsets/arithmetic are static evidence.

## Inspected field rules and differences from Simitone

| Field | Supplied executable path |
|---|---|
| 0 | person word74 (offset`0x620`) bits`0x04`/`0x08` ->1000; otherwise word58 (age reference) <12 ->2, else raw word65 |
| 1 | person word58 |
| 2 / 3 / 5 / 6 / 7 / 11 / 12 | person words10 / 9 / 15 / 11 / 12 / 17 / 18 divided by10 |
| 4 | signed word at global-derived pointer+`0x10`; semantic clock context pending |
| 13 | object word at offset`0x84` selects another object/context through `0x004670a4`, then `0x004681ec`; default100 if unavailable |
| 14 | float-bank index3 via `0x0045d283`, multiplied by float constant0.1, then `__ftol` |
| 15–20 | person words2–7 divided by10 |
| 8–10 / invalid switch values | zero |

Person-dependent cases return zero when the dynamic cast fails. Field20 reads
offset`0x59a` at VA `0x00571181` and reaches signed `IDIV10` at `0x00571061`:
this matches raw person word7/10, named NeatPersonality in the reference. It is
not the age word58 or the field14 path in this binary. The flag bits used by
field0 are recorded raw; their original meanings have not been independently
identified from those instructions.

Simitone's pinned FreeSO submodule `f86bbd34b2112296ed786741f640a0535886d180`
uses `VMAvatar.SubmitHITVars` and `HITInterpreter.GetSrcDataField` instead:
child predicate age<18 with age0 exempted, skills capped at100 after division,
field1/3/6/12 initially zero, field14 assigned age, and source value ignored
when indexing ObjectVar. Some cases agree, including field20's word7/10; others
have different predicates, populated fields or implementation paths.

For field14, objdump at VA `0x0045d287` confirms a float read from
instance+`0x78c`+index*4; the provider passes index3, multiplies by raw float bits
`0x3dcccccd` at VA `0x00669420`, and calls `0x0063604c`. Float-bank meanings,
conversion/control-word details and equivalence to ODH's adapted motive bank
remain pending. No age-based native field14 rule is inferred.

## ODH explicit source reader

`select_hit_note` accepts an optional `HitSourceFieldReader(source, field)`.
Both are exact signed int32 values read from the bytecode's variables. Its
optional result distinguishes valid zero from unsupported/missing data.
Missing results fail with source/field diagnostic; there is no fallback to
ObjectVar or zero. Invalid destination/variable operands fail before querying
the provider. Callback invocation count is bounded by the instruction budget;
no callback or result is retained beyond the plan's value trace.

Without a reader, the established29-element reference profile is unchanged.
With a reader, the provider defines valid field IDs, allowing World-bound and
variable-length object sources instead of imposing the29-field array bound.
This API supplies the missing integration point; a complete provider for the
binary's field4/13/14 contexts and register initialization is not yet implemented.
Other flags, patch effects, group effects and selection rules remain the bounded
reference profile unless separately verified. The literal Jump option remains
explicit; changing field lookup does not select it implicitly.

CLI `--field-source INT32` binds only explicitly supplied `--field` values to
that source register value. A missing field or another source fails. This is
a strict fixture binding, not an object ID remap or a native provider emulation.
JSON identifies `offline_HIT_selection_with_explicit_source_fields`, records
`field_source`, and adds each GetSrcDataField's `source` to its trace. Legacy
mode reports field_source=null. Both modes keep audio_submitted=false.

## Checks and real resource fixture

Selection passes822 synthetic checks (previously774). New checks cover zero,
positive and signed-minimum sources; negative/large/above-array field IDs;
exact callback arguments, zero/sign flags, array replacement, missing-result
failure, invalid destination without callback and bounded callback cycles.
CLI checks cover missing/repeated/overflow options, missing fields, wrong
source binding and explicit valid zero. Full Debug and ASAN/UBSAN suites each
configure96 tests:95 pass, one X11 test skips in the sandbox. No compiler
warnings; LSan disabled, with no leak-validation claim.

```sh
./build/odh-hit-note '../referencias/The Sims/SoundData/SimsGeneratedHitSource.hsm' \
  '../referencias/The Sims/SoundData/SimsGeneratedHitSource.hot' \
  '../referencias/The Sims/SoundData/simsgeneratedhitsource.hit' hungry_sated_vox \
  --field 20 30 --field 0 0 --field-source 12 \
  --choice 0 --choice 0 --jump-operand literal32
```

The native-resource fixture reads `(source12,field20)->30` and
`(source12,field0)->0`, selecting patch3212 at NoteOnPC81 after19 instructions.
Binding the same values to source1 fails at the first field query; it cannot
silently use the values from another origin. Source12 is the reference probe's
initialized register value, not proof of the original thread's caller identity.
Debug/ASAN JSON matches byte-for-byte, SHA256
`383c29649acc854f8e4ee56348099f0c8772d2de7ab658a07eca95fe547ce968`.
No audio/thread/Main continuation is claimed. Native resource identities remain
those recorded in `sound-source-variable-validation.md`.

Ignored evidence: an archived validation record (not distributed), build/full-test
logs; Ghidra `source-callback`, `source-registration`, `installed-source`,
`source-field-provider`, `person-bank-access` logs and selected function exports,
plus `supplied-objdump.txt`. No commercial code exports are published.

Next: initialize source registers from actual thread arguments, implement the
verified source fields with explicit context for4/13/14, resolve sequence-group
operand/effects, and build the sound-thread lifecycle/WaitSamp/XA Linux backend.
Simitone remains the primary integration reference, with these binary differences
tracked explicitly. Saved thread/clock and interactive lot integration remain
required; the goal is not completed by this offline fixture.
