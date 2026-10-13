"""Original synthetic textured renderer fixtures; no commercial resources."""
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]
def u(n): return struct.pack('<I', n)
def i(n): return struct.pack('<i', n)
def pas(s): return bytes([len(s)]) + s
def f(*xs): return struct.pack('<' + 'f' * len(xs), *xs)
def far(entries):
    data = b''.join(v for _, v in entries)
    index = u(len(entries)); offset = 16
    for name, value in entries:
        index += struct.pack('<IIII', len(value), len(value), offset, len(name)) + name
        offset += len(value)
    return b'FAR!byAZ' + u(1) + u(16 + len(data)) + data + index

def app(name=b'appearance', bone=b'ROOT', mesh=b'mesh'):
    return u(0) + u(1) + pas(name) + i(3) + i(7) + u(1) + pas(bone) + pas(mesh) + i(0) + i(0) + u(0)
def rig():
    bone = pas(b'ROOT') + pas(b'NULL') + i(0) + f(0, 0, 0, 0, 0, 0, 1) + i(1) + i(1) + i(0) + f(0, 0)
    return u(1) + pas(b'rig') + struct.pack('<h', 1) + bone + u(0) + u(0)
def bmf(skin=b'mesh', shift=0., normal=(0., -1., 0.), faces=1, verts=3):
    # Projection-invariant displacement toward camera allows shared-depth tests.
    d = math.sqrt(3 / 8) * shift
    points = [(-1., 2., 0.), (1., 2., 0.), (0., 4., 0.)]
    positions = [(-x - d, y + shift / 2, z + d) for x, y, z in points]
    return (pas(skin) + pas(b'x') + i(1) + pas(b'root') + i(faces) +
            (i(0) + i(1) + i(2)) * faces + i(1) +
            b''.join(i(n) for n in (0, 0, verts, 0, 0)) + i(verts) +
            f(*([.25, .25] * verts)) + i(0) + i(verts) +
            b''.join(f(*positions[j % 3], *normal) for j in range(verts)))
def bmp(rgb=(200, 40, 20), width=1, height=1):
    stride = (width * 3 + 3) & ~3
    payload = (bytes((rgb[2], rgb[1], rgb[0])) * width + bytes(stride - width * 3)) * height
    return b'BM' + u(54 + len(payload)) + bytes(4) + u(54) + u(40) + i(width) + i(height) + struct.pack('<HH', 1, 24) + u(0) + u(len(payload)) + bytes(16) + payload

def anim():
    motion = pas(b'ROOT') + u(2) + f(100) + b''.join(u(n) for n in (1, 1, 0, 0, 0, 0))
    return u(0) + u(0) + u(1) + pas(b'clip') + pas(b'pose') + f(100) + b''.join(u(n) for n in (0, 0, 2, 2, 1)) + motion
cfp = b''.join(b'\xff' + f(v) for plane in ([0., 0.], [0., 0.], [0., 0.], [0., 0.], [0., 0.], [0., 0.], [1., 1.]) for v in plane)
with tempfile.TemporaryDirectory(prefix='odh-render-') as folder:
    folder = Path(folder); archive = folder / 'animation.far'; texture_archive = folder / 'textures.far'; output = folder / 'render.ppm'
    defaults = [(b'rig.bcf', rig()), (b'app.bcf', app()), (b'mesh.bmf', bmf()), (b'anim.bcf', anim()), (b'pose.cfp', cfp), (b'head.bcf', app(b'head', mesh=b'head')), (b'head.bmf', bmf(b'head', shift=2))]
    textures = [(b'red.bmp', bmp()), (b'blue.bmp', bmp((20, 40, 200)))]
    part = ('--part', 'app.bcf', 'appearance', 'mesh.bmf', 'red.bmp')
    head = ('--part', 'head.bcf', 'head', 'head.bmf', 'blue.bmp')
    animation = ('--animation', 'anim.bcf', 'clip', 'pose.cfp')
    def run(*tail, entries=None, tex=None, parts=part, selectors=('rig.bcf', 'rig'), common=True):
        archive.write_bytes(far(defaults if entries is None else entries)); texture_archive.write_bytes(far(textures if tex is None else tex))
        args = [binary, str(archive), *selectors, str(texture_archive), *parts]
        if common: args += ['--output', str(output), '--width', '16', '--height', '16', '--scale', '2']
        return subprocess.run([*args, *tail], capture_output=True, text=True)
    def good(r):
        assert r.returncode == 0 and not r.stderr, r.stderr
        return json.loads(r.stdout)
    def bad(r, message=None):
        assert r.returncode == 2 and not r.stdout and r.stderr, r
        if message: assert message in r.stderr, r.stderr
    def replace(name, value): return [(n, value if n == name else v) for n, v in defaults]
    def pixels():
        raw = output.read_bytes(); header = b'P6\n16 16\n255\n'
        assert raw.startswith(header) and len(raw) == len(header) + 16 * 16 * 3
        raw = raw[len(header):]
        return [tuple(raw[j:j+3]) for j in range(0, len(raw), 3)]
    d = good(run()); p = pixels()
    assert d['evidence'] == 'ODH_portable_CPU_textured_mesh_render'
    assert d['camera']['original_calibrated'] is False
    assert d['vertices'] == 3 and d['triangles'] == 1 and d['textures'] == 1
    assert d['framebuffer_bytes'] == 2048 and d['texture_bytes'] == 4
    assert 0 < d['covered_pixels'] < 256 and d['colored_pixels'] == d['covered_pixels']
    assert p.count((200, 40, 20)) == d['colored_pixels']
    assert d['sources']['parts'][0]['bmf_texture_name'] == 'x'
    assert d['sources']['parts'][0]['texture_entry'] == 'red.bmp'
    assert d['pose']['frame'] == 0 and d['options']['filter'] == 'bilinear_wrap'
    assert all(math.isfinite(v) for v in d['depth_range'])
    good(run('--nearest')); assert pixels() == p
    d = good(run('--lit')); assert d['options']['lighting'] is True
    assert pixels().count((100, 20, 10)) == d['covered_pixels']
    d = good(run(parts=part + head)); assert d['triangles'] == 2 and d['textures'] == 2
    assert pixels().count((20, 40, 200)) == d['covered_pixels']
    good(run(parts=head + part)); assert pixels().count((20, 40, 200)) == d['covered_pixels']
    d = good(run(*animation, '--frame', '.5')); assert d['pose']['fraction'] == .5
    d = good(run(*animation, '--frame', '200')); assert d['pose']['completed'] is True
    d = good(run(entries=replace(b'mesh.bmf', bmf(shift=-100))))
    # Camera-direction displacement retains the projection, even out of scene.
    assert d['covered_pixels'] > 0
    # Empty face list produces valid all-background evidence without infinities.
    d = good(run(entries=replace(b'mesh.bmf', bmf(faces=0))))
    assert d['covered_pixels'] == 0 and d['depth_range'] == [None, None]
    for tail in [('--wat',), ('--frame', '0'), ('--nearest', '--nearest'), ('--lit', '--lit'), animation + animation, animation + ('--frame', '0', '--frame', '1'), ('--width', '1'), ('--height', '1'), ('--scale', '1'), ('--output', 'x')]: bad(run(*tail))
    for opt in ('--animation', '--part', '--frame', '--width', '--height', '--scale', '--output'): bad(run(opt))
    bad(run(parts=()), 'part'); bad(run(common=False), 'output')
    for n in ('NaN', 'inf', '-inf', '1junk', '1 ', ' 1', '', '1e999'):
        bad(run(*animation, '--frame', n)); bad(run('--scale', n, common=False))
    for n in ('0', '-1', '1.2', '2049', '999999999999999999999', '+16'):
        bad(run('--output', str(output), '--width', n, common=False))
    bad(run('--output', str(output), '--width', '2048', '--height', '2048', common=False), 'pixel limit')
    bad(run('--output', str(output), '--scale', '0', common=False))
    bad(run('--output', str(output), '--scale', '1e308', common=False), 'projection')
    bad(run(parts=part * 17), 'part limit')
    bad(run(parts=part * 2, tex=[(b'red.bmp', bmp(width=1024, height=1024))]), 'cumulative texture')
    bad(run(parts=part * 2, entries=replace(b'mesh.bmf', bmf(verts=32769))), 'cumulative geometry')
    bad(run(parts=part * 2, entries=replace(b'mesh.bmf', bmf(faces=65537))), 'cumulative triangle')
    bad(run(selectors=('RIG.bcf', 'rig'))); bad(run(selectors=('rig.bcf', 'RIG')))
    for altered in [('--part', 'APP.bcf', 'appearance', 'mesh.bmf', 'red.bmp'), ('--part', 'app.bcf', 'APPEARANCE', 'mesh.bmf', 'red.bmp'), ('--part', 'app.bcf', 'appearance', 'MESH.bmf', 'red.bmp'), ('--part', 'app.bcf', 'appearance', 'mesh.bmf', 'RED.bmp')]: bad(run(parts=altered))
    bad(run('--animation', 'anim.bcf', 'CLIP', 'pose.cfp'))
    bad(run('--animation', 'anim.bcf', 'clip', 'wrong.cfp'), 'xskill')
    bad(run(entries=defaults + [(b'app.bcf', app())]), 'ambiguous')
    bad(run(entries=replace(b'app.bcf', app(bone=b'missing'))), 'bone')
    bad(run(entries=replace(b'mesh.bmf', bmf(skin=b'wrong'))), 'skin name')
    bad(run(tex=[(b'red.bmp', bmp()[:-1])]))
    bad(run(entries=replace(b'mesh.bmf', bmf()[:-1])))
    bad(run('--output', str(folder / 'absent' / 'out.ppm'), common=False), 'PPM')
    bad(run('--output', '/dev/full', common=False), 'PPM')
print('Render CLI passed (original synthetic fixtures only)')
