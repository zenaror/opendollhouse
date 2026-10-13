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

def archive(rows,slots=None):
    asset = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0') + bytes(4)
    for identifier,payload in rows:
        asset += b'BHAV'+struct.pack('>IHH',76+len(payload),identifier,0)+bytes(64)+payload
    if slots is not None:
        asset += b'SLOT'+struct.pack('>IHH',76+len(slots),100,0)+bytes(64)+slots
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
    world_rows=[(4096,routine(2,bytes([0,0,11,0,0,2,16,3])))]
    for occupant,expected in [('0','returned_false'),('1','returned_true'),('3','returned_false')]:
        result=run(world_rows,'--run','--world-probe','--occupant',occupant)
        report=json.loads(result.stdout)
        assert result.returncode==0 and report['status']==expected and report['evidence']=='ODH_VM_constructed_world'
        assert report['initial_world']['occupant']==int(occupant)
    for options in [('--world-probe',),('--run','--occupant','1'),
                    ('--run','--world-probe','--occupant','2'),
                    ('--run','--world-probe','--posture','32768'),
                    ('--run','--world-probe','--autonomous','--autonomous')]:
        result=run(world_rows,*options)
        assert result.returncode==2 and not result.stdout
    record=struct.pack('<HIIIiiiIi',3,0,0,0,1,0,0,0x55,-1)+struct.pack('<iiiiiIiii',16,16,16,0,0,0x3E400000,0,-2,16)
    global_path=Path(folder)/'global.far'
    global_path.write_bytes(archive([],struct.pack('<II4sI',0,10,b'TOLS',2)+record*2))
    r=run([(4096,routine(45,bytes([1,0,2,0,0,0,0,0])))],
          '--run','--world-probe','--route-probe','--global',str(global_path),'test.iff')
    d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_true' and d['route_ticks']==33
    assert d['final_position']==[48,64,1,2] and d['route_entry_flags']==64
    assert len(d['movements'])==33 and d['evidence']=='ODH_VM_constructed_route_world'
    assert [m['tick'] for m in d['movements']]==list(range(1,34))
    r=run([(4096,routine())],'--run','--world-probe','--route-probe')
    assert r.returncode==2 and not r.stdout
    for rows,options in [([(4096,routine()),(4096,routine())],()),
                          ([(4096,routine())],('--run','--run')),
                          ([(4096,routine())],('--unknown',))]:
        result=run(rows,*options)
        assert result.returncode==2 and not result.stdout
print('BHAV CLI integration passed (synthetic fixtures only)')
