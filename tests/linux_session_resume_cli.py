"""Two real integrated-host processes; entirely synthetic TS1 resources."""
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def iff(chunks):
    header = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1'.ljust(64, b'\0')
    return header + b''.join(kind.encode() + struct.pack('>IHH', 76 + len(data), ident, 0) +
                            bytes(64) + data for kind, ident, data in chunks)


def far(path, entries=()):
    path.parent.mkdir(parents=True, exist_ok=True)
    offset = 16
    manifest = struct.pack('<I', len(entries))
    for name, data in entries:
        name = name.encode()
        manifest += struct.pack('<IIII', len(data), len(data), offset, len(name)) + name
        offset += len(data)
    path.write_bytes(b'FAR!byAZ' + struct.pack('<II', 1, offset) +
                     b''.join(data for _, data in entries) + manifest)


def strings(values):
    return struct.pack('<hH', -3, len(values)) + b''.join(b'\1' + value.encode() + b'\0\0' for value in values)


class Bits:
    def __init__(self, offset=0):
        self.bits = ''
        self.offset = offset

    def field(self, value, width):
        self.bits += '0' if value == 0 else '111' + format(value & ((1 << width) - 1), f'0{width}b')

    def integer(self, value=0):
        self.field(value, 32)

    def word(self, value=0):
        self.field(value, 16)

    def string(self, value=''):
        self.bits += '0' * (-len(self.bits) % 8)
        for byte in value.encode() + b'\0':
            self.bits += format(byte, '08b')
        if (self.offset + len(self.bits) // 8) % 2:
            self.bits += '00000000'

    def data(self):
        bits = self.bits + '0' * (-len(self.bits) % 8)
        return bytes(int(bits[pos:pos + 8], 2) for pos in range(0, len(bits), 8))


def saved_person(object_id=1, type_id=1, block_offset=None, person_posture=0, empty_hand_slot=False,
                 fresh_motives=False):
    table = Bits()
    for value in (object_id, type_id, 0):
        table.word(value)
    container = struct.pack('<II4sB', 0, 62, b'MjbO', 1) + table.data()
    block = Bits(len(container) + 4 if block_offset is None else block_offset)
    for value in (0, 0, 0, 0, 504, 504, 1):
        block.integer(value)
    block.word()  # unknown
    block.word()  # attribute count
    for _ in range(8):
        block.word()
    for index in range(68):
        block.word(object_id if index == 11 else -1 if index == 3 else 0)
    for _ in range(5):
        block.word()
    for value in (0, 0, -1, 0):  # stack, flags, relationship flag/count
        block.integer(value)
    block.word(2 if empty_hand_slot else 0)  # linked slot plus explicit empty hand slot
    if empty_hand_slot:
        for _ in range(4): block.word()
    block.word()  # dynamic sprite flags
    block.integer()  # animation events
    block.integer()  # engaged
    for _ in range(10):
        block.string()
    block.integer()  # accessories
    for _ in range(3):
        block.string()  # animation, carry, base
    block.integer()  # routing state
    for _ in range(9 + 16):
        block.integer()  # finite float zero
    for index in range(16):
        # VMAvatar.Init/apply_avatar_body baseline, opt-in for relocation fixtures.
        block.integer(0x42C80000 if fresh_motives and index != 11 else 0)
    for index in range(80):
        block.word(person_posture if index == 0 else 0)
    block.integer()  # routing frames
    for _ in range(2):  # absent active and last interactions (OBJMInteraction.IsValid)
        block.integer(); block.integer()
        # TTAIndex=-1 is the original absence sentinel; zero is an ACTIVE row0.
        block.word(); block.word(); block.word(); block.integer(-1)
        for _ in range(4):
            block.word()
        block.integer(); block.word(); block.integer(); block.integer()
    for _ in range(4):  # queue/use/motive counts, UnknownInt
        block.integer()
    payload = block.data()
    container += struct.pack('<I', len(container) + 4 + len(payload) - 12) + payload
    types = struct.pack('<II4sI6H', 0, 2, b'tjbo', 0x12345678, 0, 0, 0, 0, 1, 2) + b'person\0\0'
    return container, types


def grid(stride):
    data = bytes(4096 * stride)
    result = struct.pack('<5I', 0, 64, 64, stride, 0)
    for pos in range(0, len(data), 16384):
        run = data[pos:pos + 16384]
        result += struct.pack('<H', len(run)) + run
    return result


def make_game(root):
    user, game = root / 'UserData', root / 'GameData'
    (user / 'Houses').mkdir(parents=True)
    (user / 'Characters').mkdir()
    game.mkdir()
    (user / 'LotLocations.iff').write_bytes(iff([('STR#', 1, strings(['7,10,20']))]))
    (user / 'LotZoning.iff').write_bytes(iff([('STR#', 1, strings(['7, residential']))]))
    family = struct.pack('<II4siiiiiiII', 0, 9, b'IMAF', 7, 900, 100, 0, 0, 0, 1, 0x12345678)
    # Complete original FAMI9 tail: retained DWORD plus six family spell words.
    family += struct.pack('<I6h', 0, 0, 0, 0, 0, 0, 0)
    bank = [0] * 80
    bank[61] = 201
    neighbor = struct.pack('<II', 1, 4) + b'Sim0\0\0' + struct.pack('<ii', 0, 1)
    neighbor += struct.pack('<80h', *bank) + struct.pack('<hIii', 11, 0x12345678, -1, 0)
    neighbors = struct.pack('<II4sI', 0, 62, b'SRBN', 1) + neighbor
    (user / 'Neighborhood.iff').write_bytes(iff([('FAMI', 201, family), ('NBRS', 1, neighbors)]))
    simi = struct.pack('<II4s', 0, 0x3e, b'IMIS') + bytes(64 + 34 + 24)
    objm, objt = saved_person()
    chunks = [('SIMI', 1, simi), ('ObjM', 1, objm), ('objt', 0, objt)]
    chunks += [('Arry', ident, grid(stride)) for ident, stride in
               [(1, 1), (101, 1), (2, 8), (102, 8), (8, 1), (9, 1), (10, 1), (0, 4)]]
    (user / 'Houses/House07.iff').write_bytes(iff(chunks))
    objd = bytearray(162)
    struct.pack_into('<I', objd, 0, 138)
    for offset, value in [(10, 4096), (18, 2), (22, 65535), (38, 200)]:
        struct.pack_into('<H', objd, offset, value)
    struct.pack_into('<I', objd, 28, 0x12345678)
    body = [''] * 27
    body[0], body[12], body[13] = 'adult', 'male', '27'
    # Increment person-data[1] once, set local0=4, Sleep(local0), loop Sleep.
    rows = [(2, 1, 255, struct.pack('<hhBBBB', 1, 1, 0, 3, 18, 7)),
            (2, 2, 255, struct.pack('<hhBBBB', 0, 4, 0, 5, 9, 7)),
            (0, 2, 255, bytes(8))]
    main = struct.pack('<HHBBHHH', 0x8002, len(rows), 0, 4, 1, 0, 0)
    main += b''.join(struct.pack('<HBB', op, yes, no) + operands for op, yes, no, operands in rows)
    (user / 'Characters/User00000.iff').write_bytes(iff([
        ('OBJD', 128, objd), ('GLOB', 128, b'\x0dPersonGlobals'),
        ('STR#', 200, strings(body)), ('BHAV', 4096, main)]))
    # Material discovery also enables the ordinary build/portal preparation.
    # Supply a structurally valid global code bank; this unused routine does
    # not stand in for any original global gameplay behavior.
    far(game / 'Global/Global.far', [('PersonGlobals.iff', iff([])), ('Global.iff', iff([
        ('STR#', 150, strings(['walk'])), ('STR#', 158, strings(['swim'])),
        ('BHAV', 255, main)]))])
    for path in ['Objects/Objects.far', 'Animation/Animation.far', 'Textures/Textures.far']:
        far(game / path)
    far(root / 'ExpansionShared/ExpansionShared.far')
    far(root / 'UIGraphics/UIGraphics.far')
    for path in ['floors.iff', 'walls.iff']:
        (game / path).write_bytes(iff([]))


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='odh-session-cli-') as directory:
        base = Path(directory)
        root = base / 'game'
        make_game(root)
        env = os.environ.copy()
        for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_STATE_HOME', 'state'), ('XDG_DATA_HOME', 'data')]:
            env[key] = str(base / name)
        def run(extra):
            result = subprocess.run([binary, '--game-root', str(root)] + extra,
                                    capture_output=True, text=True, env=env, timeout=30)
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            data = json.loads(result.stdout)
            assert not data['host_failed'] and data['status'] != 'fault', data
            return data
        fresh = ['--mode', '1', '--house', '7', '--resident-id', '11', '--language', '1']
        first = run(fresh + ['--headless-ticks', '1', '--single-resident-session', '--save-session-on-exit'])
        assert first['ticks'] == 1 and not first['session_loaded'] and first['resumed_tick'] == 0, first
        saved = Path(first['saved_session_path'])
        assert saved.is_file() and saved.stat().st_size > 0
        resumed = run(['--load-session', str(saved), '--headless-ticks', '1', '--single-resident-session', '--save-session-on-exit'])
        assert resumed['session_loaded'] and resumed['resumed_tick'] == 1 and resumed['ticks'] == 2, resumed
        resumed_bytes = Path(resumed['saved_session_path']).read_bytes()
        direct = run(fresh + ['--headless-ticks', '2', '--single-resident-session', '--save-session-on-exit'])
        assert direct['ticks'] == 2 and not direct['session_loaded'], direct
        assert resumed_bytes == Path(direct['saved_session_path']).read_bytes(), 'resumed state differs from uninterrupted execution'
        assert resumed['actor_state']['person_data'][1] == direct['actor_state']['person_data'][1] == 1, 'Main restarted'
    print('Integrated session CLI: two-process continuation matches uninterrupted execution (synthetic only)')


if __name__ == '__main__':
    main()
