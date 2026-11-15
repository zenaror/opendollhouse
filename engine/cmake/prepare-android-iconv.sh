#!/usr/bin/env bash
# Build only the LGPL conversion library, never the GPL command-line utility.
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  echo 'Usage: engine/cmake/prepare-android-iconv.sh'
  echo 'Builds GNU libiconv 1.18 shared library for Android ARM64/API26.'
  echo 'Overrides: ODH_TOOLS_ROOT, ODH_DEPS_ROOT, ANDROID_NDK_HOME, ODH_ANDROID_ICONV_ROOT, ODH_BUILD_JOBS.'
  exit 0
fi
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
if [[ -z ${ANDROID_NDK_HOME:-} && -f "$odh_tools/android-env.sh" ]]; then source "$odh_tools/android-env.sh"; fi
odh_ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
odh_deps=${ODH_DEPS_ROOT:-"$odh_tools/odh-dependencies"}
odh_prefix=${ODH_ANDROID_ICONV_ROOT:-"$odh_deps/android-arm64/iconv"}
odh_archive="$odh_deps/downloads/libiconv-1.18.tar.gz"
odh_sha=3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
odh_compilers="$odh_ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
odh_cc="$odh_compilers/aarch64-linux-android26-clang"
[[ -x "$odh_cc" ]] || { echo 'Set ANDROID_NDK_HOME to an installed Android NDK.' >&2; exit 1; }
mkdir -p "$odh_deps/downloads" "$odh_deps/sources" "$odh_deps/build/libiconv-android-arm64" "$odh_prefix/include" "$odh_prefix/lib" "$odh_prefix/share/licenses" "$odh_prefix/share/source"
if [[ ! -f "$odh_archive" ]]; then
  curl --fail --location --show-error https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz -o "$odh_archive.part"
  mv -- "$odh_archive.part" "$odh_archive"
fi
printf '%s  %s\n' "$odh_sha" "$odh_archive" | sha256sum --check --status
if [[ ! -d "$odh_deps/sources/libiconv-1.18" ]]; then tar -xzf "$odh_archive" -C "$odh_deps/sources"; fi
odh_source=$(cd -- "$odh_deps/sources/libiconv-1.18" && pwd)
# The shipped source archive must correspond to the built library. Reject local
# source edits rather than silently pairing a modified library with stock source.
python3 - "$odh_archive" "$odh_deps/sources" <<'PYSOURCE'
import hashlib,pathlib,sys,tarfile
base=pathlib.Path(sys.argv[2])
with tarfile.open(sys.argv[1],'r:gz') as archive:
 for member in archive.getmembers():
  if not member.isfile():continue
  p=base/member.name
  if not p.is_file() or hashlib.sha256(p.read_bytes()).digest()!=hashlib.sha256(archive.extractfile(member).read()).digest():
   raise SystemExit('libiconv source differs from pinned archive: '+member.name)
PYSOURCE
odh_build=$(cd -- "$odh_deps/build/libiconv-android-arm64" && pwd)
(
  cd -- "$odh_build"
  CC="$odh_cc" AR="$odh_compilers/llvm-ar" RANLIB="$odh_compilers/llvm-ranlib" \
    "$odh_source/configure" --host=aarch64-linux-android --prefix="$odh_prefix" \
      --disable-shared --enable-static --with-pic --disable-nls
  make lib/localcharset.h --jobs="${ODH_BUILD_JOBS:-1}"
  make -C lib --jobs="${ODH_BUILD_JOBS:-1}"
)
# Android extracts lib*.so APK entries. Link the PIC library objects under an
# unversioned SONAME rather than distributing libtool's libiconv.so.2 name.
"$odh_cc" -shared -Wl,--no-undefined -Wl,-soname,libiconv.so -Wl,-z,max-page-size=16384 \
  -Wl,--whole-archive "$odh_build/lib/.libs/libiconv.a" -Wl,--no-whole-archive \
  -o "$odh_prefix/lib/libiconv.so"
cp "$odh_build/include/iconv.h.inst" "$odh_prefix/include/iconv.h"
cp "$odh_source/COPYING.LIB" "$odh_prefix/share/licenses/libiconv-LGPL-2.1.txt"
cp "$odh_source/COPYING" "$odh_prefix/share/licenses/libiconv-GPL-3.0.txt"
cp "$odh_archive" "$odh_prefix/share/source/"
cp -- "${BASH_SOURCE[0]}" "$odh_prefix/share/source/prepare-android-iconv.sh"
cat > "$odh_prefix/share/licenses/libiconv-source.txt" <<TEXT
GNU libiconv 1.18 library (LGPL 2.1 or later), no command-line utility bundled.
Upstream source: https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz
SHA256: $odh_sha
The unchanged source archive and Android build recipe accompany the APK.
The shared libiconv.so can be replaced and the APK rebuilt/re-signed with the
public Android packaging script. No system iconv or raised minimum API is used.
TEXT
printf 'Android API26 iconv prefix: %s\n' "$odh_prefix"
