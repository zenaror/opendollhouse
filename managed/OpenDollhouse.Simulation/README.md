# Shared scheduling component

This is the first data-only component extracted from the running Simitone
candidate. The real FreeSO scheduler calls `WakeQueue<T>`; this is not an unused
interface or another game engine. The adapter still owns entity execution,
thread wake metadata, presentation idle frames, deferred deletion and RNG changes.

The library targets .NET Standard 2.0 and C# 7.3 and has no explicit external
package references. The SDK restores the standard reference package. It uses
ordinary collections and a supplied ID selector, retaining entity references so
that deleting an entity and reusing its numeric ID does not substitute another
instance. Returned lists are live, matching insertion during the execution loop.

This boundary does **not** provide a CLR for any console. A clean dependency
inventory is not a runtime or hardware test. See [the platform decision](../../docs/portability.md).

From the repository root, the differential tests compile both the pinned
original scheduler and the actual adapted source with synthetic VM types:

```sh
DOTNET_ROLL_FORWARD=Major dotnet run \
  --project managed/OpenDollhouse.Simulation.Tests/OpenDollhouse.Simulation.Tests.csproj \
  -c Release
```

The roll-forward setting is for the test host when it has .NET 10 but no .NET 9
runtime. It does not change the library's target or establish console support.
The main game is separately published and exercised with isolated data.

## Provenance and license

`WakeQueue.cs` derives scheduling and sorted-insertion behavior from FreeSO
revision `7e76c1960252ddd033d8937d5edb47d326f5b69c`, specifically
`TSOClient/tso.simantics/Engine/VMScheduler.cs` and `VM.cs: AddToObjList`.
It is covered by MPL-2.0; the complete upstream license is preserved in
[LICENSE.FreeSO.md](../LICENSE.FreeSO.md).

The test `BaselineVMScheduler.cs` retains the same original scheduler algorithm,
with its namespace renamed and an import added for the synthetic test types.
The pinned source and [upstream project](https://github.com/riperiperi/FreeSO)
identify its origin. The tests prove equivalence to that scheduler, not to every
behavior of the original The Sims executable.
