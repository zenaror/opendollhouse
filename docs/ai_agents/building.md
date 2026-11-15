# Building from source

Run the commands below from the clone's root directory. To use a ready-made
package, follow [installation](../installation.md); building is not required to play.

## Linux application

You need a C++17 compiler, CMake 3.16 or later, zlib and SDL2 2.0.18 or later.
The default Linux configuration also uses Pango/Cairo and system fonts to draw
text. On Debian, Ubuntu or Linux Mint:

```sh
sudo apt install build-essential cmake pkg-config zlib1g-dev libsdl2-dev libpango1.0-dev
cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/desktop --parallel 2
```

The executable is `build/desktop/engine/platform/desktop/opendollhouse`.
To open a copy of the game data with a separate development profile:

```sh
./build/desktop/engine/platform/desktop/opendollhouse \
  --data-root "/path/to/copy/The Sims" \
  --save-root "/path/to/test-profile/saves" \
  --cache-dir "/path/to/test-profile/cache" \
  --language pt-BR
```

Keep these three directories separate. The data must be an extracted Complete
Collection installation; an ISO alone is not supported. See the compiled
executable's `--help` for other options.

`Release` enables optimization. `RelWithDebInfo` adds debugging information while
keeping optimization; `Debug` is useful for investigating code, but does not
represent the expected performance of a package intended for playing.

## Useful configurations

| CMake option | Purpose |
| --- | --- |
| `-DBUILD_TESTING=ON` | Enable tests and their executables. See [testing](testing.md). |
| `-DODH_BUILD_DESKTOP=OFF` | Omit the desktop SDL application. |
| `-DODH_BUILD_LEGACY_TOOLS=ON` | Enable tools from the previous implementation, disabled by default. |
| `-DODH_SANITIZERS=ON` | Instrument the code to detect memory errors and undefined behavior with GCC/Clang. |

To build only the core, without the application or tests:

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DODH_BUILD_DESKTOP=OFF -DBUILD_TESTING=OFF
cmake --build build/core --target odh_engine --parallel 2
```

This still requires the platform's data and text dependencies. It does not prove
that windowing, audio or controls are available on another system.

## Other platforms and packages

Each target needs its own compiler and libraries. Do not link a Linux library
into a console or Windows executable.

| Target | Development instructions |
| --- | --- |
| Windows and macOS Intel/Apple Silicon | [Integrated engine and cross-compilation](../../engine/README.md) |
| Android ARM64 | [Android adapter](../../engine/platform/android/README.md) |
| PlayStation Vita | [Vita adapter](../../engine/platform/vita/README.md) |
| Nintendo Switch | [Switch adapter](../../engine/platform/switch/README.md) |
| New Nintendo 3DS | [New 3DS adapter](../../engine/platform/new3ds/README.md) |

Packaging scripts are in `engine/cmake/` and `tools/`. Read a script and its
instructions before using it; it may prepare dependencies or generate large
files. Use separate output directories for each configuration and target.
Do not edit sources during a build or while collecting a package's inputs.

Only one person or agent should own a build directory at a time. For parallel
work, isolate both sources and outputs, or wait until integration finishes.
Do not delete a build or profile that another process is still using.
