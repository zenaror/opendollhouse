# Integrated engine

This directory contains the C++ engine used by OpenDollhouse. The code in
`sims3ds/src` was incorporated from [Sims3DS](https://github.com/Ryan3DSMods/Sims3DS),
revision `1bb47534a1f27667d5517b2c06c783988bd5ee67`. It includes data loading,
simulation, characters, the world, audio and application components being
adapted for each platform.

On PC, the application uses **one screen**, with the original The Sims interface
and controls. Dual-screen presentation is exclusive to the 3DS. Sharing the
engine does not mean copying the handheld's screen layout.

## Origin and licenses

Copyright and license notices remain in the files. See the
[MPL 2.0 license](sims3ds/LICENSE),
[original credits](sims3ds/CREDITS.md),
[Inter font license](sims3ds/LICENSES/OFL-1.1-Inter.txt) and
[minimp3 license](sims3ds/src/third_party/minimp3/LICENSE).
The translated hashing code also retains the [xxHashSharp BSD notice](sims3ds/LICENSES/BSD-2-Clause-xxHashSharp.txt),
recovered from the [upstream repository](https://github.com/noricube/xxHashSharp/blob/master/LICENSE).
Sims3DS derives from FreeSims and FreeSO; credits for those origins are also
preserved. Links and descriptions in the original credits refer to the upstream
repository, whose complete contents are not redistributed here.

`sims3ds/UPSTREAM.json` records the commit and hash of each file when it was
incorporated. Those hashes identify its origin; files adapted by OpenDollhouse
may differ. They do not cover commercial data, builds, internal reports or
Sims3DS distribution artwork.

## Building

From the project root, CMake builds the `odh_engine` library using C++17 and zlib.
The desktop application uses SDL2. To build only the library:

```sh
cmake -S . -B build/engine -DODH_BUILD_DESKTOP=OFF
cmake --build build/engine --parallel 2
```

Tools from the previous implementation remain in their directories and can be
enabled with `-DODH_BUILD_LEGACY_TOOLS=ON`. They support investigation and
comparison; they are not a second product engine.

`sims3ds/Makefile.port` remains a build reference for the original tools. Its
diagnostic programs are not the OpenDollhouse desktop application. The 3DS
adapter is in `sims3ds/src/platform/3ds`; packaging that target requires its own
resources and SDKs.

## Android application

The ARM64 adapter reuses the engine and SDL2 presentation on a single screen.
See [building the APK and supplying game data](platform/android/README.md).
An earlier build opened on a device. Updates still require retesting; building
the library or APK does not establish Android playability.

## Console applications

Build and game-data instructions are in the
[PlayStation Vita](platform/vita/README.md) and [Nintendo Switch](platform/switch/README.md)
adapters. Each needs libraries for its own target. A generated VPK or NRO still
needs runtime testing to check controls, memory and device services.

## Preparing the same library for other systems

The configurator below uses each target's compilers in separate directories.
It does not install SDKs, download dependencies or perform the build itself:

```sh
engine/cmake/configure-core.sh windows-x86_64
engine/cmake/configure-core.sh macos-x86_64
engine/cmake/configure-core.sh macos-arm64
engine/cmake/configure-core.sh android-arm64
```

Run only the command for your chosen target. It prints the command to build
`odh_engine` when configuration finishes. By default, it looks for environments
under `$HOME/Tools` and writes output to `build/cross/<target>`; set
`ODH_TOOLS_ROOT` and `ODH_CROSS_BUILD_ROOT` to use other directories.

| Target | Required environment | Engine dependencies |
| --- | --- | --- |
| Windows x86-64 | MinGW-w64 with POSIX threads and CMake/Ninja | zlib built for Windows; the Linux library cannot be used |
| macOS Intel and Apple Silicon | OSXCross and a macOS SDK obtained by the developer | zlib and libc++ from the SDK; each architecture has its own build |
| Android ARM64 | Android NDK selected by `ANDROID_NDK_HOME` or `android-env.sh` | zlib from the NDK; configuration uses API 26, `arm64-v8a` and code suitable for a shared library |

For Windows, if zlib is in a separate prefix from MinGW, specify it:

```sh
engine/cmake/configure-core.sh windows-x86_64 \
  -DODH_TARGET_DEPS=/path/to/windows-dependencies
```

That prefix must contain `include/zlib.h` and the Windows library in `lib/`.
For macOS, the configurator uses OSXCross's own CMake launcher; it does not
replace system compilers. For Android, it uses the NDK's official CMake file.
These configurations disable the desktop application and previous tools.

## Desktop packages built on Linux

With MinGW or OSXCross already configured, the commands below prepare target
dependencies and build the same SDL2 application. Choose `windows-x86_64`,
`macos-x86_64` or `macos-arm64`:

```sh
engine/cmake/prepare-desktop-deps.sh windows-x86_64
engine/cmake/build-desktop-package.sh windows-x86_64
```

The first script downloads official release files and checks pinned hashes:
[SDL2 2.30.0](https://github.com/libsdl-org/SDL/releases/tag/release-2.30.0),
[zlib 1.3.1](https://www.zlib.net/fossils/) for Windows, and the API availability
component from [LLVM compiler-rt 18.1.8](https://github.com/llvm/llvm-project/tree/llvmorg-18.1.8/compiler-rt)
for macOS. The latter provides real system-version checks missing from some
Linux Clang installations. No Apple SDK is downloaded.

Dependencies are placed in `$HOME/Tools/odh-dependencies/<target>`; set
`ODH_DEPS_ROOT` to use another location. `ODH_BUILD_JOBS` controls parallelism
(default: 2). macOS zlib comes from the SDK. SDL2 is dynamically linked; target
libraries and their licenses accompany the package. The Windows script expects
the MinGW compilers and license notices supplied by Debian/Ubuntu.

The package is written to `build/cross/<target>/OpenDollhouse-<target>.tar.gz`.
Extract it and keep `bin`, `lib` (when present) and `share` together.
`SOURCE-INPUTS.json` records compiled sources and `MANIFEST.json` records
distributed files. If sources change during compilation, run the command again;
the script refuses to package that inconsistent build.

On Windows, DLLs sit beside `bin/opendollhouse.exe`. macOS packages contain a
terminal executable and SDL2, without signing, notarization or an `.app`
installer. They target macOS 10.15 (Intel) and 11.0 (Apple Silicon) as build
minimums; those minimums do not certify compatibility. Supply separate data,
save and cache directories as described in the [installation guide](../docs/installation.md).
No commercial data is included.

Building and inspecting package dependencies does not establish that the game
runs on the target system. Windows/macOS runtime validation is a separate step.

## Consoles and other devices

**A compiled library is not yet an available game for that platform.**
Windowing, input, audio, storage, packaging and execution must be integrated and
checked for each target. The New Nintendo 3DS adapter has its own build path
using devkitARM/libctru; Vita and Switch have adapters under `platform/`, with
their own testing limits. Do not infer support from the presence of a compiler.

To build the New Nintendo 3DS development executable with devkitPro already
configured:

```sh
make -C engine/sims3ds/src/platform/3ds --jobs=2
```

`DEVKITPRO`, `DEVKITARM` and the SDK tools must be available in the environment.
This path produces the `.3dsx`; it does not automatically prepare a CIA
installer or establish that it runs on a console. Upstream distribution artwork
is not included; the Makefile uses the default libctru icon when needed.
