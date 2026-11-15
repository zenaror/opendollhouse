#!/usr/bin/env bash
# Build only the LGPL conversion library, never the GPL command-line utility.
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  echo 'Usage: engine/cmake/prepare-vita-iconv.sh'
  echo 'Builds GNU libiconv 1.18 static library for Vita ARM.'
  echo 'Overrides: ODH_TOOLS_ROOT, ODH_DEPENDENCY_SOURCES, ODH_LIBICONV_ARCHIVE, VITASDK, ODH_VITA_BUILD_ROOT, ODH_BUILD_JOBS.'
  exit 0
fi
odh_tools=${ODH_TOOLS_ROOT:-"$HOME/Tools"}
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
export VITASDK=${VITASDK:-"$odh_tools/vitasdk"}
odh_deps=${ODH_DEPS_ROOT:-"$odh_tools/odh-dependencies"}
odh_sources=${ODH_DEPENDENCY_SOURCES:-"$odh_deps/sources"}
odh_vita_build=${ODH_VITA_BUILD_ROOT:-"$odh_root/build/vita-app"}
odh_prefix="$odh_vita_build/deps"
odh_archive=${ODH_LIBICONV_ARCHIVE:-"$odh_deps/downloads/libiconv-1.18.tar.gz"}
odh_sha=3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
odh_compilers="$VITASDK/bin"
odh_cc="$odh_compilers/arm-vita-eabi-gcc"
[[ -x "$odh_cc" ]] || { echo 'Set VITASDK to an installed VitaSDK.' >&2; exit 1; }
mkdir -p "$odh_deps/downloads" "$odh_sources" "$odh_vita_build/iconv" "$odh_prefix/include" "$odh_prefix/lib" "$odh_prefix/share/licenses" "$odh_prefix/share/source"
if [[ ! -f "$odh_archive" ]]; then
  curl --fail --location --show-error https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz -o "$odh_archive.part"
  mv -- "$odh_archive.part" "$odh_archive"
fi
printf '%s  %s\n' "$odh_sha" "$odh_archive" | sha256sum --check --status
if [[ ! -d "$odh_sources/libiconv-1.18" ]]; then tar -xzf "$odh_archive" -C "$odh_sources"; fi
odh_source=$(cd -- "$odh_sources/libiconv-1.18" && pwd)
# The shipped source archive must correspond to the built library. Reject local
# source edits rather than silently pairing a modified library with stock source.
python3 - "$odh_archive" "$odh_sources" <<'PYSOURCE'
import hashlib,pathlib,sys,tarfile
base=pathlib.Path(sys.argv[2])
with tarfile.open(sys.argv[1],'r:gz') as archive:
 for member in archive.getmembers():
  if not member.isfile():continue
  p=base/member.name
  if not p.is_file() or hashlib.sha256(p.read_bytes()).digest()!=hashlib.sha256(archive.extractfile(member).read()).digest():
   raise SystemExit('libiconv source differs from pinned archive: '+member.name)
PYSOURCE
odh_build=$(cd -- "$odh_vita_build/iconv" && pwd)
(
  cd -- "$odh_build"
  CC="$odh_cc" AR="$odh_compilers/arm-vita-eabi-ar" RANLIB="$odh_compilers/arm-vita-eabi-ranlib" \
    "$odh_source/configure" --host=arm-vita-eabi --prefix="$odh_prefix" \
      --disable-shared --enable-static --disable-nls
  make lib/localcharset.h --jobs="${ODH_BUILD_JOBS:-1}"
  make -C lib --jobs="${ODH_BUILD_JOBS:-1}"
)
cp "$odh_build/lib/.libs/libiconv.a" "$odh_prefix/lib/"
cp "$odh_build/include/iconv.h.inst" "$odh_prefix/include/iconv.h"
cp "$odh_source/COPYING.LIB" "$odh_prefix/share/licenses/libiconv-LGPL-2.1.txt"
cp "$odh_source/COPYING" "$odh_prefix/share/licenses/libiconv-GPL-3.0.txt"
cp "$odh_archive" "$odh_prefix/share/source/"
cp -- "${BASH_SOURCE[0]}" "$odh_prefix/share/source/prepare-vita-iconv.sh"
cat > "$odh_prefix/share/licenses/libiconv-source.txt" <<TEXT
GNU libiconv 1.18 library (LGPL 2.1 or later), no command-line utility bundled.
Upstream source: https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz
SHA256: $odh_sha
The unchanged source archive and Vita build recipe accompany the VPK and relink kit.
The static library must be distributed with the application object files and
relink instructions (the Vita packaging script creates a companion relink kit).
Replace libiconv.a and rerun the provided link/SELF/package steps to modify it.
TEXT
printf 'Vita static iconv prefix: %s\n' "$odh_prefix"
