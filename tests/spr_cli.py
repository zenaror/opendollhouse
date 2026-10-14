import pathlib,subprocess,sys,tempfile,struct,json,os
exe=sys.argv[1]
def chunk(kind,id,payload):return kind.encode()+struct.pack('>IHH',76+len(payload),id,0)+bytes(64)+payload
def iff(missing=False,duplicate=False,big=False,palette_duplicate=False):
 order='>' if big else '<';spr=struct.pack(order+'4I',504,1,7,16)+struct.pack(order+'IHH',77,1,2)+bytes([4,6,3,2,0,1,5,0])
 pal=struct.pack('<II',1,2)+bytes(8)+bytes([10,20,30,40,50,60]);header=b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1'.ljust(64,b'\0');out=header+chunk('SPR#',9,spr)
 if duplicate:out+=chunk('SPR#',9,spr)
 if not missing:out+=chunk('PALT',7,pal)
 if palette_duplicate:out+=chunk('PALT',7,pal)
 return out
with tempfile.TemporaryDirectory(prefix='odh-spr-cli-') as directory:
 root=pathlib.Path(directory);path=root/'input.iff';path.write_bytes(iff());prefix=root/'sprite'
 def run(*args,ok=True):
  r=subprocess.run([exe,str(path),*map(str,args)],capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});assert (r.returncode==0)==ok,(r.returncode,r.stderr)
  if ok:assert not r.stderr;return json.loads(r.stdout)
  assert r.stderr and not r.stdout
 data=run('9','0','--output',prefix);assert data['width']==2 and data['height']==1 and data['reserved']==77 and not data['palette_missing'];assert prefix.with_suffix('.rgba').read_bytes()==bytes([10,20,30,255,40,50,60,255]);assert prefix.with_suffix('.idx').read_bytes()==bytes([0,1])
 original_path=path;body=iff();name=b'fixture.iff';path=root/'input.far';path.write_bytes(b'FAR!byAZ'+struct.pack('<II',1,16+len(body))+body+struct.pack('<I4I',1,len(body),len(body),16,len(name))+name)
 assert run('9','0','--iff-entry','fixture.iff')['iff_entry']=='fixture.iff';run('9','0','--iff-entry','missing.iff',ok=False);path=original_path
 path.write_bytes(iff(big=True));assert run('0x9','0')['big_endian'];path.write_bytes(iff())
 for args in [('65536','0'),('9','1'),('9','-1'),('9','0','--output'),('9','0','--wat'),('9','0','--missing-palette-black','--missing-palette-black')]:run(*args,ok=False)
 for kw in ({'duplicate':True},{'palette_duplicate':True}):path.write_bytes(iff(**kw));run('9','0',ok=False)
 path.write_bytes(iff(missing=True));run('9','0',ok=False);data=run('9','0','--missing-palette-black','--output',prefix);assert data['palette_missing'] and data['palette_id']==7 and prefix.with_suffix('.rgba').read_bytes()==bytes([0,0,0,255])*2
 path.write_bytes(iff());alias=root/'alias.rgba';os.link(path,alias);before=path.read_bytes();run('9','0','--output',root/'alias',ok=False);assert path.read_bytes()==before
 path=root/'input.rgba';path.write_bytes(iff());before=path.read_bytes();run('9','0','--output',root/'input',ok=False);assert path.read_bytes()==before
print('SPR# CLI, explicit fallback and alias protection passed (synthetic)')
