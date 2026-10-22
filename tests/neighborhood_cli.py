"""Synthetic IFF integration; never copies installed assets."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
binary=sys.argv[1]
checks=0
def iff(chunks):
    return b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0')+bytes(4)+b''.join(chunks)
def chunk(kind,id,data):
    return kind+struct.pack('>IHH',76+len(data),id,0)+bytes(64)+data
def locations(rows):
    return chunk(b'STR#',1,struct.pack('<hH',-3,len(rows))+b''.join(b'\1'+r.encode()+b'\0\0' for r in rows))
def family(id,house,number,guids):
    return chunk(b'FAMI',id,struct.pack('<II4siiiiiiI',0,9,b'IMAF',house,number,100,0,0,0,len(guids))+struct.pack('<'+'I'*len(guids),*guids))
def check(value):
    global checks
    checks+=1
    assert value, checks
with tempfile.TemporaryDirectory(prefix='odh-neighborhood-') as folder:
    root=Path(folder);(root/'Houses').mkdir()
    (root/'Houses/House07.iff').write_bytes(iff([]))
    (root/'Houses/House08.iff').write_bytes(iff([]))
    (root/'Neighborhood.iff').write_bytes(iff([family(200,7,900,[0xffffffff,0x80000000])]))
    def run(*args,rows=('7,10,20','8,-1,30','9,3,4','99,0,0')):
        (root/'LotLocations.iff').write_bytes(iff([locations(rows)]))
        return subprocess.run([binary,'--user-data',str(root),*args],capture_output=True,text=True)
    r=run('--mode','1');d=json.loads(r.stdout);check(r.returncode==0 and len(d['lots'])==3)
    a,b,c=d['lots'];check(a['family']=={'house_number':7,'fami_chunk_id':200,'family_number':900,'member_count':2,'member_guids':[0xffffffff,0x80000000]})
    check(b['family'] is None and b['selectable']);check(not c['file_exists'] and not c['selectable'])
    r=run('--mode','1','--house','7');d=json.loads(r.stdout);check(r.returncode==0 and len(d['lots'])==1 and d['lots'][0]['x']==10 and d['lots'][0]['path']==str(root/'Houses/House07.iff'))
    r=run('--mode','1','--house','8');check(r.returncode==0 and json.loads(r.stdout)['lots'][0]['family'] is None)
    for args in [('--mode','0'),('--mode','8'),('--mode','-1'),('--mode','1junk'),('--mode','1','--mode','1'),('--mode','1','--house','9'),('--mode','1','--house','10'),('--mode','1','--house','99'),('--mode','1','--house','32768'),('--mode','1','--house','100')]:
        r=run(*args);check(r.returncode==2 and not r.stdout)
    for rows in [('7,1',),('7,1,2','7,3,4'),('32768,0,0',),('100,0,0',),('7,0,32768',)]:
        r=run('--mode','1',rows=rows);check(r.returncode==2 and not r.stdout)
    r=run('--mode','6');check(r.returncode==0 and json.loads(r.stdout)['status']=='unsupported' and json.loads(r.stdout)['lots']==[])
    (root/'LotLocations.iff').unlink();(root/'Neighborhood.iff').unlink()
    r=subprocess.run([binary,'--user-data',str(root),'--mode','6','--house','7'],capture_output=True,text=True)
    check(r.returncode==0 and json.loads(r.stdout)['status']=='unsupported')
print(f'Neighborhood CLI: {checks} checks passed (synthetic only)')
