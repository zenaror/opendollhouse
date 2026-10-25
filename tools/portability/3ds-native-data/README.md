# 3DS native data component probe

This probe compiles the existing `src/formats.cpp` unchanged on the host and for
3DS. A generated FAR v1 archive contains an IFF 2.5 file with one BCON chunk.
It checks directory/header metadata, two constants, the unknown trailing-byte count,
and rejection of an unsupported FAR version. It uses no commercial data.

```sh
bash tools/portability/3ds-native-data/build.sh
```

`DEVKITPRO`, `DEVKITARM` and `CXX` may select existing tools. No packages are
installed. Generated binaries and reports go under ignored `reports/portability/`.
Exceptions are enabled because the existing parser reports errors that way.

For a reproducible emulator observation, use an existing RetroArch Flatpak and
a supplied libretro core, with this script's private Xvfb/configuration paths:

```sh
python3 tools/portability/3ds-native-data/run_emulator.py \
  --core refs/tools/azahar-libretro-2126.2/azahar_libretro.so \
  --probe reports/portability/3ds-native-data/odh-data-probe.3dsx \
  --report-dir reports/portability/3ds-native-execution-new
```

The tested core is the official [Azahar 2126.2 release](https://github.com/azahar-emu/azahar/releases/tag/2126.2),
asset `azahar-libretro-linux-x86_64-2126.2.zip`, SHA-256
`2223444b74de2c922bae1efcfed60002f696b3c2aaf4fc73fc03e66a395dc26e`.
The extracted `azahar_libretro.so` SHA-256 is recorded with the run. No emulator
binary is committed. The frontend used in the observed run is RetroArch 1.22.2.

The runner requires a guest debug PASS marker, no FAIL marker, unchanged
personal configuration files, and recorded process/profile cleanup. It identifies
the owned Flatpak instance from the unique config path; it does not kill other
instances. A root `xwd -silent` capture requires visual inspection. Exit zero
alone does not approve a game or hardware port.

## Isolation limit learned from the first run

Flatpak replaces XDG directory variables. Explicit RetroArch directories,
read-only access to its personal configuration path, disabled information cache,
and before/after configuration hashes are needed. The first attempt created
`Azahar.opt` and `core_info.cache` in the real profile. Copies and hashes were
preserved and both files were removed only after exact human authorization.
The final run retained identical hashes for 147 configuration files. This is a
configuration-file check, not proof that every Flatpak cache/state path is isolated.

The final run used the pre-gate-update runner archived in its report. The current
runner additionally records its own hash and requires all three existing cleanup
flags for exit zero. Those flags were already true and were checked again against
the recorded result without another emulator run.

## Scope and ABI evidence

The host fixture passed, and the ARM guest emitted `ODH_3DS_NATIVE_DATA_V1:PASS`
and displayed PASS on its console. This proves only this native reader path.
It does not run Simitone, the managed queue, a saved household or the full game,
and it is not a physical New 3DS test. This parser is not yet called by the
current Simitone candidate; a bounded shared native data API is the next step.

The installed toolchain selects `armv6k/fpu`. The linked ELF reports an aggregate
ARMv7 attribute from a discarded libgcc barrier member; the preserved link map,
v6K input objects and final disassembly explain the discrepancy. No relevant
flags were changed to hide it. See `reports/portability/3ds-native-data/report.json`
and its ABI audit. A future toolchain change requires its own verification.
