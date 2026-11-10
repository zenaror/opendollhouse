"""Integrated household posture/Snap dispatch with synthetic resources only."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from linux_household_session_resume_cli import GUIDS, make_household
from linux_session_resume_cli import Bits, far, grid, iff, saved_person, strings


def routine(rows):
    return struct.pack('<HHBBHHH', 0x8002, len(rows), 0, 4, 1, 0, 0) + b''.join(
        struct.pack('<HBB', op, yes, no) + operands for op, yes, no, operands in rows)


def expression(lhs, rhs, scope=18, yes=254):
    return (2, yes, 255, struct.pack('<hhBBBB', lhs, rhs, 0, 5, scope, 7))


def character(root, index, rows, missing_body=False, child=False, private=True):
    objd = bytearray(162)
    struct.pack_into('<I', objd, 0, 138)
    for offset, value in [(10, 4096), (18, 2), (22, 65535), (38, 200)]:
        struct.pack_into('<H', objd, offset, value)
    struct.pack_into('<I', objd, 28, GUIDS[index])
    body = [''] * 27
    body[0], body[12], body[13] = 'child' if child else 'adult', 'male', '17' if child else '27'
    chunks = [('OBJD', 128, objd), ('GLOB', 128, b'\x0dPersonGlobals'),
              ('BHAV', 4096, routine(rows)),
              ('STR#', 129, strings(['', 'global-adult'])),
              ('STR#', 130, strings(['', 'global-child']))]
    if not missing_body:
        chunks.append(('STR#', 200, strings(body)))
    if private:
        chunks.append(('STR#', 150, strings(['', '', '', f'resident{index}'])))
    (root / f'UserData/Characters/User0000{index}.iff').write_bytes(iff(chunks))


def animation(name):
    def pas(value):
        value = value.encode()
        return bytes([len(value)]) + value
    # Zero transform tracks; a timed property identifies the selected clip.
    return (pas(name) + pas('fixture-skill') + struct.pack('<6I', 0, 0, 0, 3, 3, 1) +
            pas('ROOT') + struct.pack('<9I', 3, 0, 1, 1, 0, 0, 0, 1, 1) +
            struct.pack('<iI', 0, 1) + pas('footstep') + pas(name))


def snap_world(root):
    """Two people and a single positioned, empty one-slot object."""
    table = Bits()
    for value in (1, 1, 2, 2, 3, 3, 0):
        table.word(value)
    objm = struct.pack('<II4sB', 0, 62, b'MjbO', 1) + table.data()
    objt = struct.pack('<II4s', 0, 2, b'tjbo')
    for index, guid in enumerate(GUIDS):
        single, _ = saved_person(index + 1, index + 1, len(objm) + 4,
                                 neighbor_id=11 + index, family_id=201)
        payload = single[22:]
        objm += struct.pack('<I', len(objm) + 4 + len(payload) - 12) + payload
        objt += struct.pack('<I6H', guid, 0, 0, 0, 0, index + 1, 2) + b'person\0\0'
    block = Bits()
    for value in (0, 0, 0, 0, 520, 504, 1):
        block.integer(value)
    block.word(); block.word()
    for _ in range(8): block.word()
    for index in range(68): block.word(3 if index == 11 else -1 if index == 3 else 0)
    for _ in range(5): block.word()
    for value in (0, 0, -1, 0): block.integer(value)
    block.word(2)  # linked slot followed by one vacant storage slot
    for _ in range(4): block.word()
    block.word()  # dynamic sprite flags
    block.integer()  # object UnknownInt
    payload = block.data()
    objm += struct.pack('<I', len(objm) + 4 + len(payload) - 12) + payload
    objt += struct.pack('<I6H', 0x12345680, 0, 0, 0, 0, 3, 4) + b'chair\0'
    chunks = [('SIMI', 1, struct.pack('<II4s', 0, 0x3e, b'IMIS') + bytes(64 + 34 + 24)),
              ('ObjM', 1, objm), ('objt', 0, objt)]
    chunks += [('Arry', ident, grid(stride)) for ident, stride in
               [(1, 1), (101, 1), (2, 8), (102, 8), (8, 1), (9, 1), (10, 1), (0, 4)]]
    (root / 'UserData/Houses/House07.iff').write_bytes(iff(chunks))
    objd = bytearray(162)
    struct.pack_into('<I', objd, 0, 138)
    struct.pack_into('<H', objd, 18, 4)
    struct.pack_into('<I', objd, 28, 0x12345680)
    struct.pack_into('<H', objd, 40, 128)
    storage = struct.pack('<HIIIiiiIi', 0, 0, 0, 0, 0, 0, 0, 0, -1)
    storage += struct.pack('<iiiiiIiii', 16, 16, 16, 100, 0, 0x3e400000, 7, -2, 16)
    route = struct.pack('<HIIIiiiIi', 3, 0, 0, 0, 0, 0, 0, 0, 0)
    route += struct.pack('<iiiiiIiii', 16, 16, 16, 0, 0, 0x3e400000, 0, -2, 16)
    far(root / 'GameData/Objects/Objects.far', [('chair.iff', iff([
        ('OBJD', 128, objd), ('SLOT', 128, struct.pack('<II4sI', 0, 10, b'TOLS', 2) + storage + route)]))])


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='odh-household-animation-snap-') as folder:
        base = Path(folder); root = base / 'game'; make_household(root, empty_hand_slot=True)
        env = os.environ.copy()
        for key in ('XDG_CONFIG_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME'):
            env[key] = str(base / key)
        for key in ('DISPLAY', 'WAYLAND_DISPLAY'): env.pop(key, None)
        def run(primary=11, error=None, code=0):
            result = subprocess.run([binary, '--game-root', str(root), '--mode', '1', '--house', '7',
                '--resident-id', str(primary), '--language', '1', '--headless-ticks', '2'],
                capture_output=True, text=True, env=env, timeout=30)
            if error:
                assert result.returncode == 2 and error in result.stderr and not result.stdout, result
                return
            assert result.returncode == code, (result.returncode, result.stderr, result.stdout)
            return json.loads(result.stdout)
        names = ['resident0', 'resident1', 'global-child', 'global-adult']
        far(root / 'GameData/Animation/Animation.far', [('fixture.cmx.bcf',
            struct.pack('<3I', 0, 0, len(names)) + b''.join(animation(name) for name in names))])
        far(root / 'GameData/Global/Global.far', [('PersonGlobals.iff', iff([])), ('Global.iff', iff([
            ('STR#', 150, strings(['', '', '', 'global-adult'])), ('STR#', 158, strings(['swim'])),
            ('STR#', 151, strings(['', '', '', 'global-child'])), ('STR#', 160, strings(['swim-child']))]))])
        rows = [expression(42, 1, yes=1), (44, 2, 255, bytes(8)),
                expression(0, 4, scope=9, yes=3), (0, 3, 255, bytes(8))]
        for private in (True, False):
            character(root, 0, rows, private=private)
            character(root, 1, rows, child=True, private=private)
            for primary in (11, 12):
                report = run(primary)
                assert not report['host_failed'], report
                selected = {event['actor']: event['properties'][0][1] for event in report['animation_events']}
                assert selected == ({1: 'resident0', 2: 'resident1'} if private else
                                    {1: 'global-adult', 2: 'global-child'}), report
        # Ordinary Object animation also uses each descriptor's age choice.
        # Saved PersonData age is zero in both synthetic banks, deliberately
        # distinct from the adult resident's derived BodySTR age.
        normal_rows = [(44, 1, 255, bytes([1, 0, 0, 0, 0, 0, 0, 0])),
                       expression(0, 4, scope=9, yes=2), (0, 2, 255, bytes(8))]
        character(root, 0, normal_rows)
        character(root, 1, normal_rows, child=True)
        for primary in (11, 12):
            report = run(primary)
            selected = {event['actor']: event['properties'][0][1] for event in report['animation_events']}
            assert selected == {1: 'global-adult', 2: 'global-child'}, report
        # An absent exact resident descriptor fails before Main; no primary fallback.
        character(root, 1, rows, missing_body=True)
        run(error='household startup BodySTR absent')
        snap_world(root)
        snap = bytes([0, 0, 3, 0, 4, 0, 0, 0])
        rows = [expression(0, 3, scope=10, yes=1), (46, 2, 255, snap), (46, 254, 255, snap)]
        for index in range(2): character(root, index, rows)
        for primary in (11, 12):
            report = run(primary)
            resident = report['household_main']['additional_residents'][0]
            statuses = {report['avatar']['owner']: report['status'], resident['entity_id']: resident['status']}
            assert statuses == {1: 'returned_true', 2: 'returned_false'}, report
            assert not report['host_failed'] and resident['diagnostic'] == '', report
            attachments = {report['avatar']['owner']: report['actor_state']['attachment'],
                           resident['entity_id']: resident['attachment']}
            assert attachments == {1: [3, 0], 2: None}, report
            assert report['household_main']['snap_targets'] == [{'entity_id': 3, 'slots': [1]}], report
        # Preserve explicit additional-resident routing gates.
        for opcode in (27, 45):
            character(root, 0, [expression(1, 1)])
            character(root, 1, [(opcode, 254, 255, bytes(8))])
            report = run(code=3)
            resident = report['household_main']['additional_residents'][0]
            assert resident['status'] == 'fault' and resident['diagnostic'] == (
                'household resident routing unavailable: prepared_lot_world_or_global_SLOT_unavailable'), report
    print('Household posture/Snap CLI: resident descriptors, shared SLOT occupancy, routing gates (synthetic only)')


if __name__ == '__main__':
    main()
