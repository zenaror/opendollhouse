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
import zlib

from linux_household_session_resume_cli import GUIDS, archive_sections, make_household
from linux_managed_family_session_cli import seed_managed_profile
from linux_lot_travel_cli import chunks
from linux_session_resume_cli import iff, strings

class FixtureReader:
    def __init__(self, data):
        self.data, self.position = data, 0

    def take(self, size):
        assert 0 <= size <= len(self.data) - self.position, 'truncated fixture record'
        value = self.data[self.position:self.position + size]
        self.position += size
        return value

    def read(self, fmt):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))

    def done(self):
        assert self.position == len(self.data), 'unclaimed fixture record bytes'


def session_sections(data):
    # Full framing/CRC, unlike the intentionally lightweight mutation helper.
    r = FixtureReader(data)
    assert r.take(8) == b'ODHSES\r\n'
    version, flags, count, size, checksum = r.read('HHIQI')
    assert version == 1 and flags == 0 and size == len(data) - 28
    assert zlib.crc32(data[:24]) == checksum and count <= size // 20
    result = {}
    for _ in range(count):
        name = r.take(4)
        assert all(65 <= c <= 90 or 48 <= c <= 57 or c == 32 for c in name) and name not in result
        flags, size, checksum = r.read('IQI')
        assert flags <= 1
        body = r.take(size)
        assert zlib.crc32(body) == checksum
        result[name] = body
    r.done()
    return result


def overlay_state(path, identity, family):
    r = FixtureReader(path.read_bytes())
    assert r.take(4) == b'ODHF'
    version, flags = r.read('HH')
    # This fixture owns managed operations and their property-release extension.
    # Other overlay record families are not silently skipped.
    assert version in (6, 10) and flags == 0
    assert r.take(32) == identity
    count, = r.read('I')
    assert count <= 4096 and count <= (len(r.data) - r.position) // 6
    operations, releases = [], {}
    for _ in range(count):
        tag, size = r.read('HI')
        record = FixtureReader(r.take(size))
        if tag == 5:
            assert not releases, 'managed operations must precede property records'
            assert size == 104
            family_id, = record.read('H')
            assert family_id == 201 and record.take(32) == family
            house, price, budget, action = record.read('HiiI')
            before_house, present, epoch, before_budget, before_arch, after_arch = record.read('HHQiii')
            head = record.take(32)
            assert action <= 2 and present <= 1 and (present or head == bytes(32))
            operations.append(dict(house=house, price=price, budget=budget, action=action,
                before_house=before_house, flags=present, epoch=epoch, before_budget=before_budget,
                before_arch=before_arch, after_arch=after_arch, head=head if present else None))
        else:
            assert tag == 10 and version == 10 and size == 200, ('unsupported fixture overlay record', tag)
            index, house, neighborhood, mode, policy, present, family_id, reserved = record.read('IHBBHHHH')
            epoch, revision = record.read('QQ')
            family_hash, source, installed, expected, head = [record.take(32) for _ in range(5)]
            value, cash = record.read('ii')
            assert index not in releases and (not releases or index > max(releases))
            assert family_id == 201 and reserved == 0
            assert 1 <= house <= 98 and neighborhood == 1 and mode == 1 and policy in (1, 2)
            assert present <= 1 and revision < (1 << 64) - 1
            assert (present == 0 and expected == bytes(32) and revision == 0) or (present == 1 and expected != bytes(32) and revision > 0)
            assert family_hash == family and source != bytes(32) and installed != bytes(32) and head != bytes(32)
            assert value >= 0 and cash >= 0 and epoch > 0
            releases[index] = dict(house=house, epoch=epoch, source=source, cash=cash, head=head,
                revision=revision, expected=expected if present else None, installed=installed, neighborhood=neighborhood)
        record.done()
    r.done()
    assert version != 10 or releases, 'property-release overlay must contain its record'
    assert all(index < len(operations) for index in releases)
    state = dict(house=7, epoch=1, budget=100, arch=0, head=None)
    property_heads = {}
    for index, op in enumerate(operations):
        assert (op['before_house'], op['epoch'], op['before_budget'], op['before_arch']) == (
            state['house'], state['epoch'], state['budget'], state['arch'])
        release = releases.get(index)
        if op['action'] == 0:
            assert release is None and op['before_house'] == 0 and 1 <= op['house'] <= 98
            assert op['price'] >= 0 and op['budget'] == op['before_budget'] - op['price']
            assert op['after_arch'] == op['before_arch'] and op['flags'] == 0
            state['epoch'] += 1
        elif op['action'] == 1:
            assert op['before_house'] and op['house'] == 0 and op['price'] == 0
            assert op['flags'] == 0 and op['after_arch'] == 0
            cash = op['before_budget']
            if release:
                assert (release['house'], release['epoch'], release['source']) == (
                    op['before_house'], op['epoch'], state['head'])
                previous = property_heads.get(release['house'])
                if previous is None:
                    assert release['revision'] == 0 and release['expected'] is None
                else:
                    assert (release['revision'], release['expected'], release['installed'], release['neighborhood']) == previous
                property_heads[release['house']] = (release['revision'] + 1, release['head'], release['installed'], release['neighborhood'])
                cash = release['cash']
            assert op['budget'] == cash + op['before_arch']
        else:
            assert release is None and op['house'] == op['before_house'] and op['house']
            assert op['price'] == 0 and op['flags'] == 1 and op['head'] != bytes(32)
            state['head'] = op['head']
        state.update(house=op['house'], budget=op['budget'], arch=op['after_arch'])
    return state


def read_host(data):
    r = FixtureReader(data)
    assert r.take(8) == b'ODHHST\r\n'
    version, = r.read('H')
    assert 1 <= version <= 7
    flags = r.read('B')[0] if version >= 5 else 0
    assert flags <= (15 if version == 7 else 7 if version >= 6 else 3)
    assert version != 7 or flags & 8
    household = bool(flags & 1) if version >= 5 else version in (2, 4)
    has_budget = bool(flags & 2) if version >= 5 else version in (3, 4)
    mode, house, family_id, family_number, guid, neighbor, runtime_id, tick, seed, count = r.read('BHH i I h h Q Q H'.replace(' ', ''))
    assert mode in (1, 2, 3, 4, 5, 7) and house > 0
    assert count <= (len(r.data) - r.position) // 4
    globals_ = [r.read('Hh') for _ in range(count)]
    assert all(index <= 32767 for index, _ in globals_)
    assert [index for index, _ in globals_] == sorted(set(index for index, _ in globals_))
    budget = r.read('i')[0] if has_budget else None
    r.done()
    return dict(version=version, household=household, mode=mode, house=house,
        family_id=family_id, family_number=family_number, guid=guid, neighbor=neighbor,
        runtime_id=runtime_id, tick=tick, seed=seed, budget=budget)


def read_roster(data):
    r = FixtureReader(data)
    version, count = r.read('HH')
    assert version == 1 and 1 <= count <= 8
    result, guids, neighbors, paths = {}, set(), set(), set()
    for _ in range(count):
        runtime_id, generation, guid, neighbor, objd, length = r.read('hQIhHH')
        assert 0 < length <= 4096
        asset = r.take(length).decode('utf-8')
        fingerprint = r.take(32)
        assert runtime_id > 0 and generation > 0 and guid and neighbor > 0 and objd
        assert runtime_id not in result and guid not in guids and neighbor not in neighbors and asset not in paths
        assert asset and not asset.startswith('/') and not any(c in asset for c in ('\\', ':', '\0'))
        assert all(part not in ('', '.', '..') for part in asset.split('/')) and fingerprint != bytes(32)
        result[runtime_id] = dict(guid=guid, neighbor=neighbor, generation=generation, asset=asset)
        guids.add(guid); neighbors.add(neighbor); paths.add(asset)
    r.done()
    return result


def read_simulation(data, roster):
    r = FixtureReader(data)
    version, = r.read('H')
    assert version in (1, 2)
    tick, fraction, minute, hour, day, month, year = r.read('QHBBBBi')
    fire = r.read('i')[0] if version == 2 else 20000  # Native SIMS1 compatibility value.
    assert (fraction < 30 or (tick == 0 and fraction <= 59 * 30 and fraction % 30 == 0))
    assert minute < 60 and hour < 24 and 1 <= day <= 30 and 1 <= month <= 12 and year >= 1
    assert 0 <= fire <= 20000
    count, = r.read('H')
    assert count == len(roster)
    fractions, seen = {}, set()
    for _ in range(count):
        runtime_id, bucket, *values = r.read('hB7h')
        assert runtime_id in roster and runtime_id not in seen and bucket < 30
        seen.add(runtime_id); fractions[roster[runtime_id]['guid']] = tuple(values)
    r.done()
    assert seen == set(roster)
    return tick, fractions


def archive_state(path, overlay, identity, family, house, epoch):
    data = path.read_bytes()
    digest = hashlib.sha256(data).digest()
    assert path.name == digest.hex() + '.odhs'
    state = overlay_state(overlay, identity, family)
    assert state['head'] == digest
    sections = session_sections(data)
    lease = FixtureReader(sections[b'FAML'])
    version, family_id, saved_epoch = lease.read('HHQ')
    assert (version, family_id, saved_epoch) == (1, 201, epoch)
    assert lease.take(32) == identity and lease.take(32) == family
    lease.done()
    host = read_host(sections[b'HOST'])
    assert host['household'] and (host['mode'], host['house'], host['family_id'], host['family_number']) == (1, house, 201, 900)
    assert host['budget'] == state['budget']
    roster = read_roster(sections[b'HHRD'])
    runtime_to_guid = {runtime: row['guid'] for runtime, row in roster.items()}
    assert set(runtime_to_guid.values()) == set(GUIDS)
    assert {(row['guid'], row['neighbor']) for row in roster.values()} == set(zip(GUIDS, (11, 12)))
    assert host['runtime_id'] in roster
    assert (host['guid'], host['neighbor']) == (roster[host['runtime_id']]['guid'], roster[host['runtime_id']]['neighbor'])
    sim_tick, fractions = read_simulation(sections[b'SIMS'], roster)
    assert sim_tick == host['tick'] and set(fractions) == set(GUIDS)
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
        live_budget = overlay_state(overlay, identity, family)['budget']
        before_zero = overlay.read_bytes()
        zero = run(['--house','7','--resident-id','11','--headless-ticks','0','--save-session-on-exit'],env)[0]
        assert zero['session_loaded'] and zero['resumed_tick'] == 2
        assert Path(zero['saved_session_path']).read_bytes() == first_bytes and overlay.read_bytes() == before_zero
        moved_out = run(['--house','7','--headless-move-out-family','201','--headless-ticks','0'],env,runtime=False)
        assert len(moved_out) == 1 and moved_out[0]['status'] == 'moved_out', moved_out
        state = overlay_state(overlay, identity, family)
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
        assert bad_overlay.read_bytes() == before and overlay_state(bad_overlay, identity, family)['budget'] == live_budget
        bad_root = base / 'bad-game'
        shutil.copytree(root,bad_root)
        changed = bad_root / 'UserData/Characters/User00001.iff'
        changed.write_bytes(changed.read_bytes() + iff([('JUNK',99,b'changed synthetic bytes')])[64:])
        bad_env = profile('bad-resource')
        bad_overlay = Path(bad_env['XDG_CONFIG_HOME']) / overlay.relative_to(Path(env['XDG_CONFIG_HOME']))
        before = bad_overlay.read_bytes()
        run(['--house','8','--headless-move-in-family','201','--headless-ticks','0','--save-session-on-exit'],bad_env,False,game=bad_root)
        assert bad_overlay.read_bytes() == before and overlay_state(bad_overlay, identity, family)['budget'] == live_budget

        # House8 has copied actors without original-occupancy retirement authority.
        # House7 can retire its original actors, but this fixture has no mailbox/
        # arrival resources to construct replacements. Both must remain uncharged.
        for house in (8,7):
            before = overlay.read_bytes()
            run(['--house',str(house),'--resident-id','11','--headless-move-in-family','201',
                 '--headless-ticks','0','--save-session-on-exit'],env,False)
            assert overlay.read_bytes() == before, 'unsupported existing actor committed deferred purchase'
            state = overlay_state(overlay, identity, family)
            assert state['house'] == 0 and state['budget'] == live_budget
            assert state['head'] == hashlib.sha256(first_bytes).digest()
            assert first_path.read_bytes() == first_bytes, 'rejected rehome changed retained original archive'

        assert protected == {p.relative_to(root): hashlib.sha256(p.read_bytes()).digest()
                             for p in root.rglob('*') if p.is_file()}, 'rehome modified source game resources'
    print('Native synthetic move-out retains current checkpoint; unproven destination actors, incomplete arrival resources, missing lots, changed resources and missing heads reject without spending budget or replacing archives')


if __name__ == '__main__':
    main()
