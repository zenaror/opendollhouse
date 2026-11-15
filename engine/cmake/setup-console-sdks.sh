#!/usr/bin/env bash
# Prepare/check devkitPro dependencies without building the game.
set -euo pipefail
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
export DEVKITPRO=${DEVKITPRO:-"${ODH_TOOLS_ROOT:-$HOME/Tools}/devkitpro"}
export DEVKITA64=${DEVKITA64:-"$DEVKITPRO/devkitA64"}
export DEVKITPPC=${DEVKITPPC:-"$DEVKITPRO/devkitPPC"}
odh_pacman=${ODH_DKP_PACMAN:-dkp-pacman}
odh_packages=(devkitA64 libnx switch-tools switch-cmake switch-pkg-config
  devkitPPC wut wut-tools wiiu-cmake wiiu-pkg-config
  switch-sdl2 switch-zlib wiiu-sdl2 ppc-zlib)
case ${1:-check} in
  plan)
    "$odh_pacman" -Sp --needed --print-format '%n %v %l' "${odh_packages[@]}"
    ;;
  install)
    # Privilege acquisition is the caller's responsibility. Do not run sudo here.
    if (( EUID != 0 )); then
      printf '%s\n' 'Installation requires an administrator. Review the plan first, then run this command as root with DEVKITPRO pointing to the existing SDK.' >&2
      exit 2
    fi
    # No full upgrade, reinstall, repository reset or unrelated platform changes.
    exec "$odh_pacman" -S --needed "${odh_packages[@]}"
    ;;
  check)
    odh_missing=0
    for odh_package in "${odh_packages[@]}"; do
      "$odh_pacman" -Q "$odh_package" || odh_missing=1
    done
    exit "$odh_missing"
    ;;
  wiiu-sdl2-source)
    # Optional fallback when the binary repository is unavailable. Match the
    # official recipe: https://github.com/devkitPro/wut-packages/tree/master/SDL2
    # Only this target library is built; the SDK and global pacman DB are untouched.
    odh_deps=${ODH_CONSOLE_DEPS_ROOT:-"${ODH_TOOLS_ROOT:-$HOME/Tools}/opendollhouse-console-deps"}
    odh_source="$odh_deps/sources/SDL2-2.32.10"
    odh_prefix="$odh_deps/wiiu"
    if [[ -d $odh_prefix && ! -f $odh_prefix/.odh-source-sdl2 && ! -f $odh_prefix/.odh-source-zlib ]]; then
      printf 'Refusing to modify unrecognized prefix: %s\n' "$odh_prefix" >&2; exit 2
    fi
    mkdir -p "$odh_deps/downloads"
    odh_archive="$odh_deps/downloads/SDL2-2.32.10.tar.gz"
    odh_patch="$odh_deps/downloads/SDL2-2.32.10.patch"
    [[ -f $odh_archive ]] || curl -fL --max-time 120 https://libsdl.org/release/SDL2-2.32.10.tar.gz -o "$odh_archive"
    [[ -f $odh_patch ]] || curl -fL --max-time 120 https://raw.githubusercontent.com/devkitPro/wut-packages/23c32012859d3c08761875296ae26546b89be1ca/SDL2/SDL2-2.32.10.patch -o "$odh_patch"
    printf '%s  %s\n' \
      5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 "$odh_archive" \
      fe9d639eb1aaad1dd4b148b43946e32154d1b6f2d35839d3b5a5afab660f979c "$odh_patch" | sha256sum -c -
    if [[ ! -d $odh_source ]]; then
      mkdir -p "$odh_deps/sources"
      tar -xzf "$odh_archive" -C "$odh_deps/sources"
      patch -d "$odh_source" -p1 < "$odh_patch"
      touch "$odh_source/.odh-patched"
    fi
    [[ -f $odh_source/.odh-patched ]] || { printf '%s\n' 'Source directory lacks the patch marker.' >&2; exit 2; }
    cmake -S "$odh_source" -B "$odh_deps/build-sdl2-wiiu" -G Ninja \
      "-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake" \
      "-DCMAKE_INSTALL_PREFIX=$odh_prefix" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$odh_deps/build-sdl2-wiiu" --parallel "${ODH_BUILD_JOBS:-2}"
    mkdir -p "$odh_prefix"
    touch "$odh_prefix/.odh-source-sdl2"
    cmake --install "$odh_deps/build-sdl2-wiiu"
    printf 'Wii U SDL2 prefix: %s\nUse target pkg-config metadata only; never fill missing libraries with host libraries.\n' "$odh_prefix"
    ;;
  probe)
    # Sources and artifacts are isolated; this never configures the product.
    odh_out=${ODH_CONSOLE_PROBE_ROOT:-"$odh_root/build/diagnostics/switch-wiiu-sdk-setup/probe"}
    mkdir -p "$odh_out/switch-src" "$odh_out/wiiu-src"
    cat > "$odh_out/switch-src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.13)
project(console_sdk_probe C)
add_executable(switch_sdk_probe main.c)
nx_create_nro(switch_sdk_probe)
if(ODH_SWITCH_SDL_PREFIX)
  enable_language(CXX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ODH_PROBE_SDL REQUIRED IMPORTED_TARGET sdl2)
  add_executable(switch_sdl_probe sdl-main.c)
  target_include_directories(switch_sdl_probe PRIVATE "${ODH_SWITCH_SDL_PREFIX}/include")
  target_link_libraries(switch_sdl_probe PRIVATE PkgConfig::ODH_PROBE_SDL "${ODH_SWITCH_SDL_PREFIX}/lib/libz.a")
  set_target_properties(switch_sdl_probe PROPERTIES LINKER_LANGUAGE CXX)
  nx_create_nro(switch_sdl_probe)
endif()
EOF
    cat > "$odh_out/switch-src/main.c" <<'EOF'
#include <switch.h>
#include <stdio.h>
#include <sys/stat.h>
int main(void) {
    consoleInit(NULL);
    /* libnx initializes sdmc for normal homebrew startup. Read-only probe. */
    struct stat info;
    printf("SD directory: %s\n", stat("sdmc:/switch", &info) == 0 ? "found" : "unavailable");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
EOF
    cat > "$odh_out/wiiu-src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.13)
project(console_sdk_probe C CXX)
add_executable(wiiu_sdk_probe main.c)
wut_create_rpx(wiiu_sdk_probe)
wut_create_wuhb(wiiu_sdk_probe NAME "Console SDK probe" AUTHOR "OpenDollhouse contributors")
if(ODH_WIIU_SDL_PREFIX)
  # Use the exact target archive, never the host's package search path.
  add_executable(wiiu_sdl_probe sdl-main.c)
  target_include_directories(wiiu_sdl_probe PRIVATE "${ODH_WIIU_SDL_PREFIX}/include/SDL2" "${ODH_WIIU_SDL_PREFIX}/include")
  target_link_libraries(wiiu_sdl_probe PRIVATE "${ODH_WIIU_SDL_PREFIX}/lib/libSDL2.a" "${ODH_WIIU_SDL_PREFIX}/lib/libz.a")
  set_target_properties(wiiu_sdl_probe PROPERTIES LINKER_LANGUAGE CXX)
  wut_create_rpx(wiiu_sdl_probe)
  wut_create_wuhb(wiiu_sdl_probe NAME "SDL2 SDK probe" AUTHOR "OpenDollhouse contributors")
endif()
EOF
    cat > "$odh_out/wiiu-src/sdl-main.c" <<'EOF'
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <zlib.h>
int main(void) {
    SDL_SetMainReady();
    int result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    SDL_Quit();
    return result == 0 && zlibVersion()[0] ? 0 : 1;
}
EOF
    cat > "$odh_out/wiiu-src/main.c" <<'EOF'
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/sdcard.h>
#include <sys/stat.h>
int main(void) {
    WHBProcInit();
    WHBLogConsoleInit();
    if (WHBMountSdCard()) {
        const char *path = WHBGetSdCardMountPath();
        struct stat info;
        WHBLogPrintf("SD mount %s: %s", path, stat(path, &info) == 0 ? "found" : "unavailable");
        WHBUnmountSdCard();
    } else WHBLogPrintf("SD unavailable");
    while (WHBProcIsRunning()) WHBLogConsoleDraw();
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
EOF
    cp "$odh_out/wiiu-src/sdl-main.c" "$odh_out/switch-src/sdl-main.c"
    for odh_platform in switch wiiu; do
      if [[ $odh_platform == switch ]]; then odh_toolchain=Switch; else odh_toolchain=WiiU; fi
      odh_extra=()
      if [[ $odh_platform == wiiu && -n ${ODH_WIIU_DEPS:-} ]]; then
        odh_extra+=("-DODH_WIIU_SDL_PREFIX=$ODH_WIIU_DEPS")
      fi
      if [[ $odh_platform == switch && -n ${ODH_SWITCH_DEPS:-} ]]; then
        {
          printf '%s\n' '#!/usr/bin/env bash' 'export PKG_CONFIG_DIR=' 'export PKG_CONFIG_PATH=' 'export PKG_CONFIG_SYSROOT_DIR='
          printf 'export PKG_CONFIG_LIBDIR=%q\n' "$ODH_SWITCH_DEPS/lib/pkgconfig"
          printf 'exec pkg-config --static %q "$@"\n' "--define-variable=prefix=$ODH_SWITCH_DEPS"
        } > "$odh_out/pkg-config-switch"
        chmod +x "$odh_out/pkg-config-switch"
        odh_extra+=("-DODH_SWITCH_SDL_PREFIX=$ODH_SWITCH_DEPS" "-DPKG_CONFIG_EXECUTABLE=$odh_out/pkg-config-switch")
      fi
      cmake -S "$odh_out/$odh_platform-src" -B "$odh_out/$odh_platform-build" \
        -U '*ODH_PROBE_SDL*' \
        -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/$odh_toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release "${odh_extra[@]}"
      cmake --build "$odh_out/$odh_platform-build" --parallel 2
    done
    printf '%s\n' 'Packaging probes passed. These artifacts have not been run on hardware or in an emulator; SDL, game behavior and SD access remain untested at runtime.'
    ;;
  *) printf 'Usage: %s {check|plan|install|probe|wiiu-sdl2-source}\n' "$0" >&2; exit 2 ;;
esac
