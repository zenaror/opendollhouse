#!/usr/bin/env python3
"""Package an existing Linux ELF and its corresponding local sources; never build or launch it."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = ('engine', 'include', 'src', 'tools', 'native', 'cmake', 'tests')
SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.inc', '.in', '.cmake', '.py', '.sh', '.s', '.S', '.rc', '.vert', '.frag', '.java', '.xml', '.txt', '.inl', '.patch'}
NOTICE_FILES = (
    'LICENSE', 'NOTICE.md',
    'engine/sims3ds/LICENSE', 'engine/sims3ds/CREDITS.md',
    'engine/sims3ds/LICENSES/BSD-2-Clause-xxHashSharp.txt',
    'engine/sims3ds/LICENSES/OFL-1.1-Inter.txt',
    'engine/sims3ds/src/third_party/minimp3/LICENSE',
)


def notice_destination(name):
    # Keep the project's own terms separate from the inherited engine LICENSE.
    own = {'LICENSE': 'licenses/OpenDollhouse-MPL-2.0.txt',
           'NOTICE.md': 'licenses/OpenDollhouse-NOTICE.md'}
    return own.get(name, 'licenses/' + name.removeprefix('engine/sims3ds/'))


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def source_path(repo, name):
    """Reject symlinks in every component of a selected repository path."""
    relative = Path(name)
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError('source path outside repository: ' + str(name))
    path = repo
    for component in relative.parts:
        path = path / component
        if path.is_symlink():
            raise ValueError('source symlink requires explicit review: ' + str(name))
    return path


def source_bytes(repo, name):
    path = source_path(repo, name)
    if not path.is_file():
        raise ValueError('missing source file: ' + str(name))
    return path.read_bytes()


def remove_search_paths(path):
    """Remove DT_RPATH/DT_RUNPATH from our ELF64 little-endian x86-64 copy."""
    data = bytearray(path.read_bytes())
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 62:
        raise ValueError('expected Linux x86-64 little-endian ELF64')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', data, 54)
    segments = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize) for i in range(phnum)]
    dynamic = [p for p in segments if p[0] == 2]
    if len(dynamic) != 1:
        raise ValueError('expected one dynamic table')
    p = dynamic[0]
    start, size = p[2], p[5]
    entries = []
    for at in range(start, start + size, 16):
        tag, val = struct.unpack_from('<qQ', data, at)
        if tag == 0:
            break
        entries.append((tag, val))
    else:
        raise ValueError('unterminated dynamic table')
    kept = [e for e in entries if e[0] not in (15, 29)]
    for i in range(len(entries) + 1):
        struct.pack_into('<qQ', data, start + i * 16, *(kept[i] if i < len(kept) else (0, 0)))
    # Dynamic strings may be suffix-shared by symbols or version records.
    # Retire the search-path tags only; leave the shared string table intact.
    path.write_bytes(data)



def version_needs(versions):
    """Parse GNU readelf --version-info in the C locale, excluding definitions.

    Keep every required namespace and its provider. Counts make unsupported or
    incomplete needs output fail closed instead of publishing a partial list.
    """
    required = {}
    in_needs = False
    expected_files = seen_files = 0
    provider = None
    remaining = 0
    for line in versions.splitlines():
        if line.startswith('Version '):
            if in_needs and (remaining or seen_files != expected_files):
                raise ValueError('incomplete ELF version needs')
            in_needs = line.startswith('Version needs section ')
            provider = None
            if in_needs:
                header = re.fullmatch(r'Version needs section .* contains (\d+) entr(?:y|ies):', line)
                if header is None:
                    raise ValueError('unsupported ELF version needs header')
                expected_files, seen_files = int(header[1]), 0
            continue
        if not in_needs or not line.strip() or line.lstrip().startswith('Addr:'):
            continue
        file = re.fullmatch(r'\s*(?:0x)?[0-9a-fA-F]+: +Version: +\d+ +File: +(\S+) +Cnt: +(\d+)\s*', line)
        if file:
            if remaining:
                raise ValueError('incomplete ELF version needs provider')
            provider, remaining = file[1], int(file[2])
            required.setdefault(provider, set())
            seen_files += 1
            continue
        name = re.fullmatch(r'\s*(?:0x)?[0-9a-fA-F]+: +Name: +(\S+) +Flags: +.*? +Version: +\d+\s*', line)
        if name and provider is not None and remaining > 0:
            required[provider].add(name[1])
            remaining -= 1
            continue
        raise ValueError('unsupported ELF version needs record: ' + line.strip())
    if in_needs and (remaining or seen_files != expected_files):
        raise ValueError('incomplete ELF version needs')
    return {library: sorted(names) for library, names in sorted(required.items())}


def tar_bytes(files, epoch):
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode='w', format=tarfile.PAX_FORMAT) as archive:
        for name, content, mode in sorted(files):
            entry = tarfile.TarInfo(name)
            entry.size = len(content)
            entry.mode = mode
            entry.mtime = epoch
            entry.uid = entry.gid = 0
            entry.uname = entry.gname = ''
            archive.addfile(entry, io.BytesIO(content))
    return gzip.compress(out.getvalue(), compresslevel=9, mtime=0)


def source_snapshot(repo, epoch):
    selected = []
    for directory in SOURCE_DIRS:
        for path in sorted(source_path(repo, directory).rglob('*')):
            rel = path.relative_to(repo)
            # app/build is authored construction-mode code, not build output.
            if any(part in {'__pycache__', '.git', 'node_modules', 'CMakeFiles', 'bin', 'obj'} for part in rel.parts):
                continue
            if 'build' in rel.parts and rel.parts[:5] != ('engine', 'sims3ds', 'src', 'app', 'build'):
                continue
            source_path(repo, rel)
            if path.is_file() and (path.suffix in SOURCE_SUFFIXES or path.name in {'CMakeLists.txt', 'Makefile', 'README.md', 'LICENSE', 'COPYING'}):
                if path.stat().st_size > 8 * 1024 * 1024:
                    raise ValueError('unexpected large source: ' + str(rel))
                selected.append((str(rel), source_bytes(repo, rel),
                                 0o755 if path.suffix in {'.sh', '.py'} and path.stat().st_mode & 0o111 else 0o644))
    # Public player and contributor guides travel with the source.
    # Internal plans and evidence are not part of the source distribution.
    for path in sorted(source_path(repo, 'docs').rglob('*.md')):
        rel = path.relative_to(repo)
        selected.append((str(rel), source_bytes(repo, rel), 0o644))
    for name in ('CMakeLists.txt', 'README.md', 'docs/references.md', 'docs/ai_agents/README.md',
                 'docs/ai_agents/building.md', 'engine/README.md', 'engine/sims3ds/UPSTREAM.json',
                 'engine/sims3ds/Makefile.port', 'docs/linux-package.md'):
        selected.append((name, source_bytes(repo, name), 0o644))
    for name in NOTICE_FILES:
        selected.append((name, source_bytes(repo, name), 0o644))
    selected = list({name: (name, body, mode) for name, body, mode in selected}.values())
    ledger = ''.join(hashlib.sha256(b).hexdigest() + '  ' + n + '\n' for n, b, _ in sorted(selected))
    selected.append(('SOURCE-SHA256SUMS', ledger.encode(), 0o644))
    return tar_bytes(selected, epoch), len(selected) - 1


def verify_build_sources(repo, binary, payload, report_path, system_roots):
    """Bind archived bytes to a closed build's actual depfiles, without rebuilding.

    The collector verifies archive/object inputs and freshness. Its report is
    local evidence, not a signed or reproducible-compilation attestation.
    """
    report_bytes = report_path.read_bytes()
    report = json.loads(report_bytes)
    if (report.get('mode') != 'collected'
            or report.get('all_recorded_inputs_unchanged') is not True
            or Path(report['source_root']).resolve() != repo):
        raise ValueError('expected a collected, stable build report for this repository')
    build = report['binaries']['opendollhouse']
    if build['sha256'] != digest(binary) or build['bytes'] != binary.stat().st_size:
        raise ValueError('build report identifies a different executable')
    if build['generated']:
        raise ValueError('generated source inputs require explicit packaging support')
    roots = [root.resolve(strict=True) for root in system_roots]
    for row in build['unexpected_external_dependencies']:
        path = Path(row['path']).resolve(strict=True)
        if not any(path == root or root in path.parents for root in roots):
            raise ValueError('unclassified external build input: ' + str(path))
    before = report['input_fingerprints_before']
    if not before or before != report['input_fingerprints_after']:
        raise ValueError('build report has inconsistent input fingerprints')
    for name, row in before.items():
        path = Path(name)
        if (not path.is_file() or path.stat().st_size != row['bytes']
                or digest(path) != row['sha256']
                or path.stat().st_mtime_ns != row['mtime_ns']):
            raise ValueError('build input changed since collection: ' + name)
    with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as archive:
        members = {member.name: member for member in archive.getmembers()}
        if len(members) != len(archive.getmembers()):
            raise ValueError('duplicate source archive member')
        if not build['source_files']:
            raise ValueError('build report has no local source inputs')
        for row in build['source_files']:
            member = members.get(row['path'])
            if member is None or not member.isfile():
                raise ValueError('linked source missing from archive: ' + row['path'])
            data = archive.extractfile(member).read()
            if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
                raise ValueError('linked source differs from archive: ' + row['path'])
    return {'report_sha256': hashlib.sha256(report_bytes).hexdigest(),
            'local_source_inputs': len(build['source_files']),
            'recorded_inputs_rechecked': len(before),
            'external_system_inputs': len(build['unexpected_external_dependencies']),
            'limits': 'depfile/archive input identity; no reproducible compilation, full license audit or gameplay acceptance'}


LAUNCHER = '''#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${1-}" in
  ''|--*) exec "$base/bin/opendollhouse" "$@" ;;
  -h) exec "$base/bin/opendollhouse" --help ;;
esac
# Keep the previous positional data-directory form working too.
data=$1
shift
exec "$base/bin/opendollhouse" --data-root "$data" "$@"
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, default=ROOT)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--expected-binary-sha256', required=True)
    parser.add_argument('--build-inputs', type=Path, required=True,
                        help='closed-build report from collect_cmake_link_inputs.py --collect')
    parser.add_argument('--system-dependency-root', type=Path, action='append', default=[],
                        help='explicit external SDK prefix, repeatable; its files remain system-provided')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    repo = args.repository.resolve(strict=True)
    binary = args.binary.resolve(strict=True)
    output = args.output.absolute()
    if output.exists() or output.is_symlink():
        parser.error('output already exists; preserve it and choose another name')
    if not re.fullmatch(r'[0-9a-f]{64}', args.expected_binary_sha256) or digest(binary) != args.expected_binary_sha256:
        parser.error('binary identity differs from the approved build')
    epoch = int(os.environ.get('SOURCE_DATE_EPOCH', '0'))
    if epoch < 0:
        parser.error('SOURCE_DATE_EPOCH must be nonnegative')
    for name in NOTICE_FILES:
        if not source_path(repo, name).is_file():
            parser.error('missing source notice: ' + name)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='odh-package-', dir=output.parent) as temp:
        staged = Path(temp) / 'opendollhouse'
        shutil.copyfile(binary, staged)
        if digest(staged) != args.expected_binary_sha256:
            raise ValueError('input changed while copying; preserve the build and retry after freezing it')
        subprocess.run(['strip', '--strip-debug', str(staged)], check=True)
        before_dynamic = subprocess.check_output(['readelf', '-d', str(staged)], text=True)
        before_needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', before_dynamic)
        remove_search_paths(staged)
        dynamic = subprocess.check_output(['readelf', '-d', str(staged)], text=True)
        if re.search(r'\((?:RUNPATH|RPATH)\)', dynamic):
            raise ValueError('remaining embedded library search path')
        dependencies = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
        if dependencies != before_needed:
            raise ValueError('library dependencies changed during ELF path removal')
        versions = subprocess.check_output(['readelf', '--version-info', '--wide', str(staged)],
                                           text=True, env={**os.environ, 'LC_ALL': 'C'})
        by_library = version_needs(versions)
        requirements = sorted({name for names in by_library.values() for name in names})
        sources, count = source_snapshot(repo, epoch)
        closure = verify_build_sources(repo, binary, sources, args.build_inputs,
                                       args.system_dependency_root)
        files = [('odl-launcher/bin/opendollhouse', staged.read_bytes(), 0o755),
                 ('odl-launcher/opendollhouse', LAUNCHER.encode(), 0o755),
                 ('odl-launcher/README.md', source_bytes(repo, 'docs/linux-package.md'), 0o644),
                 ('odl-launcher/source-snapshot.tar.gz', sources, 0o644)]
        for name in NOTICE_FILES:
            dest = notice_destination(name)
            files.append(('odl-launcher/' + dest, source_bytes(repo, name), 0o644))
        receipt = {'format': 1, 'platform': 'Linux x86_64', 'source_date_epoch': epoch,
                   'input_binary_sha256': digest(binary), 'packaged_binary_sha256': digest(staged),
                   'transformation': 'strip debug sections; remove ELF DT_RPATH/DT_RUNPATH on isolated copy',
                   'system_libraries': dependencies, 'required_symbol_versions': requirements,
                   'required_symbol_versions_by_library': by_library,
                   'source_archive_sha256': hashlib.sha256(sources).hexdigest(), 'source_files': count,
                   'source_status': 'archived local source inputs match the closed build report; full redistribution review remains open',
                   'build_source_inputs': closure,
                   'limits': 'existing experimental build; no gameplay, other distro, or full M1 acceptance'}
        files.append(('odl-launcher/BUILD.json', (json.dumps(receipt, indent=2) + '\n').encode(), 0o644))
        ledger = ''.join(hashlib.sha256(b).hexdigest() + '  ' + n.split('/', 1)[1] + '\n' for n, b, _ in sorted(files))
        files.append(('odl-launcher/SHA256SUMS', ledger.encode(), 0o644))
        payload = tar_bytes(files, epoch)
        if digest(binary) != args.expected_binary_sha256:
            raise ValueError('input executable changed while packaging')
        # Exclusive create preserves every existing deliverable, including races.
        with output.open('xb') as handle:
            handle.write(payload)
        print(json.dumps({'artifact': output.name, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(),
                          'binary_sha256': receipt['packaged_binary_sha256'], 'source_files': count}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
