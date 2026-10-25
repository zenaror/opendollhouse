# 3DS native wake queue fixture

This fixture compiles `native/simulation/src/odh_simulation.cpp` unchanged into
an ARM11 ELF and 3DSX. The same fixture runs on the host first. It covers live
insertion, nested current-list replacement, tick-zero migration, reset aliases,
capacity rejection, unsigned tick-zero scheduling, and recreation of scheduling
state after a new queue is created. All identities and IDs are synthetic.

```sh
bash tools/portability/3ds-native-wake/build.sh
```

Outputs go to `reports/portability/3ds-native-wake/`. The 3DS binary emits
`ODH_3DS_NATIVE_WAKE_V1:PASS` or `ODH_3DS_NATIVE_WAKE_V1:FAIL` through
`svcOutputDebugString`. Host execution and cross compilation alone do not prove
the binary ran on a 3DS or emulator.
