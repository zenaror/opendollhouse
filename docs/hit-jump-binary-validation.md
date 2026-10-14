# HIT Jump: distinct supplied binary and explicit operand probe

This checkpoint resolves the low-byte ambiguity for the separately supplied
`Sims.exe` by static x86 evidence. It does **not** establish equivalence with
the operator-reported original `Sims.exe.bak`, execute either Windows binary,
or validate original audio playback. The default bounded FreeSO selection
profile remains available without an automatic fallback.

## Input identity and original limitation

Both inputs remain outside the checkout under `../referencias/The Sims`.
Hashes were rechecked on 2026-10-14. Both are PE32 Intel80386, image base
`0x00400000`. Their version resources report Complete Collection1.91 and fixed
file version1.9.1.0; matching resources do not prove matching implementations.

| Input | Bytes | SHA256 |
|---|---:|---|
| `Sims.exe.bak` | 4422663 | `32857c25e00594a9bc5d700f7b373b6df59ab76ec51b36aa1d30f3dd973eff58` |
| `Sims.exe` | 3043328 | `6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2` |

The `.bak` entry point is file offset `0x002ee06e`, RVA `0x0031406e`,
VA `0x0071406e`, in section `stxt371`. At VAs `0x0071409e` and
`0x007140a1`, x86 instructions write opcode `E9` and its displacement into
that entry point. `objdump` independently confirms those instructions.
This is static self-modification evidence; the protection/transformation
algorithm and post-transformation image have not been established.

Its raw `.text` entropy is7.999388 bits/byte; only10598/2506752 aligned bytes
match the other executable's `.text`, with no identical4KiB blocks. These
metrics alone do not identify an algorithm. Ghidra's original import completed
with3237 analyzed functions and678 defined strings, but many invalid pcode /
out-of-image references. Its automatic pseudocode is not an original HIT oracle.

The distinct `.exe` has7 sections, entry file offset/RVA `0x00238274`,
VA `0x00638274`, `.text` entropy6.677538,18988 analyzed functions and4486
defined strings. These counts are analysis inventory, not semantic coverage.

## Supplied executable dispatch evidence

Ghidra12.1.3 PUBLIC, PE loader, `x86:LE:32:default`, Windows compiler profile,
Java21.0.12.1. Imports used separate projects,180-second analysis budgets and
two CPU workers; both completed. Subsequent exports used `-readOnly -noanalysis`.

The `Undefined Hit List` string at VA `0x006cf884` references instruction
`0x0057f1d8` in function `0x0057e3dc`. That function reads an opcode through
the pointer at instance offset `0x19c`. Instructions `0x0057e3fc`–`0x0057e408`
subtract1 from the opcode and index the dispatch table at VA `0x0057f708`.

For opcode13, table slot VA `0x0057f738` contains bytes `a7 e6 57 00`, pointing
to VA `0x0057e6a7`. Its RVA and file offset are both `0x0017f738` in this PE;
this equality was checked against its section headers, not assumed generally.

The handler at VA `0x0057e6a7` (RVA/file offset `0x0017e6a7`) has bytes
`8b 08 83 c0 04 89 07 51 8b 0d 78 6a 6f 00 e8 ef e9 ff ff`.
It reads a32-bit operand from the cursor, advances that cursor by4, and passes
the value to function `0x0057d0a9`. It does not inspect the low byte or read a
register for this operand. The returned value becomes the new cursor at
`0x0057f132`. Ghidra pseudocode was checked against the instructions and raw
file bytes, including the dispatch slot.

Function `0x0057d0a9` forms an ID/value pair with value initially zero and calls
`0x00582457` on instance offset `0x168`, then returns the pair's value at +4.
The map's complete population and missing-ID behavior remain to be verified;
ODH still requires an explicit HOT TrackData mapping and a bounded code address.
Function `0x0057b268` references EventMappingEquate, Patch, Track, TrackData and
GlobalHitList sections. Cross-file precedence and source-field meanings remain
pending. A pointer run at `0x006d9e10` was rejected as the HIT opcode table:
its users index records in strides of five pointers, unlike this dispatch.

## Explicit ODH probe and validation

`HitJumpOperand::Literal32` and CLI `--jump-operand literal32` change only
opcode13's operand interpretation. The remaining semantics are the bounded
FreeSO reference profile, including supplied29-element object variables,
explicit choice indices, initial registers, patch side effects and flags.
The JSON identifies this combination as
`offline_FreeSO_HIT_selection_with_literal32_Jump`; it is not a native-runtime
execution claim. `free-so` remains the default. Missing mappings, unsupported
instructions, code bounds and instruction budgets still fail explicitly.

Synthetic tests cover all256 possible low bytes with an unrelated register3,
all four truncated operand lengths, missing/header destinations, cyclic budget
exhaustion and invalid enum/CLI selections. Existing heuristic collision tests
continue to reject the ambiguous literal without fallback. Selection now passes
774 checks; resource tests retain63 checks. Debug and ASAN/UBSAN each configure
96 tests:95 pass, with the X11 backend test skipped in the sandbox.
Builds emit no compiler warnings. Leak detection is disabled for ASAN in this
environment; this does not validate leaks.

With the real generated HOT/HSM/HIT resources, this explicit fixture:

```sh
./build/odh-hit-note '../referencias/The Sims/SoundData/SimsGeneratedHitSource.hsm' \
  '../referencias/The Sims/SoundData/SimsGeneratedHitSource.hot' \
  '../referencias/The Sims/SoundData/simsgeneratedhitsource.hit' hungry_sated_vox \
  --field 20 30 --field 0 0 --choice 0 --choice 0 --jump-operand literal32
```

selects initial list901 index0/patch3372, jumps from PC13759 via ID259 to PC36,
then selects list1291 index0/patch3212. It stops before NoteOn at PC81 after19
instructions, destination5, requesting sequence-group kill1 without applying it.
Filename: `sounddata\voice\mood\vox_comfort_sighm4.xa`.
Debug/ASAN output is byte-identical (SHA256
`764aa29733da0f9c26ce42e072a825700a155e78086dee93f36ddee591ff6877`).
`audio_submitted` is false. This connects
the native resource chain to a first-note plan and the previously decoded XA;
it does not resume Main, run a sound thread, or play a sample.

## Reproduction and retained artifacts

Projects/logs, inventories, PE comparison and exported instructions/pseudocode
remain ignored under `reports/ghidra/`. No commercial binaries or full exported
functions are versioned. ODH-authored scripts under `tools/ghidra/` export only
selected analyzed functions or address references without editing the program.
Function exports limit references to256, instructions to8192 and decompilation
to30 seconds per function; truncation/failure is reported in the output.

The standard launcher tried to save Java preferences outside this project's
writable scope. The direct documented launcher main works with task-local
absolute settings/cache directories, without changing global Ghidra settings:

```sh
odh_root="$(pwd)"
ghidra_home=/home/rafael/Tools/Open-GBP/ghidra_12.1.3_PUBLIC
java -Xmx2G -Djava.system.class.loader=ghidra.GhidraClassLoader \
  -Djava.awt.headless=true -Dapplication.settingsdir="$odh_root/reports/ghidra/settings" \
  -Dapplication.cachedir="$odh_root/reports/ghidra/cache" -Djava.io.tmpdir=/tmp \
  --enable-native-access=ALL-UNNAMED \
  -cp "$ghidra_home/Ghidra/Framework/Utility/lib/Utility.jar" \
  ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless \
  reports/ghidra/projects ODH_TS1_Supplied -process Sims.exe -readOnly -noanalysis \
  -scriptPath tools/ghidra -postScript ODHFunctions.java \
  reports/ghidra/supplied-functions 0057e3dc 0057d0a9
```

This requires the existing dedicated analyzed project. To reproduce its import
in a new, distinctly named project, replace process/readOnly/noanalysis/script
arguments with `-import '../referencias/The Sims/Sims.exe' -processor
x86:LE:32:default -loader PeLoader -analysisTimeoutPerFile 180 -max-cpu 2`.
Do not overwrite either existing project or import both inputs under one name.

Next: verify map population, register/source-field semantics and remaining
sound instructions; establish an original post-transformation oracle before
claiming original equivalence. Linux sound-thread/backend integration, saved
thread/clock continuation and a continuously interactive lot remain pending.
