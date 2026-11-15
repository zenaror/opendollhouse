#!/usr/bin/env python3
"""Retain static library replacement inputs and the actual Switch link commands."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import shutil
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build_root', type=Path)
parser.add_argument('sdk_root', type=Path)
parser.add_argument('portlibs_root', type=Path)
args = parser.parse_args()
build = args.build_root.resolve()
sdk = args.sdk_root.resolve()
portlibs = args.portlibs_root.resolve()
app = build / 'app'
if not (app / 'opendollhouse.nro').is_file():
    raise SystemExit('Build the NRO before creating its relink kit')
commands = {}
for target in ('opendollhouse', 'opendollhouse_nro'):
    lines = subprocess.check_output(['ninja', '-C', str(app), '-t', 'commands', target], text=True).splitlines()
    if not lines:
        raise SystemExit('No actual link recipe for ' + target)
    commands[target] = lines[-1]
files = list((app / 'CMakeFiles/opendollhouse.dir').rglob('*.o'))
files += list((app / 'engine-root').rglob('*.a'))
files += list(app.rglob('*.rsp'))
files += list((build / 'deps/lib').glob('*.a'))
files += [p for p in (build / 'deps/share').rglob('*') if p.is_file()]
files += list((portlibs / 'lib').glob('*.a'))
files += [app / 'opendollhouse.nacp', app / 'opendollhouse.nro']
files = sorted(set(files))
if not any(p.suffix == '.o' for p in files) or not any(p.name == 'libiconv.a' for p in files):
    raise SystemExit('Application objects or libiconv archive missing')

def archive_name(path):
    if path.is_relative_to(portlibs):
        return str(Path('portlibs') / path.relative_to(portlibs))
    return str(path.relative_to(build))

def file_sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()

metadata = {'build_root': str(build), 'sdk_root': str(sdk),
            'sdk_alias': str(args.sdk_root.absolute()), 'portlibs_root': str(portlibs),
            'commands': commands,
            'sha256': {}, 'original_sha256': {archive_name(p): file_sha(p) for p in files}}
runner = r'''#!/usr/bin/env python3
# Replace deps/lib/libiconv.a, then run with DEVKITPRO pointing at devkitPro.
import json, os, pathlib, re, shlex, subprocess
root=pathlib.Path(__file__).resolve().parent
meta=json.loads((root/'RELINK.json').read_text())
sdk=os.environ.get('DEVKITPRO')
if not sdk: raise SystemExit('Set DEVKITPRO to your devkitPro directory')
sdk_path=pathlib.Path(sdk).absolute()
paths={meta['build_root']: root, meta['sdk_root']: sdk_path,
       meta['sdk_alias']: sdk_path, meta['portlibs_root']: root/'portlibs'}
pattern=re.compile('|'.join(re.escape(p) for p in sorted(paths, key=len, reverse=True)))
def relocate(command):
    # Tokenize the retained recipes before substitution. Quote the whole
    # relocated argument, including -L/path and --nacp=/path values.
    lexer=shlex.shlex(command, posix=True, punctuation_chars=';&|')
    lexer.whitespace_split=True
    lexer.commenters=''
    tokens=[]
    for token in lexer:
        if token=='&&':
            tokens.append(token)
        elif token in (';', '&', '|', '||'):
            raise ValueError('Unexpected shell operator in retained recipe: '+token)
        else:
            tokens.append(shlex.quote(pattern.sub(lambda m: str(paths[m.group()]), token)))
    return ' '.join(tokens)
# libnx switch.specs expands DEVKITPRO inside linker arguments. Use the
# physical SDK root there; quoted command paths retain the chosen SDK alias.
child_env=dict(os.environ, DEVKITPRO=str(sdk_path.resolve()))
for target in ('opendollhouse','opendollhouse_nro'):
    command=relocate(meta['commands'][target])
    subprocess.run(command,shell=True,cwd=root/'app',env=child_env,check=True)
print(root/'app/opendollhouse.nro')
'''
readme = '''OpenDollhouse Switch build-specific relink kit

Requires your devkitPro with devkitA64/libnx, Python3 and the same compatible
SDK version. No SDK or commercial game data is included. Application object files and static
archives retain code, symbols and relocations; only debug sections were removed
from private copies. Original build objects and debug ELF remain untouched.

Keep all archive paths together. Replace deps/lib/libiconv.a with a compatible
library, then run DEVKITPRO=/path/to/devkitpro python3 relink.py.
The result is app/opendollhouse.nro. RELINK.json retains the actual link and
NRO commands plus original file hashes. Inspect these commands before running.

The unchanged libiconv 1.18 source archive, LGPL license and build recipe are in
deps/share. To rebuild it, set ODH_SWITCH_BUILD_ROOT to this extracted directory,
ODH_DEPENDENCY_SOURCES to a separate source directory, ODH_LIBICONV_ARCHIVE to
the tar inside deps/share/source and DEVKITPRO to your SDK, then run that recipe.

Distribute this kit and corresponding library source with the NRO. Additional
portlib source and license materials must accompany the distribution package.
Relinking and successful compilation do not establish execution on a Switch.
'''
output = build / 'OpenDollhouse-Switch-relink.tar.gz'
strip = sdk / 'devkitA64/bin/aarch64-none-elf-strip'
metadata['strip_tool_sha256'] = file_sha(strip)
metadata['strip_arguments'] = ['--strip-debug']
with tempfile.TemporaryDirectory(prefix='switch-relink-', dir=build) as temporary:
    with tarfile.open(output, 'w:gz', compresslevel=3) as archive:
        for index, p in enumerate(files):
            payload = p
            if p.is_relative_to(app) and p.suffix in ('.o', '.a'):
                payload = Path(temporary) / (str(index) + p.suffix)
                shutil.copy2(p, payload)
                subprocess.run([str(strip), '--strip-debug', str(payload)], check=True)
            metadata['sha256'][archive_name(p)] = file_sha(payload)
            archive.add(payload, arcname=archive_name(p))
            if payload != p:
                payload.unlink()
        for name, content in [('RELINK.json', json.dumps(metadata, indent=2)), ('relink.py', runner), ('README.txt', readme)]:
            payload = content.encode()
            entry = tarfile.TarInfo(name)
            entry.size = len(payload)
            entry.mode = 0o755 if name == 'relink.py' else 0o644
            archive.addfile(entry, io.BytesIO(payload))
print(output)
