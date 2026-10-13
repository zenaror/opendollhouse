"""Original synthetic mesh CLI fixtures; no commercial game resources."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]
def u(n): return struct.pack('<I', n)
def i(n): return struct.pack('<i', n)
def pas(s): return bytes([len(s)]) + s
def bits(f): return struct.unpack('<I', struct.pack('<f', f))[0]
def floats(*v): return b''.join(u(bits(x)) for x in v)
def bone(name, parent, xyz):
    return pas(name) + pas(parent) + i(0) + floats(*xyz, 0., 0., 0., 1.) + i(1) + i(1) + i(0) + floats(0., 0.)
def skeleton(name=b'rig'):
    return pas(name) + struct.pack('<h', 2) + bone(b'ROOT', b'NULL', (1., 2., 3.)) + bone(b'child', b'ROOT', (0., 2., 0.))
def appearance(name=b'appearance', bindings=None):
    if bindings is None: bindings = [(b'root', b'MeSh', -2, 7), (b'child', b'unselected', 4, 9)]
    return pas(name) + i(3) + i(8) + u(len(bindings)) + b''.join(pas(b) + pas(m) + i(c) + i(z) for b, m, c, z in bindings)
def bmf(skin=b'mesh', weight=16384):
    # Three real vertices plus one child-local blend contribution to real #0.
    return (pas(skin) + pas(b'texture\n"') + i(2) + pas(b'root') + pas(b'CHILD') +
            i(1) + i(0) + i(1) + i(2) + i(2) +
            b''.join(i(x) for x in (0, 0, 3, 0, 0, 1, 0, 0, 0, 1)) +
            i(3) + floats(.1, .2, .3, .4, .5, .6) +
            i(1) + i(weight) + i(0) + i(4) +
            floats(1., 0., 0., 0., 1., 0.) + floats(0., 1., 0., 0., 1., 0.) +
            floats(0., 0., 1., 0., 1., 0.) + floats(1., 0., 0., 0., 1., 0.))
def motion(name, first):
    return pas(name) + b''.join(u(x) for x in (2, bits(100.), 1, 1, first, first, 0, 0))
def clip(name=b'clip'):
    return pas(name) + pas(b'pose') + b''.join(u(x) for x in (bits(100.), 0, 0, 4, 4, 2)) + motion(b'ROOT', 0) + motion(b'missing', 2)
def far(entries):
    data = b''.join(v for _, v in entries)
    index = u(len(entries)); offset = 16
    for name, value in entries:
        index += struct.pack('<IIII', len(value), len(value), offset, len(name)) + name
        offset += len(value)
    return b'FAR!byAZ' + u(1) + u(16 + len(data)) + data + index
skel = u(1) + skeleton() + u(0) + u(0)
app = u(0) + i(1) + appearance() + i(0)
anim = u(0) + u(0) + u(1) + clip()
planes = [[2., 4., 20., 30.], [3., 5., 20., 30.], [4., 6., 20., 30.], [0.] * 4, [0.] * 4, [0.] * 4, [1.] * 4]
cfp = b''.join(b'\xff' + u(bits(x)) for plane in planes for x in plane)
with tempfile.TemporaryDirectory(prefix='odh-mesh-') as folder:
    archive = Path(folder) / 'fixture.far'
    defaults = [(b'app.bcf', app), (b'mesh.bmf', bmf()), (b'rig.bcf', skel), (b'anim.bcf', anim), (b'pose.cfp', cfp)]
    args = ('app.bcf', 'appearance', 'mesh.bmf', 'rig.bcf', 'rig')
    animation = ('--animation', 'anim.bcf', 'clip', 'pose.cfp')
    def run(*tail, entries=None, selectors=args):
        archive.write_bytes(far(defaults if entries is None else entries))
        return subprocess.run([binary, str(archive), *selectors, *tail], capture_output=True, text=True)
    def good(r):
        assert r.returncode == 0 and not r.stderr, r.stderr
        return json.loads(r.stdout)
    def bad(r, message=None):
        assert r.returncode == 2 and not r.stdout and r.stderr, r
        if message: assert message in r.stderr, r.stderr
    def replace(name, value): return [(n, value if n == name else v) for n, v in defaults]
    d = good(run())
    assert d['evidence'] == 'ODH_CPU_skinned_mesh_reference_profile'
    assert d['renderer'] == 'none' and d['textures_decoded'] is False
    assert d['sources']['bmf_entry'] == 'mesh.bmf'
    a = d['appearance']; assert a['name'] == 'appearance' and a['type'] == 3 and a['zero'] == 8
    assert a['selected_binding'] == 0 and a['bindings'] == [dict(bone='root', mesh_name='MeSh', censor_flags=-2, zero=7), dict(bone='child', mesh_name='unselected', censor_flags=4, zero=9)]
    raw = d['raw_mesh']; assert raw['skin_name'] == 'mesh' and raw['texture_name'] == 'texture\n"'
    assert raw['bones'] == ['root', 'CHILD'] and raw['faces'] == [[0, 1, 2]]
    assert raw['real_vertices'][0]['position_bits'] == [bits(1.), 0, 0]
    assert raw['real_vertices'][0]['texcoord_bits'] == [bits(.1), bits(.2)]
    assert raw['blend_vertices'][0]['texcoord_bits'] == [0, 0]
    assert raw['blend_data'] == [dict(weight=16384, other_vertex=0)]
    assert raw['vertex_count2'] == 4 and raw['remaining_bytes'] == 0
    skinned = d['skinned_mesh']; assert skinned['faces'] == [[0, 1, 2]] and len(skinned['vertices']) == 3
    v = skinned['vertices'][0]
    assert v['position'] == [-2., 3., 3.] and v['normal'] == [0., 1., 0.], v
    assert v['primary_bone'] == 0 and v['blend_bone'] == 1 and v['blend_weight'] == .5
    assert abs(v['texcoord'][0] - .1) < 1e-7
    d = good(run(*animation, '--frame', '.5'))
    assert d['pose']['requested_frame'] == .5 and d['pose']['fraction'] == .5 and d['pose']['missing_motion_bones'] == ['missing']
    assert d['skinned_mesh']['vertices'][0]['position'] == [-4., 5., 5.]
    for weight, y in ((0, 2.), (32768, 4.)):
        d = good(run(entries=replace(b'mesh.bmf', bmf(weight=weight))))
        assert d['skinned_mesh']['vertices'][0]['position'] == [-2., y, 3.]
    for tail in [('--frame', '1'), ('--unknown',), ('--obj',), ('--obj', 'a', '--obj', 'b'), animation + animation, animation + ('--frame', '0', '--frame', '1')]: bad(run(*tail))
    for number in ('NaN', 'inf', '-inf', '1junk', '1 ', ' 1', '', '1e999'): bad(run(*animation, '--frame', number))
    for index, value in ((0, 'APP.bcf'), (1, 'APPEARANCE'), (2, 'MESH.bmf'), (3, 'RIG.bcf'), (4, 'RIG')):
        selectors = list(args); selectors[index] = value; bad(run(selectors=selectors))
    bad(run('--animation', 'anim.bcf', 'CLIP', 'pose.cfp'))
    bad(run('--animation', 'anim.bcf', 'clip', 'wrong.cfp'), 'xskill')
    bad(run(entries=defaults + [(b'app.bcf', app)]), 'ambiguous')
    bad(run(entries=replace(b'app.bcf', u(0) + i(2) + appearance() + appearance() + i(0))), 'ambiguous')
    bad(run(entries=replace(b'app.bcf', u(0) + i(1) + appearance(bindings=[(b'root', b'mesh', 0, 0)] * 2) + i(0))), 'ambiguous')
    bad(run(entries=replace(b'app.bcf', u(0) + i(1) + appearance(bindings=[(b'absent', b'mesh', 0, 0)]) + i(0))), 'bone')
    bad(run(entries=replace(b'mesh.bmf', bmf(skin=b'wrong'))), 'skin name')
    bad(run(entries=replace(b'mesh.bmf', bmf()[:-1])))
    obj = Path(folder) / 'mesh.obj'
    good(run('--obj', str(obj)))
    lines = obj.read_text().splitlines()
    assert len([x for x in lines if x.startswith('v ')]) == 3
    assert len([x for x in lines if x.startswith('vt ')]) == 3
    assert len([x for x in lines if x.startswith('vn ')]) == 3
    assert [x for x in lines if x.startswith('f ')] == ['f 1/1/1 2/2/2 3/3/3']
    assert abs(float(next(x for x in lines if x.startswith('vt ')).split()[1]) - .1) < 1e-7
    assert not any(x.startswith(('mtllib ', 'usemtl ')) for x in lines)
    bad(run('--obj', str(Path(folder) / 'absent' / 'out.obj')), 'OBJ')
    bad(run('--obj', '/dev/full'), 'OBJ')
print('Mesh CLI passed (original synthetic fixtures only)')
