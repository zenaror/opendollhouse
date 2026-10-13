"""Independent Python float32 oracle; reads a FAR entry and ODH JSON, writes no assets."""
import hashlib
import json
import math
import struct
import sys

archive, entry, report = sys.argv[1:]
with open(report, encoding='utf-8') as f:
    result = json.load(f)
animation = result['animations'][0]
with open(archive, 'rb') as f:
    assert f.read(8) == b'FAR!byAZ'
    version, index = struct.unpack('<II', f.read(8)); assert version == 1
    f.seek(index); count, = struct.unpack('<I', f.read(4)); assert count <= 200000
    matches = []
    for _ in range(count):
        size, stored, offset, length = struct.unpack('<IIII', f.read(16))
        assert length <= 1048576
        name = f.read(length)
        if name == entry.encode('ascii'):
            assert size == stored and size <= 67108864
            matches.append((offset, size))
    assert len(matches) == 1
    f.seek(matches[0][0]); payload = f.read(matches[0][1])
    assert len(payload) == matches[0][1]
cursor = 0
def take(size):
    global cursor
    assert cursor+size <= len(payload)
    value = payload[cursor:cursor+size]; cursor += size
    return value
def float32(value): return struct.unpack('<f', struct.pack('<f', value))[0]
planes = []
for size in [animation['translations']]*3 + [animation['rotations']]*4:
    assert size <= 1048576
    previous = 0.0; values = []
    while len(values) < size:
        code = take(1)[0]
        if code == 255:
            previous, = struct.unpack('<f', take(4)); repeat = 1
        elif code == 254:
            repeat = 1 + struct.unpack('<H', take(2))[0]
        else:
            assert code != 253
            n = code-126
            previous = float32(previous + float32(3.9676e-10 * n**3 * abs(n)))
            repeat = 1
        assert math.isfinite(previous) and len(values)+repeat <= size
        bits, = struct.unpack('<I', struct.pack('<f', previous))
        values.extend([bits]*repeat)
    planes.append(values)
assert planes == result['cfp']['raw_float_planes'], 'CFP float bits differ'
assert len(payload)-cursor == result['cfp']['remaining_bytes']
decoded = b''.join(struct.pack('<I', bits) for plane in planes for bits in plane)
print(json.dumps({'input_sha256':hashlib.sha256(payload).hexdigest(),
                  'decoded_sha256':hashlib.sha256(decoded).hexdigest(),
                  'decoded_bytes':len(decoded), 'consumed_bytes':cursor,
                  'equal_all_float_bits':True}, indent=2))
