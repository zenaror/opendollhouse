# Native temporary state on ARM

Run `bash tools/portability/3ds-native-state/build.sh` from the checkout.
The build runs the shared native state cases on the host and compiles the same
state implementation, Expression executor and cases for ARMv6K. Outputs are
under `reports/portability/3ds-native-state/`.

The guest emits `ODH_3DS_NATIVE_STATE_V1:PASS` or `:FAIL`. Run the existing
isolated `3ds-native-data/run_emulator.py` with this exact marker prefix and a
new report directory. A build alone does not prove execution.

This checks ownership, aliases, replacement, snapshots and direct Expression
access to synthetic TEMP/TempXL state. It does not run the full game, render a
lot or prove hardware compatibility. The final target remains the complete
native game without a managed runtime.
