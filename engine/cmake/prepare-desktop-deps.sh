#!/usr/bin/env bash
# Build pinned dependencies with target compilers; never install into the host.
set -euo pipefail
if [[ $# -ne 1 || $1 == --help ]]; then
  printf 'Usage: engine/cmake/prepare-desktop-deps.sh windows-x86_64|macos-x86_64|macos-arm64\n'
  exit 0
fi
odh_target=$1
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
odh_deps=${ODH_DEPS_ROOT:-"$odh_tools/odh-dependencies"}
odh_prefix="$odh_deps/$odh_target"
odh_jobs=${ODH_BUILD_JOBS:-2}
case "$odh_target" in windows-x86_64|macos-x86_64|macos-arm64) ;; *) printf 'Unknown target: %s\n' "$odh_target" >&2; exit 2;; esac
mkdir -p "$odh_deps/downloads" "$odh_deps/sources" "$odh_prefix/share/licenses"
fetch() {
  local name=$1 url=$2 sha=$3
  if [[ ! -f "$odh_deps/downloads/$name" ]]; then
    curl --fail --location --show-error "$url" -o "$odh_deps/downloads/$name.part"
    mv -- "$odh_deps/downloads/$name.part" "$odh_deps/downloads/$name"
  fi
  printf '%s  %s\n' "$sha" "$odh_deps/downloads/$name" | sha256sum --check --status
}
if [[ $odh_target == windows-x86_64 ]]; then
  fetch SDL2-devel-2.30.0-mingw.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.0/SDL2-devel-2.30.0-mingw.tar.gz d44bd8cd869219f43634a898d7735d0c92d4b2cca129f709dc8b98402b36d5f3
  fetch zlib-1.3.1.tar.gz https://www.zlib.net/fossils/zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
  if [[ ! -d "$odh_deps/sources/SDL2-mingw" ]]; then
    mkdir "$odh_deps/sources/SDL2-mingw"
    tar -xzf "$odh_deps/downloads/SDL2-devel-2.30.0-mingw.tar.gz" -C "$odh_deps/sources/SDL2-mingw"
  fi
  cp -a "$odh_deps/sources/SDL2-mingw/SDL2-2.30.0/x86_64-w64-mingw32/." "$odh_prefix/"
  cp "$odh_deps/sources/SDL2-mingw/SDL2-2.30.0/COPYING.txt" "$odh_prefix/share/licenses/SDL2.txt"
  if [[ ! -d "$odh_deps/sources/zlib-1.3.1" ]]; then
    tar -xzf "$odh_deps/downloads/zlib-1.3.1.tar.gz" -C "$odh_deps/sources"
  fi
  cmake -S "$odh_deps/sources/zlib-1.3.1" -B "$odh_deps/build/zlib-$odh_target" -G Ninja \
    "-DCMAKE_TOOLCHAIN_FILE=$odh_root/engine/cmake/mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$odh_prefix" -DZLIB_BUILD_EXAMPLES=OFF
  cmake --build "$odh_deps/build/zlib-$odh_target" --parallel "$odh_jobs"
  cmake --install "$odh_deps/build/zlib-$odh_target"
  cp "$odh_deps/sources/zlib-1.3.1/LICENSE" "$odh_prefix/share/licenses/zlib.txt"
else
  fetch SDL2-2.30.0.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.0/SDL2-2.30.0.tar.gz 36e2e41557e0fa4a1519315c0f5958a87ccb27e25c51776beb6f1239526447b0
  # Linux Clang installations may omit Darwin compiler-rt. Use its official
  # availability implementation, rather than removing SDL's OS-version checks.
  fetch os_version_check-18.1.8.c https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-18.1.8/compiler-rt/lib/builtins/os_version_check.c 8ed75fa38482b18f17797c1530739688b6939b7c6495b2ffc1361114b1184c1c
  fetch compiler-rt-LICENSE-18.1.8.txt https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-18.1.8/compiler-rt/LICENSE.TXT 1a8f1058753f1ba890de984e48f0242a3a5c29a6a8f2ed9fd813f36985387e8d
  if [[ ! -d "$odh_deps/sources/SDL2-2.30.0" ]]; then
    tar -xzf "$odh_deps/downloads/SDL2-2.30.0.tar.gz" -C "$odh_deps/sources"
  fi
  if [[ -f "$odh_tools/osxcross-env.sh" ]]; then source "$odh_tools/osxcross-env.sh"; fi
  odh_osxcross=${OSXCROSS_ROOT:-"$odh_tools/osxcross"}
  odh_arch=${odh_target#macos-}
  shopt -s nullglob
  odh_launchers=("$odh_osxcross/target/bin/$odh_arch"-apple-darwin*-cmake)
  shopt -u nullglob
  if [[ ${#odh_launchers[@]} -ne 1 ]]; then printf 'Expected one OSXCross launcher for %s.\n' "$odh_arch" >&2; exit 1; fi
  odh_min=10.15
  if [[ $odh_arch == arm64 ]]; then odh_min=11.0; fi
  odh_build="$odh_deps/build/SDL2-$odh_target"
  mkdir -p "$odh_build"
  "${odh_launchers[0]%-cmake}-clang" -O2 -fvisibility=hidden "-mmacosx-version-min=$odh_min" \
    -c "$odh_deps/downloads/os_version_check-18.1.8.c" -o "$odh_build/os_version_check.o"
  "${odh_launchers[0]}" -S "$odh_deps/sources/SDL2-2.30.0" -B "$odh_deps/build/SDL2-$odh_target" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$odh_prefix" \
    "-DCMAKE_OSX_ARCHITECTURES=$odh_arch" "-DCMAKE_OSX_DEPLOYMENT_TARGET=$odh_min" \
    "-DCMAKE_SHARED_LINKER_FLAGS=\"$odh_build/os_version_check.o\"" -DCMAKE_INSTALL_NAME_DIR=@rpath \
    -DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_TESTS=OFF
  cmake --build "$odh_deps/build/SDL2-$odh_target" --parallel "$odh_jobs"
  cmake --install "$odh_deps/build/SDL2-$odh_target"
  cp "$odh_deps/sources/SDL2-2.30.0/LICENSE.txt" "$odh_prefix/share/licenses/SDL2.txt"
  cp "$odh_deps/downloads/compiler-rt-LICENSE-18.1.8.txt" "$odh_prefix/share/licenses/compiler-rt.txt"
fi
printf '\nTarget dependencies ready: %s\n' "$odh_prefix"
