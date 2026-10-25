# TS1 Expression differential fixture

`BaselineVMExpression.cs` copies the original `VMExpression.Execute` body from
the working FreeSO source before the native dispatch edit (original SHA-256
`5e038fe761fc56f20b964e89aef9c9df7f194e1cbc1c43e94da00014117a73ef`).
Only the class name was changed, and the operand definitions are taken from
the production source linked into this project. The copied code is MPL-2.0;
see `../LICENSE.FreeSO.md`.

`SimanticsStubs.cs` supplies a synthetic VM/memory boundary. It records read
and write ordering, allows rejected writes and callback failures, and provides
level and list data. It does **not** validate the full `VMMemory` scope
implementation or whole-game behavior; production calls the actual
`VMMemory.GetBigVariable` and `SetBigVariable` through the bridge.

Run with the freshly built, unversioned native library for the host:

```sh
DOTNET_ROLL_FORWARD=Major dotnet run \
  --project managed/OpenDollhouse.Simulation.Expression.Tests \
  -c Release \
  -p:ODHNativeSimulationLibrary=/absolute/path/to/libodh_simulation.so
```

The roll-forward setting is needed only when the host lacks the project's
`net9.0` runtime but has a compatible newer runtime. The fixture covers all
21 TS1 opcodes, 32-bit and signed edge cases, aliasing, rejected writes,
callback errors and order, the lazy Room/1024 branch, reentrancy, concurrent
threads, the retained TSO list branch, and warmed allocation behavior.
