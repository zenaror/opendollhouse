# NBRS writer verification

Run from the repository root:

```sh
dotnet run --project tools/simitone/save-tests/NbrsTests.csproj -c Release
```

This standalone .NET 10 test project compiles the actual working NBRS, IoBuffer,
IoWriter and Endian source. Only unrelated IFF container types are stubbed. The
original pre-fix NBRS source is preserved in `NbrsOriginal.cs`, with only its
namespace changed and its SHA-256 recorded. Licensing is recorded in
`managed/LICENSE.FreeSO.md`.

The old writer declares `Entries.Count` but serializes only indexed active
neighbors. Sixteen marker-only placeholder slots therefore produce a header
sixteen records larger than its payload. The existing reader stops at EOF and
conceals that inconsistency. The corrected writer iterates `Entries`, writes
inactive slots as a single marker and leaves active-record serialization intact.

Eight synthetic source-linked groups cover the original defect, empty and
placeholder-only records, active-only baseline output, field/relationship/ID and
runtime-index preservation, first-free-ID allocation, repeated add/save/reload,
the 289-slot/273-active/16-placeholder shape, and re-save of an already malformed
legacy payload. In the latter case the reader only knows 273 records: the writer
emits those 273 coherently and does not invent the missing slots. Existing NIDs
are preserved and the next free NID remains 274.

The production change is limited to `NBRS.Write`. `Read`, `AddNeighbor`,
`GetFreeID`, character creation and existing profiles are not changed. This gate
does not prove UI creation, full-profile save atomicity or native-game equivalence.
No commercial fixture data is included or rewritten by these tests.

The placeholder tests cover the demonstrated marker `0` records. They do not
establish new behavior for arbitrary positive markers other than active marker `1`.

## Experimental patch replay

`tools/simitone/experimental-saved-code-recovery.patch` now contains 23 FreeSO
paths, adding only the NBRS writer delta to the previous 22-path patch. Apply it
to FreeSO `7e76c1960252ddd033d8937d5edb47d326f5b69c` **after**, in order:

1. `tools/simitone/0003-vm-diagnostics.patch`
2. `tools/simitone/0004-saved-frame-resolution-audit.patch`
3. `tools/simitone/0005-native-code-signature.patch`

The refreshed experimental patch SHA-256 is
`b465e061fab545dceb65e5ea430d33123b833ac3c949a6a45e76a4d92d8ce64c`.
Replay against a full temporary archive of that base matched all 28 files touched
by the complete sequence to the current working source, byte for byte. The
commands, per-file hashes and retained replay path are recorded in
`reports/simitone-linux-evaluation/nbrs-patch-replay/report.json`.
This is source replay evidence; the active UI runtime was not rebuilt or changed.
