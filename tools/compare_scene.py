"""Independent sprite placement, depth and composition check against a mesh base.
Usage: BASE.json BASE.ppm BASE.depth SPRITE.json SPRITE_PREFIX SCENE.json SCENE.ppm SCENE.depth
The base is the same mesh/camera render with the object moved outside the viewport.
Inputs are decoded native exports; this is not a GPU or original-game oracle.
"""
import hashlib
import json
import math
import pathlib
import struct
import sys

base_report, base_path, base_depth_path, sprite_report, sprite_prefix, scene_report, out_path, out_depth_path = sys.argv[1:]
scene = json.loads(pathlib.Path(scene_report).read_text())
meta = json.loads(pathlib.Path(sprite_report).read_text())
base_meta = json.loads(pathlib.Path(base_report).read_text())
for key in ('camera','sources','pose','options'):assert base_meta[key] == scene[key], 'mesh base configuration differs'
for key in ('center_tile','avatar_position','avatar_direction','zoom','rotation','object_far','object_entry','object_id','group_id','graphic_index','object_direction'):
    assert base_meta['scene'][key] == scene['scene'][key], key
assert base_meta['scene']['sprite_tested'] == 0 and base_meta['scene']['sprite_shaded'] == 0, 'base contains object pixels'
camera = scene['scene']; width = scene['options']['width']; height = scene['options']['height']
assert meta['sources']['far'] == camera['object_far'] and meta['sources']['iff_entry'] == camera['object_entry'], 'sprite source graph differs'
assert meta['objd']['id'] == camera['object_id'] and meta['objd']['group_id'] == camera['group_id'], 'sprite object graph differs'
assert meta['objd']['graphic_index'] == camera['graphic_index']
assert len(camera['layers']) == 1, 'oracle supports a single selected DGRP layer'
assert camera['alpha_profile'] == 'ODH_straight_source_over_depth_write'
assert scene['camera']['original_calibrated'] is False
assert meta['export_pixel_orientation'] == 'raw_frame_unflipped'
assert meta['dgrp']['zoom'] == camera['zoom'] and meta['dgrp']['rotation'] == camera['rotation']
assert meta['dgrp']['requested_direction'] == 1 << camera['object_direction']
layer = camera['layers'][0]; frame = meta['spr2']; ref = meta['dgrp']
assert layer['sprite_id'] == ref['sprite_id'] and layer['frame'] == ref['frame']
assert layer['width'] == frame['width'] and layer['height'] == frame['height']

def f32(v): return struct.unpack('<f', struct.pack('<f', v))[0]
def ppm(path):
    value = pathlib.Path(path).read_bytes(); header = f'P6\n{width} {height}\n255\n'.encode()
    assert value.startswith(header) and len(value) == len(header)+width*height*3
    return bytearray(value[len(header):])
def tile_world(p): return [3*p[0], 3*p[2], 3*p[1]]
def axes(p):
    x,y,z = p
    return [(x-z,x+z),(-x-z,x-z),(-x+z,-x-z),(x+z,-x+z)][camera['rotation']]
def project(p):
    c = tile_world(camera['center_tile']); p = [a-b for a,b in zip(p,c)]
    horizontal, forward = axes(p); tile_width = 32 << (camera['zoom']-1)
    return (width/2 + tile_width*horizontal/6,
            height/2 + tile_width*forward/12-tile_width*math.sqrt(3/8)*p[1]/3,
            -math.sqrt(3/8)*forward-.5*p[1])

offset = [struct.unpack('<f',struct.pack('<I',b))[0]/d for b,d in zip(ref['object_offset_bits'],(16,16,5))]
a = f32(camera['object_direction']*math.pi/4); x,y,z = offset
relative = [x*math.cos(a)-y*math.sin(a), x*math.sin(a)+y*math.cos(a),z]
rel_world = tile_world(relative); h,forward = axes(rel_world); tw=32 << (camera['zoom']-1)
cadge_half=17 << (camera['zoom']-1); baseline=87 << (camera['zoom']-1)
local_x=math.trunc(cadge_half+ref['x']+tw*h/6)-cadge_half
local_y=math.trunc(baseline+ref['y']-frame['height']+tw*forward/12-tw*math.sqrt(3/8)*rel_world[1]/3)-baseline
origin=tile_world(camera['object_position']); screen=project(origin)
anchor=[(0,tw/4),(-tw/2,0),(0,-tw/4),(tw/2,0)][camera['rotation']]
x=math.trunc(screen[0]+local_x+anchor[0]); y=math.trunc(screen[1]+local_y+anchor[1])
assert (layer['x'],layer['y'])==(x,y), 'placement differs'
back=[(.15,0,.15),(.15,0,2.85),(2.85,0,2.85),(2.85,0,.15)][camera['rotation']]
back_depth=f32(project([o+r+b for o,r,b in zip(origin,rel_world,back)])[2])
delta=f32(-3*math.sqrt(3/2)); assert layer['back_depth']==back_depth and layer['front_depth_delta']==delta
flip=bool(ref['flags']&1); assert layer['flip'] == flip
rgba=pathlib.Path(sprite_prefix+'.rgba').read_bytes(); zs=pathlib.Path(sprite_prefix+'.z').read_bytes()
sw,sh=frame['width'],frame['height']; assert len(rgba)==sw*sh*4 and len(zs)==sw*sh
color=ppm(base_path); raw=pathlib.Path(base_depth_path).read_bytes(); assert len(raw)==width*height*4
depth=list(struct.unpack('<'+str(width*height)+'f',raw))
counts=dict(tested=0,shaded=0,transparent=0,occluded=0)
for dy in range(max(0,y),min(height,y+sh)):
    for dx in range(max(0,x),min(width,x+sw)):
        counts['tested']+=1
        sx=dx-x; sx=sw-1-sx if flip else sx; sy=dy-y; source=sy*sw+sx
        alpha=rgba[source*4+3]
        if alpha==0:counts['transparent']+=1;continue
        z=f32(back_depth+((1-zs[source]/255)/.4)*delta); at=dy*width+dx
        if not z<depth[at]:counts['occluded']+=1;continue
        for c in range(3): color[at*3+c]=(rgba[source*4+c]*alpha+color[at*3+c]*(255-alpha)+127)//255
        depth[at]=z;counts['shaded']+=1
assert color==ppm(out_path), 'composited RGB bytes differ'
expected=struct.pack('<'+str(len(depth))+'f',*depth)
assert expected==pathlib.Path(out_depth_path).read_bytes(), 'composited float32 depth bytes differ'
for key,value in counts.items(): assert camera['sprite_'+key]==value,key
print(json.dumps(dict(all_RGB_depth_bytes_equal=True,independent_placement_equal=True,
                     counts=counts,ppm_sha256=hashlib.sha256(pathlib.Path(out_path).read_bytes()).hexdigest(),
                     depth_sha256=hashlib.sha256(expected).hexdigest()),indent=2))
