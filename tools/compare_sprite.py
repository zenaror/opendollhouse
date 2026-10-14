"""Independent PALT/DGRP/SPR2 oracle; raw RGBA, palette indices and Z.
Usage: compare_sprite.py FAR IFF_ENTRY REPORT_JSON OUTPUT_PREFIX
No assets are extracted; named exports are compared against decoded input bytes.
"""
import hashlib
import json
import pathlib
import struct
import sys

archive, entry, report, prefix = sys.argv[1:]
actual = json.loads(pathlib.Path(report).read_text())
with open(archive, 'rb') as stream:
    assert stream.read(8) == b'FAR!byAZ'
    version, index = struct.unpack('<II', stream.read(8)); assert version == 1
    stream.seek(index); count, = struct.unpack('<I', stream.read(4)); assert count <= 200000
    matches = []
    for _ in range(count):
        size, stored, offset, length = struct.unpack('<IIII', stream.read(16))
        assert length <= 1048576
        name = stream.read(length)
        if name == entry.encode('ascii'):
            assert size == stored and size <= 67108864
            matches.append((offset, size))
    offset, size = matches[0]; assert len(matches) == 1
    stream.seek(offset); iff = stream.read(size); assert len(iff) == size
chunks, cursor = {}, 64
while cursor < len(iff):
    kind = iff[cursor:cursor+4].decode('ascii')
    size, resource_id, flags = struct.unpack_from('>IHH', iff, cursor+4)
    assert size >= 76 and cursor+size <= len(iff)
    key = kind, resource_id; assert key not in chunks
    chunks[key] = iff[cursor+76:cursor+size]; cursor += size
assert cursor == len(iff)
obj = chunks['OBJD', actual['objd']['id']]
version, stack, base, graphics = struct.unpack_from('<IHHH', obj)
assert actual['objd']['version'] == version and actual['objd']['base_graphic'] == base
assert actual['objd']['num_graphics'] == graphics
group_id = base+actual['objd']['graphic_index']; assert group_id == actual['objd']['group_id']
group = chunks['DGRP', group_id]
version, = struct.unpack_from('<H', group); assert 20000 <= version <= 20004
count, = struct.unpack_from('<H' if version < 20003 else '<I', group, 2)
cursor, selected = (4 if version < 20003 else 6), []
assert actual['dgrp']['version'] == version
direction = actual['dgrp']['requested_direction']; rotation = actual['dgrp']['rotation']
rotated = ((direction << (rotation*2)) & 255) | (direction >> (8-rotation*2))
for _ in range(count):
    if version < 20003:
        sprites, facing, zoom = struct.unpack_from('<HBB', group, cursor); cursor += 4
    else:
        facing, zoom, sprites = struct.unpack_from('<III', group, cursor); cursor += 12
    records = []
    for _ in range(sprites):
        xb = yb = zb = 0; legacy_type = None
        if version < 20003:
            legacy_type, sprite, frame, flags, x, y = struct.unpack_from('<HHHHhh', group, cursor); cursor += 12
            if version == 20001:
                zb, = struct.unpack_from('<I', group, cursor); cursor += 4
        else:
            sprite, frame, x, y, zb, flags = struct.unpack_from('<IIiiII', group, cursor); cursor += 24
            if version == 20004:
                xb, yb = struct.unpack_from('<II', group, cursor); cursor += 8
        records.append(dict(sprite_id=sprite, frame=frame, x=x, y=y, flags=flags,
                            object_offset_bits=[xb, yb, zb], legacy_type=legacy_type))
    if facing == rotated and zoom == actual['dgrp']['zoom']:
        selected.append(records)
assert len(selected) == 1 and actual['dgrp']['direction_rotated'] == rotated
reference = selected[0][actual['dgrp']['layer']]
for key, value in reference.items():
    assert actual['dgrp'][key] == value, key
spr = chunks['SPR2', reference['sprite_id']]
version, count, default = struct.unpack_from('<III', spr); assert version == 1000
offsets = list(struct.unpack_from('<'+'I'*count, spr, 12))
start = offsets[reference['frame']]
end = min([x for x in offsets if x > start]+[len(spr)])
frame = spr[start:end]
width, height, flags, palette_id, transparent, y, x = struct.unpack_from('<HHIHHhh', frame)
for key, value in dict(version=version, default_palette_id=default, width=width, height=height,
    flags=flags, palette_id_raw=palette_id, transparent_index=transparent, x=x, y=y,
    effective_palette_id=default & 65535).items():
    assert actual['spr2'][key] == value, key
pal = chunks['PALT', default & 65535]
pal_version, colors = struct.unpack_from('<II', pal)
assert actual['palette']['version'] == pal_version and actual['palette']['size'] == colors
assert actual['palette']['reserved'] == list(pal[8:16])
palette = [tuple(pal[16+i*3:19+i*3])+(255,) for i in range(colors)]
assert 0 <= transparent < colors
rgba = bytearray(width*height*4); indices = bytearray(width*height)
z = bytearray(width*height) if flags & 2 else bytearray()
cursor, row, ended = 16, 0, False
def take(size):
    global cursor
    assert cursor+size <= len(frame)
    data = frame[cursor:cursor+size]; cursor += size
    return data
def word():
    return struct.unpack('<H', take(2))[0]
def clear(at):
    rgba[at*4:at*4+4] = bytes(palette[transparent][:3]+(0,))
    indices[at] = transparent
    if z: z[at] = 255
while cursor < len(frame):
    marker = word(); command, count = marker >> 13, marker & 8191
    if command == 5:
        ended = True; break
    if command == 4:
        assert row+count <= height
        for y in range(row, row+count):
            for x in range(width): clear(y*width+x)
        row += count; continue
    assert command == 0 and count >= 2 and row < height
    row_end = cursor+count-2; assert row_end <= len(frame)
    column = 0
    while cursor < row_end:
        marker = word(); operation, count = marker >> 13, marker & 8191
        assert operation in (1, 2, 3, 6) and column+count <= width
        for _ in range(count):
            at = row*width+column; column += 1
            if operation == 3:
                clear(at); continue
            depth = 0 if operation == 6 else take(1)[0]
            index = take(1)[0]; assert index < colors
            alpha = take(1)[0] if operation == 2 else 31; assert alpha <= 31
            rgba[at*4:at*4+4] = bytes(palette[index][:3]+(alpha*255//31,))
            indices[at] = index
            if z: z[at] = depth
        if operation in (2, 6) and count & 1: take(1)
    assert cursor == row_end
    if z:
        for x in range(column, width): z[row*width+x] = 255
    row += 1
assert ended or row == height
assert actual['spr2']['ended_with_marker'] == ended
assert actual['spr2']['remaining_bytes'] == len(frame)-cursor
assert pathlib.Path(prefix+'.rgba').read_bytes() == rgba, 'RGBA differs'
assert pathlib.Path(prefix+'.idx').read_bytes() == indices, 'palette indices differ'
if flags & 2: assert pathlib.Path(prefix+'.z').read_bytes() == z, 'Z differs'
print(json.dumps(dict(iff_sha256=hashlib.sha256(iff).hexdigest(),
    dgrp_sha256=hashlib.sha256(group).hexdigest(), palette_sha256=hashlib.sha256(pal).hexdigest(),
    spr2_sha256=hashlib.sha256(spr).hexdigest(), rgba_sha256=hashlib.sha256(rgba).hexdigest(),
    index_sha256=hashlib.sha256(indices).hexdigest(), z_sha256=hashlib.sha256(z).hexdigest(),
    all_RGBA_index_Z_bytes_equal=True, width=width, height=height,
    selected_frame=reference['frame'], frame_consumed_bytes=cursor,
    frame_remaining_bytes=len(frame)-cursor), indent=2))
