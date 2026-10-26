"""Native move-out and atomic rejection of unsupported rehome destinations.

The initial managed authority seed is a fixture only. All subsequent relocation
commands use the integrated production planners, carry preparation and journal.
The copied destination has no authority to retire its actors; the original
destination lacks the mailbox/arrival resources required after retirement.
Both reject safely; this fixture does not prove a completed relocation lifecycle.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from linux_household_session_resume_cli import GUIDS, archive_sections, make_household
from linux_managed_family_session_cli import seed_managed_profile
from linux_lot_travel_cli import chunks
from linux_session_resume_cli import iff, strings

def overlay_state(path):
    data = path.read_bytes()
    assert data[:8] == b'ODHF\x06\0\0\0'
    count = struct.unpack_from('<I', data, 40)[0]
    state = dict(house=7, epoch=1, budget=100, arch=0, head=None)
    position = 44
    for _ in range(count):
        tag, size = struct.unpack_from('<HI', data, position)
        position += 6
        body = data[position:position + size]
        position += size
        assert tag == 5 and size == 104 and len(body) == size
        assert struct.unpack_from('<H', body, 0)[0] == 201
        house, price, budget, action = struct.unpack_from('<HiiI', body, 34)
        before_house, flags, epoch, before_budget, before_arch, after_arch = struct.unpack_from('<HHQiii', body, 48)
        assert (before_house, epoch, before_budget, before_arch) == (state['house'], state['epoch'], state['budget'], state['arch'])
        if action == 0:
            assert before_house == 0 and budget == before_budget - price
            state['epoch'] += 1
        elif action == 1:
            assert house == 0 and budget == before_budget + before_arch
        else:
            assert action == 2 and house == before_house and flags == 1
            state['head'] = body[72:104]
        state.update(house=house, budget=budget, arch=after_arch)
    assert position == len(data)
    return state


def archive_state(path, overlay, identity, family, house, epoch):
    data = path.read_bytes()
    digest = hashlib.sha256(data).digest()
    assert path.name == digest.hex() + '.odhs'
    assert overlay_state(overlay)['head'] == digest
    sections = {name: body for name, _, body in archive_sections(data)}
    assert sections[b'FAML'] == struct.pack('<HHQ', 1, 201, epoch) + identity + family
    assert struct.unpack_from('<H', sections[b'HOST'], 11)[0] == house
    roster = sections[b'HHRD']
    count = struct.unpack_from('<H', roster, 2)[0]
    position = 4
    runtime_to_guid = {}
    for _ in range(count):
        runtime_id, generation, guid, neighbor_id, objd = struct.unpack_from('<hQIhH', roster, position)
        position += 18
        length = struct.unpack_from('<H', roster, position)[0]
        position += 2 + length + 32
        assert generation and objd and neighbor_id in (11,12)
        runtime_to_guid[runtime_id] = guid
    assert position == len(roster) and set(runtime_to_guid.values()) == set(GUIDS)
    sim = sections[b'SIMS']
    count = struct.unpack_from('<H', sim, 20)[0]
    position = 22
    fractions = {}
    for _ in range(count):
        runtime_id, bucket, *values = struct.unpack_from('<hB7h', sim, position)
        position += 17
        assert 0 <= bucket < 30
        fractions[runtime_to_guid[runtime_id]] = tuple(values)
    assert position == len(sim) and set(fractions) == set(GUIDS)
    return data, fractions, runtime_to_guid


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='odh-family-rehome-cli-') as directory:
        base = Path(directory)
        root = base / 'game'
        make_household(root, fresh_motives=True)
        user = root / 'UserData'
        # SIMI v0x3e uses 32 global int16 words beginning after its 12-byte
        # header. Supply the native clock's day/month/year instead of the
        # older fixture's unsupported all-zero calendar, before hashing sources.
        house7 = user / 'Houses/House07.iff'
        data = bytearray(house7.read_bytes())
        simi = [entry for entry in chunks(data) if entry[0] == b'SIMI' and entry[1] == 1]
        assert len(simi) == 1
        payload = simi[0][2]
        assert struct.unpack_from('<I', data, payload + 4)[0] == 0x3e
        for index, value in ((1,1),(7,6),(8,1997)):
            struct.pack_into('<h', data, payload + 12 + index * 2, value)
        house7.write_bytes(data)
        # Destination already contains both exact family GUIDs in its original
        # OBJM. This intentionally exercises carry application to existing actors.
        (user / 'Houses/House08.iff').write_bytes((user / 'Houses/House07.iff').read_bytes())
        (user / 'LotLocations.iff').write_bytes(iff([('STR#',1,strings(['7,10,20','8,30,40']))]))
        (user / 'LotZoning.iff').write_bytes(iff([('STR#',1,strings(['7, residential','8, residential']))]))
        protected = {p.relative_to(root): hashlib.sha256(p.read_bytes()).digest() for p in root.rglob('*') if p.is_file()}

        def profile(name):
            env = os.environ.copy()
            for key in ('XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_STATE_HOME','XDG_CACHE_HOME','ODH_STATE_DIR'):
                env[key] = str(base / name / key)
            for key in ('DISPLAY','WAYLAND_DISPLAY'):
                env.pop(key, None)
            return env

        def run(arguments, env, success=True, runtime=True, game=root):
            result = subprocess.run([binary,'--game-root',str(game),'--mode','1','--language','1'] + arguments,
                                    env=env,capture_output=True,text=True,timeout=60)
            assert result.returncode == (0 if success else 2), (result.returncode,result.stdout,result.stderr)
            records = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            if success and runtime:
                assert len(records) == 1 and not records[0]['host_failed'], records
                assert records[0]['household_main']['scheduler_entities'] == 2, records[0]
            return records

        env = profile('primary')
        overlay, identity, family = seed_managed_profile(root, env, binary)
        no_head_env = profile('no-head')
        no_head_overlay, _, _ = seed_managed_profile(root, no_head_env)
        no_head_bytes = no_head_overlay.read_bytes()
        run(['--house','7','--headless-move-out-family','201','--headless-ticks','0'], no_head_env, False)
        assert no_head_overlay.read_bytes() == no_head_bytes, 'move-out without checkpoint changed authority'

        first = run(['--house','7','--resident-id','11','--headless-ticks','2','--save-session-on-exit'], env)[0]
        first_path = Path(first['saved_session_path'])
        first_bytes, first_fractions, first_mapping = archive_state(first_path,overlay,identity,family,7,1)
        assert set(first_mapping.values()) == set(GUIDS) and set(first_fractions) == set(GUIDS)
        live_budget = overlay_state(overlay)['budget']
        before_zero = overlay.read_bytes()
        zero = run(['--house','7','--resident-id','11','--headless-ticks','0','--save-session-on-exit'],env)[0]
        assert zero['session_loaded'] and zero['resumed_tick'] == 2
        assert Path(zero['saved_session_path']).read_bytes() == first_bytes and overlay.read_bytes() == before_zero
        moved_out = run(['--house','7','--headless-move-out-family','201','--headless-ticks','0'],env,runtime=False)
        assert len(moved_out) == 1 and moved_out[0]['status'] == 'moved_out', moved_out
        state = overlay_state(overlay)
        assert state['house'] == 0 and state['budget'] == live_budget and state['head'] == hashlib.sha256(first_bytes).digest()
        assert first_path.read_bytes() == first_bytes

        # Both failure profiles are private copies of our own unhoused fixture;
        # no failure may spend budget or add a new occupancy operation.
        for label in ('bad-destination','bad-resource'):
            shutil.copytree(base / 'primary',base / label)
        bad_env = profile('bad-destination')
        bad_overlay = Path(bad_env['XDG_CONFIG_HOME']) / overlay.relative_to(Path(env['XDG_CONFIG_HOME']))
        before = bad_overlay.read_bytes()
        run(['--house','98','--headless-move-in-family','201','--headless-ticks','0','--save-session-on-exit'],bad_env,False)
        assert bad_overlay.read_bytes() == before and overlay_state(bad_overlay)['budget'] == live_budget
        bad_root = base / 'bad-game'
        shutil.copytree(root,bad_root)
        changed = bad_root / 'UserData/Characters/User00001.iff'
        changed.write_bytes(changed.read_bytes() + iff([('JUNK',99,b'changed synthetic bytes')])[64:])
        bad_env = profile('bad-resource')
        bad_overlay = Path(bad_env['XDG_CONFIG_HOME']) / overlay.relative_to(Path(env['XDG_CONFIG_HOME']))
        before = bad_overlay.read_bytes()
        run(['--house','8','--headless-move-in-family','201','--headless-ticks','0','--save-session-on-exit'],bad_env,False,game=bad_root)
        assert bad_overlay.read_bytes() == before and overlay_state(bad_overlay)['budget'] == live_budget

        # House8 has copied actors without original-occupancy retirement authority.
        # House7 can retire its original actors, but this fixture has no mailbox/
        # arrival resources to construct replacements. Both must remain uncharged.
        for house in (8,7):
            before = overlay.read_bytes()
            run(['--house',str(house),'--resident-id','11','--headless-move-in-family','201',
                 '--headless-ticks','0','--save-session-on-exit'],env,False)
            assert overlay.read_bytes() == before, 'unsupported existing actor committed deferred purchase'
            state = overlay_state(overlay)
            assert state['house'] == 0 and state['budget'] == live_budget
            assert state['head'] == hashlib.sha256(first_bytes).digest()
            assert first_path.read_bytes() == first_bytes, 'rejected rehome changed retained original archive'

        assert protected == {p.relative_to(root): hashlib.sha256(p.read_bytes()).digest()
                             for p in root.rglob('*') if p.is_file()}, 'rehome modified source game resources'
    print('Native synthetic move-out retains current checkpoint; unproven destination actors, incomplete arrival resources, missing lots, changed resources and missing heads reject without spending budget or replacing archives')


if __name__ == '__main__':
    main()
