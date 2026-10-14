"""Decode export and input preservation, using synthetic XA only."""
import hashlib,json,struct,subprocess,sys,tempfile,wave
from pathlib import Path
binary=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='odh-xa-cli-') as folder:
    root=Path(folder);source=root/'sound.xa';output=root/'sound.wav'
    header=b'XAI\0'+struct.pack('<IHHIIHH',6,1,1,22050,44100,2,16)
    # Predictor zero, scale zero: 1,2,3 become PCM 4096,8192,12288.
    source.write_bytes(header+bytes([0,0x12,0x30])+bytes(12)+b'tail')
    before=hashlib.sha256(source.read_bytes()).digest()
    r=subprocess.run([binary,str(source),str(output)],capture_output=True,text=True)
    assert r.returncode==0 and not r.stderr,(r.returncode,r.stderr)
    assert json.loads(r.stdout)==dict(evidence='XA_PCM_decode_only',sample_rate=22050,channels=1,frames=3,pcm_bytes=6,remaining_bytes=4)
    with wave.open(str(output),'rb') as wav:
        assert wav.getnchannels()==1 and wav.getsampwidth()==2 and wav.getframerate()==22050 and wav.getnframes()==3
        assert wav.readframes(3)==struct.pack('<hhh',4096,8192,12288)
    assert hashlib.sha256(source.read_bytes()).digest()==before
    for alias in [source,root/'link.xa',root/'hard.xa']:
        if alias.name=='link.xa':alias.symlink_to(source)
        if alias.name=='hard.xa':alias.hardlink_to(source)
        r=subprocess.run([binary,str(source),str(alias)],capture_output=True,text=True)
        assert r.returncode==2 and not r.stdout and source.read_bytes()==header+bytes([0,0x12,0x30])+bytes(12)+b'tail'
    output.write_bytes(b'preserved')
    source.write_bytes(header+bytes(2))
    r=subprocess.run([binary,str(source),str(output)],capture_output=True,text=True)
    assert r.returncode==2 and not r.stdout and output.read_bytes()==b'preserved'
print('XA CLI synthetic WAV export/input preservation passed')
