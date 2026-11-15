# Experimental Nintendo Switch adapter

This adapter reuses the engine and SDL application on a single screen. A complete
game NRO was built, and the companion kit regenerated it from objects, including
from a directory containing spaces. A tester reported reaching the neighborhood
with an earlier delivery, but without touch or controls. The new input and
continuous audio need device retesting; builds and computer tests do not
establish Switch performance or playability. The package does not include
commercial files, firmware or console keys.

## Building on Linux

Install devkitA64, libnx, switch-tools and the devkitPro `switch-sdl2` and
`switch-zlib` packages, including their video dependencies. Standard Linux SDL2
does not contain the Switch video adapter. Use the port supplied by devkitPro.

```sh
bash engine/cmake/build-switch-package.sh
```

The script uses `DEVKITPRO` or `~/Tools/devkitpro` and checks target libraries
before configuring. For an isolated prefix already containing Switch SDL2, zlib,
Mesa and libdrm, set `ODH_SWITCH_DEPS`. Do not fill these dependencies with Linux
libraries. The script also prepares static GNU libiconv from verified source in
a build-private prefix; SDK headers alone do not replace it. The output is
`build/switch-app/app/opendollhouse.nro`, accompanied by
`build/switch-app/OpenDollhouse-Switch-relink.tar.gz`.

Resolve repository download failures before building; the script does not
replace missing packages with host libraries. Set `ODH_SWITCH_BUILD_ROOT` to
choose another build directory. Prepared SDK and libraries are reused;
libiconv remains in the private build directory.

### Touch fix for SDL2 2.28.5

This version of the devkitPro port sends a different identifier when a finger
moves. This can lose a drag or let another finger take over the selection. The
[SDL2-2.28.5-touch-finger-id.patch](patches/SDL2-2.28.5-touch-finger-id.patch)
changes only that identifier, preserving press and release events. It is an
OpenDollhouse change to the SDL port, not an official SDL change.

Apply it to a copy of the SDL2 2.28.5 sources already prepared by devkitPro and
build that library into a separate prefix. Point `ODH_SWITCH_DEPS` at that prefix
when building the game. Preserve license notices and distribute the patch with
the sources. Do not apply the fix automatically to another SDL version.

## Distribution and relinking

Keep the NRO together with the companion kit, corresponding sources and license
notices. The kit contains application objects, static libraries and the libiconv
source/recipe, allowing replacement without rebuilding the game. Instructions
are in the kit's `README.txt`; with compatible devkitPro tools, run `relink.py`
after replacing `deps/lib/libiconv.a`.

Relinking was checked from an extracted directory and an alternate SDK path
containing spaces. The physical SDK remained at a path without spaces; that
check does not cover an SDK physically installed in a directory containing
spaces. The regenerated NRO preserved code and data, differing only in build
identification fields. Full byte equality or device execution is not claimed.

## Data and controls

The copied installation belongs in `sdmc:/switch/OpenDollhouse/game`. Saves and
cache use the sibling `saves` and `cache` directories. The application does not
write to the commercial installation. When launched with arguments, it accepts
desktop options, including `--language` for a single session. The normal launch
offers a first-run language chooser and remembers the choice in
`saves/language.txt`. Hold L at startup to choose again.

The screen preserves the original PC layout's proportions. The adapter accepts
touch and controller input, displaying the original cursor when using the
stick. See the [platform status](../../../docs/status.md#other-platforms)
for its limits. In the SDL2 2.28.5 version used here, disconnecting a controller
does not correctly report button releases: release the trigger before
disconnecting. This limitation remains open. Docked mode, suspension, resume
and physical operation require testing.
