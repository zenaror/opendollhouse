#!/usr/bin/env bash
# Build dependencies in a project-specific prefix; never modify the SDK.
set -euo pipefail
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
export VITASDK=${VITASDK:-"$odh_tools/vitasdk"}
odh_source=${ODH_DEPENDENCY_SOURCES:-"$odh_tools/odh-dependencies/sources"}
odh_sdl=${ODH_SDL_SOURCE:-"$odh_source/SDL2-2.30.0"}
odh_zlib=${ODH_ZLIB_SOURCE:-"$odh_source/zlib-1.3.1"}
odh_build=${ODH_VITA_BUILD_ROOT:-"$odh_root/build/vita-app"}
mkdir -p "$odh_build"
odh_build=$(cd -- "$odh_build" && pwd)
odh_prefix="$odh_build/deps"
for odh_required in "$VITASDK/share/vita.toolchain.cmake" "$odh_sdl/CMakeLists.txt" "$odh_zlib/CMakeLists.txt"; do
  [[ -f "$odh_required" ]] || { printf 'Missing dependency: %s\n' "$odh_required" >&2; exit 2; }
done
odh_common=(-G Ninja "-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$odh_prefix")
cmake -S "$odh_zlib" -B "$odh_build/zlib" "${odh_common[@]}" -DZLIB_BUILD_EXAMPLES=OFF
cmake --build "$odh_build/zlib" --target install --parallel "${ODH_BUILD_JOBS:-2}"
cmake -S "$odh_sdl" -B "$odh_build/SDL2" "${odh_common[@]}" -DVITA=ON -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF
cmake --build "$odh_build/SDL2" --target install --parallel "${ODH_BUILD_JOBS:-2}"
ODH_VITA_BUILD_ROOT="$odh_build" "$odh_root/engine/cmake/prepare-vita-iconv.sh"
cmake -S "$odh_root/engine/platform/vita" -B "$odh_build/app" "${odh_common[@]}" "-DCMAKE_PREFIX_PATH=$odh_prefix" "-DSDL2_DIR=$odh_prefix/lib/cmake/SDL2" "-DZLIB_ROOT=$odh_prefix" "-DODH_SDL_SOURCE=$odh_sdl" "-DODH_ZLIB_SOURCE=$odh_zlib" "-DODH_VITA_ICONV_ROOT=$odh_prefix"
cmake --build "$odh_build/app" --parallel "${ODH_BUILD_JOBS:-2}"
python3 "$odh_root/engine/platform/vita/create-relink-kit.py" "$odh_build" "$VITASDK"
printf 'Experimental package: %s\n' "$odh_build/app/OpenDollhouse.vpk"
