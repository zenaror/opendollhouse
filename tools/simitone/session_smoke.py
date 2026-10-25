#!/usr/bin/env python3
"""Observe a Simitone save/reopen session in one isolated profile and display.

Two separate processes use the same copied game and ODH_USER_DIR. Scheduled
input goes only to the private Xvfb display. A changed save artifact proves a
write occurred; screenshots and avatar samples still need human comparison.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time

from window_smoke import digest, fingerprint, stop
from compare_save_states import walk_differences


MANAGED = (
    'Simitone.dll', 'Simitone.Client.dll', 'OpenDollhouse.Simulation.dll', 'FSO.SimAntics.dll',
    'FSO.HIT.dll', 'FSO.Content.dll', 'FSO.Files.dll',
    'Simitone.runtimeconfig.json',
)


def save_paths(root):
    """Yield paths eligible as a TS1 house/neighborhood or local FSOV save."""
    for user_data in sorted(root.glob('UserData*')):
        if not user_data.is_dir():
            continue
        neighborhood = user_data / 'Neighborhood.iff'
        if neighborhood.is_file():
            yield neighborhood
        houses = user_data / 'Houses'
        if houses.is_dir():
            yield from sorted(houses.glob('House*.iff'))
    local_house = root / 'LocalHouse'
    if local_house.is_dir():
        yield from sorted(local_house.rglob('*.fsov'))


def embedded_fsov(path):
    """Read IFF chunk headers and hash FSOV payloads without retaining them."""
    chunks = []
    length = path.stat().st_size
    with path.open('rb') as stream:
        if not stream.read(60).startswith(b'IFF FILE 2.'):
            return {'parse_error': 'missing IFF header'}
        stream.seek(64)
        while stream.tell() + 76 <= length:
            header = stream.read(76)
            size = int.from_bytes(header[4:8], 'big')
            if size < 76 or stream.tell() + size - 76 > length:
                return {'parse_error': 'invalid chunk size', 'fsov_chunks': chunks}
            payload_size = size - 76
            if header[:4] == b'FSOV':
                if payload_size < 8:
                    return {'parse_error': 'short FSOV wrapper', 'fsov_chunks': chunks}
                value = hashlib.sha256()
                serialized = hashlib.sha256()
                wrapper = stream.read(8)
                if len(wrapper) != 8:
                    return {'parse_error': 'truncated FSOV wrapper', 'fsov_chunks': chunks}
                value.update(wrapper)
                version = int.from_bytes(wrapper[:4], 'little')
                declared_size = int.from_bytes(wrapper[4:8], 'little')
                remaining = payload_size - 8
                serialized_remaining = min(declared_size, remaining)
                while remaining:
                    block = stream.read(min(1024 * 1024, remaining))
                    if not block:
                        return {'parse_error': 'truncated FSOV', 'fsov_chunks': chunks}
                    value.update(block)
                    part = min(len(block), serialized_remaining)
                    serialized.update(block[:part])
                    serialized_remaining -= part
                    remaining -= len(block)
                chunks.append({'id': int.from_bytes(header[8:10], 'big'),
                               'bytes': payload_size, 'sha256': value.hexdigest(),
                               'version': version,
                               'serialized_payload_bytes': declared_size,
                               'serialized_payload_sha256': serialized.hexdigest(),
                               'wrapper_length_valid': declared_size == payload_size - 8})
            else:
                stream.seek(payload_size, 1)
    return {'fsov_chunks': chunks}


def save_inventory(root):
    inventory = {}
    for path in save_paths(root):
        facts = {'sha256': digest(path), 'bytes': path.stat().st_size}
        if path.suffix.lower() == '.iff':
            facts.update(embedded_fsov(path))
        inventory[str(path.relative_to(root))] = facts
    return inventory


def parse_actions(parser, specs, seconds, kind):
    actions = []
    for spec in specs:
        try:
            when, value = spec.split(':', 1)
            when = float(when)
            if not 0 <= when < seconds:
                raise ValueError()
            if kind == 'click':
                x, y = (int(part) for part in value.split(':'))
                if not (0 <= x < 1024 and 0 <= y < 768):
                    raise ValueError()
                actions.append({'second': when, 'kind': kind, 'x': x, 'y': y})
            else:
                if not value or any(char.isspace() for char in value):
                    raise ValueError()
                actions.append({'second': when, 'kind': kind, 'key': value})
        except (ValueError, TypeError):
            parser.error('--%s requires SECOND:%s within the phase' % (
                kind, 'X:Y on the 1024x768 display' if kind == 'click' else 'XDOTOOL_KEY'))
    return actions


def capture(display, target):
    shot = subprocess.run(['xwd', '-display', display, '-root', '-silent'],
                          capture_output=True, check=True, timeout=10)
    subprocess.run(['convert', 'xwd:-', str(target)], input=shot.stdout,
                   check=True, timeout=10)
    return digest(target)


def log_facts(log_path, profile):
    log_text = log_path.read_text(errors='replace')
    lines = log_text.splitlines()

    def records(prefix):
        values = []
        for line in lines:
            if line.startswith(prefix):
                try:
                    parsed = json.loads(line[len(prefix):])
                    values.append(parsed if isinstance(parsed, dict)
                                  else {'unparsed': line})
                except json.JSONDecodeError:
                    values.append({'unparsed': line})
        return values

    return {
        'profile_isolated': 'User data directory: ' + str(profile) in log_text,
        'vm_error_count': sum('SimAntics exception:' in line for line in lines),
        'fatal_error_count': sum('Unhandled exception.' in line or
                                 '=== FATAL ERROR ===' in line for line in lines),
        'avatar_samples': records('TS1 avatar state: '),
        'save_states': records('TS1 save state: '),
        'native_recoveries': records('Native object recovery: '),
        'native_person_recoveries': records('Native person recovery: '),
        'compatibility_summaries': [record for record in
                                    records('Saved code compatibility: ')
                                    if record.get('Kind') == 'summary'],
    }


def select_state_record(records, phase, occurrence):
    matching = [record for record in records if record.get('Phase') == phase]
    selected = None
    selected_index = None
    if occurrence is None and len(matching) == 1:
        selected_index = 0
    elif occurrence is not None and -len(matching) <= occurrence < len(matching):
        selected_index = occurrence if occurrence >= 0 else len(matching) + occurrence
    if selected_index is not None:
        candidate = matching[selected_index]
        if (candidate.get('SchemaVersion') == 1
                and isinstance(candidate.get('State'), dict)
                and 'DiagnosticError' not in candidate):
            selected = candidate
    return selected, {'Phase': phase, 'MatchingRecordCount': len(matching),
                      'SelectedIndex': selected_index,
                      'Valid': selected is not None}


def state_comparison(left, right, allow_simi_upgrade=False):
    differences = list(walk_differences(left['State'], right['State']))
    for difference in differences:
        allowed = (allow_simi_upgrade
                   and difference['Path'] == '/State/SIMI/Version'
                   and difference['Kind'] == 'value'
                   and type(difference['Left']) is int
                   and type(difference['Right']) is int
                   and difference['Left'] < 64
                   and difference['Right'] == 64)
        difference['Classification'] = ('allowed_format_metadata' if allowed
                                        else 'unexpected')
        if allowed:
            difference['Warning'] = (
                'SIMI.Write promotes a legacy version to 64; no other State '
                'difference is allowed at this boundary.')
    return {
        'LeftPhase': left['Phase'], 'RightPhase': right['Phase'],
        'LeftStateSHA256': left.get('StateSHA256'),
        'RightStateSHA256': right.get('StateSHA256'),
        'UnexpectedDifferenceCount': sum(
            item['Classification'] == 'unexpected' for item in differences),
        'AllowedFormatMetadataDifferenceCount': sum(
            item['Classification'] == 'allowed_format_metadata'
            for item in differences),
        'Differences': differences,
    }


def avatar_retained_data(record):
    result = {}
    for entity in record['State'].get('Entities', []):
        if entity.get('Kind') != 'avatar':
            continue
        identity = str(entity['ObjectID'])
        if identity in result:
            raise ValueError('duplicate avatar ObjectID ' + identity)
        avatar = entity.get('Avatar') or {}
        result[identity] = {
            'PersonData': avatar.get('PersonData'),
            'MotiveData': avatar.get('MotiveData'),
        }
    return result


def observe_phase(name, seconds, actions, argv, env, display, profile, out):
    result = {'seconds': seconds, 'actions': [], 'log': name + '.log',
              'capture': name + '.png'}
    process = None
    try:
        with (out / result['log']).open('w') as log:
            process = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                       env=env)
            result['pid'] = process.pid
            started = time.monotonic()
            pending = list(actions)
            while process.poll() is None and time.monotonic() - started < seconds:
                elapsed = time.monotonic() - started
                if pending and elapsed >= pending[0]['second']:
                    action = pending.pop(0)
                    before = '%s-before-action-%d.png' % (name, len(result['actions']))
                    capture(display, out / before)
                    if action['kind'] == 'click':
                        subprocess.run(['xdotool', 'mousemove', '--sync',
                                        str(action['x']), str(action['y']),
                                        'sleep', '0.3', 'mousedown', '1',
                                        'sleep', '0.15', 'mouseup', '1'],
                                       env=env, check=True, timeout=5)
                    else:
                        subprocess.run(['xdotool', 'key', '--clearmodifiers',
                                        action['key']], env=env, check=True,
                                       timeout=5)
                    result['actions'].append(dict(action, before_capture=before))
                time.sleep(.2)
            result['process_alive_at_deadline'] = process.poll() is None
            result['exit_before_stop'] = process.poll()
            result['actions_not_sent'] = pending
            result['windows'] = subprocess.run(
                ['xwininfo', '-display', display, '-root', '-tree'],
                capture_output=True, text=True, check=True, timeout=5).stdout
            if process.poll() is None:
                status = Path('/proc') / str(process.pid) / 'status'
                if status.exists():
                    result['memory_status'] = [line for line in
                                               status.read_text().splitlines()
                                               if line.startswith(('VmRSS:', 'VmHWM:'))]
            result['capture_sha256'] = capture(display, out / result['capture'])
    finally:
        stop(process)
    result.update(log_facts(out / result['log'], profile))
    result['user_data'] = save_inventory(profile)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', type=Path, required=True)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--first-seconds', type=float, default=45)
    parser.add_argument('--second-seconds', type=float, default=45)
    parser.add_argument('--first-click', action='append', default=[], metavar='SECOND:X:Y')
    parser.add_argument('--second-click', action='append', default=[], metavar='SECOND:X:Y')
    parser.add_argument('--first-key', action='append', default=[], metavar='SECOND:XDOTOOL_KEY')
    parser.add_argument('--second-key', action='append', default=[], metavar='SECOND:XDOTOOL_KEY')
    parser.add_argument('--expected-house', default='UserData/Houses/House07.iff',
                        help='profile-relative house IFF to require changed and carrying FSOV')
    parser.add_argument('--vm-diagnostics', action='store_true')
    parser.add_argument('--require-clean-vm', action='store_true')
    parser.add_argument('--require-save-state', action='store_true',
                        help='require save and FSOV load diagnostic phases and payload hash')
    parser.add_argument('--save-record-index', type=int,
                        help='explicit occurrence for each first-launch save phase; -1 selects last')
    parser.add_argument('--load-record-index', type=int,
                        help='explicit occurrence for each second-launch load phase; -1 selects last')
    args = parser.parse_args()
    if not (1 <= args.first_seconds <= 90 and 1 <= args.second_seconds <= 90):
        parser.error('each phase duration must be between 1 and 90 seconds')
    if args.require_clean_vm and not args.vm_diagnostics:
        parser.error('--require-clean-vm requires --vm-diagnostics')
    if args.require_save_state and not args.vm_diagnostics:
        parser.error('--require-save-state requires --vm-diagnostics')
    expected_house = Path(args.expected_house)
    if (expected_house.is_absolute() or '..' in expected_house.parts
            or len(expected_house.parts) != 3
            or not expected_house.parts[0].startswith('UserData')
            or expected_house.parts[1] != 'Houses'
            or not expected_house.parts[2].startswith('House')
            or expected_house.suffix.lower() != '.iff'):
        parser.error('--expected-house must be UserData*/Houses/House*.iff')
    expected_house_name = str(expected_house)
    phases = {}
    for name, duration in (('first', args.first_seconds),
                           ('second', args.second_seconds)):
        actions = (parse_actions(parser, getattr(args, name + '_click'), duration, 'click')
                   + parse_actions(parser, getattr(args, name + '_key'), duration, 'key'))
        phases[name] = sorted(actions, key=lambda action: action['second'])

    game = args.game_root.resolve(strict=True)
    binary = (args.runtime_dir / 'Simitone').resolve(strict=True)
    out = args.report_dir.resolve()
    protected_before = fingerprint(game)
    out.mkdir(parents=True, exist_ok=False)
    result = {
        'protected_before': protected_before,
        'binary_sha256': digest(binary),
        'managed_sha256': {name: digest(binary.parent / name) for name in MANAGED},
        'runtime': str(binary),
        'audio': 'disabled',
        'vm_diagnostics': args.vm_diagnostics,
        'require_save_state': args.require_save_state,
        'gameplay_verified': False,
        'save_state_verified': False,
        'expected_house': expected_house_name,
        'phases': {},
    }
    try:
        with tempfile.TemporaryDirectory(prefix='odh-simitone-session-') as temporary:
            root = Path(temporary)
            data = root / 'The Sims'
            profile = root / 'profile'
            shutil.copytree(game, data, ignore=shutil.ignore_patterns(
                '*.exe', '*.EXE', '*.bak', '*.iso', '*.ISO'))
            # The provider copies UserData into ODH_USER_DIR on first launch.
            # Treat those matching files as the baseline, not as a save event.
            baseline = save_inventory(data)
            result['source_save_baseline'] = baseline
            xvfb = subprocess.Popen(
                ['Xvfb', '-displayfd', '1', '-screen', '0', '1024x768x24',
                 '-nolisten', 'tcp'], stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL, text=True)
            try:
                if not select.select([xvfb.stdout], [], [], 10)[0]:
                    raise RuntimeError('Xvfb startup timeout')
                number = xvfb.stdout.readline().strip()
                if not number.isdigit():
                    raise RuntimeError('Invalid Xvfb display number')
                xvfb.stdout.close()
                display = ':' + number
                env = os.environ.copy()
                env.update(DISPLAY=display, ODH_USER_DIR=str(profile),
                           XDG_DATA_HOME=str(root / 'data'),
                           XDG_CONFIG_HOME=str(root / 'config'),
                           XDG_STATE_HOME=str(root / 'state'),
                           SDL_AUDIODRIVER='dummy', ALSOFT_DRIVERS='null',
                           LIBGL_ALWAYS_SOFTWARE='1')
                env.pop('WAYLAND_DISPLAY', None)
                env.pop('ODH_VM_DIAGNOSTICS', None)
                if args.vm_diagnostics:
                    env['ODH_VM_DIAGNOSTICS'] = '1'
                argv = [str(binary), '-path' + str(data) + '/', '-nosound']
                result['argv'] = argv
                result['environment'] = {key: env[key] for key in (
                    'DISPLAY', 'ODH_USER_DIR', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME',
                    'XDG_STATE_HOME', 'SDL_AUDIODRIVER', 'ALSOFT_DRIVERS',
                    'LIBGL_ALWAYS_SOFTWARE')}
                for name, duration in (('first', args.first_seconds),
                                       ('second', args.second_seconds)):
                    result['phases'][name] = observe_phase(
                        name, duration, phases[name], argv, env, display,
                        profile, out)
                first, second = result['phases']['first'], result['phases']['second']
                changed = {name: facts for name, facts in first['user_data'].items()
                           if baseline.get(name, {}).get('sha256') != facts['sha256']}
                result['save_artifacts_after_first'] = changed
                expected_first = first['user_data'].get(expected_house_name)
                expected_second = second['user_data'].get(expected_house_name)
                result['expected_house_baseline'] = baseline.get(expected_house_name)
                result['expected_house_after_first'] = expected_first
                result['expected_house_after_second'] = expected_second
                result['expected_house_changed'] = (
                    expected_first is not None and
                    baseline.get(expected_house_name) is not None and
                    baseline[expected_house_name]['sha256']
                    != expected_first['sha256'])
                result['expected_house_has_fsov_after_first'] = bool(
                    expected_first and any(chunk['wrapper_length_valid']
                                           for chunk in expected_first.get('fsov_chunks', [])))
                result['expected_house_has_fsov_after_second'] = bool(
                    expected_second and any(chunk['wrapper_length_valid']
                                            for chunk in expected_second.get('fsov_chunks', [])))
                result['save_artifact_observed'] = (
                    result['expected_house_changed']
                    and result['expected_house_has_fsov_after_first'])
                result['save_artifacts_after_second'] = {
                    name: second['user_data'].get(name) for name in changed}
                result['save_artifacts_persisted'] = (
                    expected_second is not None
                    and expected_first is not None
                    and expected_second['sha256'] == expected_first['sha256']
                    and result['expected_house_has_fsov_after_second'])
                neighborhood_name = str(expected_house.parent.parent / 'Neighborhood.iff')
                result['neighborhood'] = {
                    'baseline': baseline.get(neighborhood_name),
                    'after_first': first['user_data'].get(neighborhood_name),
                    'after_second': second['user_data'].get(neighborhood_name),
                }
                result['local_house_fsov_absent'] = all(
                    not any(name.startswith('LocalHouse/') and name.endswith('.fsov')
                            for name in phase['user_data'])
                    for phase in (first, second))
                result['profile_identity_same'] = (
                    first['profile_isolated'] and second['profile_isolated'])
                result['process_ids_different'] = first['pid'] != second['pid']
                result['first_exit_clean'] = (first['process_alive_at_deadline']
                                              or first['exit_before_stop'] == 0)
                for phase in (first, second):
                    phase['save_state_diagnostic_error_count'] = sum(
                        'DiagnosticError' in record or 'unparsed' in record
                        for record in phase['save_states'])

                selections = {}
                selected = {}
                for phase in ('before_save', 'after_serialize', 'save_complete'):
                    selected[phase], selections[phase] = select_state_record(
                        first['save_states'], phase, args.save_record_index)
                for phase in ('deserialize_before_load', 'after_vm_load', 'after_ep2'):
                    selected[phase], selections[phase] = select_state_record(
                        second['save_states'], phase, args.load_record_index)
                result['save_state_record_selection'] = selections
                first_fsov_hashes = {chunk['serialized_payload_sha256'].lower()
                                     for chunk in (expected_first or {}).get('fsov_chunks', [])
                                     if chunk['wrapper_length_valid']}
                second_fsov_hashes = {chunk['serialized_payload_sha256'].lower()
                                      for chunk in (expected_second or {}).get('fsov_chunks', [])
                                      if chunk['wrapper_length_valid']}
                result['save_state_phase_counts'] = {
                    phase: facts['MatchingRecordCount']
                    for phase, facts in selections.items()}
                save_complete = selected['save_complete']
                deserialize = selected['deserialize_before_load']
                result['first_serialized_payload_matches_fsov'] = bool(
                    save_complete and
                    str(save_complete.get('SerializedPayloadSHA256', '')).lower()
                    in first_fsov_hashes)
                result['second_serialized_payload_matches_fsov'] = bool(
                    deserialize and
                    str(deserialize.get('SerializedPayloadSHA256', '')).lower()
                    in second_fsov_hashes)
                result['save_payload_hash_continuity'] = bool(
                    selected['after_serialize'] and save_complete and deserialize
                    and selected['after_serialize'].get('SerializedPayloadSHA256')
                    == save_complete.get('SerializedPayloadSHA256')
                    == deserialize.get('SerializedPayloadSHA256'))

                comparisons = {}
                boundaries = (
                    ('before_save_to_after_serialize', 'before_save', 'after_serialize', False),
                    ('after_serialize_to_save_complete', 'after_serialize', 'save_complete', False),
                    ('save_complete_to_deserialize', 'save_complete', 'deserialize_before_load', True),
                    ('deserialize_to_after_vm_load', 'deserialize_before_load', 'after_vm_load', False),
                    ('after_vm_load_to_after_ep2', 'after_vm_load', 'after_ep2', False),
                )
                for name, left_phase, right_phase, allow_upgrade in boundaries:
                    left, right = selected[left_phase], selected[right_phase]
                    comparisons[name] = (state_comparison(left, right, allow_upgrade)
                                         if left is not None and right is not None else None)
                result['save_state_comparisons'] = comparisons
                strict_names = [name for name, _, _, _ in boundaries[:-1]]
                result['pre_ep2_state_exact_except_simi_upgrade'] = all(
                    comparisons[name] is not None
                    and comparisons[name]['UnexpectedDifferenceCount'] == 0
                    for name in strict_names)

                result['avatar_retained_differences_after_ep2'] = None
                result['avatar_retained_error'] = None
                result['avatar_retained_count'] = 0
                if selected['after_vm_load'] and selected['after_ep2']:
                    try:
                        before_avatar = avatar_retained_data(selected['after_vm_load'])
                        after_avatar = avatar_retained_data(selected['after_ep2'])
                        result['avatar_retained_count'] = len(before_avatar)
                        result['avatar_retained_differences_after_ep2'] = list(
                            walk_differences(before_avatar, after_avatar,
                                             '/AvatarRetained'))
                    except (KeyError, TypeError, ValueError) as error:
                        result['avatar_retained_error'] = str(error)
                result['avatar_person_motive_exact_after_ep2'] = (
                    result['avatar_retained_count'] > 0
                    and result['avatar_retained_differences_after_ep2'] == [])
                result['avatar_retained_difference_count'] = (
                    None if result['avatar_retained_differences_after_ep2'] is None
                    else len(result['avatar_retained_differences_after_ep2']))
                result['save_state_gate_passed'] = (
                    all(selected.values())
                    and first['save_state_diagnostic_error_count'] == 0
                    and second['save_state_diagnostic_error_count'] == 0
                    and not second['compatibility_summaries']
                    and result['first_serialized_payload_matches_fsov']
                    and result['second_serialized_payload_matches_fsov']
                    and result['save_payload_hash_continuity']
                    and result['pre_ep2_state_exact_except_simi_upgrade']
                    and result['avatar_person_motive_exact_after_ep2'])
                result['save_state_comparison_summary'] = {name: (
                    None if comparison is None else {
                        'UnexpectedDifferenceCount': comparison['UnexpectedDifferenceCount'],
                        'AllowedFormatMetadataDifferenceCount':
                            comparison['AllowedFormatMetadataDifferenceCount']})
                    for name, comparison in comparisons.items()}
                result['phase_summary'] = {name: {
                    'pid': phase['pid'],
                    'process_alive_at_deadline': phase['process_alive_at_deadline'],
                    'exit_before_stop': phase['exit_before_stop'],
                    'profile_isolated': phase['profile_isolated'],
                    'avatar_sample_count': len(phase['avatar_samples']),
                    'vm_error_count': phase['vm_error_count'],
                    'fatal_error_count': phase['fatal_error_count'],
                    'save_state_count': len(phase['save_states']),
                    'save_state_diagnostic_error_count': phase['save_state_diagnostic_error_count'],
                    'log': phase['log'], 'capture': phase['capture'],
                } for name, phase in result['phases'].items()}
            finally:
                stop(xvfb)
    finally:
        result['protected_after'] = fingerprint(game)
        result['protected_files_unchanged'] = (
            protected_before == result['protected_after'])
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items()
                      if key not in ('protected_before', 'protected_after',
                                     'managed_sha256', 'source_save_baseline',
                                     'phases', 'save_state_comparisons',
                                     'avatar_retained_differences_after_ep2')}, indent=2))
    clean = not args.require_clean_vm or all(
        phase['vm_error_count'] == 0 and phase['fatal_error_count'] == 0
        and phase['save_state_diagnostic_error_count'] == 0
        for phase in result['phases'].values())
    return 0 if (result.get('save_artifact_observed')
                 and result.get('save_artifacts_persisted')
                 and result.get('profile_identity_same')
                 and result.get('process_ids_different')
                 and result.get('local_house_fsov_absent')
                 and result.get('first_exit_clean')
                 and (not args.require_save_state or result.get('save_state_gate_passed'))
                 and result['phases']['second']['process_alive_at_deadline']
                 and result['protected_files_unchanged'] and clean) else 1


if __name__ == '__main__':
    raise SystemExit(main())
