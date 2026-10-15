"""Filesystem fixtures for archive/loose XA patch resolution, no game data."""
import hashlib,json,subprocess,sys,tempfile
from pathlib import Path
driver,cli=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='odh-xa-catalog-') as folder:
    root=Path(folder)
    r=subprocess.run([driver,folder],capture_output=True,text=True)
    assert r.returncode==0 and not r.stderr,(r.returncode,r.stderr)
    print(r.stdout.strip())
    hsm=root/'a.hsm';hot=root/'a.hot'
    hsm.write_text('')
    hot.write_text('[Patch]\n1=0,fixture,"wrong\\folder\\case.xa",0,0\n')
    archive=root/'a.far';before=hashlib.sha256(archive.read_bytes()).digest()
    args=[cli,str(hsm),str(hot),'1','--far',str(archive)]
    r=subprocess.run(args,capture_output=True,text=True)
    assert r.returncode==0 and not r.stderr,r.stderr
    result=json.loads(r.stdout)
    assert result['patch']==1 and result['frames']==3 and result['entry']=='Voice\\Case.XA'
    assert result['key']=='case.xa' and result['offset']==16 and result['remaining_bytes']==4
    assert hashlib.sha256(archive.read_bytes()).digest()==before
    r=subprocess.run([cli,str(hsm),str(hot),'1','--xa',str(root/'CASE.Xa')],capture_output=True,text=True)
    assert r.returncode==0 and json.loads(r.stdout)['entry']=='',r.stderr
    for extra in [[],['--far'],['--bad',str(archive)]]:
        bad=subprocess.run([cli,str(hsm),str(hot),'1',*extra],capture_output=True,text=True)
        assert bad.returncode==2 and not bad.stdout
    bad=subprocess.run(args+['--xa',str(root/'CASE.Xa')],capture_output=True,text=True)
    assert bad.returncode==2 and 'ambiguous' in bad.stderr and not bad.stdout
print('XA patch CLI provenance, input preservation and rejection passed')
