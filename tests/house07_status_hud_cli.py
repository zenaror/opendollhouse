#!/usr/bin/env python3
"""Real House07 numeric HUD pixels, live restore, and external input protection."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Independent pixel decoding; strings are numeric, never translated labels.
FONT = ["111101101101111", "010010010010010", "111001111100111",
        "111001111001111", "101101111001001", "111100111001111",
        "111100111101111", "111001001001001", "111101111101111",
        "111101111001111"]
FONT += ["000010000010000", "000000111000000"]

def verify_pixels(path, hud):
    header, pixels = path.read_bytes().split(b'\n255\n', 1)
    assert header == b'P6\n320 240' and len(pixels) == 320*240*3
    def number(value, x, y, scale=1):
        for ch in value:
            mask = FONT[int(ch)] if ch.isdigit() else FONT[10 if ch == ':' else 11]
            for row in range(5):
                for col in range(3):
                    expected = bytes((240,245,250) if mask[row*3+col] == '1' else (18,26,38))
                    for dy in range(scale):
                        for dx in range(scale):
                            offset = ((y+row*scale+dy)*320+x+col*scale+dx)*3
                            assert pixels[offset:offset+3] == expected, (value, row, col)
            x += 4*scale
    number(f'{hud["clock"]["hour"]:02}:{hud["clock"]["minute"]:02}',22,9,2)
    number(str(hud['actor']),116,11)
    for row,value in enumerate(hud['needs']):
        number(str(row+1),9,28+row*10)
        number(str(value),20,28+row*10)
    number('8',9,98)
    number(str(hud['room_score']),20,98)
    return hashlib.sha256(pixels).hexdigest()

def main():
    if len(sys.argv) != 3:
        raise SystemExit(f'usage: {sys.argv[0]} ODH_PERSON GAME_ROOT')
    binary,game = (Path(arg).resolve(strict=True) for arg in sys.argv[1:])
    protected = [game/p for p in ('UserData/Houses/House07.iff','UserData/Neighborhood.iff',
        'UserData/Characters/User00000.iff','UserData/Characters/User00001.iff',
        'GameData/Animation/Animation.far','GameData/Global/Global.far','GameData/Objects/Objects.far')]
    def hashes():
        return {str(p.relative_to(game)):hashlib.file_digest(p.open('rb'),'sha256').hexdigest() for p in protected}
    before = hashes()
    try:
        with tempfile.TemporaryDirectory(prefix='odh-house07-status-') as directory:
            temp=Path(directory);env=os.environ.copy()
            for key in ('DISPLAY','WAYLAND_DISPLAY'):env.pop(key,None)
            for key in ('XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_STATE_HOME'):env[key]=str(temp/key)
            def run(options, ok=True):
                result=subprocess.run([str(binary),'--game-root',str(game)]+options,env=env,
                                      capture_output=True,text=True,timeout=240)
                if not ok:
                    assert result.returncode != 0, result.stdout
                    return
                assert result.returncode == 0, (result.stdout[-2000:],result.stderr[-2000:])
                report=json.loads(result.stdout)
                assert not report['host_failed'] and report['tick_budget_exhausted']
                return report
            fresh=['--mode','1','--house','7','--resident-id','48','--language','1']
            capture=['--headless-status-frame',str(temp/'new.ppm'),'--headless-status-tick','31']
            new=run(fresh+['--headless-ticks','31','--save-session-on-exit']+capture)
            hud=new['status_hud'];sim=new['simulation']
            assert hud['profile']=='numeric_portable_v2' and hud['actor']==105
            assert hud['clock']=={key:sim['clock'][key] for key in ('hour','minute')}
            assert hud['needs']==next(a['needs'] for a in sim['avatars'] if a['id']==hud['actor'])
            assert hud['room_score']==next(a['room_score'] for a in sim['avatars'] if a['id']==hud['actor'])
            first_hash=verify_pixels(temp/'new.ppm',hud)
            restored=run(['--load-session',new['saved_session_path'],'--headless-ticks','30',
                '--headless-status-frame',str(temp/'live.ppm'),'--headless-status-tick','30'])
            assert restored['session_loaded'] and restored['render']['tick']==61
            live=restored['status_hud'];sim=restored['simulation']
            assert live['profile']=='numeric_portable_v2' and live['actor']==105
            assert live['clock']=={key:sim['clock'][key] for key in ('hour','minute')}
            assert live['needs']==next(a['needs'] for a in sim['avatars'] if a['id']==live['actor'])
            assert live['room_score']==next(a['room_score'] for a in sim['avatars'] if a['id']==live['actor'])
            live_hash=verify_pixels(temp/'live.ppm',live)
            direct=run(fresh+['--headless-ticks','61'])
            assert direct['simulation']==restored['simulation'], 'capture altered simulation state'
            for invalid in (['--headless-status-frame',str(temp/'bad.ppm')],
                            ['--headless-status-tick','1'],
                            ['--headless-status-frame',str(temp/'bad.ppm'),'--headless-status-tick','0'],
                            ['--headless-status-frame',str(temp/'bad.ppm'),'--headless-status-tick','3']):
                run(fresh+['--headless-ticks','2']+invalid,False)
            assert not (temp/'bad.ppm').exists()
            print(json.dumps({'evidence':'external_House07_numeric_status_HUD_PPM',
                'fresh_hud':hud,'resumed_hud':live,'fresh_rgb_sha256':first_hash,
                'resumed_rgb_sha256':live_hash,'capture_state_unchanged':True,
                'original_inputs_unchanged':True,'GUI_validated':False},indent=2))
    finally:
        assert before == hashes(), 'external original game inputs changed'

if __name__=='__main__':main()
