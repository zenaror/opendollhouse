"""Independent raw-skeleton and numerical pose oracle; never extracts assets.

Usage: compare_pose.py FAR SKELETON_ENTRY POSE_JSON [ANIMATION_CFP_JSON]
The optional report comes from odh-animation --cfp, whose raw planes have their
own independent compare_cfp.py gate. Pose calculations below use Python doubles;
comparison is numerical, not a claim of float32 or original-runtime equivalence.
"""
import hashlib
import json
import math
import struct
import sys

archive, entry, report, *animation_reports = sys.argv[1:]
assert len(animation_reports) <= 1
with open(report, encoding='utf-8') as stream:
    result = json.load(stream)
with open(archive, 'rb') as stream:
    assert stream.read(8) == b'FAR!byAZ'
    version, index = struct.unpack('<II', stream.read(8)); assert version == 1
    stream.seek(index); count, = struct.unpack('<I', stream.read(4))
    assert count <= 200000
    matches = []
    for _ in range(count):
        size, stored, offset, length = struct.unpack('<IIII', stream.read(16))
        assert length <= 1048576
        name = stream.read(length)
        if name == entry.encode('ascii'):
            assert size == stored and size <= 67108864
            matches.append((offset, size))
    assert len(matches) == 1
    stream.seek(matches[0][0]); payload = stream.read(matches[0][1])
    assert len(payload) == matches[0][1]
cursor = 0
def take(size):
    global cursor
    assert size >= 0 and cursor + size <= len(payload)
    data = payload[cursor:cursor+size]; cursor += size
    return data
def number(fmt):
    return struct.unpack('<'+fmt, take(struct.calcsize('<'+fmt)))[0]
def text():
    return take(number('B')).decode('latin1')
def words(count):
    return [number('I') for _ in range(count)]
collections = []
for _ in range(number('i')):
    name, count = text(), number('h'); assert 0 <= count <= 4096
    bones = []
    while len(bones) < count:
        bone_name, parent = text(), text()
        if not bone_name:
            continue
        groups = []
        for _ in range(number('i')):
            groups.append([[text(), text()] for _ in range(number('i'))])
        bones.append(dict(name=bone_name, parent=parent, properties=groups,
            raw_translation_bits=words(3), raw_rotation_bits=words(4),
            can_translate=number('i'), can_rotate=number('i'), can_blend=number('i'),
            wiggle_value_bits=number('I'), wiggle_power_bits=number('I')))
    collections.append((name, bones))
assert number('i') == number('i') == 0
selected = [b for n, b in collections if n == result['skeleton']]
assert len(selected) == 1
raw_bones = selected[0]
assert len(raw_bones) == len(result['bones'])
for raw, actual in zip(raw_bones, result['bones']):
    for field, value in raw.items():
        if field != 'parent':
            assert value == actual[field], (raw['name'], field)

def floating(bits):
    value, = struct.unpack('<f', struct.pack('<I', bits))
    assert math.isfinite(value)
    return value
def translation(bits):
    x, y, z = map(floating, bits)
    return [-x, y, z]
def rotation(bits):
    x, y, z, w = map(floating, bits)
    return [x, -y, -z, -w]
def lerp(a, b, fraction):
    return [x+(y-x)*fraction for x, y in zip(a, b)]
def slerp(a, b, fraction):
    dot = sum(x*y for x, y in zip(a, b))
    sign = -1 if dot < 0 else 1
    dot = abs(dot)
    if dot > 0.999999:
        left, right = 1-fraction, fraction*sign
    else:
        angle = math.acos(dot)
        denominator = math.sin(angle)
        left = math.sin((1-fraction)*angle)/denominator
        right = sign*math.sin(fraction*angle)/denominator
    return [left*x+right*y for x, y in zip(a, b)]
local = [(translation(b['raw_translation_bits']), rotation(b['raw_rotation_bits']))
         for b in raw_bones]
lookup = {b['name']: i for i, b in enumerate(raw_bones)}
assert len(lookup) == len(raw_bones)
missing = []
if animation_reports:
    with open(animation_reports[0], encoding='utf-8') as stream:
        animation_report = json.load(stream)
    animation, = animation_report['animations']
    assert result['animation'] == animation['name']
    planes = animation_report['cfp']['raw_float_planes']
    frame, fraction = result['frame'], result['fraction']
    for motion in animation['motions']:
        if motion['bone'] not in lookup:
            missing.append(motion['bone']); continue
        index = lookup[motion['bone']]
        assert motion['frames'] > 0
        current = min(frame, motion['frames']-1)
        next_frame = min(current+1, motion['frames']-1)
        trans, quat = local[index]
        if motion['has_translation']:
            start = motion['first_translation']
            trans = lerp(translation([p[start+current] for p in planes[:3]]),
                         translation([p[start+next_frame] for p in planes[:3]]), fraction)
        if motion['has_rotation']:
            start = motion['first_rotation']
            quat = slerp(rotation([p[start+current] for p in planes[3:]]),
                         rotation([p[start+next_frame] for p in planes[3:]]), fraction)
        local[index] = trans, quat
assert result['missing_motion_bones'] == missing

def matrix(trans, quat):
    x, y, z, w = quat
    return [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), trans[0]],
            [2*(x*y+z*w), 1-2*(z*z+x*x), 2*(y*z-x*w), trans[1]],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(y*y+x*x), trans[2]],
            [0, 0, 0, 1]]
def multiply(a, b):
    return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)]
cache = {}
def absolute(index):
    if index in cache:
        return cache[index]
    parent = raw_bones[index]['parent']
    value = matrix(*local[index])
    if parent != 'NULL':
        value = multiply(absolute(lookup[parent]), value)
    cache[index] = value
    return value
max_error = 0
def close(expected, actual):
    global max_error
    assert len(expected) == len(actual)
    for a, b in zip(expected, actual):
        assert math.isfinite(a) and math.isfinite(b)
        error = abs(a-b); max_error = max(max_error, error)
        assert error <= 0.0001 + 0.00001*abs(a), (a, b, error)
for index, bone in enumerate(result['bones']):
    close(local[index][0], bone['translation'])
    close(local[index][1], bone['rotation'])
    value = absolute(index)
    close([x for row in value[:3] for x in row], bone['absolute_matrix'])
    close([value[i][3] for i in range(3)], bone['absolute_position'])
    parent = raw_bones[index]['parent']
    assert bone['parent_index'] == (-1 if parent == 'NULL' else lookup[parent])
print(json.dumps(dict(skeleton_entry=entry, input_sha256=hashlib.sha256(payload).hexdigest(),
    bones=len(raw_bones), compared_raw_fields=True, numerical_pose_equal_with_tolerance=True,
    max_absolute_error=max_error, tolerance_absolute=0.0001, tolerance_relative=0.00001,
    consumed_bytes=cursor, trailing_bytes=len(payload)-cursor), indent=2))
