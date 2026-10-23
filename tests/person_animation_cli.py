"""Person playback integration with synthetic FAR/IFF/BCF inputs only."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]


def u(value):
    return struct.pack('<I', value)


def pas(value):
    value = value.encode('ascii')
    return bytes([len(value)]) + value


def clip(name, events=(), frames=3):
    # Declare transform arrays and include a broken CFP below to distinguish
    # header resolution from eager decoding, which would reject that payload.
    result = pas(name) + pas('fixture-skill') + b''.join(u(n) for n in [0, 0, 0, frames, frames, 1])
    result += pas('ROOT') + b''.join(u(n) for n in [frames, 0, 1, 1, 0, 0, 0, bool(events)])
    if events:
        result += u(len(events))
        for time, pairs in events:
            result += struct.pack('<iI', time, len(pairs))
            result += b''.join(pas(key) + pas(value) for key, value in pairs)
    return result


def far(path, entries):
    payload = b''.join(value for _, value in entries)
    index = u(len(entries))
    offset = 16
    for name, value in entries:
        name = name.encode('ascii')
        index += struct.pack('<IIII', len(value), len(value), offset, len(name)) + name
        offset += len(value)
    path.write_bytes(b'FAR!byAZ' + u(1) + u(16 + len(payload)) + payload + index)


def archive(path, name, chunks):
    payload = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60, b'\0') + bytes(4)
    for kind, rid, data in chunks:
        payload += kind.encode('ascii') + struct.pack('>IHH', 76 + len(data), rid, 0) + bytes(64) + data
    far(path, [(name, payload)])


def objd(guid, table=0):
    result = bytearray(68)
    struct.pack_into('<I', result, 0, 138)
    struct.pack_into('<H', result, 10, 4096)
    struct.pack_into('<H', result, 18, 2)
    struct.pack_into('<H', result, 26, table)
    struct.pack_into('<I', result, 28, guid)
    return result


def routine(rows):
    return struct.pack('<HHBBHHH', 0x8002, len(rows), 1, 4, 0, 0, 0) + b''.join(
        struct.pack('<HBB', op, t, f) + bytes(data) for op, t, f, data in rows)


def strings(values, language=1):
    return struct.pack('<hH', -3, len(values)) + b''.join(
        bytes([language]) + value.encode('ascii') + b'\0\0' for value in values)


def expression(lhs, rhs, lhs_scope=18, rhs_scope=7, t=254, f=255, op=5):
    return (2, t, f, struct.pack('<hhBBBB', lhs, rhs, 0, op, lhs_scope, rhs_scope))


def animate(source=0, flags=0, ident=1, t=254, f=255):
    return (44, t, f, [ident & 255, ident >> 8, 0, 0, source, flags, 0, 0])


with tempfile.TemporaryDirectory(prefix='odh-person-animation-') as folder:
    root = Path(folder)
    avatar, semi, glob, action, animations = [root / name for name in
                                            ['avatar.far', 'semi.far', 'global.far', 'action.far', 'animations.far']]
    args = [binary, '--avatar', str(avatar), 'Person.iff', '128',
            '--semiglobal', str(semi), 'FixturePerson.iff', '--global', str(glob), 'Global.iff']
    action_args = ['--action', str(action), 'Chair.iff', '128', '4096']

    def setup(main=None, private=(), global_tables=(), table=0, action_rows=None, action_tables=(), action_table=0, global_rows=None):
        archive(avatar, 'Person.iff', [('OBJD', 128, objd(1, table)), ('GLOB', 128, b'\x0dFixturePerson'),
                                     ('BHAV', 4096, routine(main or [animate()]))] + list(private))
        archive(semi, 'FixturePerson.iff', [])
        archive(glob, 'Global.iff', list(global_tables) +
                ([('BHAV', 256, routine(global_rows))] if global_rows else []))
        archive(action, 'Chair.iff', [('OBJD', 128, objd(2, action_table)),
                                    ('BHAV', 4096, routine(action_rows or [expression(1, 1)]))] + list(action_tables))

    def catalog(clips=None, cfp=True):
        far(animations, [('nested\\fixture.cmx.bcf', u(0) + u(0) + u(len(clips or [clip('clip')])) +
                          b''.join(clips or [clip('clip')]))] +
            ([('nested/fixture-skill.cfp', b'\xfd')] if cfp else []))

    def run(extra=(), age=27, enabled=True):
        return subprocess.run(args + ['--age', str(age)] +
                              (['--animation-source', str(animations)] if enabled else []) + list(extra),
                              capture_output=True, text=True)

    def decoded(result, code=0):
        assert result.returncode == code, (result.returncode, result.stderr, result.stdout)
        assert not result.stderr, result.stderr
        data = json.loads(result.stdout)
        if 'animation_profile' in data:
            assert isinstance(data['animation_profile'], str) and data['animation_profile']
            assert isinstance(data['animation_resolution_diagnostic'], str)
            assert isinstance(data['animation_events'], list)
        return data

    def selected(name='clip', age=27, extra=()):
        data = decoded(run(['--ticks', '0'] + list(extra), age), 3)
        assert data['status'] == 'waiting' and data['ticks'] == 0
        assert data['animation_resolution_diagnostic'] == ''
        assert data['animation_progress'] == dict(name=name, frame=0, frames=3, queued_events=0, end=False, loop=False)
        assert data['animation_events'] == []
        return data

    fallback = [('STR#', 129, strings(['', 'clip']))]
    catalog()
    setup(private=fallback)
    # Enabling is opt-in; legacy unsupported opcode and output stay intact.
    legacy = decoded(run(enabled=False), 3)
    assert legacy['diagnostic'] == 'unsupported primitive 44'
    assert all(key not in legacy for key in ['animation_profile', 'animation_resolution_diagnostic',
                                            'animation_progress', 'animation_events'])
    selected()
    data = decoded(run())
    assert data['status'] == 'returned_true' and data['ticks'] == 4
    assert data['animation_progress'] is None and data['animation_events'] == []
    assert data['animation_resolution_diagnostic'] == ''
    # Missing CFP is also valid for the metadata-only silent host.
    catalog(cfp=False)
    selected()

    # A real false branch consumes xevt through a local, then reenters the same
    # header. Wrong ownership/identity would restart forever instead of ending.
    catalog([clip('clip', [(0, [('xevt', '7'), ('footstep', 'left')])])])
    setup(main=[animate(flags=32, f=1), expression(4, 0, rhs_scope=25, t=0)], private=fallback)
    data = decoded(run())
    assert data['ticks'] == 4 and data['actor_state']['person_data'][4] == 7
    assert data['animation_progress'] is None
    assert data['animation_events'] == [dict(tick=1, actor=1, time_id=0,
                                            properties=[['xevt', '7'], ['footstep', 'left']])]
    assert [row['pc'] for row in data['trace']] == [0, 1, 0]

    # The host's retained event bytes fill before the valid animation finishes.
    # Dropping evidence must not alter Playback delivery or Main's true return.
    catalog([clip('clip', [(0, [('footstep', 'v' * 255)])] * 1000)])
    setup(private=fallback)
    data = decoded(run())
    assert data['status'] == 'returned_true' and data['ticks'] == 4 and not data['host_failed']
    assert data['diagnostics_truncated'] and 0 < len(data['animation_events']) < 1000
    assert data['diagnostics_dropped']['animation'] == 1000 - len(data['animation_events'])
    assert data['diagnostics_dropped']['trace'] == 0
    catalog()

    # Timed gestures are visual state, separate from PersonData22/23.
    catalog([clip('clip', [(0, [('lefthand', '1'), ('righthand', '2')])])])
    setup(private=fallback)
    data = decoded(run(['--ticks', '1']), 3)
    assert data['animation_hands'] == dict(left=1, right=2)
    assert data['actor_state']['person_data'][22:24] == [0, 0]
    assert data['animation_events'][0]['properties'] == [['lefthand', '1'], ['righthand', '2']]
    data = decoded(run())
    assert data['animation_progress'] is None and data['animation_hands'] == dict(left=1, right=2)
    catalog([clip('clip', [(0, [('lefthand', 'bad')])])])
    data = decoded(run(), 3)
    assert data['status'] == 'fault' and data['animation_hands'] is None
    assert 'gesture' in data['diagnostic']
    catalog()

    # Object tables: explicit table plus child offset, then native fallback.
    for age, table_id in [(27, 200), (18, 200), (17, 201), (0, 201)]:
        setup(private=[('STR#', table_id, strings(['', 'clip']))], table=200)
        selected(age=age)
    for age, table_id in [(27, 129), (17, 130)]:
        setup(private=[('STR#', table_id, strings(['', 'clip']))], table=200)
        selected(age=age)
    # An existing selected table with no string must not fall back.
    setup(private=[('STR#', 200, strings([''])), *fallback], table=200)
    data = decoded(run(), 3)
    assert data['status'] == 'fault' and data['animation_progress'] is None
    assert data['animation_resolution_diagnostic']

    # All three global sources, including age-sensitive Misc and stock130.
    for source, age, table_id in [(1, 27, 128), (1, 17, 128), (2, 27, 130), (2, 17, 130),
                                  (3, 27, 156), (3, 18, 156), (3, 17, 157)]:
        setup(main=[animate(source)], global_tables=[('STR#', table_id, strings(['', 'clip']))])
        selected(age=age)
    # Language1 indexing ignores unrelated translation records.
    translated = struct.pack('<hH', -3, 4) + b'\x02other\0\0\x02missing\0\0\x01\0\0\x01cLiP\0\0'
    setup(private=[('STR#', 129, translated)])
    selected()

    # Queue dispatch changes CodeOwner to object2 while Caller stays avatar1.
    # Shared global256 inherits that owner and selects its private table.
    catalog([clip('clip'), clip('chair')])
    queue = (17, 1, 253, [0, 0, 1, 0, 0, 0, 0, 0])
    call_global = (256, 254, 255, [1, 0, 0, 0, 0, 0, 0, 0])
    for age, selected_table in [(27, 200), (17, 201)]:
        setup(main=[queue, expression(5, 1)], private=fallback, action_table=200,
              action_rows=[call_global], global_rows=[animate()],
              action_tables=[('STR#', selected_table, strings(['', 'chair']))])
        data = decoded(run(action_args, age))
        assert data['status'] == 'returned_true' and data['completion']['result'] is True
        assert any(row['owner'] == 2 and row['routine'] == 256 for row in data['trace'])
        assert data['ticks'] >= 4 and data['animation_progress'] is None
    # Inspect selected name before progression to distinguish object2 fallback
    # from object1, and exercise owner fallback for both ages.
    for age, selected_table in [(27, 129), (17, 130)]:
        setup(main=[queue, expression(5, 1)], private=fallback, action_table=200,
              action_rows=[call_global], global_rows=[animate()],
              action_tables=[('STR#', selected_table, strings(['', 'chair']))])
        data = decoded(run(action_args + ['--ticks', '1'], age), 3)
        assert data['status'] == 'waiting' and data['animation_progress']['name'] == 'chair'
    # IDFromParam source0 becomes enum65536 and resolves StackObject2 even
    # while the calling shared routine still has avatar CodeOwner1.
    setup(main=[expression(0, 2, lhs_scope=10, t=1),
                (256, 2, 255, [1, 0, 0, 0, 0, 0, 0, 0]),
                (17, 3, 253, [0, 0, 1, 0, 0, 0, 0, 0]), expression(5, 1)],
          private=fallback, action_tables=[('STR#', 129, strings(['', 'chair']))],
          global_rows=[animate(flags=4, ident=0)])
    data = selected(name='chair', extra=action_args)
    assert data['stack'][-1]['owner'] == 1 and data['stack'][-1]['stack_object'] == 2
    data = decoded(run(action_args))
    assert data['status'] == 'returned_true' and data['animation_progress'] is None
    assert any(row['owner'] == 1 and row['routine'] == 256
               for row in data['trace'])
    # Nonblocking primary loops leave a typed progress record after Main ends.
    setup(main=[animate(flags=1)], private=fallback)
    data = decoded(run())
    assert data['status'] == 'returned_true' and data['ticks'] == 0
    assert data['animation_progress'] == dict(name='clip', frame=0, frames=3, queued_events=0, end=False, loop=True)

    # Posture reset selects global walk table by age and normalized posture.
    names = ['', 'sit', 'kneel', 'stand']
    catalog([clip(name) for name in names[1:]])
    for age, table_id in [(27, 150), (17, 151)]:
        for posture, expected in [(1, 'sit'), (2, 'kneel'), (0, 'stand'), (-1, 'stand'), (4, 'stand')]:
            setup(main=[expression(0, posture, t=1), expression(42, 1, t=2), animate(ident=0)],
                  global_tables=[('STR#', table_id, strings(names))])
            data = decoded(run(age=age))
            assert data['ticks'] == 0 and data['status'] == 'returned_true'
            assert data['animation_progress'] == dict(name=expected, frame=0, frames=3, queued_events=0, end=False, loop=True)
            assert data['actor_state']['person_data'][0] == posture and data['actor_state']['person_data'][42] == 4

    # Resolution failures are runtime faults with their own diagnostic.
    catalog()
    for private in [[], [('STR#', 129, strings(['', 'missing']))],
                    [('STR#', 129, strings(['', '']))], [('STR#', 129, strings(['', 'clip'], 2))]]:
        setup(private=private)
        data = decoded(run(), 3)
        assert data['status'] == 'fault' and data['ticks'] == 0
        assert data['animation_resolution_diagnostic'] and data['animation_progress'] is None
        assert data['animation_events'] == []
    setup(private=fallback)
    catalog([clip('clip'), clip('CLIP')])
    data = decoded(run(), 3)
    assert 'ambiguous' in data['animation_resolution_diagnostic'].lower()
    catalog()
    duplicate = root / 'duplicate.far'
    far(duplicate, [('duplicate.bcf', u(0) + u(0) + u(1) + clip('CLIP'))])
    data = decoded(run(['--animation-source', str(duplicate)]), 3)
    assert 'ambiguous' in data['animation_resolution_diagnostic'].lower()
    # Duplicate selected STR# remains an explicit parse/selection error.
    setup(private=fallback + fallback)
    result = run()
    assert result.returncode == 2 and not result.stdout and 'ambiguous' in result.stderr.lower()

    # Repeated sources are accepted up to the bounded 128-source host profile.
    setup(private=fallback)
    empty = root / 'empty.far'
    far(empty, [])
    selected(extra=['--animation-source', str(empty)] * 127)
    result = run(['--animation-source', str(empty)] * 128)
    assert result.returncode == 2 and not result.stdout and result.stderr
    result = run(['--animation-source'])
    assert result.returncode == 2 and not result.stdout and result.stderr
    result = run(['--animation-source', str(root / 'absent.far')])
    assert result.returncode == 2 and not result.stdout and result.stderr

print('Person animation CLI passed (synthetic silent playback only)')
