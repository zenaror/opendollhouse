# Synthetic native recovery policy regression tests

Run from the OpenDollhouse checkout:

```sh
dotnet run --project tools/simitone/recovery-tests/RecoveryTests.csproj -c Release
dotnet run --project tools/simitone/recovery-tests/person-state/PersonStateTests.csproj -c Release
```

The harness compiles the **actual working** `VMTS1ObjectRecovery.cs` through
`Compile Include`. It does not copy the recovery algorithm. Override
`SimitoneSourceRoot` to point at another Simitone checkout. The default SDK is
.NET 10; a .NET 9 SDK may use `-p:TargetFramework=net9.0`.

These are synthetic policy tests. Explicit doubles in `Stubs.cs` provide the
adapter's entity, queue, category, callback and scheduler boundary. They execute
no BHAV, use no commercial files, and initialize no graphics/audio/content
providers. Normal `VMContext` construction immediately resolves the global game
resource and its TTAB/TTAs; using the compiled engine without data would require
separate content-fixture infrastructure. This harness is not evidence that the
real engine's placement, callbacks or gameplay work.

Expected behavior comes from the supplied-profile native exports documented in
`docs/simitone-save-code-signatures.md`: 72 mutable words, nine retained fields,
identity/version, separate EP3 temporary-state policy, notifications before the
reset snapshot, and group-wide detach/restart/Init/placement ordering. Tests also
exercise category-index consistency, callback changes after EP10, contained-child
links, shared group price, mutation-free preflight and fail-closed rejection.
The engine integration gate remains separate.

`person-state/` is a separate project that compiles the actual
`VMTS1SavedPersonRecoveryState.cs` against the published **real `FSO.Files.dll`**.
It uses synthetic `OBJMPerson` values of those real format types, without engine
stubs. It checks immutable copies, ordered ObjectUses boundaries and flags,
every saved interaction field and Args, and the native target/icon selection
rules. The native active-icon pointer comparison does not check TTA validity;
an invalid TTA therefore does not suppress an icon match. LastInteraction,
queued interactions and arbitrary stack objects do not select full reset.
These tests do not parse a commercial save or execute person recovery. Override
`SimitoneRuntimeRoot` to use another published FSO.Files assembly.


Run the five snapshot cases separately:

```sh
dotnet run --project tools/simitone/recovery-tests/person-state/PersonStateTests.csproj -c Release
```
