# Experimental PS Vita adapter

This adapter builds the same C++ engine and SDL application for a single screen.
It does not include commercial data. Building a VPK does not establish device
execution, memory use or performance.

## Building on Linux

You need VitaSDK, CMake, Ninja and the official sources for SDL2 2.30.0,
zlib 1.3.1 and GNU libiconv 1.18. The script looks for the SDK in
`~/Tools/vitasdk` and sources in `~/Tools/odh-dependencies/sources`. Set `VITASDK`,
`ODH_SDL_SOURCE` and `ODH_ZLIB_SOURCE` to use other locations.

```sh
engine/cmake/build-vita-package.sh
```

The script builds dependencies in the isolated `build/vita-app/deps` directory;
it does not install files into the SDK. The expected output is
`build/vita-app/app/OpenDollhouse.vpk`. License notices accompany the VPK. The
script also produces `build/vita-app/OpenDollhouse-Vita-relink.tar.gz`; distribute
that kit alongside the VPK because the text-conversion library is statically
linked under the LGPL. This is not a public release and still needs package,
emulator and device validation.

## Data and controls

The adapter looks for the copied installation at `ux0:/data/OpenDollhouse/game`,
with `GameData`, `UserData` and expansions at the same level. Saves and cache use
the sibling `saves` and `cache` directories, outside the original game data.

Presentation reuses the 800×600 PC canvas while preserving its aspect ratio on
the display. SDL supplies front touch and controller input. The left half of the
rear touchpad acts as L2 (next Sim and center); the right half acts as R2 (hold
a cursor click). Physical L/R rotate. Sticks move the camera and cursor. See the
[complete mapping](../../../docs/controls.md). Computer input tests do not
establish device behavior; touch, half orientation, controls, suspension,
resume and performance need physical retesting. Do not treat compilation as
parity with Linux.

## Text conversion and relinking

Some VitaSDK installations have the `iconv.h` header without the library that
implements conversion. The preparer builds **only the library** GNU libiconv
1.18, using pinned source and hash, in `build/vita-app/deps`. It does not change
the SDK or shared codec, and does not include the command-line utility:

```sh
engine/cmake/prepare-vita-iconv.sh
```

The official [libiconv 1.18 source](https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz)
and its recipe are placed in `deps/share/source`; notices are in
`deps/share/licenses`. The package uses GNU `libiconv_*` symbols, and CMake
receives that header and `libiconv.a` explicitly rather than accepting only the
SDK's incomplete header.

When building the VPK, the script creates a build-specific kit with application
objects, required static files, library sources and the actual link/executable
creation commands. The entire SDK is not copied. This lets you replace the
library and regenerate the package using your VitaSDK:

```sh
# After extracting the kit and replacing deps/lib/libiconv.a:
VITASDK="/path/to/vitasdk" python3 relink.py
```

The output is `app/OpenDollhouse-modified.vpk`. To rebuild the library, see the
kit's `README.txt`. Preserve the kit and sources when sharing the VPK;
providing only the executable does not preserve this option.

`tests/iconv` builds the real codec and links a small executable with the Vita
toolchain and `--no-undefined`. That link test does not run conversions on the
device or approve the interface or complete package.
