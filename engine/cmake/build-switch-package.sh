#!/usr/bin/env bash
# The installed devkitPro SDL2 port supplies its Switch video/audio backend.
set -euo pipefail
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
export DEVKITPRO=${DEVKITPRO:-"$odh_tools/devkitpro"}
export DEVKITA64=${DEVKITA64:-"$DEVKITPRO/devkitA64"}
odh_deps=${ODH_SWITCH_DEPS:-"$DEVKITPRO/portlibs/switch"}
odh_build=${ODH_SWITCH_BUILD_ROOT:-"$odh_root/build/switch-app"}
for odh_required in "$DEVKITPRO/cmake/Switch.cmake" "$odh_deps/include/SDL2/SDL.h" "$odh_deps/lib/libSDL2.a" "$odh_deps/lib/libz.a" "$odh_deps/lib/libEGL.a" "$odh_deps/lib/libglapi.a" "$odh_deps/lib/libdrm_nouveau.a"; do
  if [[ ! -f "$odh_required" ]]; then
    printf 'Missing target dependency: %s\nInstall devkitPro packages switch-sdl2 switch-zlib (including their dependencies), or set ODH_SWITCH_DEPS to an isolated target prefix.\n' "$odh_required" >&2
    exit 2
  fi
done
mkdir -p "$odh_build"
ODH_SWITCH_BUILD_ROOT="$odh_build" "$odh_root/engine/cmake/prepare-switch-iconv.sh"
# The SDK wrapper forcibly uses its own prefix. Keep isolated dependencies local.
{
  printf '%s\n' '#!/usr/bin/env bash' 'export PKG_CONFIG_DIR=' 'export PKG_CONFIG_PATH=' 'export PKG_CONFIG_SYSROOT_DIR='
  printf 'export PKG_CONFIG_LIBDIR=%q\n' "$odh_deps/lib/pkgconfig"
  printf 'exec pkg-config --static %q "$@"\n' "--define-variable=prefix=$odh_deps"
} > "$odh_build/pkg-config"
chmod +x "$odh_build/pkg-config"
cmake -S "$odh_root/engine/platform/switch" -B "$odh_build/app" -G Ninja \
  "-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  "-DPKG_CONFIG_EXECUTABLE=$odh_build/pkg-config" "-DCMAKE_PREFIX_PATH=$odh_deps" \
  "-DODH_SWITCH_ICONV_ROOT=$odh_build/deps" \
  "-DZLIB_LIBRARY=$odh_deps/lib/libz.a" "-DZLIB_INCLUDE_DIR=$odh_deps/include"
cmake --build "$odh_build/app" --parallel "${ODH_BUILD_JOBS:-2}"
python3 "$odh_root/engine/platform/switch/create-relink-kit.py" "$odh_build" "$DEVKITPRO" "$odh_deps"
printf 'Experimental package: %s\n' "$odh_build/app/opendollhouse.nro"
