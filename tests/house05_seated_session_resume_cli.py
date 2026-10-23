#!/usr/bin/env python3
"""Real-data House05 Sit/save/restart/Stand check for the Linux host.

Commercial files are read-only inputs. Saves and XDG state stay in a temporary
directory; this opt-in integration check is not a CTest fixture.
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path, help='integrated odh-person executable')
    parser.add_argument('--game-root', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'referencias/The Sims')
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    game = args.game_root.resolve(strict=True)
    binary = args.binary.resolve(strict=True)
    protected = [game / name for name in (
        'UserData/Houses/House05.iff', 'UserData/Neighborhood.iff',
        'UserData/Characters/User00009.iff', 'GameData/Animation/Animation.far',
        'GameData/Global/Global.far', 'GameData/Objects/Objects.far')]
    for path in protected:
        require(path.is_file(), f'required original game input missing: {path}')
    before = {path: digest(path) for path in protected}
    try:
        with tempfile.TemporaryDirectory(prefix='odh-house05-seated-session-') as directory:
            temporary = Path(directory).resolve()
            env = os.environ.copy()
            env.pop('DISPLAY', None)
            env.pop('WAYLAND_DISPLAY', None)
            for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                              ('XDG_STATE_HOME', 'state')]:
                env[key] = str(temporary / name)

            def run(phase, options):
                command = [str(binary), '--game-root', str(game)] + options
                result = subprocess.run(command, capture_output=True, text=True,
                                        timeout=args.timeout, env=env)
                try:
                    report = json.loads(result.stdout)
                except json.JSONDecodeError as error:
                    raise RuntimeError(f'{phase}: exit {result.returncode}; {result.stderr.strip()}\n'
                                       f'{result.stdout[:4096]}') from error
                require(result.returncode == 0,
                        f'{phase}: exit {result.returncode}; {report.get("diagnostic")}; '
                        f'{result.stderr.strip()}')
                require(report['host_failed'] is False and report['status'] == 'waiting',
                        f'{phase}: runtime did not remain healthy: {report}')
                require(report['action_incomplete'] is False,
                        f'{phase}: requested action remained incomplete')
                return report

            seated = run('Sit and save', [
                '--mode', '1', '--house', '5', '--resident-id', '34', '--language', '1',
                '--single-resident-session', '--headless-sit-object', '173',
                '--headless-ticks', '512', '--save-session-on-exit'])
            interaction = seated['interaction_runtime']
            require(interaction is not None and interaction['action'] == 8213 and
                    interaction['status'] == 'returned_true' and interaction['result'] is True and
                    interaction['scheduled_target'] == 173 and interaction['queue_size'] == 0 and
                    interaction['queue_active'] is False,
                    f'Sit did not complete on House05 object 173: {interaction}')
            require(seated['actor_state']['person_data'][0] == 1 and
                    interaction['actor']['attachment']['container'] == 173,
                    'resident was not saved seated and attached to object 173')
            save = Path(seated['saved_session_path']).resolve(strict=True)
            require(save.is_relative_to(temporary / 'data') and save.is_file() and save.stat().st_size,
                    f'save escaped the isolated XDG directory or is empty: {save}')

            standing = run('Load in new process and Stand', [
                '--load-session', str(save), '--headless-stand', '--headless-ticks', '512',
                '--single-resident-session', '--save-session-on-exit'])
            stand = standing['stand_runtime']
            require(standing['session_loaded'] is True and standing['resumed_tick'] == seated['ticks'],
                    'second process did not resume the saved tick')
            require(stand is not None and stand['precondition_met'] is True and
                    stand['precondition_posture'] == 1 and stand['precondition_attachment'] == 173 and
                    stand['entry_point'] == 27 and stand['action'] == 8216 and
                    stand['status'] == 'returned_true' and stand['result'] is True and
                    stand['actor_posture'] == 0 and stand['actor_attached'] is False,
                    f'Stand did not complete from the restored semiglobal session: {stand}')
            evidence = {
                'sit_action': interaction['action'],
                'stand_action': stand['action'],
                'saved_tick': seated['ticks'],
                'resumed_tick': standing['resumed_tick'],
                'final_tick': standing['ticks'],
                'stand_detached': not stand['actor_attached'],
                'original_inputs_unchanged': True,
            }
    finally:
        changed = [str(path) for path, value in before.items()
                   if not path.is_file() or digest(path) != value]
        require(not changed, 'original commercial inputs changed: ' + ', '.join(changed))
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
