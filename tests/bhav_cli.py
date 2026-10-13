"""Synthetic BHAV format -> CLI trace integration, no commercial fixtures."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]

def routine(opcode=2, operands=bytes([0,0,7,0,0,5,8,7]), yes=254, no=255):
    return struct.pack('<HHBBHHH',0x8002,1,0,4,0,0,0) + struct.pack('<HBB',opcode,yes,no) + operands

def archive(rows):
    asset = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0') + bytes(4)
    for identifier,payload in rows:
        asset += b'BHAV'+struct.pack('>IHH',76+len(payload),identifier,0)+bytes(64)+payload
    name=b'test.iff'
    return (b'FAR!byAZ'+struct.pack('<II',1,16+len(asset))+asset+struct.pack('<I',1)+
            struct.pack('<IIII',len(asset),len(asset),16,len(name))+name)

with tempfile.TemporaryDirectory(prefix='odh-bhav-') as folder:
    path=Path(folder)/'test.far'
    def run(rows,*options):
        path.write_bytes(archive(rows))
        return subprocess.run([binary,str(path),'test.iff','0x1000',*options],capture_output=True,text=True)
    rows=[(4096,routine())]
    result=run(rows)
    assert result.returncode==0 and json.loads(result.stdout)['metadata']['arguments']==4
    result=run(rows,'--run')
    report=json.loads(result.stdout)
    assert result.returncode==0 and report['status']=='returned_true' and report['steps']==1
    result=run([(4096,routine(4097,bytes([255])*8)),(4097,routine(49))],'--run')
    report=json.loads(result.stdout)
    assert result.returncode==3 and report['status']=='fault' and report['stack_depth']==2
    assert [e['routine'] for e in report['trace']]==[4096,4097] and 'primitive 49' in report['diagnostic']
    for rows,options in [([(4096,routine()),(4096,routine())],()),
                          ([(4096,routine())],('--run','--run')),
                          ([(4096,routine())],('--unknown',))]:
        result=run(rows,*options)
        assert result.returncode==2 and not result.stdout
print('BHAV CLI integration passed (synthetic fixtures only)')
