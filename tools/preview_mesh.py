"""Diagnostic surface preview from odh-mesh reports, without textures.

Usage: preview_mesh.py OUTPUT.png MESH_REPORT [MESH_REPORT ...]
Pillow is a host-only preview dependency. Painter sorting and the provisional
orthographic projection below are not the game's camera/depth/compositor.
"""
import json
import math
import sys
from PIL import Image, ImageDraw, ImageFont

output, *reports = sys.argv[1:]
assert 1 <= len(reports) <= 16
vertices, triangles = [], []
for report in reports:
    with open(report, encoding='utf-8') as stream:
        source = json.load(stream)
    mesh = source['skinned_mesh']
    assert len(mesh['vertices']) <= 65536 and len(mesh['faces']) <= 131072
    offset = len(vertices)
    for vertex in mesh['vertices']:
        assert all(math.isfinite(x) for x in vertex['position']+vertex['normal'])
        vertices.append(vertex)
    for face in mesh['faces']:
        assert len(face) == 3 and all(0 <= i < len(mesh['vertices']) for i in face)
        triangles.append([offset+i for i in face])
assert vertices
projected = [(v['position'][0]-v['position'][2],
              (v['position'][0]+v['position'][2])*0.5-v['position'][1]) for v in vertices]
xmin, xmax = min(p[0] for p in projected), max(p[0] for p in projected)
ymin, ymax = min(p[1] for p in projected), max(p[1] for p in projected)
scale = min(640/max(xmax-xmin, 1e-9), 640/max(ymax-ymin, 1e-9))
points = [(384+(p[0]-(xmin+xmax)/2)*scale,
           425+(p[1]-(ymin+ymax)/2)*scale) for p in projected]
triangles.sort(key=lambda face: sum(sum(vertices[i]['position']) for i in face)/3)
image = Image.new('RGB', (768, 800), '#f5f5f2')
draw = ImageDraw.Draw(image)
for face in triangles:
    normal = [sum(vertices[i]['normal'][axis] for i in face)/3 for axis in range(3)]
    length = math.sqrt(sum(x*x for x in normal))
    light = max(0, (normal[0]*.3+normal[1]*.8+normal[2]*.5)/length) if length else 0
    gray = round(125+100*light)
    draw.polygon([points[i] for i in face], fill=(gray, gray, gray), outline=(105, 110, 115))
try:
    font = ImageFont.truetype('DejaVuSans.ttf', 18)
except OSError:
    font = ImageFont.load_default()
draw.text((24, 20), 'ODH: CPU skinned mesh / diagnostic surface', fill='#253047', font=font)
draw.text((24, 50), 'No textures; provisional projection and painter ordering', fill='#535a66', font=font)
draw.text((24, 765), f'{len(vertices)} vertices / {len(triangles)} triangles', fill='#535a66', font=font)
image.save(output)
