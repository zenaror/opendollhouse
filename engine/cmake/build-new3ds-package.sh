#!/usr/bin/env bash
# Isolated devkitARM build of the current integrated engine; no commercial assets.
set -euo pipefail
odh_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
odh_build=${ODH_3DS_BUILD_ROOT:-"$odh_root/build/new3ds-app"}
export DEVKITPRO=${DEVKITPRO:-"$HOME/Tools/devkitpro"}
export DEVKITARM=${DEVKITARM:-"$DEVKITPRO/devkitARM"}
export CTRULIB="$DEVKITPRO/libctru"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
[[ -f "$DEVKITARM/3ds_rules" && -f "$CTRULIB/lib/libctru.a" ]] || { echo 'Set DEVKITPRO/DEVKITARM to devkitARM + libctru.' >&2; exit 2; }
mkdir -p "$odh_build"
python3 - "$odh_root" "$odh_build" <<'PY'
import pathlib,sys,hashlib,json,shutil
root,out=map(pathlib.Path,sys.argv[1:]);src=root/'engine/sims3ds/src';dest=out/'src';files={}
previous=json.loads((out/'source-inputs-before.json').read_text()) if (out/'source-inputs-before.json').exists() else {}
for p in src.rglob('*'):
 if not p.is_file():continue
 rel=p.relative_to(src);data=p.read_bytes();files[str(p.relative_to(root))]=hashlib.sha256(data).hexdigest();q=dest/rel;q.parent.mkdir(parents=True,exist_ok=True)
 if not q.exists() or q.read_bytes()!=data:shutil.copyfile(p,q)
# Shared portable services used by the integrated engine on every platform.
for p in sorted((root/'include/odh').rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(root/'include');data=p.read_bytes();files[str(p.relative_to(root))]=hashlib.sha256(data).hexdigest();q=dest/rel;q.parent.mkdir(parents=True,exist_ok=True)
 if not q.exists() or q.read_bytes()!=data:shutil.copyfile(p,q)
p=root/'src/pose.cpp';q=dest/'common/odh_pose.cpp';data=p.read_bytes();files[str(p.relative_to(root))]=hashlib.sha256(data).hexdigest()
if not q.exists() or q.read_bytes()!=data:shutil.copyfile(p,q)
p=root/'tools/json.hpp';q=dest/'json.hpp';data=p.read_bytes();files[str(p.relative_to(root))]=hashlib.sha256(data).hexdigest()
if not q.exists() or q.read_bytes()!=data:shutil.copyfile(p,q)
# Remove only obsolete source files named by this build's previous mirror manifest.
for old in previous:
 try:rel=pathlib.Path(old).relative_to('engine/sims3ds/src')
 except ValueError:
  try:rel=pathlib.Path('odh')/pathlib.Path(old).relative_to('include/odh')
  except ValueError:
   if old=='src/pose.cpp':rel=pathlib.Path('common/odh_pose.cpp')
   elif old=='tools/json.hpp':rel=pathlib.Path('json.hpp')
   else:continue
 if '..' in rel.parts:raise SystemExit('Invalid previous source mirror path')
 if old not in files and (dest/rel).is_file():(dest/rel).unlink()
for p in [*list((root/'include/odh').rglob('*')),root/'src/pose.cpp',root/'tools/json.hpp',*list((root/'engine/platform/new3ds').rglob('*')),root/'engine/cmake/build-new3ds-package.sh',root/'engine/sims3ds/LICENSE',root/'engine/sims3ds/CREDITS.md',*list((root/'engine/sims3ds/LICENSES').rglob('*'))]:
 if p.is_file():files[str(p.relative_to(root))]=hashlib.sha256(p.read_bytes()).hexdigest()
(out/'source-inputs-before.json').write_text(json.dumps(files,indent=2)+'\n')
PY
make -C "$odh_build/src/platform/3ds" -j"${ODH_BUILD_JOBS:-2}" TARGET=opendollhouse APP_TITLE=OpenDollhouse APP_DESCRIPTION='Native engine - requires your own game data' APP_AUTHOR='OpenDollhouse and Sims3DS contributors'
mkdir -p "$odh_build/package/3ds/OpenDollhouse/licenses"
cp "$odh_build/src/platform/3ds/opendollhouse.3dsx" "$odh_build/src/platform/3ds/opendollhouse.smdh" "$odh_build/package/3ds/OpenDollhouse/"
cp "$odh_root/engine/sims3ds/LICENSE" "$odh_root/engine/sims3ds/CREDITS.md" "$odh_root/engine/sims3ds/LICENSES/"* "$odh_build/package/3ds/OpenDollhouse/licenses/"
cp "$odh_root/engine/sims3ds/src/third_party/minimp3/LICENSE" "$odh_build/package/3ds/OpenDollhouse/licenses/minimp3.txt"
cp "$odh_root/engine/platform/new3ds/licenses/"* "$odh_build/package/3ds/OpenDollhouse/licenses/"
cp "$odh_root/engine/platform/new3ds/README.md" "$odh_build/package/3ds/OpenDollhouse/README.md"
python3 - "$odh_root" "$odh_build" <<'PY'
import pathlib,sys,json,hashlib
root,out=map(pathlib.Path,sys.argv[1:]);before=json.loads((out/'source-inputs-before.json').read_text());after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'engine/sims3ds/src').rglob('*') if p.is_file()};
for p in [*list((root/'include/odh').rglob('*')),root/'src/pose.cpp',root/'tools/json.hpp',*list((root/'engine/platform/new3ds').rglob('*')),root/'engine/cmake/build-new3ds-package.sh',root/'engine/sims3ds/LICENSE',root/'engine/sims3ds/CREDITS.md',*list((root/'engine/sims3ds/LICENSES').rglob('*'))]:
 if p.is_file():after[str(p.relative_to(root))]=hashlib.sha256(p.read_bytes()).hexdigest()
(out/'source-inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
if before!=after:raise SystemExit('Sources changed during build: rerun incrementally before accepting this package.')
pkg=out/'package';manifest={str(p.relative_to(pkg)):hashlib.sha256(p.read_bytes()).hexdigest() for p in pkg.rglob('*') if p.is_file() and p.name!='MANIFEST.json'};(pkg/'MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
PY
(cd "$odh_build/package" && zip -qr "$odh_build/OpenDollhouse-new3ds.zip" .)
printf 'Built %s (no emulator/hardware execution implied).\n' "$odh_build/OpenDollhouse-new3ds.zip"
