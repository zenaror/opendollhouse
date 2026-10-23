"""Synthetic saved-scene decode/alias/render; no original game data."""
import json
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


def chunk(kind, ident, payload):
    return kind.encode('ascii') + struct.pack('>IHH', 76 + len(payload), ident, 0) + bytes(64) + payload


header = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1'.ljust(64, b'\0')


def iff(chunks):
    return header + b''.join(chunk(*c) for c in chunks)


def far(path, entries):
    offset = 16
    manifest = struct.pack('<I', len(entries))
    for name, data in entries:
        name = name.encode('ascii')
        manifest += struct.pack('<IIII', len(data), len(data), offset, len(name)) + name
        offset += len(data)
    path.write_bytes(b'FAR!byAZ' + struct.pack('<II', 1, offset) +
                     b''.join(data for _, data in entries) + manifest)


def grid(stride, data):
    result = struct.pack('<5I', 0, 64, 64, stride, 0)
    for pos in range(0, len(data), 16384):
        run = data[pos:pos + 16384]
        result += struct.pack('<H', len(run)) + run + bytes(len(run) % 2)
    return result


def material_map(ident, name, alias):
    result = struct.pack('<4I', 0, 1, 0, bool(name))
    if name:
        encoded = name.encode('ascii')
        result += encoded + b'\0' + bytes(not len(encoded) % 2)
        result += struct.pack('<IB', 0, alias) + bytes(5 + 2 * ident)
    return result


def fields(values):
    bits = ''.join('0' if value == 0 else '111' + format(value & ((1 << width) - 1), f'0{width}b')
                   for value, width in values)
    return bytes(int(bits[p:p + 8].ljust(8, '0'), 2) for p in range(0, len(bits), 8))


def saved_entities(count):
    table = fields([(value, 16) for ident in range(1, count + 1) for value in [ident, 1]] + [(0, 16)])
    objm = struct.pack('<II4sB', 0, 62, b'MjbO', 1) + table
    for ident in range(1, count + 1):
        prefix = [(0, 32)] * 4 + [(504, 32), (504, 32), (1, 32), (0, 16), (0, 16)]
        prefix += [(0, 16)] * 8
        prefix += [(ident if n == 11 else -1 if n == 3 else 0, 16) for n in range(68)]
        prefix += [(0, 16)] * 5
        # No stack/relations/slots/dynamic flags, then final UnknownInt.
        prefix += [(0, 32), (0, 32), (-1, 32), (0, 32), (0, 16), (0, 16), (0, 32)]
        block = fields(prefix)
        objm += struct.pack('<I', len(objm) + 4 + len(block) - 12) + block
    objt = struct.pack('<II4s', 0, 2, b'tjbo')
    if count:
        objt += struct.pack('<I6H', 0x12345678, 0, 0, 0, 0, 1, 4) + b'object\0\0'
    return objm, objt


def house(floor_name='TILE.flr', wall_name='PAINT.wll', preferred=True, entities=0, pool=False, water=False):
    floor = bytearray(4096)
    floor[31 * 64 + 31] = 42
    wall = bytearray(4096 * 8)
    wall[(31 * 64 + 31) * 8:(31 * 64 + 31) * 8 + 8] = bytes([1, 0, 1, 0, 43, 0, 0, 0])
    pool_grid, water_grid = bytearray(4096), bytearray(4096)
    pool_grid[31 * 64 + 31] = int(pool)
    water_grid[31 * 64 + 31] = int(water)
    objm, objt = saved_entities(entities)
    chunks = [('ObjM', 1, objm), ('objt', 0, objt)]
    for ident, stride, data in [(1, 1, floor), (101, 1, bytes(4096)),
                                (2, 8, wall), (102, 8, bytes(4096 * 8)),
                                (8, 1, bytes(4096)), (9, 1, pool_grid),
                                (10, 1, water_grid), (0, 4, bytes(4096 * 4))]:
        chunks.append(('Arry', ident, grid(stride, data)))
    if preferred:
        # ID1 wins even though ID0 and irrelevant ID7 precede it.
        chunks += [('FLRm', 0, material_map(0, 'missing.flr', 42)),
                   ('FLRm', 7, material_map(7, 'missing.flr', 42))]
    ident = int(preferred)
    chunks += [('FLRm', ident, material_map(ident, floor_name, 42)),
               ('WALm', ident, material_map(ident, wall_name, 43))]
    return iff(chunks)


def palette(red, green, blue):
    return struct.pack('<4I', 1, 1, 0, 0) + bytes([red, green, blue])


def floor_material(red=210):
    sprite = struct.pack('<4I2H3I', 1000, 1, 7, 16, 2, 2, 1, 0, 0)
    sprite += (struct.pack('<HH2s', 6, (6 << 13) | 2, b'\0\0') * 2 +
               struct.pack('<H', 5 << 13))
    return iff([('PALT', 7, palette(red, 10, 20)), ('SPR2', 513, sprite)])


def wall_sprite(frames):
    frame = bytes(4) + struct.pack('<HH', 1, 1) + bytes([4, 6, 2, 1, 0, 0, 5, 0])
    start = 12 + 4 * frames
    return struct.pack('<3I', 1000, frames, 7) + b''.join(
        struct.pack('<I', start + i * len(frame)) for i in range(frames)) + frame * frames


def wall_material(global_resource=False):
    chunks = [('PALT', 7, palette(10, 180, 20)), ('SPR#', 2049, wall_sprite(4))]
    if global_resource:
        chunks += [('SPR#', 1026, wall_sprite(4)), ('SPR#', 4098, wall_sprite(46))]
    return iff(chunks)


with tempfile.TemporaryDirectory(prefix='odh-saved-scene-') as folder:
    root = Path(folder)
    lot, definitions, floors, walls, tile, paint = [root / n for n in
        ['house.iff', 'mixed.far', 'floors.iff', 'walls.iff', 'tile.flr', 'paint.wll']]
    output, depth = root / 'frame.ppm', root / 'frame.depth'
    lot.write_bytes(house())
    objd = bytearray(32)
    struct.pack_into('<I', objd, 0, 138)
    struct.pack_into('<H', objd, 18, 4)
    struct.pack_into('<I', objd, 28, 0x12345678)
    far(definitions, [('Object.IFF', iff([('OBJD', 1, objd)])),
                      ('ignored.bin', b'not IFF'), ('tiny.txt', b'x')])
    # Global floor is deliberately empty: remapping must choose the external file.
    floors.write_bytes(header)
    walls.write_bytes(wall_material(True))
    tile.write_bytes(floor_material())
    paint.write_bytes(wall_material())
    args = [binary, str(lot), str(root / 'unused-animation.far'),
            str(root / 'unused-textures.far'), str(output),
            '--object-definitions', str(definitions), '--objects',
            '--floors', str(floors), '--walls', str(walls), '--level', '1',
            '--width', '128', '--height', '128', '--depth-output', str(depth)]
    resources = ['--architecture-resource', str(tile), '--architecture-resource', str(paint)]
    extra_definitions = []
    for index in range(128):
        extra = root / f'extra-{index:03}.far'
        extra_objd = bytearray(32)
        struct.pack_into('<I', extra_objd, 0, 138)
        struct.pack_into('<I', extra_objd, 28, 0x20000000 + index)
        far(extra, [(f'Extra{index:03}.iff', iff([('OBJD', index + 1, extra_objd)]))])
        extra_definitions.append(extra)

    def run(extra=(), base=None, success=True):
        for path in [output, depth]:
            if path.exists():
                path.unlink()
        result = subprocess.run((args if base is None else base) + list(extra),
                                capture_output=True, text=True)
        check(result.returncode == (0 if success else 2), result.stdout + result.stderr)
        if success:
            check(not result.stderr, result.stderr)
            return json.loads(result.stdout)
        check(not result.stdout, result.stdout)
        check(not output.exists() and not depth.exists(), 'error wrote output')
        return result.stderr

    result = run(resources)
    check(result['entities'] == result['avatars_rendered'] == 0)
    check(result['architecture_prepared'] and result['floor_map_resolved'] == result['wall_map_resolved'] == 1)
    check(result['floor_draws'][0]['pattern'] == 256 and result['floor_materials'][0]['source'] == str(tile))
    check(result['wall_draws'][0]['pattern'] == 256 and result['wall_draws'][0]['pattern_source'] == str(paint))
    check(result['covered_pixels'] > 0 and result['wall_shaded_samples'] > 0)
    check(output.read_bytes().startswith(b'P6\n128 128\n255\n') and depth.stat().st_size == 128 * 128 * 4)
    check(bytes([210, 10, 20]) in output.read_bytes(), 'external floor color absent')
    first_frame = output.read_bytes()
    at_source_limit = ['--object-definitions', str(definitions)]
    for path in extra_definitions[:127]:
        at_source_limit += ['--object-definitions', str(path)]
    definition_base = args[:5] + ['--floors', str(floors), '--walls', str(walls)]
    check(run(resources + at_source_limit, base=definition_base)['entities'] == 0,
          '128 definition sources pass the bounded catalog')
    over_source_limit = at_source_limit + ['--object-definitions', str(extra_definitions[127])]
    check('definition source count exceeds limit' in
          run(resources + over_source_limit, base=definition_base, success=False),
          '129 definition sources exceed the bounded catalog')
    check(run(resources)['covered_pixels'] == result['covered_pixels'] and output.read_bytes() == first_frame)
    check('require --architecture-resource' in run(success=False))
    # A real saved object anchors the camera; there is still no avatar or
    # animation/skeleton dependency. Object sprite rendering is optional here.
    lot.write_bytes(house(entities=1))
    object_anchor_args = [arg for arg in args if arg != '--objects']
    anchored = run(resources, base=object_anchor_args)
    check(anchored['entities'] == 1 and anchored['avatars_rendered'] == 0 and anchored['covered_pixels'] > 0)
    # 600 valid records fit the prefix/common decoders, but retained source
    # metadata plus banks exceed SavedWorld's existing 65536-word cap.
    lot.write_bytes(house(entities=600))
    check('saved world retained word budget exceeded' in run(resources, success=False))
    only_args = args[:5] + ['--architecture-only', '--floors', str(floors),
                           '--level', '1', '--width', '128', '--height', '128',
                           '--depth-output', str(depth)]
    only = run(resources, base=only_args)
    check(only['architecture_only'] and not only['world_entities_restored'])
    check(only['entities'] == only['avatars_rendered'] == 0 and only['covered_pixels'] > 0)
    check(not only['objects_rendered'] and not only['walls_rendered'] and only['floor_map_resolved'] == 1)
    for extra in [['--objects'], ['--walls', str(walls)],
                  ['--object-definitions', str(definitions)], ['--object-definition-file', str(lot)],
                  ['--neighborhood', str(lot)], ['--skeleton', 'ROOT.bcf', 'ROOT']]:
        check('excludes objects, walls, definitions, neighborhood and skeleton' in
              run(resources + extra, base=only_args, success=False))
    check('--architecture-only requires --floors' in
          run(base=args[:5] + ['--architecture-only'], success=False))
    for water_profile in [{'pool': True}, {'water': True}]:
        lot.write_bytes(house(**water_profile))
        check('pool/water floor geometry unsupported' in run(resources, base=only_args, success=False))
    lot.write_bytes(house(preferred=False))
    check(run(resources)['floor_map_resolved'] == 1, 'ID0 fallback failed')
    lot.write_bytes(house(floor_name='missing.flr'))
    alias_error = run(resources, success=False)
    check('external material alias unresolved' in alias_error)
    check('floor_map_misses=1 floor_alias_ids=[42]' in alias_error, alias_error)
    lot.write_bytes(house())
    # Explicit FAR and later loose duplicate retain deterministic precedence.
    archive = root / 'materials.FAR'
    far(archive, [('tile.flr', floor_material(20)), ('paint.wll', paint.read_bytes()),
                  ('ignored.bin', b'not IFF')])
    check(run(['--architecture-resource', str(archive)])['floor_materials'][0]['asset'] == 'tile.flr')
    check(run(['--architecture-resource', str(archive)] + resources)['floor_materials'][0]['source'] == str(tile))
    # The .iff filter never swallows malformed definition IFFs.
    broken = root / 'broken.far'
    far(broken, [('broken.IFF', b'not IFF'), ('ignored.bin', b'x')])
    malformed_args = list(args)
    malformed_args[malformed_args.index('--object-definitions') + 1] = str(broken)
    check('read outside resource' in run(resources, base=malformed_args, success=False))
    check('requires --floors' in run(resources, base=args[:8], success=False))
    for path in ['', 'a' * 4097]:
        check('bounded host profile' in run(['--architecture-resource', path], success=False))
    check('explicit .flr, .wll or .far' in run(['--architecture-resource', str(lot)], success=False))
    check('not a regular file' in run(['--architecture-resource', str(root / 'absent.flr')], success=False))
    directory = root / 'directory.flr'
    directory.mkdir()
    check('not a regular file' in run(['--architecture-resource', str(directory)], success=False))
    broken_resource = root / 'broken.far'
    broken_resource.write_bytes(b'not a FAR archive')
    check('FAR' in run(['--architecture-resource', str(broken_resource)], success=False))
    check('incomplete saved scene option' in run(['--architecture-resource'], success=False))
    check('source count exceeds bounded host profile' in run(resources * 129, success=False))
    check(run(['--architecture-resource', str(tile)] * 255 +
              ['--architecture-resource', str(paint)])['architecture_resources'] == 256)
    output_alias_args = list(args)
    output_alias_args[output_alias_args.index('--depth-output') + 1] = str(output)
    check('output aliases an input or another output' in run(resources, base=output_alias_args, success=False))
    # Both outputs guard every newly supplied resource before reads/truncation.
    for source in [tile, paint, archive, definitions]:
        original = source.read_bytes()
        for option_index in [4, args.index('--depth-output') + 1]:
            for mode in ['same', 'symlink', 'hardlink']:
                alias = source if mode == 'same' else root / ('alias-' + mode)
                if mode == 'symlink':
                    alias.symlink_to(source)
                elif mode == 'hardlink':
                    alias.hardlink_to(source)
                changed = list(args)
                changed[option_index] = str(alias)
                check('output aliases an input' in run(resources + ['--architecture-resource', str(archive)],
                                                     base=changed, success=False))
                check(source.read_bytes() == original)
                if mode != 'same':
                    alias.unlink()

print(f'{checks} saved scene CLI checks passed (synthetic only)')
