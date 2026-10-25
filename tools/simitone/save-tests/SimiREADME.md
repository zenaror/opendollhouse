# SIMI serializer regression tests

```sh
dotnet run --project tools/simitone/save-tests/SimiTests.csproj -c Release
```

This compiles the actual working `SIMI.cs` against the published `FSO.Files.dll`
for its real chunk and IO types. Source types intentionally win over the DLL's
older SIMI types (`CS0436` is suppressed). No serializer algorithm is copied.
Set `SimitoneSourceRoot` and `SimitoneRuntimeRoot` for other checkouts/builds.

Synthetic records exercise input versions0,0x3e,0x3f,0x40,0x45, all38 runtime
globals (including word37), distinct budget income/expense values, exact stream
alignment with an adjacent sentinel, no mutation and deterministic rewrites.
The reader establishes a 32-word wire layout through0x3f and a 64-word layout
from0x40 onward. Writing at least0x40 preserves all38 runtime words; higher input
versions retain their version under that currently supported shared layout.
The source Version field and original arrays remain untouched.

These tests validate the SIMI payload only. They do not establish full household
save/reopen behavior, commercial-save compatibility or original-game parity.


`SimiBaseline.json` records the intentionally failing run against the original
writer (0/7 passed). `SimiVerification.json` records the fixed writer (7/7
passed), including the source/test/published IO-library hashes and actual output.
The baseline is historical evidence, not a failing test invoked by this project.
The projects select their entry points explicitly; run them sequentially because
they share this folder's build cache.

The pure state-comparison checks run independently:

```sh
python3 -m unittest discover -s tools/simitone/save-tests -p test_compare_save_states.py
```
