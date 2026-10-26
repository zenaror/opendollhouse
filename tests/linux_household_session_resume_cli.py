"""Synthetic whole-household fresh-process continuation and safe rejections."""
import hashlib
import json
import os
from pathlib import Path
import struct
import signal
import time
import subprocess
import sys
import tempfile
import zlib
from linux_session_resume_cli import Bits, iff, strings, grid, make_game, saved_person

GUIDS = (0x12345678, 0x12345679)

def make_household(root, secondary_posture=0, empty_hand_slot=False):
    make_game(root)
    user = root / 'UserData'
    family = struct.pack('<II4siiiiiiI', 0, 9, b'IMAF', 7, 900, 100, 0, 0, 0, 2) + struct.pack('<2I', *GUIDS)
    records = b''
    for i, guid in enumerate(GUIDS):
        bank = [0] * 80; bank[61] = 201
        records += struct.pack('<II', 1, 4) + f'Sim{i}'.encode() + b'\0\0' + struct.pack('<ii', i, 1)
        records += struct.pack('<80h', *bank) + struct.pack('<hIii', 11+i, guid, -1, 0)
    neighbors = struct.pack('<II4sI', 0, 62, b'SRBN', 2) + records
    (user/'Neighborhood.iff').write_bytes(iff([('FAMI',201,family),('NBRS',1,neighbors)]))
    # Reuse identical bounded person layouts while rebasing aligned block offsets.
    table = Bits()
    for value in (1,1,2,2,0): table.word(value)
    objm = struct.pack('<II4sB',0,62,b'MjbO',1) + table.data()
    objt = struct.pack('<II4s',0,2,b'tjbo')
    for i,guid in enumerate(GUIDS):
        single,_ = saved_person(i+1,i+1,len(objm)+4,secondary_posture if i else 0,empty_hand_slot)
        payload = single[22:]
        objm += struct.pack('<I',len(objm)+4+len(payload)-12) + payload
        objt += struct.pack('<I6H',guid,0,0,0,0,i+1,2) + b'person\0\0'
    simi = struct.pack('<II4s',0,0x3e,b'IMIS') + bytes(64+34+24)
    chunks=[('SIMI',1,simi),('ObjM',1,objm),('objt',0,objt)]
    chunks += [('Arry',ident,grid(stride)) for ident,stride in [(1,1),(101,1),(2,8),(102,8),(8,1),(9,1),(10,1),(0,4)]]
    (user/'Houses/House07.iff').write_bytes(iff(chunks))
    second=bytearray((user/'Characters/User00000.iff').read_bytes())
    # Distinct character GUID and BHAV arithmetic produce independent resources.
    offset=second.index(b'OBJD')+76
    struct.pack_into('<I',second,offset+28,GUIDS[1])
    bhav=second.index(b'BHAV')+76+12
    struct.pack_into('<h',second,bhav+4+2,2)
    (user/'Characters/User00001.iff').write_bytes(second)


def move_characters_to_private(root, data_home):
    private = data_home/'opendollhouse'/'Characters'
    private.mkdir(parents=True)
    for name in ('User00000.iff', 'User00001.iff'):
        (root/'UserData'/'Characters'/name).replace(private/name)
    return private


def archive_sections(data):
    result=[];pos=28
    count=struct.unpack_from('<H',data,12)[0]
    for _ in range(count):
        ident=data[pos:pos+4]; flags=struct.unpack_from('<I',data,pos+4)[0]
        size=struct.unpack_from('<Q',data,pos+8)[0]
        result.append((ident,flags,data[pos+20:pos+20+size]));pos+=20+size
    assert pos==len(data)
    return result


def rewrite_archive(data, change):
    sections=change(archive_sections(data));payload=b''
    for ident,flags,body in sections:
        payload += ident + struct.pack('<IQI',flags,len(body),zlib.crc32(body)) + body
    header=bytearray(data[:28]);struct.pack_into('<H',header,12,len(sections));struct.pack_into('<Q',header,16,len(payload));struct.pack_into('<I',header,24,zlib.crc32(payload))
    return bytes(header)+payload


def negative_archives(data):
    def missing(sections): return [s for s in sections if s[0]!=b'HHRD']
    def mismatch(sections):
        out=[]
        for ident,flags,body in sections:
            if ident==b'HHRD':
                body=bytearray(body);body[-1]^=1;body=bytes(body)
            out.append((ident,flags,body))
        return out
    def partial(sections):
        out=[]
        for ident,flags,body in sections:
            if ident==b'HHRD':
                size=52+struct.unpack_from('<H',body,22)[0]
                body=body[:2]+struct.pack('<H',1)+body[4:4+size]
            out.append((ident,flags,body))
        return out
    return [('missing',rewrite_archive(data,missing)),('mismatch',rewrite_archive(data,mismatch)),('partial',rewrite_archive(data,partial))]


def main():
    binary=str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='odh-household-cli-') as directory:
        base=Path(directory);root=base/'game';make_household(root)
        data_home=base/'player-data';private_characters=move_characters_to_private(root,data_home)
        env=os.environ.copy()
        for key in ('XDG_CONFIG_HOME','XDG_STATE_HOME'): env[key]=str(base/key)
        env['XDG_DATA_HOME']=str(data_home)
        for key in ('DISPLAY','WAYLAND_DISPLAY'):env.pop(key,None)
        def run(options,success=True):
            result=subprocess.run([binary,'--game-root',str(root)]+options,capture_output=True,text=True,env=env,timeout=30)
            if not success:
                assert result.returncode==2,(result.returncode,result.stdout,result.stderr)
                return result
            assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
            report=json.loads(result.stdout);assert not report['host_failed'],report
            assert report['household_main']['scheduler_entities']==2,report['household_main']
            return report
        for primary in (11,12):
            fresh=['--mode','1','--house','7','--resident-id',str(primary),'--language','1']
            first=run(fresh+['--headless-ticks','1','--save-session-on-exit'])
            path=Path(first['saved_session_path']);saved=path.read_bytes()
            assert dict((i,b) for i,_,b in archive_sections(saved))[b'HOST'][8:10]==b'\x04\0'
            roster=dict((i,b) for i,_,b in archive_sections(saved))[b'HHRD']
            assets=[];offset=4
            for _ in range(struct.unpack_from('<H',roster,2)[0]):
                offset+=18
                length=struct.unpack_from('<H',roster,offset)[0];offset+=2
                assets.append(roster[offset:offset+length].decode());offset+=length+32
            assert len(assets)==2 and all(asset.startswith('PlayerData/Characters/') for asset in assets),assets
            if primary==11:
                scheduled=run(['--load-session',str(path),'--headless-ticks','3','--instruction-budget','25000',
                    '--headless-resident-interaction','2','9999','3'])
                interaction=scheduled['interaction_runtime']
                assert interaction['scheduled_tick']==4 and interaction['scheduled_target']==9999
                assert interaction['trigger']=='headless_schedule' and not interaction['enqueued']
                assert interaction['actor']['id']==2, interaction
                assert interaction['diagnostic']=='scheduled target is not a defined live object'
            resumed=run(['--load-session',str(path),'--headless-ticks','5','--save-session-on-exit'])
            assert resumed['session_loaded'] and resumed['resumed_tick']==1 and resumed['ticks']==6,resumed
            assert resumed['avatar']['owner']==primary-10,resumed['avatar']
            resumed_bytes=Path(resumed['saved_session_path']).read_bytes()
            direct=run(fresh+['--headless-ticks','6','--save-session-on-exit'])
            assert resumed_bytes==Path(direct['saved_session_path']).read_bytes(),'full household restart differs from continuous run'
            before=hashlib.sha256(path.read_bytes()).digest()
            for name,malformed in negative_archives(saved):
                bad=base/(name+'.odhs');bad.write_bytes(malformed);run(['--load-session',str(bad),'--headless-ticks','1'],False)
                assert hashlib.sha256(path.read_bytes()).digest()==before,'rejected restore changed previous save'
            run(['--load-session',str(path),'--single-resident-session','--headless-ticks','1'],False)
        previous=path.read_bytes()
        # A fresh process must refuse a roster whose player-owned IFF bytes changed.
        changed_character=private_characters/'User00001.iff'
        unchanged_bytes=changed_character.read_bytes()
        changed_character.write_bytes(unchanged_bytes+iff([('JUNK',99,b'changed')])[64:])
        changed_restore=run(['--load-session',str(path),'--headless-ticks','1'],False)
        assert 'saved household roster differs from family, World or character resources' in changed_restore.stderr,changed_restore.stderr
        assert path.read_bytes()==previous,'changed private character replaced the previous save'
        changed_character.write_bytes(unchanged_bytes)
        # Unsupported state on the extra resident cannot replace a good save.
        seated_root=base/'seated-game';make_household(seated_root,secondary_posture=1)
        seated_data=base/'seated-player-data';move_characters_to_private(seated_root,seated_data)
        seated_env=dict(env,XDG_DATA_HOME=str(seated_data),XDG_STATE_HOME=str(base/'seated-state'))
        result=subprocess.run([binary,'--game-root',str(seated_root),'--mode','1','--house','7',
            '--resident-id','11','--headless-ticks','1','--save-session-on-exit'],capture_output=True,text=True,env=seated_env,timeout=30)
        assert result.returncode==2 and 'resident seated state has no verified completed Sit binding' in result.stderr,(result.returncode,result.stdout,result.stderr)
        assert path.read_bytes()==previous,'unsupported secondary posture replaced the previous save'
        # Stop after the production log confirms immutable loaded character bytes.
        # All mutations are confined to this synthetic installation.
        changed_root=base/'changed-game';make_household(changed_root)
        changed_data=base/'changed-player-data';changed_private=move_characters_to_private(changed_root,changed_data)
        # Bounded inert chunks leave enough parsing work after the log marker for
        # the parent to stop the child reliably on fast Release builds.
        primary_file=changed_private/'User00000.iff'
        primary_file.write_bytes(primary_file.read_bytes()+iff([('JUNK',i,b'') for i in range(2000)])[64:])
        changed_env=dict(env,ODH_STATE_DIR=str(base/'changed-state'),XDG_DATA_HOME=str(changed_data))
        child=subprocess.Popen([binary,'--game-root',str(changed_root),'--mode','1','--house','7',
            '--resident-id','11','--headless-ticks','6','--save-session-on-exit'],stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,text=True,env=changed_env)
        log=base/'changed-state/opendollhouse/logs/odh-person.log';deadline=time.monotonic()+10
        while child.poll() is None and time.monotonic()<deadline:
            if log.exists() and 'household character resources pinned' in log.read_text():
                child.send_signal(signal.SIGSTOP)
                target=changed_private/'User00001.iff'
                data=bytearray(target.read_bytes());data[80]^=1;target.write_bytes(data)
                child.send_signal(signal.SIGCONT)
                break
            time.sleep(0.0001)
        else:
            child.kill();child.communicate();raise AssertionError('could not observe pinned-byte boundary before child exit')
        stdout,stderr=child.communicate(timeout=30)
        assert child.returncode==2 and 'household character resource changed since it was loaded' in stderr,(child.returncode,stdout,stderr)
        assert path.read_bytes()==previous,'changed loaded character replaced the previous save'

    print('Synthetic player-owned character IFFs survive household save/fresh-process resume; changed private IFF hashes, mismatched and partial rosters are rejected')

if __name__=='__main__':main()
