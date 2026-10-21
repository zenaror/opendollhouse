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

def archive(rows,slots=None,slot_id=100,objd=None,strings=(),sounds=(),bcons=(),globs=()):
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
    for identifier,payload in bcons:
        asset += b'BCON'+struct.pack('>IHH',76+len(payload),identifier,0)+bytes(64)+payload
    for identifier,payload in globs:
        asset += b'GLOB'+struct.pack('>IHH',76+len(payload),identifier,0)+bytes(64)+payload
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
    result=run(rows,'--run','--world-probe','--caller-object-word','9','0',
               '--stack-object-word','10','1')
    report=json.loads(result.stdout)
    assert result.returncode==0 and report['caller_word']==[9,0]
    assert report['stackobject_word']==[10,1] and report['status']=='returned_true'
    for options in [('--run','--world-probe','--caller-object-word','68','0'),
                    ('--run','--world-probe','--stack-object-word','0','32768')]:
        result=run(rows,*options)
        assert result.returncode==2 and not result.stdout
    result=run([(4096,routine(4097,bytes([255])*8)),(4097,routine(49))],'--run')
    report=json.loads(result.stdout)
    assert result.returncode==3 and report['status']=='fault' and report['stack_depth']==2
    assert [e['routine'] for e in report['trace']]==[4096,4097] and 'primitive 49' in report['diagnostic']
    # Explicit single code owner -> GLOB128 semiglobal -> global call graph.
    # Each resource role accepts only its own BHAV ID range.
    semi=Path(folder)/'semi.far'; globals_path=Path(folder)/'globals.far'
    owner_rows=[(4096,routine(8192,bytes([255])*8))]
    binding=bytes([4])+b'tEsT'+b'\xa3'*7
    def linked(owner=owner_rows,globs=((128,binding),),semi_rows=None,*extra):
        path.write_bytes(archive(owner,globs=globs))
        semi.write_bytes(archive(semi_rows if semi_rows is not None else [(8192,routine(256,bytes([255])*8))]))
        globals_path.write_bytes(archive([(256,routine())]))
        return subprocess.run([binary,str(path),'test.iff','4096','--semiglobal',str(semi),'test.iff',
                               '--global',str(globals_path),'test.iff',*extra],capture_output=True,text=True)
    r=linked(); d=json.loads(r.stdout)
    assert r.returncode==0 and d['evidence']=='static_structure_only'
    assert d['semiglobal_binding']['name']=='tEsT' and d['semiglobal_binding']['remaining_bytes']==7
    r=linked(owner_rows,((128,binding),),None,'--run'); d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_true' and d['steps']==3
    assert [t['routine'] for t in d['trace']]==[4096,8192,256]
    assert d['semiglobal_binding']['code_owner_entry']=='test.iff'
    for owner,globs,semi_rows,diagnostic in [
        (owner_rows,(),None,'nonempty code-owner'),
        (owner_rows,((128,b'\0'),),None,'nonempty code-owner'),
        (owner_rows,((128,binding),(128,binding)),None,'ambiguous'),
        (owner_rows,((129,binding),),None,'nonempty code-owner'),
        (owner_rows,((128,b'\x05test'),),None,'truncated GLOB'),
        (owner_rows,((128,b'\x05other'),),None,'does not match'),
        (owner_rows,((128,b'\x05a/bcd'),),None,'flat semiglobal'),
        (owner_rows,((128,b'\x05a\\bcd'),),None,'flat semiglobal'),
        (owner_rows,((128,b'\x05a:bcd'),),None,'flat semiglobal'),
        (owner_rows,((128,b'\x05a\0bcd'),),None,'flat semiglobal'),
        (owner_rows,((128,binding),),[(4096,routine())],'resource role'),
        (owner_rows,((128,binding),),[(256,routine())],'resource role'),
        (owner_rows,((128,binding),),[(8192,routine()),(8192,routine())],'ambiguous'),
        (owner_rows+[(8193,routine())],((128,binding),),None,'resource role'),
    ]:
        r=linked(owner,globs,semi_rows)
        assert r.returncode==2 and not r.stdout and diagnostic in r.stderr,(r.returncode,r.stdout,r.stderr)
    r=linked(owner_rows,((128,binding),),None,'--semiglobal',str(semi),'test.iff')
    assert r.returncode==2 and not r.stdout and 'repeated option' in r.stderr
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
    # A contained literal snap with operand flags0 obtains direction from SLOT,
    # then notifies an object explicitly lacking a main-thread backend.
    oriented=bytearray(snap_route);struct.pack_into('<I',oriented,26,4096)
    body=(struct.pack('<HHBBHHH',0x8002,2,0,4,0,0,0)+
          struct.pack('<HBB',46,1,255)+bytes([0,0,3,0,0,0,0,0])+
          struct.pack('<HBB',49,254,255)+bytes([255])*8)
    path.write_bytes(archive([(4096,body)],struct.pack('<II4sI',0,10,b'TOLS',2)+storage+oriented,128,prefix))
    r=subprocess.run(args,capture_output=True,text=True);d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_true' and d['steps']==2
    assert d['container']==2 and d['final_position']==[64,64,1,0]
    assert d['idle_notifications']==[{'entity':2,'status':'no_thread'}]
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
    # Reset uses only explicit global body tables: the object's STR150 is not
    # an avatar body resource override. Tick the persistent loop after return.
    def write_bcf_entries(entries):
        payloads=[(name,u(0)+u(0)+u(len(clips))+b''.join(clips)) for name,clips in entries]
        offset=16; data=b''; index=u(len(payloads))
        for name,payload in payloads:
            index+=struct.pack('<IIII',len(payload),len(payload),offset,len(name))+name
            data+=payload;offset+=len(payload)
        bcf_path.write_bytes(b'FAR!byAZ'+u(1)+u(offset)+data+index)
    write_bcf_entries([(b'test.bcf',[clip(b'Adult'),clip(b'Child')]),
                       (b'reset.bcf',[clip(b'Sit'),clip(b'Floor'),clip(b'Stand'),clip(b'ChildSit')])])
    def reset_global(strings):
        global_path.write_bytes(archive([],struct.pack('<II4sI',0,10,b'TOLS',2)+record*2,strings=strings))
    body_tables=((150,table([b'',b'sIT',b'Floor',b'Stand'])),
                 (151,table([b'',b'ChildSit',b'Floor',b'Stand'])))
    reset_global(body_tables)
    reset_options=('--reset-probe','reset.bcf')
    for initial,name in [('1','Sit'),('2','Floor'),('0','Stand'),('7','Stand')]:
        r=animated(strings=((150,table([b'',b'MissingOverride'])),),operands=bytes(8),
                   extra=reset_options+('--posture',initial,'--post-ticks','10'))
        d=json.loads(r.stdout)
        assert r.returncode==0 and d['status']=='returned_true' and d['steps']==1
        assert d['animation_progress']['name']==name and d['animation_progress']['loop']
        assert not d['animation_progress']['end'] and d['post_ticks']==10 and d['execution_ticks']==0
        assert d['simulation_ticks']==d['route_ticks']==10 and d['posture']==int(initial)
        assert d['reset_resource']=={'archive':str(bcf_path),'entry':'reset.bcf',
            'global_archive':str(global_path),'global_entry':'test.iff','walk_table_id':150,
            'language':1,'avatar_resource_override':False,'hand_slot':0,'hand_empty':True,'head_seek':0}
        assert len(d['animation_events'])==1 and d['trace'][0]['instruction']['opcode']==44
    # Post ticks cannot advance a VM that is still runnable/budget exhausted.
    path.write_bytes(archive([(4096,routine(44,bytes(8),yes=0))],
                     struct.pack('<II4sI',0,10,b'TOLS',2)+storage+snap_route,128,prefix))
    r=subprocess.run(animation_args+list(reset_options)+['--post-ticks','10'],capture_output=True,text=True)
    d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='budget_exhausted' and d['steps']==10000
    assert d['post_ticks']==0 and d['requested_post_ticks']==10 and d['simulation_ticks']==0
    r=animated(operands=bytes(8),extra=reset_options+('--posture','1','--age','17'))
    d=json.loads(r.stdout)
    assert r.returncode==0 and d['animation_progress']['name']=='ChildSit' and d['reset_resource']['walk_table_id']==151
    assert d['post_ticks']==0 and d['animation_progress']['frame']==0
    for strings,message in [((), 'table absent'),(( (150,table([b''])),), 'index missing'),
                            (((150,table([b'',b'Missing'])),), 'clip missing'),
                            (((150,struct.pack('<hH',-3,2)+b'\x02\0\0\x02Sit\0\0'),),'language 1')]:
        reset_global(strings)
        r=animated(operands=bytes(8),extra=reset_options+('--posture','1'))
        d=json.loads(r.stdout)
        assert r.returncode==3 and d['status']=='fault' and message in d['animation_diagnostic']
    reset_global(body_tables)
    r=animated(operands=bytes(8),extra=('--reset-probe','absent.bcf'))
    assert r.returncode==2 and 'exact archive entry not found' in r.stderr and not r.stdout
    for extra in [('--post-ticks','1'),reset_options+('--post-ticks','4097'),
                  reset_options+('--post-ticks','1','--post-ticks','2'),
                  reset_options+reset_options]:
        r=animated(operands=bytes(8),extra=extra)
        assert r.returncode==2 and not r.stdout
    r=subprocess.run(args+list(reset_options),capture_output=True,text=True)
    assert r.returncode==2 and not r.stdout
    # Explicitly selecting the same entry reuses identities rather than reloads.
    reset_global(((150,table([b'',b'Adult',b'Adult',b'Adult'])),))
    r=animated(operands=bytes(8),extra=('--reset-probe','test.bcf','--posture','1'))
    assert r.returncode==0 and json.loads(r.stdout)['animation_progress']['name']=='Adult'
    write_bcf_entries([(b'test.bcf',[clip(b'Adult')]),(b'reset.bcf',[clip(b'ADULT')])])
    r=animated(operands=bytes(8),extra=reset_options)
    assert r.returncode==2 and 'casefold' in r.stderr and not r.stdout
    # Two VM starts retain one world/controller graph and a global instruction budget.
    original_prefix=prefix
    function_prefix=bytearray(prefix)+bytes(68-len(prefix))
    struct.pack_into('<H',function_prefix,10,4096)
    struct.pack_into('<HH',function_prefix,64,4096,4097)
    floor=struct.pack('<HIIIiiiIi',3,0,0xC1800000,0,0,0,0,4097,-1)+struct.pack('<iiiiiIiii',16,16,16,0,0,0x3E400000,0,-2,16)
    def body(instructions):
        return struct.pack('<HHBBHHH',0x8002,len(instructions),0,4,0,0,0)+b''.join(
            struct.pack('<HBB',op,yes,no)+operands for op,operands,yes,no in instructions)
    set_sit=bytes([0,0,1,0,0,5,18,7])
    set_stand=bytes([0,0,0,0,0,5,18,7])
    initial=body([(46,bytes([0,0,3,0,4,0,0,0]),1,255),
                  (2,set_sit,2,255),(44,bytes(8),254,255)])
    standing=body([(46,bytes([1,0,3,0,0,0,0,0]),1,255),
                   (44,bytes([2,0,0,0,0,0,0,0]),2,1),
                   (44,bytes(8),3,255),(2,set_stand,254,255)])
    write_bcf_entries([(b'test.bcf',[clip(b'Adult')]),(b'reset.bcf',[clip(b'Sit'),clip(b'Stand')]),
                       (b'extra.bcf',[clip(b'Rise',b'202')])])
    reset_global(body_tables)
    stand_options=['--reset-probe','reset.bcf','--sound-probe','--stand-after','--animation-extra','extra.bcf']
    def sequence(first=initial,second=standing,definition=function_prefix,extra=()):
        path.write_bytes(archive([(4096,first),(4097,second)],
            struct.pack('<II4sI',0,10,b'TOLS',3)+storage+snap_route+floor,128,definition,
            ((129,table([b'',b'Adult',b'Rise'])),)))
        return subprocess.run(animation_args+stand_options+list(extra),capture_output=True,text=True)
    r=sequence(extra=('--post-ticks','2'));d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_true' and d['container']==0 and d['posture']==0
    assert d['final_position']==[64,48,1,0] and d['post_ticks']==2
    assert [p['routine'] for p in d['phases']]==[4096,4097]
    assert d['phases'][0]['container']==2 and d['phases'][0]['posture']==1
    assert d['phases'][1]['container']==0 and d['phases'][1]['posture']==0
    assert d['total_steps']==sum(p['steps'] for p in d['phases'])==len(d['trace'])
    assert {e['phase'] for e in d['trace']}=={0,1}
    assert [e['total_step'] for e in d['trace']]==list(range(d['total_steps']))
    assert d['animation_progress']['name']=='Sit' # Actual reset precedes the posture write.
    assert d['stand_selection']['stand']==4097 and d['stand_selection']['entry']=='test.iff'
    assert [x['entry'] for x in d['animation_resources']]==['test.bcf','reset.bcf','extra.bcf']
    assert [e['tick'] for e in d['animation_events']]==sorted(e['tick'] for e in d['animation_events'])
    r=sequence(second=routine(46,bytes([9,0,3,0,0,0,0,0])));d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_false' and d['container']==2 and d['posture']==1
    r=sequence(first=routine(2,bytes([0,0,1,0,0,2,7,7])));d=json.loads(r.stdout)
    assert d['status']=='returned_false' and len(d['phases'])==1 and {e['phase'] for e in d['trace']}=={0}
    r=sequence(second=routine(50));d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='fault' and d['container']==2, (r.returncode,d['status'],d.get('phases'))
    r=sequence(second=routine(2,bytes([0,0,1,0,0,5,8,7]),yes=0));d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='budget_exhausted' and d['total_steps']==10000
    assert sum(p['steps'] for p in d['phases'])==10000 and d['phases'][1]['steps']==9997
    for definition,message in [(function_prefix[:67],None),
             (function_prefix[:44]+struct.pack('<H',1)+function_prefix[46:],'function table'),
             (function_prefix[:66]+bytes(2),'Stand is absent')]:
        r=sequence(definition=definition)
        assert r.returncode==2 and (message is None or message in r.stderr) and not r.stdout
    for extra in [('--animation-extra','EXTRA.BCF'),('--animation-extra','test.bcf'),
                  tuple(['--animation-extra','x.bcf']*17)]:
        r=sequence(extra=extra);assert r.returncode==2 and not r.stdout
    write_bcf_entries([(b'test.bcf',[clip(b'Adult')]),(b'reset.bcf',[clip(b'Sit')]),
                       (b'extra.bcf',[clip(b'ADULT')])])
    r=sequence();assert r.returncode==2 and 'casefold' in r.stderr
    for options in [('--stand-after',),('--animation-extra','extra.bcf')]:
        r=subprocess.run(args+list(options),capture_output=True,text=True)
        assert r.returncode==2 and not r.stdout
    # A second live VM shares occupied-slot, definition and comfort memory.
    write_bcf_entries([(b'test.bcf',[clip(b'Adult')]),(b'reset.bcf',[clip(b'Sit'),clip(b'Stand')]),
                       (b'extra.bcf',[clip(b'Rise',b'202')])])
    reset_global(body_tables)
    main_definition=bytearray(function_prefix)
    struct.pack_into('<H',main_definition,10,4098)
    def expr(lhs,rhs,op,left,right):
        return struct.pack('<hhBBBB',lhs,rhs,0,op,left,right)
    main_body=body([(2,expr(0,20000,5,9,7),1,255),(0,bytes(8),2,255),
                    (2,expr(0,0,5,10,20),3,255),(2,expr(0,1,2,10,7),4,6),
                    (2,expr(9,2,2,21,7),5,6),(2,expr(6,128,3,15,26),6,255),
                    (2,expr(0,2,5,9,7),7,255),(0,bytes(8),2,255)])
    notified_initial=body([(46,bytes([0,0,3,0,4,0,0,0]),1,255),
                            (49,bytes(8),2,255),(2,set_sit,3,255),(44,bytes(8),254,255)])
    def main_sequence(main=main_body,definition=main_definition,bcons=((4097,b'\x01\x00\x01\x00'),),extra=(),first=notified_initial):
        path.write_bytes(archive([(4096,first),(4097,standing),(4098,main)],
            struct.pack('<II4sI',0,10,b'TOLS',3)+storage+snap_route+floor,128,definition,
            ((129,table([b'',b'Adult',b'Rise'])),),bcons=bcons))
        return subprocess.run(animation_args+stand_options+['--main-probe']+list(extra),capture_output=True,text=True)
    r=main_sequence(extra=('--hold-ticks','6','--post-ticks','2'));d=json.loads(r.stdout)
    assert r.returncode==0 and d['status']=='returned_true' and d['scene_status']=='returned', (r.returncode,r.stderr,d.get('scene_status'),d.get('object_thread',{}).get('diagnostic'))
    assert d['idle_notifications']==[{'entity':2,'status':'notified'}]
    assert d['object_thread']['routine']==4098 and d['object_thread']['status']=='waiting'
    assert d['comfort_initial']==0 and d['comfort_final']>=2 and d['phases'][0]['comfort']==1
    assert d['actual_hold_ticks']==d['requested_hold_ticks']==6 and d['post_ticks']==2
    assert d['object_thread']['clock']==d['simulation_ticks'] and d['object_thread']['rng_cycles']>0
    assert d['total_steps']==sum(p['steps'] for p in d['phases'])+d['object_thread']['steps']
    assert d['object_thread']['steps']==len(d['object_thread']['trace'])
    assert d['object_thread']['rng_backend']=='none' and d['object_thread']['rng_cycles_semantics']=='requested advances only'
    assert d['avatar_definition_profile']=={'source':'constructed_definition','object_type':2}
    r=main_sequence();d=json.loads(r.stdout)
    assert r.returncode==0 and d['actual_hold_ticks']==0 and d['phases'][0]['comfort']==1
    for extra in [('--hold-ticks','4097'),('--hold-ticks','1','--hold-ticks','2'),('--main-probe',)]:
        r=main_sequence(extra=extra);assert r.returncode==2 and not r.stdout
    for opts in [('--main-probe',),('--hold-ticks','1')]:
        r=subprocess.run(animation_args+list(opts),capture_output=True,text=True)
        assert r.returncode==2 and not r.stdout
    for main_id in [0,280,5000]:
        absent=bytearray(main_definition);struct.pack_into('<H',absent,10,main_id)
        r=main_sequence(definition=absent)
        assert r.returncode==2 and 'Main is absent' in r.stderr and not r.stdout
    r=main_sequence(bcons=());d=json.loads(r.stdout)
    assert r.returncode==3 and d['status']=='returned_true' and d['scene_status']=='fault'
    assert len(d['phases'])==1 and d['object_thread']['status']=='fault'
    assert 'scope 26' in d['object_thread']['diagnostic'] and d['comfort_final']==0
    r=main_sequence(bcons=((4097,b'\x01\x00\x01\x00'),)*2)
    assert r.returncode==2 and 'ambiguous private BCON' in r.stderr and not r.stdout
    # Faulting Main prevents avatar execution; the chair cannot write My Motive.
    r=main_sequence(main=routine(2,expr(6,1,5,14,7)));d=json.loads(r.stdout)
    assert r.returncode==3 and d['scene_status']=='fault' and d['steps']==0
    assert 'write scope 14' in d['object_thread']['diagnostic'] and d['comfort_final']==0
    r=main_sequence(main=routine(2,expr(9,2,5,21,7)));d=json.loads(r.stdout)
    assert r.returncode==3 and d['scene_status']=='fault' and 'write scope 21' in d['object_thread']['diagnostic']
    r=main_sequence(main=struct.pack('<HH',0x8000,1)+bytes(8)+struct.pack('<HBB',2,254,255)+expr(0,1,5,8,7));d=json.loads(r.stdout)
    assert r.returncode==3 and d['scene_status']=='fault' and 'metadata' in d['object_thread']['diagnostic']
    r=main_sequence(main=routine(50));d=json.loads(r.stdout)
    assert r.returncode==3 and d['scene_status']=='fault' and len(d['object_thread']['trace'])==1
    r=main_sequence(main=routine(2,expr(0,1,5,8,7),yes=0),extra=('--hold-ticks','6','--post-ticks','2'));d=json.loads(r.stdout)
    assert r.returncode==3 and d['scene_status']=='budget_exhausted' and d['total_steps']==10000
    assert d['steps']==0 and d['actual_hold_ticks']==0 and d['post_ticks']==0
    # A globally supplied Sleep BHAV remains under this chair's context.
    global_sleep=body([(0,bytes(8),254,255)])
    global_path.write_bytes(archive([(280,global_sleep)],struct.pack('<II4sI',0,10,b'TOLS',2)+record*2,
                                  strings=body_tables))
    global_call=body([(280,struct.pack('<hhhh',20000,0,0,0),1,255),
                      (2,expr(0,0,5,10,20),2,255),(2,expr(6,128,3,15,26),254,255)])
    r=main_sequence(main=global_call);d=json.loads(r.stdout)
    assert r.returncode==0 and d['comfort_final']==1
    assert 280 in [e['routine'] for e in d['object_thread']['trace']]
    prefix=original_prefix
    global_path.write_bytes(archive([],struct.pack('<II4sI',0,10,b'TOLS',2)+record*2))
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
