# Native Expression on ARM

Run `bash tools/portability/3ds-native-expression/build.sh` from the checkout.
The build runs the shared native Expression cases on the host, then compiles
the same executor and cases for ARM11/ARMv6K. Outputs are under
`reports/portability/3ds-native-expression/`.

The 3DS program reports `ODH_3DS_NATIVE_EXPRESSION_V1:PASS` or `:FAIL` through
the guest debug service. Use the existing isolated emulator runner with this
exact marker prefix; compile success alone does not prove guest execution.

This fixture tests the migrated Expression executor with synthetic memory.
It does not run the rest of the VM, load a commercial save, render the game or
establish hardware compatibility. The full native game remains the target.
