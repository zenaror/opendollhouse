"""Opt-in real-data native checkpoint validation; never imports/copies game assets.

Run only after the coordinating agent authorizes execution against a stable build.
Artificial managed authority is seeded solely in private temporary profiles. This
is not a UI move-out/move-in or household relocation validation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def family7(neighborhood):
    data = neighborhood.read_bytes()
    position = 64
    matches = []
    while position < len(data):
        assert position + 76 <= len(data), 'truncated IFF header'
        size = struct.unpack_from('>I', data, position + 4)[0]
        assert size >= 76 and position + size <= len(data), 'invalid IFF chunk extent'
        if data[position:position + 4] == b'FAMI':
            payload = data[position + 76:position + size]
            assert len(payload) >= 40 and payload[8:12] == b'IMAF'
            house, number, budget, arch = struct.unpack_from('<iiii', payload, 12)
            if house == 7:
                chunk = struct.unpack_from('>H', data, position + 8)[0]
                count = struct.unpack_from('<i', payload, 36)[0]
                assert 1 <= count <= 8 and 40 + 4 * count <= len(payload)
                guids = struct.unpack_from('<' + 'I' * count, payload, 40)
                matches.append(dict(chunk=chunk, house=house, number=number, budget=budget,
                                    value_in_arch=arch, guids=guids))
        position += size
    assert position == len(data) and len(matches) == 1, 'House07 source family absent or ambiguous'
    family = matches[0]
    assert family['budget'] >= 0 and family['value_in_arch'] >= 0
    assert family['budget'] + family['value_in_arch'] <= 0x7fffffff
    return family


def seed(root, env, family):
    identity = bytes.fromhex(sha(root / 'UserData/Neighborhood.iff'))
    members = family['guids']
    immutable = struct.pack('<HIIII', family['chunk'], family['number'], family['house'], family['budget'], len(members))
    immutable += struct.pack('<' + 'I' * len(members), *members)
    family_hash = hashlib.sha256(immutable).digest()
    refunded = family['budget'] + family['value_in_arch']

    def operation(action, destination, before_house, before_budget, before_arch):
        payload = struct.pack('<H32sHiiI', family['chunk'], family_hash, destination, 0, refunded, action)
        payload += struct.pack('<HHQiii32s', before_house, 0, 1, before_budget, before_arch, 0, bytes(32))
        assert len(payload) == 104
        return struct.pack('<HI', 5, 104) + payload

    data = b'ODHF' + struct.pack('<HH', 6, 0) + identity + struct.pack('<I', 2)
    data += operation(1, 0, 7, family['budget'], family['value_in_arch'])
    data += operation(0, 7, 0, refunded, 0)
    overlay = Path(env['XDG_CONFIG_HOME']) / 'opendollhouse/player-state' / ('neighborhood-' + identity.hex() + '.odhp')
    overlay.parent.mkdir(parents=True)
    overlay.write_bytes(data)
    return overlay


def sections(path):
    data = path.read_bytes()
    assert data[:8] == b'ODHSES\r\n' and zlib.crc32(data[:24]) == struct.unpack_from('<I', data, 24)[0]
    assert struct.unpack_from('<Q', data, 16)[0] == len(data) - 28
    count = struct.unpack_from('<I', data, 12)[0]
    position = 28
    result = {}
    for _ in range(count):
        name, flags, length, crc = struct.unpack_from('<4sIQI', data, position)
        position += 20
        body = data[position:position + length]
        position += length
        assert len(body) == length and zlib.crc32(body) == crc and name not in result
        result[name] = (flags, body)
    assert position == len(data)
    return result


def section_report(path):
    return {name.decode('ascii'): dict(flags=flags, bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
            for name, (flags, body) in sections(path).items()}


def main():
    checkout = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=checkout / 'build/linux-checkpoint/odh-person')
    parser.add_argument('--game-root', type=Path, default=checkout / '../referencias/The Sims')
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'real-1/result.json')
    args = parser.parse_args()
    binary, game, output = args.binary.resolve(), args.game_root.resolve(), args.output.resolve()
    assert not output.exists(), 'refusing to overwrite an existing evidence report'
    protected_reference = json.loads((checkout / 'reports/native-residential-switch/real-1/result.json').read_text())
    protected_paths = protected_reference['protected_before'].keys()
    report = dict(evidence_type='native Linux real installed-data bounded runtime; artificial temporary managed authority',
                  limitations=['No UI relocation exercised; seeded sidecar is test authority only.',
                               'No portable household state migration exercised.',
                               'No commercial resources or session archive bytes retained in this report.',
                               'No hardware or console validation.'],
                  binary_sha256=sha(binary), game_root=str(game), runs=[], cases={}, passed=False)
    report['protected_before'] = {name: sha(game / name) for name in protected_paths}
    family = family7(game / 'UserData/Neighborhood.iff')
    report['authority_seed'] = dict(source_family_chunk=family['chunk'], source_family_number=family['number'],
                                   source_budget=family['budget'], source_value_in_arch=family['value_in_arch'],
                                   resulting_budget=family['budget'] + family['value_in_arch'], occupancy_epoch=2,
                                   purchase_price=0, synthetic_authority=True)
    try:
        with tempfile.TemporaryDirectory(prefix='odh-real-family-checkpoint-') as temporary:
            base = Path(temporary)

            def profile(label):
                env = os.environ.copy()
                for key in ('DISPLAY', 'WAYLAND_DISPLAY'):
                    env.pop(key, None)
                for key in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_STATE_HOME', 'XDG_CACHE_HOME', 'ODH_STATE_DIR'):
                    env[key] = str(base / label / key)
                overlay = seed(game, env, family)
                return env, overlay

            def run(label, options, env, expected_exit=0):
                command = [str(binary), '--game-root', str(game)] + options
                item = dict(label=label, command=command, expected_exit=expected_exit)
                report['runs'].append(item)
                child = subprocess.Popen(command, cwd=checkout, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                item['pid'] = child.pid
                try:
                    stdout, stderr = child.communicate(timeout=180)
                except subprocess.TimeoutExpired:
                    child.kill()
                    stdout, stderr = child.communicate()
                    item['timed_out'] = True
                item.update(exit=child.returncode, process_waited=child.poll() is not None,
                            pid_absent=not Path('/proc', str(child.pid)).exists(),
                            stdout_sha256=hashlib.sha256(stdout.encode()).hexdigest(),
                            stderr_sha256=hashlib.sha256(stderr.encode()).hexdigest())
                records = [json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
                item['record_count'] = len(records)
                item['visits'] = [{key: record.get(key) for key in ('ticks', 'resumed_tick', 'session_loaded', 'host_failed')}
                                  | {'house': record.get('integrated_session_context', {}).get('house_number')}
                                  for record in records]
                assert child.returncode == expected_exit, (label, child.returncode, stderr[-2000:])
                if expected_exit == 0:
                    assert records and all(not record['host_failed'] for record in records), label
                return records

            common = ['--mode', '1', '--language', '14', '--house', '7', '--resident-id', '47',
                      '--instruction-budget', '500000', '--no-audio']
            env, overlay = profile('continuation')
            first = run('fresh-two-ticks', common + ['--headless-ticks', '2', '--save-session-on-exit'], env)[0]
            assert not first['session_loaded'] and first['ticks'] == 2
            first_path = Path(first['saved_session_path'])
            first_hash, first_sections = sha(first_path), section_report(first_path)
            assert 'FAML' in first_sections
            before_zero = overlay.read_bytes()
            zero = run('automatic-resume-zero', common + ['--headless-ticks', '0', '--save-session-on-exit'], env)[0]
            zero_path = Path(zero['saved_session_path'])
            assert zero['session_loaded'] and zero['resumed_tick'] == 2 and zero['ticks'] == 2
            report['cases']['zero_tick'] = dict(archive_bytes_equal=sha(zero_path) == first_hash,
                                               all_sections_equal=section_report(zero_path) == first_sections,
                                               overlay_unchanged=overlay.read_bytes() == before_zero)
            assert all(report['cases']['zero_tick'].values())
            continued = run('automatic-resume-two-more', common + ['--headless-ticks', '2', '--save-session-on-exit'], env)[0]
            assert continued['session_loaded'] and continued['resumed_tick'] == 2 and continued['ticks'] == 4
            current_path = Path(continued['saved_session_path'])
            current_hash, current_overlay = sha(current_path), overlay.read_bytes()
            report['checkpoint_sections'] = section_report(current_path)
            report['checkpoint_sha256'] = current_hash
            run('reject-old-checkpoint', ['--load-session', str(first_path), '--headless-ticks', '0', '--save-session-on-exit'], env, 2)
            report['cases']['old_checkpoint_rejected_unchanged'] = (overlay.read_bytes() == current_overlay and sha(current_path) == current_hash and sha(first_path) == first_hash)
            assert report['cases']['old_checkpoint_rejected_unchanged']
            control_env, _ = profile('continuous')
            control = run('continuous-four-ticks', common + ['--headless-ticks', '4', '--save-session-on-exit'], control_env)[0]
            control_path = Path(control['saved_session_path'])
            report['cases']['continuous_equivalence'] = dict(all_sections_equal=sections(control_path) == sections(current_path),
                                                            archive_bytes_equal=sha(control_path) == current_hash)
            assert all(report['cases']['continuous_equivalence'].values())
            travel_env, travel_overlay = profile('travel')
            visits = run('travel-seven-five-seven', common + ['--headless-travel-house', '5', '--headless-travel-house', '7',
                         '--headless-ticks', '2', '--save-session-on-exit'], travel_env)
            assert [visit['integrated_session_context']['house_number'] for visit in visits] == [7, 5, 7]
            assert [visit['session_loaded'] for visit in visits] == [False, False, True]
            assert [visit['resumed_tick'] for visit in visits] == [0, 0, 1]
            final_path = Path(visits[-1]['saved_session_path'])
            report['cases']['travel_reload'] = dict(passed=True, final_archive_sha256=sha(final_path),
                                                    overlay_sha256=sha(travel_overlay), final_sections=section_report(final_path))
            report['temporary_profiles_removed_on_exit'] = True
        report['passed'] = True
    except BaseException as error:
        report['failure'] = str(error)
        raise
    finally:
        report['protected_after'] = {name: sha(game / name) for name in protected_paths}
        report['protected_unchanged'] = report['protected_before'] == report['protected_after']
        if not report['protected_unchanged']:
            report['passed'] = False
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')
    assert report['protected_unchanged'], 'protected installed resources changed'
    print(json.dumps(dict(passed=report['passed'], report=str(output))))


if __name__ == '__main__':
    main()
