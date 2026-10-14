"""Synthetic immutable host asset/cache integration; existing CLI is pixel control."""
import json,math,struct,subprocess,sys,tempfile
from pathlib import Path
host,render,*mode=sys.argv[1:]
u=lambda n:struct.pack('<I',n)
i=lambda n:struct.pack('<i',n)
f=lambda *n:struct.pack('<'+'f'*len(n),*n)
pas=lambda s:bytes([len(s)])+s

def far(entries):
    data=b''.join(v for _,v in entries);index=u(len(entries));offset=16
    for name,value in entries:index+=struct.pack('<IIII',len(value),len(value),offset,len(name))+name;offset+=len(value)
    return b'FAR!byAZ'+u(1)+u(16+len(data))+data+index
bone=pas(b'ROOT')+pas(b'NULL')+i(0)+f(0,0,0,0,0,0,1)+i(1)+i(1)+i(0)+f(0,0)
rig=u(1)+pas(b'rig')+struct.pack('<h',1)+bone+u(0)+u(0)
app=u(0)+u(1)+pas(b'appearance')+i(3)+i(7)+u(1)+pas(b'ROOT')+pas(b'mesh')+i(0)+i(0)+u(0)
mesh=pas(b'mesh')+pas(b'x')+i(1)+pas(b'ROOT')+i(1)+i(0)+i(1)+i(2)+i(1)+b''.join(i(n) for n in (0,0,3,0,0))+i(3)+f(.25,.25,.25,.25,.25,.25)+i(0)+i(3)+b''.join(f(-x,y,z,0,1,0) for x,y,z in ((-1,2,0),(1,2,0),(0,4,0)))
motion=pas(b'ROOT')+u(2)+f(100)+b''.join(u(n) for n in (1,1,0,0,0,0))
anim=u(0)+u(0)+u(1)+pas(b'clip')+pas(b'pose')+f(100)+b''.join(u(n) for n in (0,0,2,2,1))+motion
cfp=b''.join(b'\xff'+f(v) for plane in ([0,1],[0,0],[0,0],[0,0],[0,0],[0,0],[1,1]) for v in plane)
bmp=b'BM'+u(58)+bytes(4)+u(54)+u(40)+i(1)+i(1)+struct.pack('<HH',1,24)+u(0)+u(4)+bytes(16)+bytes((20,40,200,0))
def chunk(kind,id,data):return kind+struct.pack('>IHH',76+len(data),id,0)+bytes(64)+data
obj=bytearray(42);struct.pack_into('<IHHH',obj,0,138,0,1128,1)
record=struct.pack('<IIiiIIII',1128,0,0,0,0,0,0,0)
dgrp=struct.pack('<HI',20004,1)+struct.pack('<III',1,3,1)+record
packet=struct.pack('<HBB',(1<<13)|1,255,1)
frame=struct.pack('<HHIHHhh',1,1,7,0xA3A3,0,99,99)+struct.pack('<H',2+len(packet))+packet+struct.pack('<H',5<<13)
spr=struct.pack('<IIII',1000,1,2,16)+frame
palette=struct.pack('<II',1,2)+bytes(8)+bytes((0,0,0,20,40,200))
iff=b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60,b'\0')+bytes(4)+b''.join(chunk(*x) for x in ((b'OBJD',100,obj),(b'DGRP',1128,dgrp),(b'SPR2',1128,spr),(b'PALT',2,palette)))
with tempfile.TemporaryDirectory(prefix='odh-scene-host-') as folder:
    p=Path(folder);a=p/'anim.far';t=p/'tex.far';o=p/'objects.far';a.write_bytes(far([(b'rig.bcf',rig),(b'app.bcf',app),(b'mesh.bmf',mesh),(b'anim.bcf',anim),(b'POSE.cfp',cfp)]));t.write_bytes(far([(b'red.bmp',bmp)]));o.write_bytes(far([(b'object.iff',iff)]))
    r=subprocess.run([host,str(a),str(t),str(o),str(p/'host'),*mode],capture_output=True,text=True);assert r.returncode==0 and not r.stderr,r.stderr
    failure=json.loads((p/'host-fault.json').read_text());assert failure['fault'] is True and 'missing' in failure['diagnostic']
    assert json.loads(r.stdout)==dict(cache_clips=1,cache_samples=14,vertices=3,triangles=1)
    r=subprocess.run([render,str(a),'rig.bcf','rig',str(t),'--part','app.bcf','appearance','mesh.bmf','red.bmp','--animation','anim.bcf','clip','POSE.cfp','--frame','.5','--scene',str(o),'object.iff','100','--width','160','--height','160','--output',str(p/'control.ppm'),'--depth-output',str(p/'control.depth')],capture_output=True,text=True);assert r.returncode==0 and not r.stderr,r.stderr
    for ext in ('.ppm','.depth'):assert (p/('host'+ext)).read_bytes()==(p/('control'+ext)).read_bytes(),ext
    # Independent CLI control: a DGRP with zero sprite references contributes
    # nothing, so its result must equal the host's avatar-only branch.
    empty_dgrp=struct.pack('<HI',20004,1)+struct.pack('<III',1,3,0)
    empty_iff=iff[:64]+chunk(b'OBJD',100,obj)+chunk(b'DGRP',1128,empty_dgrp)
    empty=p/'empty.far';empty.write_bytes(far([(b'empty.iff',empty_iff)]))
    r=subprocess.run([render,str(a),'rig.bcf','rig',str(t),'--part','app.bcf','appearance','mesh.bmf','red.bmp','--animation','anim.bcf','clip','POSE.cfp','--frame','.5','--scene',str(empty),'empty.iff','100','--width','160','--height','160','--output',str(p/'avatar-control.ppm'),'--depth-output',str(p/'avatar-control.depth')],capture_output=True,text=True)
    assert r.returncode==0 and not r.stderr,r.stderr
    for ext in ('.ppm','.depth'):assert (p/('host-avatar'+ext)).read_bytes()==(p/('avatar-control'+ext)).read_bytes(),ext
    # Source ambiguity remains explicit even through the folded CFP cache resolver.
    a.write_bytes(far([(b'rig.bcf',rig),(b'app.bcf',app),(b'mesh.bmf',mesh),(b'anim.bcf',anim),(b'POSE.cfp',cfp),(b'pose.cfp',cfp)]))
    r=subprocess.run([host,str(a),str(t),str(o),str(p/'bad')],capture_output=True,text=True);assert r.returncode==2 and not r.stdout and 'ambiguous' in r.stderr
print('Reusable scene host: synthetic animated pixels/depth match explicit CLI, cache/identity checks passed')
