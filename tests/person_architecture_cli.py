"""CLI parsing/catalog boundary with explicit synthetic sources; no game data."""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]
checks = 0


def check(value, diagnostic=''):
    global checks
    checks += 1
    assert value, diagnostic


with tempfile.TemporaryDirectory(prefix='odh-person-architecture-') as folder:
    root = Path(folder)
    output = root / 'frame.ppm'
    # Valid syntax reaches the catalog before a deliberately absent definition.
    # Keeping that later failure explicit avoids implying a decoded scene here.
    args = [binary, '--avatar-file', str(root / 'person.iff'), '128',
            '--semiglobal', str(root / 'semi.far'), 'Person.iff',
            '--global', str(root / 'global.far'), 'Global.iff',
            '--animation-source', str(root / 'animations.far'),
            '--render-animation-far', str(root / 'animations.far'),
            '--render-textures', str(root / 'textures.far'),
            '--render-skeleton', 'skeleton.bcf', 'ROOT',
            '--render-frame', str(output), '--render-saved-avatar', '1',
            '--saved-world-main', '--object-definition-file', str(root / 'absent-definition.iff'),
            '--lot', str(root / 'house.iff'), '7', '--render-floors', str(root / 'floors.iff')]

    def run(extra=(), base=None):
        result = subprocess.run((args if base is None else base) + list(extra),
                                capture_output=True, text=True)
        check(result.returncode == 2 and not result.stdout, result.stdout + result.stderr)
        check(not output.exists(), 'failed invocation created an output')
        return result.stderr

    iff = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1'.ljust(64, b'\0')
    floor, wall, archive = root / 'tile.FLR', root / 'paint.WLL', root / 'materials.FAR'
    floor.write_bytes(iff)
    wall.write_bytes(iff)
    # FAR has both families, preserving caller order across archive and loose sources.
    entries = [('tile.flr', iff), ('paint.wll', iff)]
    manifest = struct.pack('<I', len(entries))
    offset = 16
    for name, payload in entries:
        name = name.encode('ascii')
        manifest += struct.pack('<IIII', len(payload), len(payload), offset, len(name)) + name
        offset += len(payload)
    archive.write_bytes(b'FAR!byAZ' + struct.pack('<II', 1, offset) +
                        b''.join(payload for _, payload in entries) + manifest)

    baseline = run()
    check('cannot open IFF file' in baseline, baseline)
    ordered = ['--architecture-resource', str(floor), '--architecture-resource', str(archive),
               '--architecture-resource', str(wall)]
    check(run(ordered) == baseline, 'valid catalog failed before the deliberate definition failure')
    # Both source orders catalog successfully; no implicit directory enumeration.
    check(run(['--architecture-resource', str(wall), '--architecture-resource', str(floor)]) == baseline)
    broken = root / 'broken.far'
    broken.write_bytes(b'not a FAR archive')
    error = run(['--architecture-resource', str(broken)])
    check(error != baseline and ('FAR' in error or 'view' in error or 'read' in error), error)
    absent = root / 'absent.flr'
    missing_error = run(['--architecture-resource', str(absent)])
    check('architecture resource source is not a regular file' in missing_error, missing_error)
    check(run(['--architecture-resource', str(absent), '--architecture-resource', str(broken)]) == missing_error)
    check(run(['--architecture-resource', str(broken), '--architecture-resource', str(absent)]) == error)
    check('explicit .flr, .wll or .far file' in run(['--architecture-resource', str(root / 'tile.iff')]))
    check('explicit .flr, .wll or .far file' in run(['--architecture-resource', str(root)]))
    directory = root / 'directory.flr'
    directory.mkdir()
    check('not a regular file' in run(['--architecture-resource', str(directory)]))
    check('bounded host profile' in run(['--architecture-resource', '']))
    check('bounded host profile' in run(['--architecture-resource', 'x' * 4097]))
    check('invalid or repeated' in run(['--architecture-resource']))
    check('--architecture-resource requires --render-floors' in
          run(['--architecture-resource', str(floor)], base=args[:-2]))
    check('--architecture-resource requires --render-floors' in
          run(['--architecture-resource', str(floor)], base=[binary]))
    # 256 explicit sources are bounded metadata; the next is refused in parsing.
    check(run(['--architecture-resource', str(floor)] * 256) == baseline)
    check('source count exceeds bounded host profile' in
          run(['--architecture-resource', str(floor)] * 257))
    # Check output identity before catalog reads, preserving every input byte.
    for source in [floor, wall, archive]:
        original = source.read_bytes()
        for alias_kind in ['same', 'symlink', 'hardlink']:
            alias = source if alias_kind == 'same' else root / (alias_kind + '.ppm')
            if alias_kind == 'symlink':
                alias.symlink_to(source)
            elif alias_kind == 'hardlink':
                alias.hardlink_to(source)
            aliased_args = list(args)
            aliased_args[aliased_args.index('--render-frame') + 1] = str(alias)
            check('person frame output aliases an input' in
                  run(['--architecture-resource', str(source)], base=aliased_args))
            check(source.read_bytes() == original)
            if alias_kind != 'same':
                alias.unlink()

print(f'{checks} person architecture CLI checks passed (synthetic catalog/errors only)')
