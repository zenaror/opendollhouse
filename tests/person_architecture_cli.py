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


def far(path, entries):
    manifest = struct.pack('<I', len(entries))
    offset = 16
    payloads = []
    for name, payload in entries:
        name = name.encode('ascii')
        manifest += struct.pack('<IIII', len(payload), len(payload), offset, len(name)) + name
        payloads.append(payload)
        offset += len(payload)
    path.write_bytes(b'FAR!byAZ' + struct.pack('<II', 1, offset) +
                     b''.join(payloads) + manifest)


def iff_chunk(kind, chunk_id, payload):
    return kind.encode('ascii') + struct.pack('>IHH', 76 + len(payload), chunk_id, 0) + bytes(64) + payload


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
    far(archive, [('tile.flr', iff), ('paint.wll', iff)])

    # A selected OBJD IFF coexists with unrelated non-IFF content in this FAR.
    # The command must parse only the requested entry.
    definition_archive = root / 'mixed-definitions.far'
    definition = bytearray(32)
    struct.pack_into('<I', definition, 0, 138)
    struct.pack_into('<H', definition, 14, 128)
    struct.pack_into('<I', definition, 28, 0x12345678)
    selected_iff = iff + iff_chunk('OBJD', 321, definition)
    far(definition_archive, [('Selected.iff', selected_iff), ('ignored.bin', b'not an IFF')])
    empty_definition = root / 'empty-definition.iff'
    empty_definition.write_bytes(iff)
    definition_args = list(args)
    definition_index = definition_args.index('--object-definition-file')
    definition_args[definition_index + 1] = str(empty_definition)
    definition_args += ['--object-definition-entry', str(definition_archive), 'Selected.iff']

    baseline = run()
    check('cannot open IFF file' in baseline, baseline)
    # Reaching the later absent house proves this exact entry's OBJD was indexed;
    # scanning ignored.bin as IFF would fail earlier with a different diagnostic.
    check(run(base=definition_args) == baseline,
          'selected FAR IFF was not isolated from unrelated non-IFF payloads')
    malformed_archive = root / 'malformed-selected.far'
    malformed_objd = bytearray(32)
    struct.pack_into('<I', malformed_objd, 0, 137)
    far(malformed_archive, [('Broken.iff', iff + iff_chunk('OBJD', 321, malformed_objd)),
                            ('ignored.bin', b'not an IFF')])
    check('unsupported OBJD metadata version' in run(
        ['--object-definition-entry', str(malformed_archive), 'Broken.iff'],
        base=definition_args[:-3]), 'selected OBJD payload was not validated')
    check('exact archive entry not found' in run(
        ['--object-definition-entry', str(definition_archive), 'Absent.iff'],
        base=definition_args[:-3]))
    duplicate_archive = root / 'duplicate-entry.far'
    far(duplicate_archive, [('Repeated.iff', selected_iff), ('Repeated.iff', selected_iff)])
    check('ambiguous archive entry' in run(
        ['--object-definition-entry', str(duplicate_archive), 'Repeated.iff'],
        base=definition_args[:-3]))
    non_iff_error = run(['--object-definition-entry', str(definition_archive), 'ignored.bin'],
                        base=definition_args[:-3])
    check('read outside resource' in non_iff_error, non_iff_error)
    boundary_error = run(['--object-definition-entry', str(definition_archive), 'Selected.iff'] * 255,
                         base=definition_args[:-3])
    check(boundary_error == baseline, boundary_error)
    check('definition source count exceeds bounded host profile' in run(
        ['--object-definition-entry', str(definition_archive), 'Selected.iff'] * 256,
        base=definition_args[:-3]))
    check('archive/entry path outside bounded host profile' in run(
        ['--object-definition-entry', '', 'Selected.iff'], base=definition_args[:-3]))
    check('archive/entry path outside bounded host profile' in run(
        ['--object-definition-entry', str(definition_archive), 'x' * 4097],
        base=definition_args[:-3]))
    check('invalid or repeated' in run(['--object-definition-entry'], base=definition_args[:-3]))
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

    for alias_kind in ['same', 'symlink', 'hardlink']:
        alias = definition_archive if alias_kind == 'same' else root / (alias_kind + '-definition.ppm')
        if alias_kind == 'symlink':
            alias.symlink_to(definition_archive)
        elif alias_kind == 'hardlink':
            alias.hardlink_to(definition_archive)
        aliased_args = list(definition_args)
        aliased_args[aliased_args.index('--render-frame') + 1] = str(alias)
        check('person frame output aliases an input' in run(base=aliased_args), alias_kind)
        if alias_kind != 'same':
            alias.unlink()

print(f'{checks} person architecture CLI checks passed (synthetic catalog/errors only)')
