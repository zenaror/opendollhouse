"""Actual Linux household construction/arrival/resume; synthetic TS1 bytes only."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from linux_household_session_resume_cli import GUIDS, make_household
from linux_household_animation_snap_cli import expression, routine
from linux_session_resume_cli import Bits, far, grid, iff, saved_person, strings

MAILBOX = 0xEF121974
CLOCK = (13, 9, 17, 10, 2026)  # hour, day, minute, month, year, in saved SIMI


def make_missing(root, failure=None, clock=CLOCK):
    make_household(root,continuous_main=True)
    user = root / 'UserData'
    # Two FAMI/NBRS/resources, but only the primary and mailbox exist in OBJM.
    records = b''
    secondary_age = 17 if failure == 'child' else 44
    for index, guid in enumerate(GUIDS):
        bank = [0] * 80
        bank[7], bank[10], bank[31], bank[58], bank[61] = 17, 23, 11 + index, secondary_age if index else 44, 201
        if index == 1 and failure == 'ghost': bank[68] = 1
        records += struct.pack('<II', 1, 4) + f'Sim{index}'.encode() + b'\0\0' + struct.pack('<ii', index, 1)
        records += struct.pack('<80h', *bank) + struct.pack('<hIii', 11 + index, guid, -1, 0)
    family = struct.pack('<II4siiiiiiI', 0, 9, b'IMAF', 7, 900, 100, 0, 0, 0, 2) + struct.pack('<2I', *GUIDS)
    family += struct.pack('<I6h',0,0,0,0,0,0,0)  # Complete version-9 extension.
    (user / 'Neighborhood.iff').write_bytes(iff([
        ('FAMI', 201, family), ('NBRS', 1, struct.pack('<II4sI', 0, 62, b'SRBN', 2) + records)]))
    table = Bits()
    for value in (1, 1, 2, 2, 0): table.word(value)
    objm = struct.pack('<II4sB', 0, 62, b'MjbO', 1) + table.data()
    single, _ = saved_person(1, 1, len(objm) + 4, neighbor_id=11, family_id=201)
    payload = single[22:]
    objm += struct.pack('<I', len(objm) + 4 + len(payload) - 12) + payload
    block = Bits()
    for value in (0, 0, 0, 0, 744, 744, 1): block.integer(value)
    block.word(); block.word()
    for _ in range(8): block.word()
    # Authored singleton mailbox root: no floor-chain parent, and virtual
    # native record0 exists explicitly (physical slots remain separate).
    words = [0] * 68
    words[3], words[11], words[14], words[26] = -1, 2, -1, 0
    for value in words: block.word(value)
    for _ in range(5): block.word()
    for value in (0, 0, -1, 0): block.integer(value)
    block.word(1)  # One virtual record0; zero count would mean UNKNOWN.
    block.word(); block.word()  # Authored empty native link: unknown=0, child=0.
    block.word(); block.integer()  # Dynamic sprite count and unchanged object tail.
    payload = block.data()
    objm += struct.pack('<I', len(objm) + 4 + len(payload) - 12) + payload
    objt = struct.pack('<II4s', 0, 2, b'tjbo')
    for ident, guid, kind in ((1, GUIDS[0], 2), (2, MAILBOX if failure != 'mailbox' else 0x11112222, 4)):
        objt += struct.pack('<I6H', guid, 0, 0, 0, 0, ident, kind) + b'fixture\0'
    clock_globals = [0] * 32
    for index, value in zip((0, 1, 5, 7, 8), clock): clock_globals[index] = value
    if failure == 'clock':
        # The rejection source is ONLY invalid SIMI day0. Keep the other31
        # authored words identical and prove the initial clock is valid.
        valid_globals = tuple(clock_globals)
        hour, day, minute, seconds, month, year = (clock_globals[i] for i in (0, 1, 5, 6, 7, 8))
        assert 0 <= hour < 24 and 1 <= day <= 30 and 0 <= minute < 60
        assert seconds == 0 and 1 <= month <= 12 and year >= 1
        clock_globals[1] = 0
        assert tuple((i, old, new) for i, (old, new) in enumerate(zip(valid_globals, clock_globals))
                     if old != new) == ((1, day, 0),)
    chunks = [('SIMI', 1, struct.pack('<II4s', 0, 0x3e, b'IMIS') + struct.pack('<32h', *clock_globals) + bytes(34 + 24)),
              ('ObjM', 1, objm), ('objt', 0, objt)]
    chunks += [('Arry', ident, grid(stride)) for ident, stride in
               [(1, 1), (101, 1), (2, 8), (102, 8), (8, 1), (9, 1), (10, 1), (0, 4)]]
    (user / 'Houses/House07.iff').write_bytes(iff(chunks))
    mailbox = bytearray(162)
    struct.pack_into('<I', mailbox, 0, 138)
    struct.pack_into('<H', mailbox, 18, 4)
    struct.pack_into('<H', mailbox, 134, 16)
    struct.pack_into('<I', mailbox, 28, MAILBOX if failure != 'mailbox' else 0x11112222)
    if failure == 'ep5_blocker': struct.pack_into('<H', mailbox, 42, 4099)
    mailbox_chunks = [('OBJD', 128, mailbox)]
    if failure == 'ep5_blocker':
        # A MISSING EP5 BHAV legitimately evaluates false. This real BHAV
        # reaches Sleep, which cannot complete a synchronous placement check.
        mailbox_chunks.append(('BHAV', 4099, routine([(0, 254, 255, bytes(8))])))
    far(root / 'GameData/Objects/Objects.far', [('mailbox.iff', iff(mailbox_chunks))])
    secondary = bytearray(162)
    struct.pack_into('<I', secondary, 0, 138)
    for offset, value in [(10, 4096), (18, 2), (22, 65535), (38, 200), (86, 4097), (156, 4098)]:
        struct.pack_into('<H', secondary, offset, value)
    struct.pack_into('<I', secondary, 28, GUIDS[1])
    if failure == 'ep9': struct.pack_into('<H', secondary, 88, 4099)
    if failure == 'ep5_actor': struct.pack_into('<H', secondary, 42, 4099)
    if failure == 'portal': struct.pack_into('<H', secondary, 34, 4099)
    init = [expression(0, 11, scope=0, yes=1), expression(42, 2, scope=3)]
    effects = {'roomimpact': (7, 1), 'lighting': (51, 1), 'window': (40, 0x4000)}
    if failure in effects:
        init[1] = expression(42, 2, scope=3, yes=2)
        init.append(expression(*effects[failure], scope=3))
    if failure in ('dynamic_footprint', 'burning'):
        init[1] = expression(42, 2, scope=3, yes=2)
        # Init first enables CanWalk (0x0008), then dynamic footprint
        # (0x8000). Burning (0x0200) remains outside the supported profile.
        init.append(expression(8, 264, scope=3, yes=3))
        if failure == 'burning':
            init.append(expression(8, -31992, scope=3))  # 0x8308
        elif failure == 'dynamic_footprint':
            init.append(expression(8, -32504, scope=3))  # 0x8108
    if failure == 'global_write':
        # Native RuntimeGlobals now supports this write; preserve the common
        # constructor/placement preconditions and verify the effect in Main.
        init[1] = expression(42, 2, scope=3, yes=2)
        init.append(expression(9, 123, scope=6))
    if failure == 'primitive': init = [(0, 254, 255, bytes(8))]
    ep8 = [(2, 254, 255, struct.pack('<hhBBBB', 0, 2, 0, 3, 0, 7))]
    # Each true branch checks a distinct lifecycle boundary before Main sleeps.
    checks = [(0, 13, 0), (58, secondary_age, 18), (61, 201, 18), (31, 12, 18),
              (7, 17, 18), (10, 23, 18), (65, 0, 18), (0, 100, 14)]
    checks += [(field, value, 3) for field, value in zip((24, 47, 21, 46, 45), clock)]
    # Actual newborn Main checks the supported write/inheritance AND native
    # room publication before reaching Sleep; app0 alone is insufficient.
    effects_to_check = {'global_write': (9, 123, 6), 'ghost': (68, 1, 18),
                        'roomimpact': (7, 1, 3), 'lighting': (51, 1, 3),
                        'window': (40, 0x4000, 3), 'portal': (29, 0, 3)}
    if failure in effects_to_check:
        checks.append(effects_to_check[failure])
        checks.append((29, 0, 3))  # Geometric room1 encoded as ObjectData29=0.
    rows = [(2, index + 1, 255, struct.pack('<hhBBBB', lhs, rhs, 0, 2, scope, 7))
            for index, (lhs, rhs, scope) in enumerate(checks)]
    count = len(checks)
    rows += [(2, count + 1, 255, struct.pack('<hhBBBB', 1, 2, 0, 3, 18, 7)),
             expression(0, 4, scope=9, yes=count + 2), (0, count + 1, 255, bytes(8))]
    body = [''] * 27
    body[0], body[11], body[12], body[13] = 'adult', 'Ada;Bea', 'male', '27'
    if failure == 'child': body[0], body[13] = 'child', '17'
    secondary_chunks = [
        ('OBJD', 128, secondary), ('GLOB', 128, b'\x0dPersonGlobals'), ('STR#', 200, strings(body)),
        ('BHAV', 4096, routine(rows)), ('BHAV', 4097, routine(init)), ('BHAV', 4098, routine(ep8))]
    if failure == 'ep5_actor':
        secondary_chunks.append(('BHAV', 4099, routine([(0, 254, 255, bytes(8))])))
    (user / 'Characters/User00001.iff').write_bytes(iff(secondary_chunks))


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='odh-missing-household-') as folder:
        base = Path(folder); root = base / 'game'; make_missing(root)
        env = os.environ.copy()
        for key in ('XDG_CONFIG_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME'): env[key] = str(base / key)
        for key in ('DISPLAY', 'WAYLAND_DISPLAY'): env.pop(key, None)
        originals = {path: hashlib.sha256(path.read_bytes()).digest() for path in root.rglob('*') if path.is_file()}
        fresh = ['--mode', '1', '--house', '7', '--resident-id', '11', '--language', '1']

        def profile(name):
            return dict(env,XDG_CONFIG_HOME=str(base/'profiles'/name/'config'),
                        XDG_DATA_HOME=str(base/'profiles'/name/'data'))

        def run(options, selected=root, error=None, control=None):
            result = subprocess.run([binary, '--game-root', str(selected)] + options,
                                    capture_output=True, text=True, env=profile(control or selected.name), timeout=30)
            if error:
                assert result.returncode == 2 and error in result.stderr, (result.returncode, result.stdout, result.stderr)
                return result.stderr
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            report = json.loads(result.stdout)
            assert not report['host_failed'], report
            extra = report['household_main']['additional_residents'][0]
            assert extra['entity_id'] == 3 and extra['guid'] == GUIDS[1], extra
            assert extra['status'] == 'waiting', extra  # all startup/body/inheritance assertions reached Sleep
            assert extra['position'] == [728, 728, 1, 0], extra
            # The scheduler owns both avatars AND imported native object
            # Threads, including an authenticated empty mailbox code bank.
            assert len(report['household_main']['additional_residents']) == 1, report
            assert (report['avatar']['owner'], report['avatar']['guid']) == (1, GUIDS[0]), report
            assert report['status'] == 'waiting' and report['diagnostic'] == '', report
            assert type(report['session_loaded']) is bool, report
            assert report['imported_object_main_count'] == (0 if report['session_loaded'] else 1), report
            assert report['recovered_object_main_count'] == 0, report
            assert report['object_main_fault_count'] == 0 and report['last_object_main_fault'] == '', report
            objects = report['create_object_instance']['runtime_objects']
            assert len(objects) == 1, objects
            mailbox = objects[0]
            assert (mailbox['id'], mailbox['guid']) == (2, MAILBOX), mailbox
            assert mailbox['status'] == 'ready' and mailbox['main_steps'] == 0, mailbox
            assert mailbox['position'] == [744, 744, 1, 0] and mailbox['attachment'] is None, mailbox
            assert {report['avatar']['owner'], extra['entity_id'], mailbox['id']} == {1, 2, 3}, report
            assert report['household_main']['scheduler_entities'] == 2 + len(objects) == 3, report
            return report

        first = run(fresh + ['--headless-ticks', '1', '--save-session-on-exit'])
        # Select the second household member, who is absent from the original
        # lot. The launcher must materialize that GUID before resolving the
        # player's avatar selector; the serialized first resident remains a
        # household peer.
        selected_missing = ['--mode', '1', '--house', '7', '--resident-id', '12', '--language', '1']
        result = subprocess.run([binary, '--game-root', str(root)] + selected_missing +
                                ['--headless-ticks', '1'], capture_output=True, text=True,
                                env=profile('selected-missing'), timeout=30)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        selected_report = json.loads(result.stdout)
        assert not selected_report['host_failed'], selected_report
        assert selected_report['avatar']['guid'] == GUIDS[1] and selected_report['avatar']['owner'] == 3, selected_report
        selected_member = next(member for member in selected_report['family_world_reconciliation']['members']
                               if member['guid'] == GUIDS[1])
        assert selected_member['presence'] == 'present_with_position' and selected_member['object_id'] == 3, selected_member
        dynamic_lot = base / 'dynamic-footprint'; make_missing(dynamic_lot, 'dynamic_footprint')
        dynamic_report = run(fresh + ['--headless-ticks', '1'], dynamic_lot)
        dynamic_member = dynamic_report['household_main']['additional_residents'][0]
        assert dynamic_member['flags'] == -32504 and not dynamic_member['static_footprint'], dynamic_member
        path = Path(first['saved_session_path'])
        resumed = run(['--load-session', str(path), '--headless-ticks', '5', '--save-session-on-exit'])
        assert resumed['session_loaded'] and resumed['resumed_tick'] == 1 and resumed['ticks'] == 6, resumed
        resumed_bytes = path.read_bytes()
        direct = run(fresh + ['--headless-ticks', '6', '--save-session-on-exit'], control='continuous')
        assert resumed_bytes == Path(direct['saved_session_path']).read_bytes(), 'materialized fresh-process resume differs from continuous run'
        previous = path.read_bytes()
        alternate_clock = base / 'alternate-clock'
        make_missing(alternate_clock, clock=(6, 23, 41, 4, 1999))
        run(fresh + ['--headless-ticks', '1'], alternate_clock)
        # These cases are supported native behavior, not generic capability
        # failures. Keep every former case with effectful Main assertions.
        for effect in ('global_write', 'ghost', 'roomimpact', 'lighting', 'window', 'portal'):
            supported = base / effect; make_missing(supported, effect)
            before_sources = {p: hashlib.sha256(p.read_bytes()).digest()
                              for p in supported.rglob('*') if p.is_file()}
            observed = run(fresh + ['--headless-ticks', '1', '--save-session-on-exit'], supported)
            probe = observed['room_info_probe']
            assert probe['status'] == 'complete' and probe['room'] == 1, probe
            inventory = probe['room_inventory']
            assert inventory['status'] == 'complete' and inventory['count'] == 3, inventory
            assert {o['owner_id'] for o in inventory['obstacles']} == {1, 2, 3}, inventory
            if effect in ('window', 'portal'):
                # A singleton with no opposite-room part cannot manufacture
                # a door/window target. This is NOT portal traversal coverage.
                walk = probe['attempt_walk_snapshot']
                assert walk['state'] == 'ready', walk
                assert walk['architecture_complete'] is True and walk['portal_inventory_complete'] is True, walk
                assert walk['portal_openings'] == 0 and walk['portal_edges'] == 0, walk
            assert Path(observed['saved_session_path']).is_file(), observed
            assert path.read_bytes() == previous, effect + ' changed prior archive'
            for source, fingerprint in before_sources.items():
                assert hashlib.sha256(source.read_bytes()).digest() == fingerprint, effect + ' changed fixture sources'
        failures = [('ep9', 'absent placement EP9'), ('ep5_actor', 'Snap live EP5:'),
                    ('ep5_blocker', 'Snap live EP5:'),
                    ('primitive', 'startup unsupported'), ('burning', 'startup unsupported'),
                    ('child', 'missing household resident walk/swim tables unavailable'),
                    ('mailbox', 'one positioned singleton mailbox'),
                    ('clock', 'complete person recovery preparation: original avatar actual motive decay/clock services are unavailable')]
        for failure, diagnostic in failures:
            rejected = base / failure; make_missing(rejected, failure)
            before_sources = {p: hashlib.sha256(p.read_bytes()).digest()
                              for p in rejected.rglob('*') if p.is_file()}
            rejected_stderr = run(fresh + ['--headless-ticks', '1', '--save-session-on-exit'], rejected, diagnostic)
            if failure in ('ep5_actor', 'ep5_blocker'):
                assert 'interaction check primitive requires synchronous support: 0' in rejected_stderr, rejected_stderr
            if failure == 'burning':
                assert 'Flags write rejected: old=264 new=-31992 changed_mask=33280' in rejected_stderr, rejected_stderr
            assert path.read_bytes() == previous, failure + ' changed prior archive'
            for source, fingerprint in before_sources.items():
                assert hashlib.sha256(source.read_bytes()).digest() == fingerprint, failure + ' changed fixture sources'
        for source, fingerprint in originals.items():
            assert hashlib.sha256(source.read_bytes()).digest() == fingerprint, 'runtime modified source installation'
    print('Synthetic missing household adult: Init/EP8/Main/body/NBRS/birth/arrival, dynamic-footprint flag and fresh-process resume; 8 fail-closed cases and 6 supported effects with native Main/RoomInfo proof')


if __name__ == '__main__': main()
