"""Original synthetic FAR/BCF/CFP fixtures; no commercial game resources."""
import json
import math
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

binary = sys.argv[1]
def u(n): return struct.pack('<I', n)
def i(n): return struct.pack('<i', n)
def pas(s): return bytes([len(s)]) + s
def bits(f): return struct.unpack('<I', struct.pack('<f', f))[0]
def bone(name, parent, translation=(0., 0., 0.)):
    groups = i(1) + i(1) + pas(b'key') + pas(b'value<&"')
    return (pas(name) + pas(parent) + groups +
            b''.join(u(bits(f)) for f in (*translation, 0., 0., 0., 1.)) +
            i(1) + i(1) + i(0) + u(bits(.25)) + u(bits(.5)))
def skeleton(name=b'rig<&"'):
    return pas(name) + struct.pack('<h', 2) + bone(b'ROOT', b'NULL', (1., 2., 3.)) + bone(b'child<&"', b'ROOT', (0., 1., 0.))
def motion(name, first):
    return pas(name) + b''.join(u(n) for n in [2, bits(100.), 1, 1, first, first, 0, 0])
def clip(name=b'clip'):
    return (pas(name) + pas(b'pose') + b''.join(u(n) for n in [bits(100.), 0, 0, 4, 4, 2]) +
            motion(b'ROOT', 0) + motion(b'missing', 2))
def far(entries):
    data = b''.join(v for _, v in entries)
    index = u(len(entries)); offset = 16
    for name, value in entries:
        index += struct.pack('<IIII', len(value), len(value), offset, len(name)) + name
        offset += len(value)
    return b'FAR!byAZ' + u(1) + u(16 + len(data)) + data + index
skeleton_bcf = u(1) + skeleton() + u(0) + u(0)
animation_bcf = u(0) + u(0) + u(1) + clip()
# Explicit float escapes, four samples per plane, positive authored root X/Y/Z.
planes = [[2., 4., 20., 30.], [3., 5., 20., 30.], [4., 6., 20., 30.],
          [0.] * 4, [0.] * 4, [0.] * 4, [1.] * 4]
cfp = b''.join(b'\xff' + u(bits(n)) for plane in planes for n in plane)
with tempfile.TemporaryDirectory(prefix='odh-pose-') as folder:
    archive = Path(folder) / 'fixture.far'
    defaults = [(b'rig.bcf', skeleton_bcf), (b'anim.bcf', animation_bcf), (b'pose.cfp', cfp)]
    def run(*args, entries=None):
        archive.write_bytes(far(defaults if entries is None else entries))
        return subprocess.run([binary, str(archive), *args], capture_output=True, text=True)
    def good(r):
        assert r.returncode == 0 and not r.stderr, r.stderr
        return json.loads(r.stdout)
    def bad(r, message=None):
        assert r.returncode == 2 and not r.stdout and r.stderr, r
        if message: assert message in r.stderr, r.stderr
    args = ('rig.bcf', 'rig<&"')
    animation = ('--animation', str(archive), 'anim.bcf', 'clip', 'pose.cfp')
    d = good(run(*args))
    assert d['evidence'] == 'ODH_skeletal_pose_reference_profile'
    assert d['skeleton'] == 'rig<&"' and d['animation'] == ''
    root, child = d['bones']
    assert root['parent_index'] == -1 and child['parent_index'] == 0
    assert root['raw_translation_bits'] == [bits(1.), bits(2.), bits(3.)]
    assert root['raw_rotation_bits'] == [0, 0, 0, bits(1.)]
    assert [root[k] for k in ('can_translate', 'can_rotate', 'can_blend')] == [1, 1, 0]
    assert root['properties'] == [[['key', 'value<&"']]]
    assert root['wiggle_value_bits'] == bits(.25) and root['wiggle_power_bits'] == bits(.5)
    # Reference profile flips translation X and quaternion Y/Z signs.
    assert root['translation'] == [-1., 2., 3.] and root['absolute_position'] == [-1., 2., 3.]
    assert child['absolute_position'] == [-1., 3., 3.]
    assert len(root['absolute_matrix']) == 12
    d = good(run(*args, *animation, '--frame', '.5'))
    assert d['requested_frame'] == .5 and d['frame'] == 0 and d['fraction'] == .5
    assert d['missing_motion_bones'] == ['missing'] and not d['completed']
    assert d['bones'][0]['translation'] == [-3., 4., 5.]
    assert d['bones'][1]['absolute_position'] == [-3., 5., 5.]
    d = good(run(*args, *animation, '--frame', '100'))
    assert d['completed'] and d['bones'][0]['translation'] == [-4., 5., 6.]
    d = good(run(*args, *animation, '--frame', '-.5'))
    assert d['completed'] and d['frame'] == 0 and d['fraction'] == 0
    for tail in [('--frame', '1'), ('--unknown',), ('--svg',), ('--svg', 'a', '--svg', 'b'), animation + animation,
                 animation + ('--frame', '0', '--frame', '1')]:
        bad(run(*args, *tail))
    for number in ('NaN', 'inf', '-inf', '1junk', '1 ', ' 1', '', '1e999'):
        bad(run(*args, *animation, '--frame', number))
    bad(run('RIG.bcf', args[1]))
    bad(run(args[0], 'RIG<&"'))
    bad(run(*args, '--animation', str(archive), 'anim.bcf', 'CLIP', 'pose.cfp'))
    bad(run(*args, '--animation', str(archive), 'anim.bcf', 'clip', 'wrong.cfp'), 'xskill')
    bad(run(*args, entries=[(b'rig.bcf', skeleton_bcf), (b'rig.bcf', skeleton_bcf)]), 'ambiguous')
    bad(run(*args, entries=[(b'rig.bcf', u(2) + skeleton() + skeleton() + u(0) + u(0))]), 'ambiguous')
    bad(run(*args, *animation, entries=[(b'rig.bcf', skeleton_bcf),
        (b'anim.bcf', u(0) + u(0) + u(2) + clip() + clip()), (b'pose.cfp', cfp)]), 'ambiguous')
    bad(run(*args, entries=[(b'rig.bcf', skeleton_bcf[:-1])]))
    bad(run(*args, *animation, entries=[(b'rig.bcf', skeleton_bcf),
        (b'anim.bcf', animation_bcf), (b'pose.cfp', cfp[:-1])]))
    svg = Path(folder) / 'diagnostic.svg'
    good(run(*args, '--svg', str(svg)))
    tree = ET.fromstring(svg.read_text())
    ns = {'svg': 'http://www.w3.org/2000/svg'}
    assert 'no mesh or game rendering' in tree.find('svg:title', ns).text
    assert args[1] in tree.find('svg:title', ns).text
    assert len(tree.findall('.//svg:line', ns)) == 1 and len(tree.findall('.//svg:circle', ns)) == 2
    assert tree.findall('.//svg:circle/svg:title', ns)[1].text == 'child<&"'
    for element in tree.findall('.//svg:circle', ns):
        assert all(math.isfinite(float(element.attrib[k])) for k in ('cx', 'cy'))
    bad(run(*args, '--svg', str(Path(folder) / 'absent' / 'out.svg')), 'SVG')
    bad(run(*args, '--svg', '/dev/full'), 'SVG')
print('Pose CLI passed (original synthetic fixtures only)')
