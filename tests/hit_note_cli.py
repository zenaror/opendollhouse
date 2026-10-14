"""Offline selection, explicit fixture choice and failure reporting; synthetic only."""
import json,struct,subprocess,sys,tempfile
from pathlib import Path
binary=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='odh-hit-note-') as folder:
    root=Path(folder);hsm=root/'fixture.hsm';hot=root/'fixture.hot';hit=root/'fixture.hit'
    hsm.write_text('tkd_entry 5 (0x5)\n')
    hot.write_text('''[TrackData]
0x5=0x10
[EventMapping]
fixture=1,7,0,0,0,0
[Track]
7=0,entry,1024,0,0,0,0,@tkd_entry,0
[Patch]
9=0,first,"Voice\\first.XA",0,0
10=0,second,"Voice\\second.XA",0,0
[GlobalHitList]
5=9,10
6=10,9
''')
    hit.write_bytes(b'HIT!'+struct.pack('<II',1,8)+b'TRAX'+bytes([69,1,4,5,6,68,5,39,5,6,50,5,2,5]))
    args=[binary,str(hsm),str(hot),str(hit),'FIXTURE']
    originals=[p.read_bytes() for p in [hsm,hot,hit]]
    r=subprocess.run(args+['--field','20','30','--choice','0','--choice','0'],capture_output=True,text=True)
    assert r.returncode==0 and not r.stderr,(r.returncode,r.stderr)
    d=json.loads(r.stdout)
    assert not d['audio_submitted'] and d['note']['patch_id']==10 and d['note']['filename']=='Voice\\second.XA'
    assert d['choices']==[dict(list=5,index=0,patch=9),dict(list=6,index=0,patch=10)]
    assert d['note']['requested_group_kills']==[1] and d['note']['steps']==6
    assert d['object_vars_fixture'][20]==30 and d['diagnostic']==''
    assert [t['opcode'] for t in d['note']['trace']]==[69,4,68,39,6,2]
    for extra in [[],['--choice','99'],['--choice','0'],['--choice','0','--choice','0','--choice','0']]:
        r=subprocess.run(args+extra,capture_output=True,text=True);assert r.returncode==3 and not r.stderr
        d=json.loads(r.stdout);assert d['note'] is None and d['diagnostic'] and not d['audio_submitted']
    for extra in [['--field','29','0'],['--field','20','1','--field','20','2'],['--field','0','2147483648'],['--choice','-1'],['--field'],['--jump-operand'],['--jump-operand','auto'],['--jump-operand','literal32','--jump-operand','free-so'],['--field-source'],['--field-source','2147483648'],['--field-source','0','--field-source','12']]:
        r=subprocess.run(args+extra,capture_output=True,text=True);assert r.returncode==2 and not r.stdout
    r=subprocess.run(args[:-1]+['missing'],capture_output=True,text=True);assert r.returncode==2 and not r.stdout
    assert originals==[p.read_bytes() for p in [hsm,hot,hit]]
    hot.write_text(hot.read_text()+'\n[TrackData]\n0x103=0x15\n')
    hit.write_bytes(b'HIT!'+struct.pack('<II',1,8)+b'TRAX'+bytes([13,3,1,0,0,5,50,10,0,0,0,2,5]))
    r=subprocess.run(args+['--choice','0'],capture_output=True,text=True)
    assert r.returncode==3 and json.loads(r.stdout)['note'] is None
    r=subprocess.run(args+['--choice','0','--jump-operand','literal32'],capture_output=True,text=True)
    d=json.loads(r.stdout)
    assert r.returncode==0 and not r.stderr and d['jump_operand']=='literal32' and not d['audio_submitted']
    assert d['evidence']=='offline_FreeSO_HIT_selection_with_literal32_Jump'
    assert d['note']['patch_id']==10 and d['note']['trace'][0]['branch_id']==259
    assert d['note']['trace'][0]['next_pc']==21
    hit.write_bytes(b'HIT!'+struct.pack('<II',1,8)+b'TRAX'+bytes([4,2,20,72,3,1,2,5,50,10,0,0,0,2,5]))
    for extra in [['--field-source','12'],['--field-source','1','--field','20','30']]:
        r=subprocess.run(args+['--choice','0']+extra,capture_output=True,text=True)
        d=json.loads(r.stdout);assert r.returncode==3 and d['note'] is None and 'lookup failed for source 12 field 20' in d['diagnostic']
    r=subprocess.run(args+['--choice','0','--field-source','12','--field','20','0'],capture_output=True,text=True)
    d=json.loads(r.stdout)
    assert r.returncode==0 and d['evidence']=='offline_HIT_selection_with_explicit_source_fields' and d['field_source']==12
    assert d['note']['trace'][1]['source']==12 and d['note']['trace'][1]['field_value']==0 and d['note']['trace'][1]['zero']
    r=subprocess.run(args+['--choice','0','--field-source','1','--field','20','30','--args','1','1024','0'],capture_output=True,text=True)
    d=json.loads(r.stdout)
    assert r.returncode==0 and d['evidence']=='offline_HIT_selection_with_explicit_arguments'
    assert d['initial_args_fixture']==[1,1024,0] and d['note']['trace'][1]['source']==1 and d['note']['trace'][1]['field_value']==30
    for extra in [['--args'],['--args','1'],['--args','1','2'],['--args','1','2','2147483648'],['--args','-2147483649','0','0'],['--args','0','0','0','--args','1','2','3']]:
        r=subprocess.run(args+extra,capture_output=True,text=True);assert r.returncode==2 and not r.stdout
    for source in [0,-2147483648,2147483647]:
        r=subprocess.run(args+['--choice','0','--field-source',str(source),'--field','20','0','--args',str(source),'0','0'],capture_output=True,text=True)
        d=json.loads(r.stdout);assert r.returncode==0 and d['initial_args_fixture']==[source,0,0] and d['note']['trace'][1]['source']==source
print('HIT note CLI offline fixture selection passed')
