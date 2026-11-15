#!/usr/bin/env python3
"""Check source recovery without compiling or launching the game."""
import argparse
import hashlib
import importlib.util
import io
import json
import shutil
from pathlib import Path
import tarfile
import tempfile


def unpack(payload):
    with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as archive:
        return {entry.name: archive.extractfile(entry).read() for entry in archive}



def check_version_needs(package):
    # A producer may define the same familiar namespaces without requiring them.
    # Only .gnu.version_r records contribute, regardless of namespace spelling.
    definitions = """Version definition section '.gnu.version_d' contains 1 entry:
 Addr: 0x10  Offset: 0x10  Link: 3 (.dynstr)
 0x0000: Rev: 1  Flags: BASE  Index: 1  Cnt: 1  Name: GLIBC_99.0
"""
    needs = """Version needs section '.gnu.version_r' contains 5 entries:
 Addr: 0x20  Offset: 0x20  Link: 3 (.dynstr)
 0x0000: Version: 1  File: libgcc_s.so.1  Cnt: 1
 0x0010: Name: GCC_3.0  Flags: none  Version: 25
 0x0020: Version: 1  File: libz.so.1  Cnt: 1
 0x0030: Name: ZLIB_1.2.0  Flags: none  Version: 10
 0x0040: Version: 1  File: libc.so.6  Cnt: 2
 0x0050: Name: GLIBC_2.38  Flags: none  Version: 8
 0x0060: Name: GLIBC_2.2.5  Flags: none  Version: 6
 0x0070: Version: 1  File: libcustom.so.1  Cnt: 2
 0x0080: Name: VENDOR-ABI.1  Flags: WEAK  Version: 3
 0x0090: Name: ZLIB_1.2.0  Flags: none  Version: 4
 0x00a0: Version: 1  File: libc.so.6  Cnt: 1
 0x00b0: Name: GLIBC_2.38  Flags: none  Version: 8
"""
    symbols = """Version symbols section '.gnu.version' contains 1 entry:
 Addr: 0x30  Offset: 0x30  Link: 3 (.dynstr)
 000: 2 (GLIBCXX_99.0)
"""
    expected = {'libgcc_s.so.1': ['GCC_3.0'], 'libz.so.1': ['ZLIB_1.2.0'],
                'libc.so.6': ['GLIBC_2.2.5', 'GLIBC_2.38'],
                'libcustom.so.1': ['VENDOR-ABI.1', 'ZLIB_1.2.0']}
    # GNU readelf can omit 0x on provider offsets in --wide output.
    wide_needs = needs.replace('0x', '')
    for text in (definitions + needs + symbols, symbols + needs + definitions,
                 definitions + wide_needs + symbols):
        assert package.version_needs(text) == expected
    for text in ('No version information found in this file.\n', definitions + symbols,
                 "Version needs section '.gnu.version_r' contains 0 entries:\n"):
        assert package.version_needs(text) == {}
    # Truncated/unsupported need records must not silently remove dependencies.
    malformed = [needs.replace('contains 5 entries:', 'contains 6 entries:'),
                 needs.replace('Cnt: 2', 'Cnt: 3', 1),
                 needs.replace('Name: GCC_3.0', 'Unknown: GCC_3.0'),
                 needs.replace('Version: 1  File: libgcc_s.so.1  Cnt: 1', 'Name: ORPHAN  Flags: none  Version: 1'),
                 needs.replace('contains 5 entries:', 'contains several entries:'),
                 needs[:needs.rfind(' 0x00b0:')]]
    for text in malformed:
        try:
            package.version_needs(text)
        except ValueError:
            pass
        else:
            raise AssertionError('incomplete/unsupported version needs accepted')
    print('PASS ELF version needs: provider identity, all namespaces, definitions excluded, incomplete output refused')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packager', type=Path, default=Path(__file__).resolve().parents[1] / 'tools/package_linux.py')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('package_linux', args.packager)
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    check_version_needs(package)
    destinations = [package.notice_destination(name) for name in package.NOTICE_FILES]
    assert len(destinations) == len(set(destinations)), 'notice destination collision'
    assert package.notice_destination('LICENSE') == 'licenses/OpenDollhouse-MPL-2.0.txt'
    assert package.notice_destination('engine/sims3ds/LICENSE') == 'licenses/LICENSE'
    assert {'LICENSE', 'NOTICE.md'} <= set(package.NOTICE_FILES)
    with tempfile.TemporaryDirectory(prefix='odh-source-contract-') as temp:
        repo = Path(temp)
        names = ['CMakeLists.txt', 'README.md', 'docs/references.md',
                 'docs/ai_agents/README.md', 'docs/ai_agents/building.md',
                 'docs/ai_agents/engine.md', 'docs/ai_agents/testing.md',
                 'docs/nested/user-guide.md',
                 'docs/linux-package.md', 'engine/README.md',
                 'engine/sims3ds/UPSTREAM.json', 'engine/sims3ds/Makefile.port',
                 'tools/package_linux.py', 'engine/sims3ds/src/app/build/UIBuildMode.cpp',
                 'engine/sims3ds/src/app/build/UIBuildMode.h', *package.NOTICE_FILES]
        for name in names:
            path = repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('alternate repository: ' + name).encode())
        ignored = repo / 'engine/__pycache__/excluded.py'
        ignored.parent.mkdir(parents=True)
        ignored.write_text('cache')
        for name in ['engine/build/CMakeFiles/generated.cpp',
                     'engine/build/generated.cpp', 'engine/CMakeFiles/generated.cpp']:
            generated = repo / name
            generated.parent.mkdir(parents=True, exist_ok=True)
            generated.write_text('generated output')
        (repo / 'engine/commercial.iff').write_bytes(b'not source')
        payload, count = package.source_snapshot(repo, 0)
        recovered = unpack(payload)
        assert count == len(names), (count, len(names), sorted(recovered))
        assert set(recovered) == set(names) | {'SOURCE-SHA256SUMS'}
        for name in names:
            assert recovered[name] == (repo / name).read_bytes(), name
        for row in recovered['SOURCE-SHA256SUMS'].decode().splitlines():
            sha, name = row.split('  ', 1)
            assert hashlib.sha256(recovered[name]).hexdigest() == sha, name
        assert package.source_snapshot(repo, 0)[0] == payload
        # A closed-build receipt must identify both this executable and the
        # actual archived dependency bytes. A stale or incomplete snapshot
        # cannot become a package simply because its own checksums are valid.
        binary = repo / 'test-executable'
        binary.write_bytes(b'fixture executable')
        linked = repo / 'engine/sims3ds/src/app/build/UIBuildMode.cpp'
        fingerprint = lambda p: {'sha256': package.digest(p), 'bytes': p.stat().st_size,
                                 'mtime_ns': p.stat().st_mtime_ns}
        row = {'path': linked.relative_to(repo).as_posix(), **fingerprint(linked)}
        pins = {str(p): fingerprint(p) for p in (binary, linked)}
        report = {'mode': 'collected', 'source_root': str(repo),
                  'all_recorded_inputs_unchanged': True,
                  'input_fingerprints_before': pins, 'input_fingerprints_after': pins,
                  'binaries': {'opendollhouse': {
                      **fingerprint(binary), 'source_files': [row], 'generated': [],
                      'unexpected_external_dependencies': []}}}
        report_path = repo / 'build-inputs.json'
        def check_report(value=report, source_payload=payload, roots=()):
            report_path.write_text(json.dumps(value))
            return package.verify_build_sources(repo, binary, source_payload, report_path, roots)
        assert check_report()['local_source_inputs'] == 1
        def bad_report(value=report, source_payload=payload, contains=''):
            try:
                check_report(value, source_payload)
            except ValueError as error:
                assert contains in str(error), str(error)
            else:
                raise AssertionError('invalid build-source binding accepted')
        changed = json.loads(json.dumps(report))
        changed['binaries']['opendollhouse']['sha256'] = '0' * 64
        bad_report(changed, contains='different executable')
        changed = json.loads(json.dumps(report))
        changed['mode'] = 'planning_only'
        bad_report(changed, contains='collected')
        missing_payload = package.tar_bytes(
            [(n, b, 0o644) for n, b in recovered.items() if n != row['path']], 0)
        bad_report(source_payload=missing_payload, contains='missing from archive')
        altered_payload = package.tar_bytes(
            [(n, b if n != row['path'] else b'wrong archived bytes', 0o644)
             for n, b in recovered.items()], 0)
        bad_report(source_payload=altered_payload, contains='differs from archive')
        original = linked.read_bytes()
        old_time = linked.stat().st_mtime_ns
        linked.write_bytes(b'changed after build collection')
        bad_report(contains='changed since collection')
        linked.write_bytes(original)
        import os
        os.utime(linked, ns=(old_time, old_time))
        assert check_report()['recorded_inputs_rechecked'] == 2
        changed = json.loads(json.dumps(report))
        changed['binaries']['opendollhouse']['unexpected_external_dependencies'] = [
            {'path': str(binary), **fingerprint(binary)}]
        bad_report(changed, contains='unclassified external')
        assert check_report(changed, roots=[repo])['external_system_inputs'] == 1
        changed = json.loads(json.dumps(report))
        changed['binaries']['opendollhouse']['generated'] = [{'path': 'generated.h'}]
        bad_report(changed, contains='generated source inputs')
        def rejected(call):
            try:
                call()
            except ValueError as error:
                assert 'symlink' in str(error), str(error)
            else:
                raise AssertionError('source symlink accepted')

        # Exercise both archive selection and the reader used for package
        # README/notices. Neither an explicit file nor its parent may escape.
        external = repo.parent / (repo.name + '-external')
        external.mkdir()
        try:
            (external / 'external.md').write_text('EXTERNAL TREE CONTENT')
            for name in ['engine/linked.cpp', 'docs/linux-package.md',
                         package.NOTICE_FILES[0]]:
                path = repo / name
                original = path.read_bytes() if path.exists() else None
                if original is not None:
                    path.unlink()
                path.symlink_to(external / 'external.md')
                rejected(lambda: package.source_snapshot(repo, 0))
                rejected(lambda: package.source_bytes(repo, name))
                path.unlink()
                if original is not None:
                    path.write_bytes(original)
            for name in ['docs', 'engine']:
                path = repo / name
                saved = repo / (name + '-saved')
                path.rename(saved)
                shutil.copytree(saved, external / name)
                path.symlink_to(external / name, target_is_directory=True)
                rejected(lambda: package.source_snapshot(repo, 0))
                member = 'docs/linux-package.md' if name == 'docs' else 'engine/sims3ds/LICENSE'
                rejected(lambda: package.source_bytes(repo, member))
                path.unlink()
                saved.rename(path)
        finally:
            shutil.rmtree(external)
    print('PASS source recovery, deterministic archive, data/build exclusion, symlink rejection, closed-build binding and stale/missing/mismatched source refusal')


if __name__ == '__main__':
    main()
