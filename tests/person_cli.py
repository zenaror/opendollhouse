"""Resource owner + native entry host integration, entirely synthetic inputs."""
import json,struct,subprocess,sys,tempfile
from pathlib import Path
binary=sys.argv[1]
def routine(rows):
 return struct.pack('<HHBBHHH',0x8002,len(rows),0,4,0,0,0)+b''.join(struct.pack('<HBB',op,t,f)+bytes(data) for op,t,f,data in rows)
def expr(temp,value,t=254):return (2,t,255,[temp,0,value,0,0,5,8,7])
def call(op,t=254,f=255,args=bytes(8)):return (op,t,f,args)
def objd(guid):
 b=bytearray(68);struct.pack_into('<I',b,0,138);struct.pack_into('<H',b,10,4096);struct.pack_into('<H',b,18,2);struct.pack_into('<I',b,28,guid);return b
def archive(path,name,chunks):
 asset=b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0')+bytes(4)
 for kind,rid,data in chunks:asset+=kind.encode('ascii')+struct.pack('>IHH',76+len(data),rid,0)+bytes(64)+data
 name=name.encode('ascii');path.write_bytes(b'FAR!byAZ'+struct.pack('<II',1,16+len(asset))+asset+struct.pack('<I',1)+struct.pack('<IIII',len(asset),len(asset),16,len(name))+name)
with tempfile.TemporaryDirectory(prefix='odh-person-') as folder:
 root=Path(folder);avatar=root/'avatar.far';semi=root/'semi.far';glob=root/'global.far';action=root/'action.far'
 def bcons(tables):
  return [('BCON',rid,bytes([len(values),0])+struct.pack('<'+'H'*len(values),*values)) for rid,values in tables]
 def setup(main=None,global_rows=None,action_rows=None,private_bcons=(),semi_bcons=(),global_bcons=(),action_bcons=()):
  archive(avatar,'Person.iff',[('OBJD',128,objd(0x11111111)),('GLOB',128,b'\x0dFixturePerson\xa3'),('BHAV',4096,routine(main or [call(8192)]))]+bcons(private_bcons))
  archive(semi,'FixturePerson.iff',[('BHAV',8192,routine([call(256,1,1),expr(2,99)]))]+bcons(semi_bcons))
  archive(glob,'Global.iff',[('BHAV',256,routine(global_rows or [(17,1,253,[0,0,1,0,0,0,0,0]),expr(3,1)]))]+bcons(global_bcons))
  archive(action,'Chair.iff',[('OBJD',128,objd(0x22222222)),('BHAV',4096,routine(action_rows or [expr(0,22,255)]))]+bcons(action_bcons))
 args=[binary,'--avatar',str(avatar),'Person.iff','128','--semiglobal',str(semi),'FixturePerson.iff','--global',str(glob),'Global.iff']
 action_args=['--action',str(action),'Chair.iff','128','4096']
 def run(extra=()):return subprocess.run(args+list(extra),capture_output=True,text=True)
 def next_random(seed,maximum):
  if maximum==0:return seed,0
  mask=(1<<64)-1;seed^=seed>>12;seed^=(seed<<25)&mask;seed^=seed>>27
  return seed,((seed*2685821657736338717)&mask)%maximum
 def random_row(destination,scope,maximum,range_scope=7,t=1):
  return (8,t,255,list(struct.pack('<hHhH',destination,scope,maximum,range_scope)))
 def check_temp(index,value):return (2,254,255,list(struct.pack('<hhBBBB',index,value,0,2,8,7)))
 for seed_value,maximum in [(1,100),(0,100),((1<<64)-1,-1),(1,0),(1,1)]:
  seed_after,result=next_random(seed_value,maximum&65535);signed_result=result if result<32768 else result-65536
  setup(main=[random_row(0,8,maximum),check_temp(0,signed_result)]);r=run(['--seed',str(seed_value)]);d=json.loads(r.stdout)
  assert r.returncode==0 and d['steps']==2 and d['status']=='returned_true'
  assert d['rng_seed_initial']==seed_value and d['rng_seed_final']==seed_after and d['rng_cycle_requests']==0
  assert d['random_reads']==[dict(owner=1,range=maximum&65535,result=signed_result,destination_scope=8,destination_index=0,seed_before=seed_value,seed_after=seed_after)]
  assert 'explicit host fixture' in d['rng_seed_source']
 # Sleep consumes the same state between two random primitive calls.
 first,_=next_random(1,100);cycled,_=next_random(first,1);last,value=next_random(cycled,100)
 setup(main=[call(256,args=[2,0,0,0,0,0,0,0])],global_rows=[random_row(0,8,100), (0,2,255,bytes(8)),random_row(0,8,100,t=3),check_temp(0,value)])
 r=run();d=json.loads(r.stdout)
 assert r.returncode==0 and d['ticks']==3 and d['rng_cycle_requests']==1 and d['rng_seed_final']==last
 assert [v['seed_before'] for v in d['random_reads']]==[1,cycled] and [v['seed_after'] for v in d['random_reads']]==[first,last]
 for destination,scope,maximum,range_scope in [(20,8,100,7),(0,7,100,7),(0,10,100,7),(0,8,20,8),(0,8,0,256),(0,256,100,7)]:
  setup(main=[random_row(destination,scope,maximum,range_scope,t=254)]);r=run();d=json.loads(r.stdout)
  assert r.returncode==3 and d['status']=='fault' and d['rng_seed_final']==1 and d['random_reads']==[]
 for value in ['-1','18446744073709551616','1garbage','0x','+1','']:
  setup();r=run(['--seed',value]);assert r.returncode==2 and not r.stdout
 setup();r=run(['--seed','0','--seed','1']);assert r.returncode==2 and not r.stdout
 setup();r=run(action_args);d=json.loads(r.stdout)
 assert r.returncode==0 and not r.stderr and d['status']=='returned_true' and d['ticks']==1 and d['steps']==7
 assert d['completion']['result'] is False and d['queue_size']==0 and not d['queue_active']
 assert [t['owner'] for t in d['trace']]==[1,1,1,2,1,1,1]
 assert d['trace'][2]['routine']==d['trace'][4]['routine']==256 and d['trace'][2]['pc']==d['trace'][4]['pc']==0
 assert d['avatar']['guid']==0x11111111 and d['action']['guid']==0x22222222 and d['actor_state']['flags']==256
 assert 'full Init' in d['initialization'] and 'FreeSO' in d['constructor_profile'] and d['stack']==[]
 def tuned(temp,query,t=254):return (2,t,255,[temp,0,query&255,query>>8,0,5,8,26])
 setup(global_rows=[tuned(3,0,1),tuned(4,0x2000,2),tuned(5,0x4000,3),(17,4,253,[0,0,1,0,0,0,0,0]),expr(6,1)],
       action_rows=[tuned(0,0,1),tuned(1,0x2000,255)],private_bcons=[(4096,[65534])],semi_bcons=[(8192,[11])],
       global_bcons=[(256,[0])],action_bcons=[(4096,[25]),(8192,[126])])
 r=run(action_args);d=json.loads(r.stdout)
 assert r.returncode==0 and d['completion']['result'] is False and d['ticks']==1
 assert d['tuning_reads']==[
  dict(owner=1,scope='private',table=4096,index=0,value=-2,semiglobal_fallback=False),
  dict(owner=1,scope='semiglobal',table=8192,index=0,value=11,semiglobal_fallback=False),
  dict(owner=1,scope='global',table=256,index=0,value=0,semiglobal_fallback=False),
  dict(owner=2,scope='private',table=4096,index=0,value=25,semiglobal_fallback=False),
  dict(owner=2,scope='semiglobal',table=8192,index=0,value=126,semiglobal_fallback=True)]
 assert 'BCON only' in d['tuning_profile']
 # Existing semiglobal with missing table must not use the private cache.
 setup(main=[tuned(0,0x2000)],private_bcons=[(8192,[99])]);r=run();d=json.loads(r.stdout)
 assert r.returncode==3 and d['diagnostic']=='unsupported read scope 26 index 8192' and d['tuning_reads']==[]
 setup(main=[tuned(0,0x4001)],global_bcons=[(256,[0])]);r=run();d=json.loads(r.stdout)
 assert r.returncode==3 and d['diagnostic']=='unsupported read scope 26 index 16385' and d['tuning_reads']==[]
 setup(private_bcons=[(4096,[1]),(4096,[2])]);r=run()
 assert r.returncode==2 and not r.stdout and 'ambiguous BCON' in r.stderr
 setup();r=run(action_args+['--ticks','0']);d=json.loads(r.stdout)
 assert r.returncode==3 and d['tick_budget_exhausted'] and d['status']=='yielded' and d['completion']['tick']==0
 setup(global_rows=[(17,1,253,[0,0,0,0,0,0,0,0]),expr(3,1)])
 r=run(action_args);d=json.loads(r.stdout)
 assert r.returncode==3 and d['action_incomplete'] and d['completion'] is None and d['queue_size']==1 and d['actor_state']['flags']==320
 # Proper clock cadence with a real Sleep controller, not synthetic true completion.
 setup(main=[call(256,args=[2,0,0,0,0,0,0,0])],global_rows=[(0,254,255,bytes(8))])
 r=run();d=json.loads(r.stdout)
 assert r.returncode==0 and d['ticks']==3 and d['steps']==2 and d['rng_cycle_requests']==1 and 'xorshift64star' in d['rng_backend']
 assert d['rng_seed_initial']==1 and d['rng_seed_final']==33554433 and d['random_reads']==[]
 r=run(['--ticks','1']);d=json.loads(r.stdout)
 assert r.returncode==3 and d['tick_budget_exhausted'] and d['ticks']==1 and d['status']=='waiting' and d['completion'] is None
 setup();legacy=bytearray(routine([expr(0,1)]));struct.pack_into('<H',legacy,0,0x8001)
 archive(avatar,'Person.iff',[('OBJD',128,objd(1)),('GLOB',128,b'\x0dFixturePerson'),('BHAV',4096,legacy)])
 for extra in [[],action_args]:
  r=run(extra);d=json.loads(r.stdout)
  assert r.returncode==3 and not r.stderr and d['status']=='fault' and d['steps']==0 and d['stack_depth']==0
  assert d['diagnostic']=='execution requires BHAV 8002 metadata'
 # Unknown primitive remains a coherent runtime failure, with owner/PC/provenance.
 setup(global_rows=[(44,254,255,bytes(8))]);r=run();d=json.loads(r.stdout)
 assert r.returncode==3 and d['status']=='fault' and d['diagnostic']=='unsupported primitive 44'
 assert d['stack'][-1]['owner']==1 and d['stack'][-1]['routine']==256 and d['stack'][-1]['caller']==1
 setup(main=[expr(0,1,0)]);r=run();d=json.loads(r.stdout)
 assert r.returncode==3 and d['instruction_budget_exhausted'] and d['steps']==10000 and len(d['trace'])==10000
 for index in [1,25,26,29,41,62]:
  setup(main=[(2,254,255,[index,0,0,0,0,2,3,7])]);r=run();d=json.loads(r.stdout)
  assert r.returncode==3 and d['status']=='fault' and d['diagnostic']==f'unsupported read scope 3 index {index}'
 setup();r=run(['--flags','65535','--age','27']+action_args);d=json.loads(r.stdout)
 assert r.returncode==0 and d['flags_initial']==65535 and d['actor_state']['flags']==-1 and d['actor_state']['age']==27
 for extra in [['--ticks','4097'],['--age','32768'],['--flags','0','--flags','1'],['--routine','9999'],['--routine','256']]:
  setup();r=run(extra);assert r.returncode==2 and not r.stdout and r.stderr
 setup();invalid_type=objd(1);struct.pack_into('<H',invalid_type,18,0)
 archive(avatar,'Person.iff',[('OBJD',128,invalid_type),('GLOB',128,b'\x0dFixturePerson'),('BHAV',4096,routine([expr(0,1)]))]);r=run()
 assert r.returncode==2 and not r.stdout and 'ObjectType2' in r.stderr
 setup();archive(semi,'FixturePerson.iff' ,[('BHAV',4096,routine([expr(0,1)]))]);r=run()
 assert r.returncode==2 and not r.stdout and 'resource role' in r.stderr
 setup();archive(avatar,'Person.iff',[('OBJD',128,objd(1)),('GLOB',128,b'\x05other'),('BHAV',4096,routine([expr(0,1)]))]);r=run()
 assert r.returncode==2 and not r.stdout and 'GLOB128' in r.stderr
 setup();archive(action,'Chair.iff',[('OBJD',128,objd(2)),('GLOB',128,b'\x04Semi'),('BHAV',4096,routine([expr(0,1)]))]);r=run(action_args)
 assert r.returncode==2 and not r.stdout and 'action semiglobal' in r.stderr
 # Explicit house selection and native-style zoning input, separate from FARs.
 house=root/'House.iff';zone=root/'LotZoning.iff'
 def plain_iff(path,chunks):
  data=b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0')+bytes(4)
  for kind,rid,payload in chunks:data+=kind.encode()+struct.pack('>IHH',76+len(payload),rid,0)+bytes(64)+payload
  path.write_bytes(data)
 saved=[0]*32;saved[10]=99;saved[23]=56
 simi=struct.pack('<II4s',0,0x3e,b'IMIS')+struct.pack('<32h',*saved)+bytes(34+24)
 zoning=struct.pack('<hH',-3,3)+b'\x011, residential\0road\0\x0158, community\0road\0\x021, malformed translation\0road\0'
 plain_iff(house,[('SIMI',1,simi)]);plain_iff(zone,[('STR#',1,zoning)])
 def lot_args(number=1):return ['--lot',str(house),str(number),'--zoning',str(zone)]
 read_lot=(2,1,255,[0,0,10,0,0,5,8,6]);zone_call=(1,2,255,[28,99,0,0,0,0,0,0])
 for number,expected,fallback in [(1,0,False),(58,1,False),(81,2,True),(89,2,True),(80,1,True),(90,1,True)]:
  setup(main=[read_lot,zone_call,(2,254,255,[0,0,expected,0,0,2,8,7])]);r=run(lot_args(number));d=json.loads(r.stdout)
  assert r.returncode==0 and d['status']=='returned_true' and d['steps']==3 and d['ticks']==0
  assert d['global_reads']==[dict(index=10,value=number)]
  assert d['zoning_reads']==[dict(input=number,value=expected,fallback=fallback)]
  assert d['lot']['selected_id']==number and d['lot']['serialized_id']==99 and d['lot']['serialized_globals']==32
 setup(main=[read_lot,zone_call,(2,254,255,[0,0,0,0,0,2,8,7])]);r=run(['--lot',str(house),'1']);d=json.loads(r.stdout)
 assert r.returncode==3 and d['diagnostic']=='lot zoning resource unavailable' and d['zoning_reads']==[]
 r=run();d=json.loads(r.stdout);assert r.returncode==3 and d['diagnostic']=='unsupported read scope 6 index 10'
 for index in [0,1,3,4,5,6,7,8,9,17,20,25,32,33]:
  setup(main=[(2,254,255,[0,0,index,0,0,5,8,6])]);r=run(lot_args());d=json.loads(r.stdout)
  assert r.returncode==3 and d['diagnostic']==f'unsupported read scope 6 index {index}' and d['global_reads']==[]
 setup(main=[(2,254,255,[0,0,23,0,0,5,8,6])]);r=run(lot_args());d=json.loads(r.stdout)
 assert r.returncode==0 and d['global_reads']==[dict(index=23,value=56)]
 for extra in [['--zoning',str(zone)],['--lot',str(house),'0'],['--lot',str(house),'32768'],lot_args()+['--lot',str(house),'2']]:
  r=run(extra);assert r.returncode==2 and not r.stdout
 plain_iff(house,[('SIMI',1,simi),('SIMI',1,simi)]);r=run(lot_args());assert r.returncode==2 and 'ambiguous SIMI1' in r.stderr
 plain_iff(house,[('SIMI',2,simi)]);r=run(lot_args());assert r.returncode==2 and 'SIMI1 absent' in r.stderr
 plain_iff(house,[('SIMI',1,simi)])
 plain_iff(zone,[('STR#',1,zoning),('STR#',1,zoning)]);r=run(lot_args());assert r.returncode==2 and 'ambiguous zoning' in r.stderr
 plain_iff(zone,[('STR#',2,zoning)]);r=run(lot_args());assert r.returncode==2 and 'STR#1 absent' in r.stderr
 plain_iff(zone,[('STR#',1,struct.pack('<hH',-3,1)+b'\x011, unknown\0\0')]);r=run(lot_args())
 assert r.returncode==2 and 'unsupported lot zoning label' in r.stderr
print('person host CLI checks passed (synthetic fixtures only)')
