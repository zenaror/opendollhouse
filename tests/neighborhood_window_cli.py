"""Synthetic argument/resource failures, without opening an X11 window."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
binary = sys.argv[1]
checks = 0

def check(value):
    global checks
    checks += 1
    assert value, checks

def run(*args):
    return subprocess.run([binary, *args], capture_output=True, text=True, timeout=10)

def iff(chunks):
    return b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0')+bytes(4)+b''.join(chunks)

def chunk(kind, ident, data):
    return kind+struct.pack('>IHH',76+len(data),ident,0)+bytes(64)+data

def far1(entries):
    result=bytearray(b'FAR!byAZ'+struct.pack('<II',1,0))
    index=[]
    for name,data in entries:
        offset=len(result);result.extend(data)
        index.append((name.encode(),len(data),offset))
    manifest=len(result);result.extend(struct.pack('<I',len(index)))
    for name,size,offset in index:
        result.extend(struct.pack('<IIII',size,size,offset,len(name))+name)
    struct.pack_into('<I',result,12,manifest)
    return bytes(result)

def solid_bmp(width, height, rgb):
    row_size=width*3
    pixels=bytes((rgb[2],rgb[1],rgb[0]))*width*height
    return b'BM'+struct.pack('<IHHI',54+len(pixels),0,0,54)+struct.pack(
        '<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),0,0,0,0)+pixels

def bmp_with_pixels(width, height, rgb, overrides):
    row_size=width*3
    pixels=bytearray(bytes((rgb[2],rgb[1],rgb[0]))*width*height)
    for x,y,color in overrides:
        offset=(height-1-y)*row_size+x*3
        pixels[offset:offset+3]=bytes((color[2],color[1],color[0]))
    return b'BM'+struct.pack('<IHHI',54+len(pixels),0,0,54)+struct.pack(
        '<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),0,0,0,0)+pixels

with tempfile.TemporaryDirectory(prefix='odh-map-cli-') as folder:
    root = Path(folder)
    args = ['--user-data', str(root), '--ui-graphics', str(root/'UIGraphics.far')]
    result = run(*args, '--mode', '6')
    check(result.returncode == 0 and json.loads(result.stdout) == {
        'evidence': 'map_selection_only', 'mode': 6, 'status': 'unsupported', 'selected_house': None})
    for extra in [[], ['--mode'], ['--mode','0'], ['--mode','8'], ['--mode','-1'],
                  ['--mode','1x'], ['--mode',''], ['--mode','1','--mode','1'],
                  ['--mode','1','--house','7'], ['--mode','1','--user-data',str(root)],
                  ['--mode','1','--language'], ['--mode','1','--language','0'],
                  ['--mode','1','--language','21'],
                  ['--mode','1','--language','1','--language','1']]:
        result = run(*args, *extra)
        check(result.returncode == 2 and not result.stdout)
    # An empty FAR1 archive is structurally valid but has no exact map resource.
    (root/'UIGraphics.far').write_bytes(b'FAR!byAZ'+struct.pack('<III',1,16,0))
    for mode in [1,2,3,4,5,7]:
        result = run(*args, '--mode', str(mode))
        check(result.returncode == 2 and not result.stdout and 'exact archive entry not found' in result.stderr)

    # Export prepares the same frame as the window, but does not connect to X11.
    mask=(255,0,255)
    base=bmp_with_pixels(800,600,(10,20,30),[
        (0,0,mask),(2,0,mask),(4,0,(254,2,254)),(6,0,(255,1,255))])
    layer=bmp_with_pixels(800,600,mask,[(0,0,(70,80,90))])
    (root/'UIGraphics.far').write_bytes(far1([
        ('Nbhd\\NScreen.BMP',base),('Nbhd\\DiffN1-N2_8.bmp',layer)]))
    (root/'Houses').mkdir()
    (root/'Houses/House07.iff').write_bytes(iff([]))
    lot_locations=struct.pack('<hH',-3,1)+b'\1' + b'7,100,100\0\0'
    (root/'LotLocations.iff').write_bytes(iff([
        chunk(b'STR#',1,lot_locations),chunk(b'STR#',3,lot_locations),
        chunk(b'STR#',7,lot_locations)]))
    (root/'Neighborhood.iff').write_bytes(iff([]))
    exported=root/'map.ppm'
    result=run(*args,'--mode','1','--export-frame',str(exported))
    check(result.returncode==0 and json.loads(result.stdout)=={
        'evidence':'neighborhood_frame_export_only','mode':1,'width':800,'height':600,
        'lots':1,'house_files':1,'families':0,'missing_layers':[]})
    ppm=exported.read_bytes()
    header=b'P6\n800 600\n255\n';check(ppm.startswith(header) and len(ppm)==len(header)+800*600*3)
    pixels=ppm[len(header):]
    check(pixels[:3]==bytes((70,80,90)))
    check(pixels[3:6]==bytes((10,20,30)))
    check(pixels[6:9]==bytes((32,36,44)))
    check(pixels[12:15]==bytes((32,36,44)))
    check(pixels[18:21]==bytes((32,36,44)))
    check(pixels[(100*800+100)*3:(100*800+100)*3+3]==bytes((40,220,120)))
    (root/'UIGraphics.far').write_bytes(far1([('Nbhd\\NScreen.BMP',base)]))
    without_layer=root/'map-without-optional-layer.ppm'
    result=run(*args,'--mode','1','--export-frame',str(without_layer))
    result_json=json.loads(result.stdout)
    check(result.returncode==0 and result_json['evidence']=='neighborhood_frame_export_only')
    check(result_json['missing_layers']==['Nbhd\\DiffN1-N2_8.bmp'])
    check('neighborhood map layer is missing: Nbhd\\DiffN1-N2_8.bmp' in result.stderr)
    optional_pixels=without_layer.read_bytes().split(b'\n',3)[3]
    check(optional_pixels[:3]==bytes((32,36,44)))

    # The three Vacation Island layers stack in source order, and magenta in a
    # later layer leaves the earlier layer visible at that pixel.
    red=solid_bmp(4,1,(200,10,10))
    blue=solid_bmp(4,1,(10,10,200))
    tree_layer=bmp_with_pixels(4,1,(255,0,255),[(1,0,(10,200,10))])
    base3=solid_bmp(800,600,(20,30,40))
    (root/'UIGraphics.far').write_bytes(far1([
        ('VIsland\\visland.bmp',base3),
        ('VIsland\\visland_waves001.bmp',red),
        ('VIsland\\visland_port.bmp',blue),
        ('VIsland\\visland_trees.bmp',tree_layer)]))
    island=root/'island.ppm'
    result=run(*args,'--mode','3','--export-frame',str(island))
    check(result.returncode==0 and json.loads(result.stdout)['missing_layers']==[])
    island_pixels=island.read_bytes().split(b'\n',3)[3]
    check(island_pixels[:3]==bytes((10,10,200)))
    check(island_pixels[3:6]==bytes((10,200,10)))
    check(island_pixels[6:9]==bytes((10,10,200)))

    # Magic Town's wave strip starts 62 pixels below the base image's top.
    base7=solid_bmp(800,600,(20,30,40))
    waves7=solid_bmp(4,1,(200,180,10))
    (root/'UIGraphics.far').write_bytes(far1([
        ('Magicland\\DScreen.bmp',base7),('Magicland\\DScreen_waves1.bmp',waves7)]))
    magic=root/'magic.ppm'
    result=run(*args,'--mode','7','--export-frame',str(magic))
    check(result.returncode==0 and json.loads(result.stdout)['missing_layers']==[])
    magic_pixels=magic.read_bytes().split(b'\n',3)[3]
    check(magic_pixels[(61*800)*3:(61*800)*3+3]==bytes((20,30,40)))
    check(magic_pixels[(62*800)*3:(62*800)*3+3]==bytes((200,180,10)))
    (root/'UIGraphics.far').write_bytes(far1([('Magicland\\DScreen.bmp',base7)]))
    missing_magic=root/'magic-without-waves.ppm'
    result=run(*args,'--mode','7','--export-frame',str(missing_magic))
    missing_magic_json=json.loads(result.stdout)
    check(result.returncode==0 and missing_magic_json['missing_layers']==[
        'Magicland\\DScreen_waves1.bmp'])
    check('neighborhood map layer is missing: Magicland\\DScreen_waves1.bmp'
          in result.stderr)
print(f'Neighborhood window CLI: {checks} checks passed (synthetic, no GUI)')
