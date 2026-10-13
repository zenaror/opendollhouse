"""Synthetic private-IFF OBJD/DGRP/SPR2/PALT decode and raw export integration."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]
pack = struct.pack


def chunk(kind, ident, data):
    return kind.encode('ascii') + pack('>IHH', 76 + len(data), ident, 0) + bytes(64) + data


def frame(z=True, all_transparent=False):
    if all_transparent:
        packets = pack('<H', (3 << 13) | 4)
    elif z:
        packets = (pack('<HBB', (1 << 13) | 1, 7, 1)
                   + pack('<HBBBB', (2 << 13) | 1, 42, 2, 15, 0)
                   + pack('<H', (3 << 13) | 1)
                   + pack('<HBB', (6 << 13) | 1, 1, 0))
    else:
        packets = pack('<HBBBB', (6 << 13) | 4, 1, 2, 1, 2)
    return (pack('<HHIHHhh', 4, 1, 7 if z else 1, 99, 0, -3, 5)
            + pack('<H', 2 + len(packets)) + packets + pack('<H', 5 << 13) + b'T')


def archive(**options):
    obj_version = options.get('obj_version', 136)
    base = options.get('base', 1128)
    obj = pack('<IHHH', obj_version, 0, base, options.get('count', 2))
    first = frame(z=False)
    second = frame(z=options.get('z', True), all_transparent=options.get('all_transparent', False))
    spr = (pack('<IIIII', options.get('spr_version', 1000), 2,
                options.get('default_palette', 0x10007), 20, 20 + len(first)) + first + second)
    records = b''
    for selected_frame in [options.get('ref_frame', 1), 0]:
        records += pack('<IIiiIIII', options.get('ref_id', 20), selected_frame, -9, 11,
                        0x3F800000, 1, 0x80000000, 0x7FC12345)
    image = pack('<III', options.get('direction', 1), 3, 2) + records
    dgrp = pack('<HI', options.get('dgrp_version', 20004), 2 if options.get('duplicate_image') else 1) + image
    if options.get('duplicate_image'):
        dgrp += image
    palette = pack('<II', 1, 3) + bytes(range(8)) + bytes([0, 0, 0, 10, 20, 30, 80, 90, 100]) + b'PT'
    pieces = [('OBJD', 100, obj), ('DGRP', base if base < 65536 else 0, dgrp), ('SPR2', 20, spr)]
    # The second OBJD graphic points to its own group rather than treating INDEX as a DGRP ID.
    if base < 65535:
        pieces.append(('DGRP', base + 1, dgrp))
    if not options.get('missing_palette'):
        pieces.append(('PALT', 7, palette))
    duplicate = options.get('duplicate')
    if duplicate:
        pieces += [item for item in pieces if item[0] == duplicate][:1]
    asset = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60, b'\0') + bytes(4)
    asset += b''.join(chunk(*item) for item in pieces)
    name = b'object.iff'
    entries = 2 if options.get('duplicate_far') else 1
    record = pack('<IIII', len(asset), len(asset), 16, len(name)) + name
    return b'FAR!byAZ' + pack('<II', 1, 16 + len(asset)) + asset + pack('<I', entries) + record * entries


with tempfile.TemporaryDirectory(prefix='odh-sprite-') as folder:
    folder = Path(folder)
    far = folder / 'synthetic.far'

    def run(*args, selector='100', **fixture):
        far.write_bytes(archive(**fixture))
        return subprocess.run([binary, str(far), 'object.iff', selector, *args], capture_output=True, text=True)

    def success(*args, **fixture):
        result = run(*args, **fixture)
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout)

    def failure(*args, **fixture):
        result = run(*args, **fixture)
        assert result.returncode == 2 and not result.stdout, (args, fixture, result.stdout, result.stderr)
        return result

    before = set(folder.iterdir())
    d = success()
    assert set(folder.iterdir()) == before | {far}
    assert d['evidence'] == 'ODH_DGRP_SPR2_paletted_sprite_decode'
    assert d['objd'] == dict(id=100, version=136, base_graphic=1128, num_graphics=2, graphic_index=0, group_id=1128)
    assert d['dgrp']['version'] == 20004 and d['dgrp']['direction_rotated'] == 1
    assert d['dgrp']['sprite_id'] == 20 and d['dgrp']['frame'] == 1
    assert (d['dgrp']['x'], d['dgrp']['y']) == (-9, 11)
    assert d['dgrp']['object_offset_bits'] == [0x80000000, 0x7FC12345, 0x3F800000]
    assert d['dgrp']['legacy_type'] is None
    assert d['spr2']['default_palette_id'] == 0x10007 and d['spr2']['effective_palette_id'] == 7
    assert d['spr2']['palette_id_raw'] == 99 and d['spr2']['transparent_index'] == 0
    assert (d['spr2']['x'], d['spr2']['y']) == (5, -3)
    assert d['spr2']['ended_with_marker'] and d['spr2']['remaining_bytes'] == 1
    assert d['palette'] == dict(version=1, size=3, reserved=list(range(8)), remaining_bytes=2)
    assert d['stats'] == dict(opaque=2, partial=1, transparent=1, has_z=True, z_min=0, z_max=42)
    assert d['flip_required'] and d['export_pixel_orientation'] == 'raw_frame_unflipped'
    assert d['placement']['profile'] == 'raw_frame_only_not_calibrated'
    assert not any(d['placement'][key] for key in ['original_calibrated', 'origin_applied', 'crop_applied', 'dgrp_offsets_applied'])
    assert d['output'] is None

    prefix = folder / 'decoded'
    d = success('--output', str(prefix))
    assert prefix.with_suffix('.rgba').read_bytes() == bytes([10, 20, 30, 255, 80, 90, 100, 123, 0, 0, 0, 0, 10, 20, 30, 255])
    assert prefix.with_suffix('.idx').read_bytes() == bytes([1, 2, 0, 1])
    assert prefix.with_suffix('.z').read_bytes() == bytes([7, 42, 255, 0])
    assert d['output']['rgba'] == str(prefix) + '.rgba'

    prefix = folder / 'noz'
    d = success('--output', str(prefix), z=False)
    assert prefix.with_suffix('.rgba').read_bytes() == bytes([10, 20, 30, 255, 80, 90, 100, 255] * 2)
    assert prefix.with_suffix('.idx').read_bytes() == bytes([1, 2, 1, 2])
    assert not prefix.with_suffix('.z').exists() and d['output']['z'] is None
    assert d['stats'] == dict(opaque=4, partial=0, transparent=0, has_z=False, z_min=None, z_max=None)
    d = success(all_transparent=True)
    assert d['stats'] == dict(opaque=0, partial=0, transparent=4, has_z=True, z_min=None, z_max=None)

    d = success('--graphic', '1', '--layer', '1', selector='0x64')
    assert d['objd']['group_id'] == 1129 and d['dgrp']['frame'] == 0 and not d['stats']['has_z']
    d = success('--rotation', '1', direction=4)
    assert d['dgrp']['direction_rotated'] == 4 and d['dgrp']['rotation'] == 1
    d = success('--direction', '64', '--rotation', '1')
    assert d['dgrp']['direction_rotated'] == 1  # Eight-bit wrap.
    for version in [138, 139, 140, 141, 142]:
        assert success(obj_version=version)['objd']['version'] == version

    for args in [('--graphic', '2'), ('--layer', '2'), ('--direction', '2'), ('--zoom', '2'),
                 ('--rotation', '4'), ('--direction', '256'), ('--graphic', '-1'),
                 ('--layer', '+1'), ('--zoom', '4294967296'), ('--zoom', '1junk'),
                 ('--zoom', '0x'), ('--zoom', ' 1'), ('--layer', ''), ('--output', ''),
                 ('--layer', '0', '--layer', '1'), ('--wat', '1'), ('--layer',)]:
        failure(*args)
    for ident in ['-1', '+1', '65536', '100junk', '0x', '']:
        failure(selector=ident)
    for typ in ['OBJD', 'DGRP', 'SPR2', 'PALT']:
        assert 'ambiguous' in failure(duplicate=typ).stderr
    assert 'ambiguous' in failure(duplicate_far=True).stderr
    assert 'ambiguous' in failure(duplicate_image=True).stderr
    for fixture in [dict(missing_palette=True), dict(default_palette=8), dict(ref_id=65536),
                    dict(ref_frame=2), dict(spr_version=1001), dict(dgrp_version=20005),
                    dict(obj_version=137), dict(count=0)]:
        failure(**fixture)
    failure('--graphic', '1', base=65535)
    result = failure('--output', str(folder / 'absent' / 'decoded'))
    assert 'cannot open output' in result.stderr and 'partial outputs may remain' in result.stderr
    # Force a write/close failure after open without depending on filesystem fullness.
    if Path('/dev/full').exists():
        bad = folder / 'full'
        bad.with_suffix('.rgba').symlink_to('/dev/full')
        result = failure('--output', str(bad))
        assert 'cannot write output' in result.stderr

print('Sprite CLI passed (synthetic fixtures only; raw frames, no scene placement)')
