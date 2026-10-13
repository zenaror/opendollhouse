"""Synthetic FAR/IFF/SLOT selector and raw field integration."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary=sys.argv[1]
def archive(duplicate=False):
    slot=struct.pack('<II4sI',0,4,b'TOLS',3)
    for typ,score in [(1,7),(3,11),(3,22)]:
        slot+=struct.pack('<HIIIiiiIi',typ,0x80000000,0x7FC12345,0,score,0,0,0xFFFFFFFF,-1)
    chunk=b'SLOT'+struct.pack('>IHH',76+len(slot),100,0)+bytes(64)+slot
    asset=b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0')+bytes(4)+chunk
    if duplicate: asset+=chunk
    name=b'test.iff'
    return b'FAR!byAZ'+struct.pack('<II',1,16+len(asset))+asset+struct.pack('<I',1)+struct.pack('<IIII',len(asset),len(asset),16,len(name))+name
with tempfile.TemporaryDirectory(prefix='odh-slot-') as folder:
    p=Path(folder)/'synthetic.far'
    def run(*selectors,duplicate=False):
        p.write_bytes(archive(duplicate))
        return subprocess.run([binary,str(p),'test.iff',*selectors],capture_output=True,text=True)
    r=run('100');d=json.loads(r.stdout)
    assert r.returncode==0 and len(d['records'])==3 and d['records'][0]['offset_bits'][1]==0x7FC12345
    assert d['records'][0]['proximity_raw'] is None and d['remaining_bytes']==0
    r=run('0x64','3','1');d=json.loads(r.stdout)
    assert r.returncode==0 and len(d['records'])==1 and d['records'][0]['record_index']==2 and d['records'][0]['standing']==22
    for args in [('100','3','2'),('100','9','0'),('100','3'),('-1',),('65536',),('101',),('100junk',)]:
        r=run(*args);assert r.returncode==2 and not r.stdout
    r=run('100',duplicate=True);assert r.returncode==2 and 'ambiguous' in r.stderr and not r.stdout
print('SLOT CLI passed (synthetic fixtures only)')
