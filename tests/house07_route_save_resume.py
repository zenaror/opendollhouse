#!/usr/bin/env python3
"""Real House07 Go Here route save/restart/resume check.

This opt-in integration test needs the installed TS1 game data. It writes only
to temporary XDG profile directories and treats the commercial files as
read-only inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--game-root', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'referencias/The Sims')
    parser.add_argument('--timeout', type=float, default=240)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    game = args.game_root.resolve(strict=True)
    protected = [game / name for name in (
        'UserData/Houses/House07.iff', 'UserData/Neighborhood.iff',
        'UserData/Characters/User00000.iff', 'GameData/Animation/Animation.far',
        'GameData/Global/Global.far', 'GameData/Objects/Objects.far')]
    if any(not path.is_file() for path in protected):
        parser.error('game-root is missing one or more required original game files')
    before = {path: digest(path) for path in protected}

    with tempfile.TemporaryDirectory(prefix='odh-house07-route-resume-') as temporary:
        root = Path(temporary)

        def environment(profile):
            env = os.environ.copy()
            env.pop('DISPLAY', None)
            env.pop('WAYLAND_DISPLAY', None)
            for key, folder in (('XDG_CONFIG_HOME', 'config'),
                                ('XDG_DATA_HOME', 'data'),
                                ('XDG_STATE_HOME', 'state')):
                env[key] = str(root / profile / folder)
            return env

        def run(profile, options):
            result = subprocess.run(
                [str(binary), '--game-root', str(game), *options],
                capture_output=True, text=True, timeout=args.timeout,
                env=environment(profile))
            if result.returncode != 0:
                raise RuntimeError(
                    f'{profile}: exit {result.returncode}\n{result.stderr[-3000:]}')
            try:
                return json.loads(result.stdout)
            except json.JSONDecodeError as error:
                raise RuntimeError(f'{profile}: invalid JSON report: {result.stdout[:2000]}') from error

        common = [
            '--mode', '1', '--house', '7', '--resident-id', '48', '--language', '1',
            '--single-resident-session', '--headless-sit-object', '128',
            '--headless-stand-tick', '512', '--headless-single-resident-go-here',
            '27', '26', '1', '1025']
        saved = run('resume', [*common, '--headless-ticks', '1188',
                               '--save-session-on-exit'])
        archive = saved.get('saved_session_path')
        if not archive:
            raise RuntimeError('active route save did not report an archive path')
        archive = Path(archive).resolve(strict=True)
        if not archive.is_relative_to(root / 'resume' / 'data'):
            raise RuntimeError(f'save escaped the temporary profile: {archive}')
        first_route = saved.get('route_runtime') or {}
        if (saved.get('ticks') != 1188 or not first_route.get('active') or
                first_route.get('movement_callbacks', 0) <= 0):
            raise RuntimeError('tick 1188 did not capture a live, moving route')

        resumed = run('resume', [
            '--load-session', str(archive), '--headless-ticks', '14',
            '--single-resident-session'])
        route = resumed.get('route_runtime') or {}
        go_here = resumed.get('go_here_runtime') or {}
        if (not resumed.get('session_loaded') or resumed.get('resumed_tick') != 1188 or
                resumed.get('ticks') != 1202):
            raise RuntimeError('fresh process did not resume at tick 1188 and run 14 ticks')
        if (route.get('active') or route.get('movement_callbacks') != 14 or
                route.get('end_position', [])[:3] != [440, 424, 1]):
            raise RuntimeError(f'restored route did not finish at the requested tile: {route}')
        if (go_here.get('route_status') != 'arrived' or go_here.get('result') is not True or
                go_here.get('queue_active') or go_here.get('queue_size') != 0):
            raise RuntimeError(f'Go Here did not complete after restore: {go_here}')

        direct = run('direct', [*common, '--headless-ticks', '1190',
                                '--save-session-on-exit'])
        direct_route = direct.get('route_runtime') or {}
        resumed_positions = route.get('movement_positions') or []
        direct_positions = direct_route.get('movement_positions') or []
        if (not direct_route.get('active') or len(resumed_positions) < 2 or
                direct_positions[-2:] != resumed_positions[:2]):
            raise RuntimeError('the first two resumed steps differ from continuous execution')

    after = {path: digest(path) for path in protected}
    if before != after:
        changed = [str(path) for path in protected if before[path] != after[path]]
        raise RuntimeError(f'commercial game inputs changed: {changed}')
    print(json.dumps({
        'result': 'passed', 'save_tick': 1188, 'resume_end_tick': 1202,
        'movement_steps_after_resume': route['movement_callbacks'],
        'same_first_two_steps_as_continuous_run': True,
        'destination': route['end_position'], 'go_here': go_here['route_status'],
        'protected_game_files': len(protected), 'game_files_unchanged': True,
    }, separators=(',', ':')))


if __name__ == '__main__':
    main()
