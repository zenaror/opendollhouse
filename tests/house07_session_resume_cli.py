#!/usr/bin/env python3
"""Real-data, display-free Sit/save/restart/Stand check for the integrated host.

Requires an installed TS1 Complete Collection containing the original House07
and its residents. Commercial resources are read-only inputs and are never
copied into the repository. This is an opt-in integration check, not a CTest
fixture: the host must implement --headless-sit-object and --headless-stand.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def check_runtime(report, phase):
    require(report['host_failed'] is False, f'{phase}: host failed: {report["host_diagnostic"]}')
    require(report['status'] in ('waiting', 'yielded'),
            f'{phase}: expected live Main, got {report["status"]}: {report["diagnostic"]}')
    require(report['instruction_budget_exhausted'] is False, f'{phase}: instruction limit reached')
    require(report['action_incomplete'] is False, f'{phase}: action remained incomplete')
    require(report['queue_size'] == 0 and report['queue_active'] is False,
            f'{phase}: action queue is not idle')
    require(report['completion'] is not None and report['completion']['result'] is True,
            f'{phase}: missing successful action completion')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path, help='integrated odh-person executable')
    parser.add_argument('--game-root', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'referencias/The Sims')
    parser.add_argument('--resident-id', type=int, default=48,
                        help='neighborhood/NBRS ID (default: 48, GUID 850010198; runtime actor ID is 105)')
    parser.add_argument('--sit-object', type=int, default=128)
    parser.add_argument('--sit-ticks', type=int, default=512)
    parser.add_argument('--stand-ticks', type=int, default=512,
                        help='resume tick budget; House07 Stand must finish its real animation/action chain')
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    if (not 1 <= args.resident_id <= 32767 or not 1 <= args.sit_object <= 32767 or
            not 1 <= args.sit_ticks <= 4096 or not 1 <= args.stand_ticks <= 4096 or args.timeout <= 0):
        parser.error('IDs must be 1..32767, ticks 1..4096, and timeout positive')
    game = args.game_root.resolve(strict=True)
    binary = args.binary.resolve(strict=True)
    protected = [game / name for name in (
        'UserData/Houses/House07.iff', 'UserData/Neighborhood.iff',
        'UserData/Characters/User00000.iff', 'GameData/Animation/Animation.far')]
    for path in protected:
        require(path.is_file(), f'required original game input missing: {path}')
    before = {path: digest(path) for path in protected}
    evidence = None
    try:
        with tempfile.TemporaryDirectory(prefix='odh-house07-session-resume-') as directory:
            temporary = Path(directory).resolve()
            env = os.environ.copy()
            # No display is requested or inherited by this test.
            env.pop('DISPLAY', None)
            env.pop('WAYLAND_DISPLAY', None)
            for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                              ('XDG_STATE_HOME', 'state')]:
                env[key] = str(temporary / name)

            def run(phase, options):
                command = [str(binary), '--game-root', str(game)] + options
                result = subprocess.run(command, capture_output=True, text=True,
                                        timeout=args.timeout, env=env)
                if result.returncode != 0:
                    detail = result.stderr.strip()
                    if result.stdout.strip():
                        try:
                            data = json.loads(result.stdout)
                            detail += '\n' + json.dumps({key: data.get(key) for key in (
                                'status', 'diagnostic', 'host_failed', 'host_diagnostic', 'ticks',
                                'session_loaded', 'resumed_tick', 'saved_session_path',
                                'interaction_runtime', 'stand_runtime')})
                        except json.JSONDecodeError:
                            detail += '\n' + result.stdout[:4096]
                    raise RuntimeError(f'{phase}: exit {result.returncode}; command={command!r}\n{detail}')
                try:
                    report = json.loads(result.stdout)
                except json.JSONDecodeError as error:
                    raise RuntimeError(f'{phase}: host did not produce valid JSON: {result.stdout[:4096]}') from error
                check_runtime(report, phase)
                return report

            def saved_archive(report, phase):
                raw = report['saved_session_path']
                require(isinstance(raw, str) and bool(raw), f'{phase}: save path missing')
                path = Path(raw).resolve(strict=True)
                require(path.is_relative_to(temporary / 'data'),
                        f'{phase}: archive escaped isolated XDG data directory: {path}')
                require(not path.is_relative_to(game), f'{phase}: save was written inside game data')
                require(path.is_file() and path.stat().st_size > 0, f'{phase}: saved archive is empty')
                return path

            seated = run('Sit and save', [
                '--mode', '1', '--house', '7', '--resident-id', str(args.resident_id),
                '--language', '1', '--headless-sit-object', str(args.sit_object),
                '--headless-ticks', str(args.sit_ticks), '--single-resident-session', '--save-session-on-exit'])
            require(seated['session_loaded'] is False and seated['resumed_tick'] == 0,
                    'first process unexpectedly resumed another session')
            require(seated['ticks'] == args.sit_ticks, 'Sit process did not reach requested stop')
            interaction = seated['interaction_runtime']
            require(interaction is not None, 'Sit: interaction_runtime is absent')
            require(interaction['enqueued'] is True and interaction['status'] == 'returned_true' and
                    interaction['result'] is True and interaction['scheduled_target'] == args.sit_object,
                    f'Sit failed or targeted another object: {interaction}')
            require(interaction['queue_size'] == 0 and interaction['queue_active'] is False,
                    'Sit action queue remained active')
            require(seated['actor_state']['person_data'][0] == 1, 'Sim did not remain seated at save')
            attachment = interaction['actor']['attachment']
            require(attachment is not None and attachment['container'] == args.sit_object,
                    f'Sim did not remain attached to the selected seat: {attachment}')
            archive = saved_archive(seated, 'Sit')
            seated_archive_hash = digest(archive)
            standing = run('Load and Stand', [
                '--load-session', str(archive), '--headless-stand',
                '--headless-ticks', str(args.stand_ticks), '--single-resident-session', '--save-session-on-exit'])
            require(standing['session_loaded'] is True and standing['resumed_tick'] == seated['ticks'],
                    'second process did not resume the exact saved tick')
            require(standing['ticks'] == seated['ticks'] + args.stand_ticks,
                    'second process did not advance the requested number of ticks')
            stand = standing['stand_runtime']
            require(stand is not None, 'Stand: stand_runtime is absent')
            require(stand['precondition_met'] is True and stand['precondition_posture'] == 1 and
                    stand['precondition_attachment'] == args.sit_object,
                    f'Loaded Sim was not seated in the original seat before Stand: {stand}')
            require(stand['entry_point'] == 27 and stand['status'] == 'returned_true' and
                    stand['result'] is True, f'Stand did not complete: {stand}')
            require(stand['actor_posture'] == 0 and stand['actor_attached'] is False and
                    standing['actor_state']['person_data'][0] == 0,
                    f'Sim did not finish standing and detached: {stand}')
            standing_archive = saved_archive(standing, 'Stand')
            evidence = {
                'evidence': 'House07 real resources; separate integrated headless processes',
                'claim': 'Sim sat, saved, resumed in a fresh process, stood and saved again; no visual or original-executable equivalence claim',
                'resident_id': args.resident_id, 'seat': args.sit_object,
                'saved_tick': seated['ticks'], 'resumed_tick': standing['resumed_tick'],
                'final_tick': standing['ticks'], 'sit_archive_sha256': seated_archive_hash,
                'stand_archive_sha256': digest(standing_archive),
                'source_sha256': {str(path.relative_to(game)): value for path, value in before.items()},
                'original_inputs_unchanged': True,
            }
    finally:
        changed = [str(path) for path, value in before.items()
                   if not path.is_file() or digest(path) != value]
        require(not changed, 'original commercial inputs changed: ' + ', '.join(changed))
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
