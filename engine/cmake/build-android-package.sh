#!/usr/bin/env bash
# Produce a local debug-signed ARM64 APK from installed SDK/NDK and SDL sources.
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  echo 'Usage: engine/cmake/build-android-package.sh'
  echo 'Requires Android SDK/NDK, JDK, CMake, Ninja, zip, official SDL2 source.'
  echo 'Overrides: ODH_TOOLS_ROOT, ANDROID_HOME, ANDROID_NDK_HOME, ODH_SDL_SOURCE, ODH_ANDROID_BUILD_ROOT, ODH_BUILD_JOBS.'
  exit 0
fi
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
if [[ -z ${ANDROID_NDK_HOME:-} && -f "$odh_tools/android-env.sh" ]]; then source "$odh_tools/android-env.sh"; fi
odh_sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
odh_ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
odh_sdl=${ODH_SDL_SOURCE:-"$odh_tools/odh-dependencies/sources/SDL2-2.30.0"}
odh_iconv=${ODH_ANDROID_ICONV_ROOT:-"${ODH_DEPS_ROOT:-$odh_tools/odh-dependencies}/android-arm64/iconv"}
odh_build=${ODH_ANDROID_BUILD_ROOT:-"$odh_root/build/android-arm64-app"}
[[ -f "$odh_ndk/build/cmake/android.toolchain.cmake" && -d "$odh_sdl/android-project" ]] || { echo 'Set Android NDK and official SDL2 sources.' >&2; exit 1; }
odh_bt=$(find "$odh_sdk/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1)
odh_platform=$(find "$odh_sdk/platforms" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1)
[[ -f "$odh_platform/android.jar" && -x "$odh_bt/d8" ]] || { echo 'Install SDK platform and build-tools.' >&2; exit 1; }
for odh_command in cmake ninja javac jar keytool zip; do command -v "$odh_command" >/dev/null; done
mkdir -p "$odh_build"
odh_build=$(cd -- "$odh_build" && pwd)
odh_source_manifest() {
  python3 - "$odh_root" "$odh_iconv" <<'PYMANIFEST'
import hashlib,json,pathlib,sys
root=pathlib.Path(sys.argv[1]); files=[]
for directory in ('engine','src','include','tools'):
 files += [p for p in (root/directory).rglob('*') if p.is_file() and not any(part in {'bin','obj','__pycache__','CMakeFiles','.git','node_modules'} for part in p.relative_to(root).parts) and p.suffix in ('.c','.cpp','.h','.hpp','.inl','.inc','.cmake','.txt','.java','.xml','.sh')]
files += [root/'CMakeLists.txt',root/'src/pie_menu.cpp']
result={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(files))}
dep=pathlib.Path(sys.argv[2])
for name in ('include/iconv.h','lib/libiconv.so','share/source/libiconv-1.18.tar.gz','share/source/prepare-android-iconv.sh'):
 result['dependency/android-iconv/'+name]=hashlib.sha256((dep/name).read_bytes()).hexdigest()
print(json.dumps(result,sort_keys=True,indent=2))
PYMANIFEST
}
if [[ ! -f "$odh_iconv/lib/libiconv.so" || ! -f "$odh_iconv/include/iconv.h" ]]; then
  ODH_ANDROID_ICONV_ROOT="$odh_iconv" "$odh_root/engine/cmake/prepare-android-iconv.sh"
fi
odh_source_manifest > "$odh_build/source-inputs-before.json"
mkdir -p "$odh_build/classes" "$odh_build/dex" "$odh_build/apk/lib/arm64-v8a" "$odh_build/apk/assets/licenses"
cmake -S "$odh_root/engine/platform/android" -B "$odh_build/native" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$odh_ndk/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release -DODH_SDL_SOURCE="$odh_sdl" -DODH_ANDROID_ICONV_ROOT="$odh_iconv"
cmake --build "$odh_build/native" --target main --parallel "${ODH_BUILD_JOBS:-2}"
cp "$odh_iconv/lib/libiconv.so" "$odh_build/apk/lib/arm64-v8a/"
cp "$odh_build/native/libmain.so" "$odh_build/apk/lib/arm64-v8a/"
cp "$odh_build/native/SDL2/libSDL2.so" "$odh_build/apk/lib/arm64-v8a/"
"$odh_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-unneeded "$odh_build/apk/lib/arm64-v8a/"*.so
mapfile -t odh_java < <(find "$odh_sdl/android-project/app/src/main/java" "$odh_root/engine/platform/android/java" -name '*.java' -type f | sort)
javac -encoding UTF-8 -source 8 -target 8 -bootclasspath "$odh_platform/android.jar:$odh_bt/core-lambda-stubs.jar" -d "$odh_build/classes" "${odh_java[@]}"
jar cf "$odh_build/classes.jar" -C "$odh_build/classes" .
"$odh_bt/d8" --lib "$odh_platform/android.jar" --min-api 26 --output "$odh_build/dex" "$odh_build/classes.jar"
"$odh_bt/aapt2" link -I "$odh_platform/android.jar" --manifest "$odh_root/engine/platform/android/AndroidManifest.xml" -o "$odh_build/unsigned.apk"
cp "$odh_build/dex/classes.dex" "$odh_build/apk/"
cp "$odh_root/engine/sims3ds/LICENSE" "$odh_build/apk/assets/licenses/Sims3DS-MPL-2.0.txt"
cp "$odh_root/engine/sims3ds/CREDITS.md" "$odh_build/apk/assets/licenses/Sims3DS-CREDITS.md"
cp "$odh_iconv/share/licenses/"* "$odh_build/apk/assets/licenses/"
# Supply corresponding LGPL source and build recipe alongside the debug APK.
cp "$odh_iconv/share/source/"* "$odh_build/"
cp "$odh_sdl/LICENSE.txt" "$odh_build/apk/assets/licenses/SDL2.txt"
# libc++/compiler runtime are linked statically from the NDK.
cp "$odh_ndk/NOTICE" "$odh_build/apk/assets/licenses/Android-NDK.txt"
cp "$odh_ndk/NOTICE.toolchain" "$odh_build/apk/assets/licenses/Android-NDK-toolchain.txt"
cp "$odh_root/engine/sims3ds/LICENSES/"* "$odh_build/apk/assets/licenses/"
cp "$odh_root/engine/sims3ds/src/third_party/minimp3/LICENSE" "$odh_build/apk/assets/licenses/minimp3.txt"
(cd "$odh_build/apk" && zip -q -r "$odh_build/unsigned.apk" .)
"$odh_bt/zipalign" -P 16 -f 4 "$odh_build/unsigned.apk" "$odh_build/aligned.apk"
if [[ ! -f "$odh_build/debug.keystore" ]]; then
  keytool -genkeypair -keystore "$odh_build/debug.keystore" -storepass android -keypass android -alias androiddebugkey -dname 'CN=Android Debug,O=Android,C=US' -keyalg RSA -keysize 2048 -validity 10000
fi
"$odh_bt/apksigner" sign --ks "$odh_build/debug.keystore" --ks-pass pass:android --key-pass pass:android --out "$odh_build/OpenDollhouse-arm64-debug.apk" "$odh_build/aligned.apk"
"$odh_bt/apksigner" verify --verbose "$odh_build/OpenDollhouse-arm64-debug.apk"
"$odh_bt/zipalign" -c -P 16 4 "$odh_build/OpenDollhouse-arm64-debug.apk"
odh_source_manifest > "$odh_build/source-inputs-after.json"
if ! cmp -s "$odh_build/source-inputs-before.json" "$odh_build/source-inputs-after.json"; then
  echo 'Source files changed during build; run again incrementally before accepting this APK.' >&2
  exit 1
fi
sha256sum "$odh_build/OpenDollhouse-arm64-debug.apk" > "$odh_build/SHA256SUMS"
printf '\nDebug APK (hardware execution not verified): %s\n' "$odh_build/OpenDollhouse-arm64-debug.apk"
