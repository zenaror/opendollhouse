#!/usr/bin/env python3
"""Retain this build's application objects and actual link recipe for LGPL relinking."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build_root', type=Path)
parser.add_argument('sdk_root', type=Path)
parser.add_argument('--app-dir', type=Path, help='Application build directory (default: BUILD_ROOT/app)')
parser.add_argument('--deps-dir', type=Path, help='Dependency prefix (default: BUILD_ROOT/deps)')
parser.add_argument('--packaging-patch', type=Path, help='Optional reviewed packaging delta accompanying an unchanged as-built source snapshot')
args = parser.parse_args()
build = args.build_root.resolve()
sdk = args.sdk_root.resolve()
app = (args.app_dir or build / 'app').resolve()
deps = (args.deps_dir or build / 'deps').resolve()

def archive_name(path):
    try:
        return str(Path('app') / path.relative_to(app))
    except ValueError:
        return str(Path('deps') / path.relative_to(deps))
if not (app / 'OpenDollhouse.vpk').is_file():
    raise SystemExit('Build the VPK before creating its relink kit')
commands = {}
targets = ('opendollhouse', 'opendollhouse.velf', 'eboot.bin.out', 'eboot.bin')
for name in targets:
    lines = subprocess.check_output(['ninja', '-C', str(app), '-t', 'commands', name], text=True).splitlines()
    if not lines:
        raise SystemExit('No actual link recipe for ' + name)
    commands[name] = lines[-1]
files = list((app / 'CMakeFiles/opendollhouse.dir').rglob('*.o'))
files += list((app / 'CMakeFiles/opendollhouse.dir').rglob('*.obj'))
files += list((app / 'engine-root').rglob('*.a'))
files += list(app.rglob('*.rsp'))
files += list((deps / 'lib').glob('*.a'))
files += [app / 'OpenDollhouse.vpk']
files += [p for p in (deps / 'share/source').iterdir() if p.is_file()]
files += [p for p in (deps / 'share/licenses').iterdir() if p.is_file()]
files = sorted(set(files))
if not any(p.suffix in ('.o', '.obj') for p in files) or not any(p.name == 'libiconv.a' for p in files):
    raise SystemExit('Application objects or libiconv archive missing; refusing incomplete relink kit')
packaging_inputs = [('packaging/create-relink-kit.py', Path(__file__).read_bytes())]
if args.packaging_patch:
    packaging_inputs.append(('packaging/packaging-fix.patch', args.packaging_patch.read_bytes()))
manifest = {'build_root': str(build), 'sdk_root': str(sdk),
            'app_root': str(app), 'deps_root': str(deps),
            'targets': targets, 'commands': commands,
            'sha256': {archive_name(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
manifest['sha256'].update({name: hashlib.sha256(body).hexdigest() for name, body in packaging_inputs})
runner = r'''#!/usr/bin/env python3
# Replace deps/lib/libiconv.a, then run with VITASDK pointing at your SDK.
import json, os, pathlib, shlex, subprocess, zipfile
root=pathlib.Path(__file__).resolve().parent
meta=json.loads((root/'RELINK.json').read_text())
sdk=os.environ.get('VITASDK')
if not sdk: raise SystemExit('Set VITASDK to your VitaSDK directory')
# Remap complete absolute path arguments only. CMake may encode the original
# source path INSIDE a relative object filename; those names must stay intact.
paths={meta['build_root']: root, meta['sdk_root']: pathlib.Path(sdk).resolve(),
       meta.get('app_root', str(pathlib.Path(meta['build_root'])/'app')): root/'app',
       meta.get('deps_root', str(pathlib.Path(meta['build_root'])/'deps')): root/'deps'}
def relocate_path(argument):
    for old, new in sorted(paths.items(), key=lambda item: len(item[0]), reverse=True):
        if argument == old or argument.startswith(old + '/'):
            return str(new) + argument[len(old):]
    return argument

def relocate_argument(argument):
    if argument.startswith('/'):
        return relocate_path(argument)
    # Joined compiler/linker search paths and absolute response-file arguments.
    for flag in ('--sysroot=', '-isysroot=', '-I', '-L', '-B', '@'):
        if argument.startswith(flag + '/'):
            return flag + relocate_path(argument[len(flag):])
    return argument

def relocated_command(command):
    # These are the generated link recipes, whose only shell control tokens
    # are conjunctions and the CMake ':' no-op. Quote every actual argument.
    return ' '.join(token if token in ('&&', ':') else shlex.quote(relocate_argument(token))
                    for token in shlex.split(command))

for target in meta.get('targets', ('opendollhouse','eboot.bin')):
    subprocess.run(relocated_command(meta['commands'][target]), shell=True, cwd=root/'app', check=True)
source=root/'app/OpenDollhouse.vpk'
output=root/'app/OpenDollhouse-modified.vpk'
with zipfile.ZipFile(source) as src,zipfile.ZipFile(output,'w') as dst:
    for info in src.infolist():
        payload=(root/'app/eboot.bin').read_bytes() if info.filename=='eboot.bin' else src.read(info.filename)
        dst.writestr(info,payload)
print(output)
'''
readme = '''OpenDollhouse Vita — build-specific LGPL relink kit

Requires your VitaSDK and Python3 on Linux; the SDK is not copied here.
This is application object code and dependency archives, not a second engine.

1. Keep all paths in this archive together after extraction.
2. To rebuild libiconv, use the unchanged source tar and prepare-vita-iconv.sh
   in deps/share/source. Set ODH_VITA_BUILD_ROOT to this extracted directory,
   ODH_DEPENDENCY_SOURCES to a separate source directory and ODH_LIBICONV_ARCHIVE
   to the tar inside deps/share/source. Set VITASDK to your SDK.
3. Or replace deps/lib/libiconv.a with another compatible static library.
4. Run: VITASDK=/path/to/vitasdk python3 relink.py
5. The result is app/OpenDollhouse-modified.vpk.

packaging/create-relink-kit.py is the exact generator of this kit. An optional
packaging/packaging-fix.patch records a post-build packaging correction; the
as-built project source snapshot remains separate and unchanged.

RELINK.json preserves the actual original ELF, VELF and SELF commands, file hashes
and paths to remap. relink.py executes those commands; inspect them first.
No game data, saves, SDK, private signing key or full commercial installation
is included. Keep this kit and library source with the distributed VPK.
This enables relinking; it does not establish execution on a Vita.
'''
output = build / 'OpenDollhouse-Vita-relink.tar.gz'
with tarfile.open(output, 'w:gz') as archive:
    for p in files:
        archive.add(p, arcname=archive_name(p))
    for name, content in [('RELINK.json', json.dumps(manifest, indent=2)), ('relink.py', runner), ('README.txt', readme)] + packaging_inputs:
        data = content if isinstance(content, bytes) else content.encode()
        entry = tarfile.TarInfo(name)
        entry.size = len(data)
        entry.mode = 0o755 if name == 'relink.py' else 0o644
        archive.addfile(entry, io.BytesIO(data))
print(output)
