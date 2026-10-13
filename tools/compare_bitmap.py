"""Check the C++ decoder's native RGBA against Pillow, without extracting BMPs.
Usage: compare_bitmap.py FAR BMP_ENTRY RGBA_OUTPUT
"""
import hashlib
import io
import json
import pathlib
import struct
import sys
from PIL import Image

archive, entry, output = sys.argv[1:]
with open(archive, 'rb') as stream:
    assert stream.read(8) == b'FAR!byAZ'
    version, index = struct.unpack('<II', stream.read(8)); assert version == 1
    stream.seek(index); count, = struct.unpack('<I', stream.read(4)); assert count <= 200000
    selected = []
    for _ in range(count):
        size, stored, offset, length = struct.unpack('<IIII', stream.read(16))
        assert length <= 1048576
        name = stream.read(length)
        if name == entry.encode('ascii'):
            assert size == stored and size <= 67108864
            selected.append((offset, size))
    offset, size = selected[0]; assert len(selected) == 1
    stream.seek(offset); payload = stream.read(size); assert len(payload) == size
with Image.open(io.BytesIO(payload)) as image:
    assert image.width*image.height <= 1048576
    expected = image.convert('RGBA').tobytes()
    dimensions = image.size
actual = pathlib.Path(output).read_bytes()
assert actual == expected, 'RGBA pixels differ from Pillow BMP decoding'
print(json.dumps(dict(input_sha256=hashlib.sha256(payload).hexdigest(),
    decoded_sha256=hashlib.sha256(actual).hexdigest(), width=dimensions[0], height=dimensions[1],
    decoded_bytes=len(actual), all_RGBA_bytes_equal=True), indent=2))
