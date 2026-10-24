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
    (root/'UIGraphics.far').write_bytes(far1([('Nbhd\\NScreen.BMP',solid_bmp(800,600,(10,20,30)))]))
    (root/'Houses').mkdir()
    (root/'Houses/House07.iff').write_bytes(iff([]))
    (root/'LotLocations.iff').write_bytes(iff([chunk(b'STR#',1,
        struct.pack('<hH',-3,1)+b'\1' + b'7,100,100\0\0')]))
    (root/'Neighborhood.iff').write_bytes(iff([]))
    exported=root/'map.ppm'
    result=run(*args,'--mode','1','--export-frame',str(exported))
    check(result.returncode==0 and json.loads(result.stdout)=={
        'evidence':'neighborhood_frame_export_only','mode':1,'width':800,'height':600,
        'lots':1,'house_files':1,'families':0})
    ppm=exported.read_bytes()
    header=b'P6\n800 600\n255\n';check(ppm.startswith(header) and len(ppm)==len(header)+800*600*3)
    pixels=ppm[len(header):]
    check(pixels[:3]==bytes((10,20,30)))
    check(pixels[(100*800+100)*3:(100*800+100)*3+3]==bytes((40,220,120)))
print(f'Neighborhood window CLI: {checks} checks passed (synthetic, no GUI)')
