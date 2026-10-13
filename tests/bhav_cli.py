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

def archive(rows,slots=None,slot_id=100,objd=None,strings=(),sounds=()):
    asset = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0') + bytes(4)
    for identifier,payload in rows:
        asset += b'BHAV'+struct.pack('>IHH',76+len(payload),identifier,0)+bytes(64)+payload
    if slots is not None:
        asset += b'SLOT'+struct.pack('>IHH',76+len(slots),slot_id,0)+bytes(64)+slots
    if objd is not None:
        asset += b'OBJD'+struct.pack('>IHH',76+len(objd),16808,0)+bytes(64)+objd
    for identifier,payload in strings:
        asset += b'STR#'+struct.pack('>IHH',76+len(payload),identifier,0)+bytes(64)+payload
    for identifier,payload in sounds:
        asset += b'FWAV'+struct.pack('>IHH',76+len(payload),identifier,0)+bytes(64)+payload
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
    prefix=bytearray(42);struct.pack_into('<I',prefix,0,138);struct.pack_into('<H',prefix,26,129)
    struct.pack_into('<I',prefix,28,0x12345678);struct.pack_into('<H',prefix,40,128)
    storage=struct.pack('<HIIIiiiIi',0,0,0,0,0,0,0,0,-1)+struct.pack('<iiiiiIiii',16,16,16,100,0,0x3E400000,7,-2,16)
    snap_route=struct.pack('<HIIIiiiIi',3,0,0,0,0,0,0,0,0)+struct.pack('<iiiiiIiii',16,16,16,0,0,0x3E400000,0,-2,16)
    path.write_bytes(archive([(4096,routine(46,bytes([0,0,3,0,4,0,0,0])))],
                  struct.pack('<II4sI',0,10,b'TOLS',2)+storage+snap_route,128,prefix))
    args=[binary,str(path),'test.iff','4096','--run','--world-probe','--route-probe','--snap-probe',
          '--object-def','16808','--global',str(global_path),'test.iff']
    result=subprocess.run(args,capture_output=True,text=True);d=json.loads(result.stdout)
    assert result.returncode==0 and d['container']==2 and d['container_slot']==0 and d['final_position']==[64,64,1,0]
    assert d['object_binding']=={'objd_id':16808,'guid':0x12345678,'master_id':0,'sub_index':0,'slot_table':128,'animation_table':129}
    def typed(guid,owner=10,entity=0):
        operand=struct.pack('<IhBB',guid,entity,owner,0)
        path.write_bytes(archive([(4096,routine(32,operand))],
                         struct.pack('<II4sI',0,10,b'TOLS',2)+storage+snap_route,128,prefix))
        return subprocess.run(args,capture_output=True,text=True)
    r=typed(0x12345678);d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_true' and d['trace'][0]['instruction']['opcode']==32
    r=typed(0x87654321);assert json.loads(r.stdout)['status']=='returned_false'
    r=typed(0x12345678,7,2);assert json.loads(r.stdout)['status']=='returned_true'
    r=typed(0x12345678,7,99);d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='fault'
    r=typed(0x12345678,255);d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='fault'
    multipart=bytearray(prefix);struct.pack_into('<H',multipart,20,1)
    path.write_bytes(archive([(4096,routine())],struct.pack('<II4sI',0,10,b'TOLS',2)+storage+snap_route,128,multipart))
    r=subprocess.run(args,capture_output=True,text=True)
    assert r.returncode==2 and 'master resource not supplied' in r.stderr and not r.stdout
    # Explicit synthetic animation resources; no transforms or skeleton are supplied.
    def u(n): return struct.pack('<I',n)
    def pas(s): return bytes([len(s)])+s
    def clip(name,event=b'100'):
        return (pas(name)+pas(b'absent.cfp')+
                b''.join(u(n) for n in [0,0,0,0,0,1])+pas(b'ROOT')+
                b''.join(u(n) for n in [4,0,0,0,0,0,0,1,1,40,1])+
                pas(b'xevt')+pas(event))
    bcf_path=Path(folder)/'animation.far'
    def write_bcf(clips):
        payload=u(0)+u(0)+u(len(clips))+b''.join(clips)
        name=b'test.bcf'
        bcf_path.write_bytes(b'FAR!byAZ'+u(1)+u(16+len(payload))+payload+u(1)+
                             struct.pack('<IIII',len(payload),len(payload),16,len(name))+name)
    write_bcf([clip(b'Adult'),clip(b'Child',b'101')])
    def table(values):
        return struct.pack('<hH',-3,len(values))+b''.join(b'\x01'+v+b'\0\0' for v in values)
    animation_args=args+['--animation-probe',str(bcf_path),'test.bcf']
    def animated(strings=((129,table([b'',b'aDuLt'])),(130,table([b'',b'CHILD']))),
                 operands=bytes([1,0,0,0,0,0,0,0]),no=0,extra=()):
        path.write_bytes(archive([(4096,routine(44,operands,no=no))],
                         struct.pack('<II4sI',0,10,b'TOLS',2)+storage+snap_route,128,prefix,strings))
        return subprocess.run(animation_args+list(extra),capture_output=True,text=True)
    sound_args=animation_args+['--sound-probe']
    def sounded(sounds=((1,b'chair_sit_wood\0trailer'),),operands=bytes([1,0,0x22,0x56,15,200,9,8]),yes=254):
        path.write_bytes(archive([(4096,routine(23,operands,yes=yes))],
                         struct.pack('<II4sI',0,10,b'TOLS',2)+storage+snap_route,128,prefix,sounds=sounds))
        return subprocess.run(sound_args,capture_output=True,text=True)
    r=sounded();d=json.loads(r.stdout)
    assert r.returncode==0 and d['sound_backend']=='none' and d['status']=='returned_true'
    assert d['evidence']=='ODH_VM_constructed_route_snap_silent_animation_sound_requests_world'
    assert d['sound_requests']==[{'event_id':1,'name':'chair_sit_wood','source':2,'pan':False,
           'zoom':False,'loop':True,'sample_rate':0x5622,'flags':15,'volume':200,'status':'skipped_no_backend'}]
    assert d['sound_code_owner']=={'archive':str(path),'entry':'test.iff'}
    global_path.write_bytes(archive([],struct.pack('<II4sI',0,10,b'TOLS',2)+record*2,
                                  sounds=((1,b'global_sound\0'),)))
    r=sounded();assert json.loads(r.stdout)['sound_requests'][0]['name']=='chair_sit_wood'
    r=sounded(sounds=());assert json.loads(r.stdout)['sound_requests'][0]['name']=='global_sound'
    r=sounded(operands=bytes([2,0,0,0,0,0,0,0]));d=json.loads(r.stdout)
    assert r.returncode==0 and d['sound_requests'][0]['status']=='missing_resource'
    assert d['sound_requests'][0]['source']==1 and d['sound_requests'][0]['pan'] and d['sound_requests'][0]['zoom']
    r=sounded(sounds=((1,b'piano_play\0'),),operands=bytes([1,0,0,0,0,0,0,0]));d=json.loads(r.stdout)
    assert d['sound_requests'][0]['loop']
    r=sounded(sounds=((1,b'one\0'),(1,b'two\0')))
    assert r.returncode==2 and 'ambiguous FWAV' in r.stderr and not r.stdout
    r=sounded(sounds=((1,b'nonterminated'),))
    assert r.returncode==2 and 'NUL terminator' in r.stderr and not r.stdout
    r=sounded(yes=0);d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='budget_exhausted' and len(d['sound_requests'])==4096
    assert d['sound_event_limit_reached'] and not d['sound_events_truncated'] and d['steps']==4096
    global_path.write_bytes(archive([],struct.pack('<II4sI',0,10,b'TOLS',2)+record*2))
    path.write_bytes(archive([(4096,routine(23,bytes(8)))],
                           struct.pack('<II4sI',0,10,b'TOLS',2)+storage+snap_route,128,prefix))
    r=subprocess.run(animation_args,capture_output=True,text=True);d=json.loads(r.stdout)
    assert r.returncode==3 and 'primitive 23' in d['diagnostic'] and 'sound_requests' not in d
    r=subprocess.run(args+['--sound-probe'],capture_output=True,text=True)
    assert r.returncode==2 and not r.stdout
    r=animated();d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_true' and d['age']==21
    assert d['evidence']=='ODH_VM_constructed_route_snap_silent_animation_world'
    assert d['animation_events']==[{'tick':1,'actor':1,'time_id':40,'properties':[['xevt','100']]}]
    assert d['animation_progress'] is None and d['simulation_ticks']==d['route_ticks']==5
    assert len(d['trace'])==2 and d['movements']==[]
    r=animated(extra=('--age','17'));d=json.loads(r.stdout)
    assert r.returncode==0 and d['age']==17 and d['animation_events'][0]['properties']==[['xevt','101']]
    r=animated(extra=('--age','18'));d=json.loads(r.stdout)
    assert r.returncode==0 and d['animation_events'][0]['properties']==[['xevt','100']]
    r=animated(no=255);d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_false' and d['simulation_ticks']==2
    assert d['animation_progress']['name']=='Adult' and d['animation_progress']['frame']>0
    # IDFromParam maps Object to StackObject65536. Explicit autonomous argument0=1
    # supplies the same selected animation identity.
    r=animated(operands=bytes([0,0,0,0,0,4,0,0]),no=255,extra=('--autonomous',));d=json.loads(r.stdout)
    assert r.returncode==0 and d['animation_events'][0]['properties']==[['xevt','100']]
    assert not d['animation_events_truncated']
    for strings,message in [(((129,table([b''])),(130,table([b'',b'Adult']))),'index missing'),
                            (((129,table([b'',b'Missing'])),),'clip missing'),
                            ((),'table absent')]:
        r=animated(strings=strings);d=json.loads(r.stdout)
        assert r.returncode==3 and d['status']=='fault' and not d['animation_events']
        assert message in d['animation_diagnostic']
    alternate=bytearray(prefix);struct.pack_into('<H',alternate,26,200)
    original=prefix;prefix=alternate
    r=animated();assert r.returncode==0 # Absent selected table uses129.
    r=animated(strings=((200,table([b''])),(129,table([b'',b'Adult']))));d=json.loads(r.stdout)
    assert r.returncode==3 and 'index missing' in d['animation_diagnostic']
    prefix=original
    wrapped=bytearray(prefix);struct.pack_into('<H',wrapped,26,65535);prefix=wrapped
    r=animated(strings=((0,table([b'',b'Child'])),),extra=('--age','17'));d=json.loads(r.stdout)
    assert r.returncode==0 and d['animation_events'][0]['properties']==[['xevt','101']]
    prefix=original
    r=animated(strings=((129,struct.pack('<hH',-3,2)+b'\x02\0\0\x02Adult\0\0'),));d=json.loads(r.stdout)
    assert r.returncode==3 and 'language 1' in d['animation_diagnostic']
    r=animated(operands=bytes([1,0,0,0,3,0,0,0]));d=json.loads(r.stdout)
    assert r.returncode==3 and 'scope not supplied' in d['animation_diagnostic']
    for options in [('--age','32768'),('--age','1','--age','2'),('--animation-probe',str(bcf_path))]:
        r=animated(extra=options);assert r.returncode==2 and not r.stdout
    many_events=(pas(b'Adult')+pas(b'absent.cfp')+
                 b''.join(u(n) for n in [0,0,0,0,0,1])+pas(b'ROOT')+
                 b''.join(u(n) for n in [4,0,0,0,0,0,0,1,4096])+
                 (u(40)+u(1)+pas(b'xevt')+pas(b'100'))*4096)
    write_bcf([many_events])
    r=animated();d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='waiting' and d['simulation_ticks']==1
    assert len(d['animation_events'])==4096 and d['animation_event_limit_reached']
    assert not d['animation_events_truncated']
    write_bcf([clip(b'Adult'),clip(b'ADULT')])
    r=animated();assert r.returncode==2 and 'casefold' in r.stderr and not r.stdout
    for options in [('--age','21'),('--animation-probe',str(bcf_path),'test.bcf'),('--sound-probe',)]:
        result=run([(4096,routine())],*options)
        assert result.returncode==2 and not result.stdout
    args[args.index('16808')]='16809'
    result=subprocess.run(args,capture_output=True,text=True)
    assert result.returncode==2 and not result.stdout
    for rows,options in [([(4096,routine()),(4096,routine())],()),
                          ([(4096,routine())],('--run','--run')),
                          ([(4096,routine())],('--unknown',))]:
        result=run(rows,*options)
        assert result.returncode==2 and not result.stdout
print('BHAV CLI integration passed (synthetic fixtures only)')
