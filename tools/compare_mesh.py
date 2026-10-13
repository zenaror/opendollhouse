"""Independent BCF/BMF and CPU skinning oracle; reads inputs without extraction.

Usage: compare_mesh.py FAR APPEARANCE_ENTRY BMF_ENTRY MESH_REPORT
Raw fields compare exactly. Skinning uses Python doubles against exported pose
matrices already covered by compare_pose.py; this is not an original-game oracle.
"""
import hashlib
import json
import math
import struct
import sys

archive, appearance_entry, mesh_entry, report = sys.argv[1:]
with open(report, encoding='utf-8') as stream:
    actual = json.load(stream)
with open(archive, 'rb') as stream:
    assert stream.read(8) == b'FAR!byAZ'
    version, index = struct.unpack('<II', stream.read(8)); assert version == 1
    stream.seek(index); count, = struct.unpack('<I', stream.read(4)); assert count <= 200000
    selected = {}
    for _ in range(count):
        size, stored, offset, length = struct.unpack('<IIII', stream.read(16))
        assert length <= 1048576
        name = stream.read(length).decode('ascii')
        if name in (appearance_entry, mesh_entry):
            assert name not in selected and size == stored and size <= 67108864
            selected[name] = (offset, size)
    payloads = {}
    for name in (appearance_entry, mesh_entry):
        offset, size = selected[name]; stream.seek(offset)
        payloads[name] = stream.read(size); assert len(payloads[name]) == size

class Reader:
    def __init__(self, payload):
        self.payload, self.cursor = payload, 0
    def take(self, size):
        assert size >= 0 and self.cursor+size <= len(self.payload)
        data = self.payload[self.cursor:self.cursor+size]; self.cursor += size
        return data
    def number(self, fmt='i'):
        return struct.unpack('<'+fmt, self.take(struct.calcsize('<'+fmt)))[0]
    def count(self, bound):
        count = self.number(); assert 0 <= count <= bound
        return count
    def text(self):
        return self.take(self.number('B')).decode('latin1')
    def words(self, count, fmt='I'):
        return [self.number(fmt) for _ in range(count)]

r = Reader(payloads[appearance_entry]); assert r.number() == 0
appearances = []
for _ in range(r.count(4096)):
    appearance = dict(name=r.text(), type=r.number(), zero=r.number())
    appearance['bindings'] = [dict(bone=r.text(), mesh_name=r.text(),
        censor_flags=r.number(), zero=r.number()) for _ in range(r.count(4096))]
    appearances.append(appearance)
assert r.number() == 0
chosen = [a for a in appearances if a['name'] == actual['appearance']['name']]
assert len(chosen) == 1
for field, value in chosen[0].items():
    assert actual['appearance'][field] == value, field

r = Reader(payloads[mesh_entry])
mesh = dict(skin_name=r.text(), texture_name=r.text())
mesh['bones'] = [r.text() for _ in range(r.count(4096))]
mesh['faces'] = [r.words(3, 'i') for _ in range(r.count(131072))]
mesh['bindings'] = [dict(zip(('bone_index', 'first_real', 'real_count', 'first_blend', 'blend_count'),
    r.words(5, 'i'))) for _ in range(r.count(4096))]
uv = [r.words(2) for _ in range(r.count(65536))]
mesh['blend_data'] = [dict(weight=r.number(), other_vertex=r.number()) for _ in range(r.count(65536))]
mesh['vertex_count2'] = r.number()
mesh['real_vertices'] = [dict(position_bits=r.words(3), normal_bits=r.words(3),
    texcoord_bits=coords) for coords in uv]
mesh['blend_vertices'] = [dict(position_bits=r.words(3), normal_bits=r.words(3),
    texcoord_bits=[0, 0]) for _ in mesh['blend_data']]
mesh['remaining_bytes'] = len(r.payload)-r.cursor
assert mesh == actual['raw_mesh'], 'raw BMF fields differ'

def fold(name):
    return ''.join(chr(ord(c)+32) if 'A' <= c <= 'Z' else c for c in name)
def real(bits):
    value, = struct.unpack('<f', struct.pack('<I', bits)); assert math.isfinite(value)
    return value
def coordinates(bits):
    x, y, z = map(real, bits)
    return [-x, y, z]
pose_bones = actual['pose']['bones']
lookup = {fold(b['name']): i for i, b in enumerate(pose_bones)}
assert len(lookup) == len(pose_bones)
primary = [None]*len(mesh['real_vertices'])
secondary = [None]*len(mesh['blend_vertices'])
clamped = []
for binding_index, binding in enumerate(mesh['bindings']):
    original = binding['bone_index']; assert original >= 0 and mesh['bones']
    bone_index = min(original, len(mesh['bones'])-1)
    if bone_index != original:
        clamped.append(binding_index)
    pose_index = lookup[fold(mesh['bones'][bone_index])]
    for slots, first, count in [(primary, binding['first_real'], binding['real_count']),
                                (secondary, binding['first_blend'], binding['blend_count'])]:
        assert count >= 0
        if count:
            assert first >= 0 and first+count <= len(slots)
        for index in range(first, first+count):
            assert slots[index] is None
            slots[index] = pose_index
assert all(x is not None for x in primary+secondary)
def transform(index, vector, point=True):
    matrix = pose_bones[index]['absolute_matrix']; assert len(matrix) == 12
    return [sum(matrix[row*4+k]*vector[k] for k in range(3)) +
            (matrix[row*4+3] if point else 0) for row in range(3)]
vertices = []
for index, raw in enumerate(mesh['real_vertices']):
    normal = coordinates(raw['normal_bits'])
    if normal == [0, 0, 0]:
        normal = [0, 1, 0]
    vertices.append(dict(position=transform(primary[index], coordinates(raw['position_bits'])),
        normal=transform(primary[index], normal, False),
        texcoord=list(map(real, raw['texcoord_bits'])), primary_bone=primary[index],
        blend_bone=None, blend_weight=0))
for index, blend in enumerate(mesh['blend_data']):
    other, weight = blend['other_vertex'], blend['weight']/32768
    assert 0 <= other < len(vertices) and 0 <= weight <= 1
    base = transform(primary[other], coordinates(mesh['real_vertices'][other]['position_bits']))
    alternate = transform(secondary[index], coordinates(mesh['blend_vertices'][index]['position_bits']))
    vertices[other]['position'] = [(1-weight)*a+weight*b for a, b in zip(base, alternate)]
    vertices[other]['blend_bone'], vertices[other]['blend_weight'] = secondary[index], weight
assert actual['skinned_mesh']['clamped_binding_indices'] == clamped
assert actual['skinned_mesh']['faces'] == mesh['faces']
assert len(vertices) == len(actual['skinned_mesh']['vertices'])
maximum = 0
for expected, observed in zip(vertices, actual['skinned_mesh']['vertices']):
    for field, value in expected.items():
        if isinstance(value, list):
            assert len(value) == len(observed[field])
            for a, b in zip(value, observed[field]):
                assert math.isfinite(a) and math.isfinite(b)
                error = abs(a-b); maximum = max(maximum, error)
                assert error <= 0.0001+0.00001*abs(a), (field, a, b)
        else:
            assert value == observed[field], field
print(json.dumps(dict(appearance_sha256=hashlib.sha256(payloads[appearance_entry]).hexdigest(),
    bmf_sha256=hashlib.sha256(payloads[mesh_entry]).hexdigest(), real_vertices=len(vertices),
    blend_vertices=len(secondary), triangles=len(mesh['faces']), raw_fields_equal=True,
    numerical_skinning_equal_with_tolerance=True, max_absolute_error=maximum,
    tolerance_absolute=0.0001, tolerance_relative=0.00001,
    consumed_bytes=r.cursor, remaining_bytes=mesh['remaining_bytes']), indent=2))
