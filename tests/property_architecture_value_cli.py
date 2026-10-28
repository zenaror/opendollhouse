"""Synthetic resource-binding regressions; no commercial game data required."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())

def chunk(kind, ident, payload):
    return kind + struct.pack('>IHH', len(payload) + 76, ident, 0) + bytes(64) + payload

def iff(*chunks):
    return b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1'.ljust(60, b'\0') + bytes(4) + b''.join(chunks)

def strings(values):
    return struct.pack('<hH', -3, len(values)) + b''.join(b'\1' + v.encode('ascii') + b'\0\0' for v in values)

def array(ident, stride, data):
    assert len(data) == 4 * stride
    return chunk(b'Arry', ident, struct.pack('<5iH', 0, 2, 2, stride, 0, len(data)) + data)

def house(segment, style=1, pattern=5, alias=5):
    advanced = pattern > 255
    wall_stride = 14 if advanced else 8
    wall = (struct.pack('<BB6H', segment, 0, style, 0, pattern, 0, 0, 0) if advanced
            else bytes([segment, 0, style, 0, pattern, 0, 0, 0])) + bytes(wall_stride * 3)
    name = b'material.wll'
    map_data = struct.pack('<4i', 0, 1, 0, 1) + name + b'\0' + (b'\0' if len(name) % 2 == 0 else b'')
    map_data += struct.pack('<iB', 0, alias) + bytes(5)
    floor_id, wall_id, floor_stride = (11, 12, 2) if advanced else (1, 2, 1)
    return iff(array(floor_id, floor_stride, bytes(4 * floor_stride)),
               array(floor_id + 100, floor_stride, bytes(4 * floor_stride)), array(wall_id, wall_stride, wall),
               array(wall_id + 100, wall_stride, bytes(4 * wall_stride)), array(8, 1, bytes(4)), array(108, 1, bytes(4)),
               array(9, 1, bytes([32] * 4)), chunk(b'WALm', 0, map_data))

with tempfile.TemporaryDirectory(prefix='odh-architecture-price-cli-') as temporary:
    root = Path(temporary)
    build = root / 'Build.iff'
    build.write_bytes(iff(chunk(b'STR#', 0x81, strings(['20', 'wall', 'd'] * 5)),
                         chunk(b'STR#', 0x82, strings(['0', 'floor', 'd'] * 2)),
                         chunk(b'STR#', 0x83, strings(['2', 'pattern', 'd'] * 6)),
                         chunk(b'STR#', 0x85, strings(['100', 'pool', 'd']))))
    floors, walls, material, lot = (root / name for name in ('floors.iff', 'walls.iff', 'material.wll', 'House.iff'))
    floors.write_bytes(iff())
    walls.write_bytes(iff(chunk(b'SPR#', 1541, b'')))  # Known global pattern5 fallback.
    material.write_bytes(iff())  # Deliberately missing STR#0.
    def run(expected, include=True):
        command = [exe, str(lot), '--build-iff', str(build), '--floor-globals', str(floors), '--wall-globals', str(walls)]
        if include:
            command += ['--resource-file', str(material)]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == expected, (command, result.returncode, result.stdout, result.stderr)
        return json.loads(result.stdout) if result.stdout else None
    lot.write_bytes(house(0))
    assert run(0)['total'] == 0  # An unused stale pattern must not open bad metadata.
    lot.write_bytes(house(1, style=2))
    assert run(0)['total'] == 10  # Fence style does not consume wallpaper.
    lot.write_bytes(house(1))
    run(2)  # Used declared resource with missing price is a real metadata error.
    unresolved = run(3, include=False)
    assert unresolved['reason'] == 'incomplete_wallpaper_lookup'  # No silent global fallback.
    material.write_bytes(iff(chunk(b'STR#', 0, strings(['custom', '11', 'd']))))
    value = run(0)
    assert value['total'] == 21 and value['dynamic_price_resources_loaded'] == 1
    assert value['inspection_only'] and not value['refund_authorized']
    lot.write_bytes(house(1, pattern=261, alias=5))
    value = run(0)
    assert value['total'] == 21 and value['dynamic_price_resources_loaded'] == 1
    lot.write_bytes(house(1, pattern=0, alias=0))
    material.write_bytes(iff())
    assert run(0)['total'] == 10  # None/zero pattern remains unremapped.
print('price CLI unused metadata, declared aliases, exact resources and zero-pattern checks passed')
