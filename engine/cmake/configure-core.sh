#!/usr/bin/env bash
# Configure the shared engine only. A target compiler is not an application port.
set -euo pipefail
usage() {
  cat <<'HELP'
Usage: engine/cmake/configure-core.sh TARGET [additional CMake options...]
Targets: windows-x86_64, macos-x86_64, macos-arm64, android-arm64

Build directories: build/cross/TARGET (override with ODH_CROSS_BUILD_ROOT).
SDK root: $HOME/Tools (override with ODH_TOOLS_ROOT).
Windows dependencies: -DODH_TARGET_DEPS=/path/to/windows/prefix
This configures odh_engine only; run the printed build command afterwards.
HELP
}
if [[ ${1:-} == --help || ${1:-} == -h ]]; then usage; exit 0; fi
if [[ $# -lt 1 ]]; then usage >&2; exit 2; fi
odh_target=$1
shift
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
odh_build=${ODH_CROSS_BUILD_ROOT:-"$odh_root/build/cross"}/$odh_target
odh_command=(cmake)
odh_options=()
case "$odh_target" in
  windows-x86_64)
    odh_options+=("-DCMAKE_TOOLCHAIN_FILE=$odh_root/engine/cmake/mingw64.cmake")
    ;;
  macos-x86_64|macos-arm64)
    if [[ -f "$odh_tools/osxcross-env.sh" ]]; then source "$odh_tools/osxcross-env.sh"; fi
    odh_osxcross=${OSXCROSS_ROOT:-"$odh_tools/osxcross"}
    odh_arch=${odh_target#macos-}
    shopt -s nullglob
    odh_launchers=("$odh_osxcross/target/bin/$odh_arch"-apple-darwin*-cmake)
    shopt -u nullglob
    if [[ ${#odh_launchers[@]} -ne 1 ]]; then
      printf 'Expected one OSXCross CMake launcher for %s under %s/target/bin.\n' "$odh_arch" "$odh_osxcross" >&2
      exit 1
    fi
    odh_command=("${odh_launchers[0]}")
    odh_options+=("-DCMAKE_OSX_ARCHITECTURES=$odh_arch")
    ;;
  android-arm64)
    if [[ -z ${ANDROID_NDK_HOME:-} && -f "$odh_tools/android-env.sh" ]]; then source "$odh_tools/android-env.sh"; fi
    odh_ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
    if [[ ! -f "$odh_ndk/build/cmake/android.toolchain.cmake" ]]; then
      printf 'Set ANDROID_NDK_HOME to an installed Android NDK.\n' >&2
      exit 1
    fi
    odh_options+=("-DCMAKE_TOOLCHAIN_FILE=$odh_ndk/build/cmake/android.toolchain.cmake"
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
    ;;
  *) printf 'Unknown target: %s\n' "$odh_target" >&2; usage >&2; exit 2 ;;
esac
"${odh_command[@]}" -S "$odh_root" -B "$odh_build" -G Ninja \
  "${odh_options[@]}" -DCMAKE_BUILD_TYPE=Release \
  -DODH_BUILD_DESKTOP=OFF -DODH_BUILD_LEGACY_TOOLS=OFF "$@"
printf '\nConfigured the shared engine, not a playable package. Build it with:\n'
printf 'cmake --build %q --target odh_engine --parallel 2\n' "$odh_build"
