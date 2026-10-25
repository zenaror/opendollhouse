#!/usr/bin/env python3
"""Test the real window path: click House07 ground, press Space, walk there.

Requires Xvfb and xdotool. Synthetic X11 input is sent only to this isolated
test window; original game files are read-only inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time


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
    parser.add_argument('--timeout', type=float, default=40)
    parser.add_argument('--save-resume', action='store_true',
                        help='press S during the walk and resume the whole household in a new process')
    parser.add_argument('--select-additional', action='store_true',
                        help='select the second Sim through the window before ordering Go Here')
    parser.add_argument('--report-movements', action='store_true',
                        help='include the bounded movement trace for a continuous-run comparison')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    binary = args.binary.resolve(strict=True)
    game = args.game_root.resolve(strict=True)
    protected = [game / name for name in (
        'UserData/Houses/House07.iff', 'UserData/Neighborhood.iff',
        'UserData/Characters/User00000.iff', 'GameData/Animation/Animation.far',
        'GameData/Global/Global.far', 'GameData/Objects/Objects.far')]
    if any(not path.is_file() for path in protected):
        parser.error('game-root is missing one or more required original game files')
    before = {path: digest(path) for path in protected}
    actor_id = 137 if args.select_additional else 105

    def actor_report(report):
        if not args.select_additional:
            return report.get('go_here_runtime') or {}, report.get('route_runtime') or {}
        members = report.get('household_main', {}).get('additional_residents', [])
        member = next((item for item in members if item['entity_id'] == actor_id), None)
        if member is None:
            raise RuntimeError('the selected additional resident is missing from the household report')
        go = dict(member.get('go_here') or {})
        target = go.get('target_position')
        go['tile'] = [target[0] // 16, target[1] // 16] if target else None
        route = dict(member.get('routing') or {})
        route['end_position'] = member.get('position')
        return go, route

    def require_primary_still(report):
        if not args.select_additional:
            return
        route = report.get('route_runtime') or {}
        if (route.get('movement_callbacks') != 0 or
                route.get('start_position', [])[:3] != route.get('end_position', [])[:3]):
            raise RuntimeError('the primary Sim moved while the selected additional resident walked')

    with tempfile.TemporaryDirectory(prefix='odh-house07-window-go-here-') as temporary:
        root = Path(temporary)
        xvfb = subprocess.Popen(
            ['Xvfb', '-displayfd', '1', '-screen', '0', '800x600x24', '-nolisten', 'tcp'],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        game_process = None
        try:
            ready, _, _ = select.select([xvfb.stdout], [], [], 10)
            if not ready:
                raise RuntimeError('Xvfb did not provide an isolated display number')
            display_number = xvfb.stdout.readline().strip()
            xvfb.stdout.close()
            if not display_number.isdigit():
                raise RuntimeError('Xvfb returned an invalid display number')
            env = os.environ.copy()
            env['DISPLAY'] = f':{display_number}'
            env.pop('WAYLAND_DISPLAY', None)
            env.pop('ODH_STATE_DIR', None)
            for key, folder in (('XDG_CONFIG_HOME', 'config'),
                                ('XDG_DATA_HOME', 'data'),
                                ('XDG_STATE_HOME', 'state')):
                env[key] = str(root / folder)
            game_process = subprocess.Popen(
                [str(binary), '--game-root', str(game), '--mode', '1', '--house', '7',
                 '--resident-id', '48', '--language', '1'],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)

            def xdotool(*arguments, check=True):
                result = subprocess.run(['xdotool', *arguments], env=env,
                                        capture_output=True, text=True, timeout=5)
                if check and result.returncode:
                    raise RuntimeError(f'xdotool {arguments!r} failed: {result.stderr.strip()}')
                return result

            window = None
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                result = xdotool('search', '--onlyvisible', '--name', 'OpenDollhouse', check=False)
                if result.returncode == 0 and result.stdout.strip():
                    window = result.stdout.splitlines()[0]
                    break
                if game_process.poll() is not None:
                    raise RuntimeError('game exited before opening its window')
                time.sleep(0.2)
            if window is None:
                raise RuntimeError('game window did not appear')

            log_path = root / 'state/opendollhouse/logs/odh-person.log'

            def wait_log(message, actor=None):
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    contents = log_path.read_text(errors='replace') if log_path.exists() else ''
                    if any(message in line and (actor is None or f" actor={actor}" in line)
                           for line in contents.splitlines()):
                        return contents
                    if 'experimental session save failed' in contents:
                        raise RuntimeError(f'window save failed: {contents[-3000:]}')
                    if game_process.poll() is not None:
                        raise RuntimeError(f'game exited waiting for {message}')
                    time.sleep(0.02)
                raise RuntimeError(f'timed out waiting for log event: {message}')

            wait_log('lot frame presented tick=')
            xdotool('windowfocus', '--sync', window)
            if args.select_additional:
                xdotool('mousemove', '--window', window, '30', '588', 'click', '1')
                time.sleep(0.5)
                xdotool('mousemove', '--window', window, '300', '58', 'click', '1')
                wait_log('UI active Sim changed to 137')

            # This is a known walkable ground pixel in the initial House07
            # camera. The resulting report identifies the selected tile.
            xdotool('mousemove', '--window', window, '435', '328', 'click', '1')
            time.sleep(0.5)
            xdotool('windowfocus', '--sync', window, 'key', '--clearmodifiers', 'space')
            if args.save_resume:
                wait_log('Go Here walking tick=', actor_id)
                xdotool('key', '--clearmodifiers', 's')
                wait_log('experimental session save written tick=')
            else:
                wait_log(f'Go Here completed actor={actor_id} ')
            if game_process.poll() is None:
                xdotool('key', '--clearmodifiers', 'Escape')
            stdout, stderr = game_process.communicate(timeout=30)
            if game_process.returncode != 0:
                raise RuntimeError(f'game exited {game_process.returncode}: {stderr[-3000:]}')
            try:
                report = json.loads(stdout)
            except json.JSONDecodeError as error:
                raise RuntimeError(f'game did not return a JSON report: {stdout[:2000]}') from error
            save_tick = None
            window_report = report
            full_path_match = None
            if report.get('host_failed') or actor_report(report)[0].get('trigger') != 'window_input':
                raise RuntimeError('initial Go Here did not come from the live window input path')
            if args.save_resume:
                archives = list((root / 'data/opendollhouse/saves').glob('*.odhs'))
                if len(archives) != 1:
                    raise RuntimeError('window S did not produce exactly one temporary save')
                archive = archives[0].resolve(strict=True)
                if not archive.is_relative_to(root / 'data'):
                    raise RuntimeError('window save escaped the temporary profile')

                def resume(ticks, save_on_exit=False):
                    save_options = ['--save-session-on-exit'] if save_on_exit else []
                    result = subprocess.run(
                        [str(binary), '--game-root', str(game), '--load-session', str(archive),
                         '--headless-ticks', str(ticks), *save_options], env=env, capture_output=True,
                        text=True, timeout=args.timeout)
                    if result.returncode:
                        raise RuntimeError(f'household resume failed: {result.stderr[-3000:]}')
                    return json.loads(result.stdout)

                original_archive = archive.read_bytes()
                probe = resume(0, save_on_exit=True)
                if archive.read_bytes() != original_archive:
                    raise RuntimeError('zero-tick load and save changed the active household archive')
                probe_go, probe_route = actor_report(probe)
                if not probe.get('session_loaded') or not probe_route.get('active'):
                    raise RuntimeError('window save did not preserve an active local route')
                save_tick = probe['resumed_tick']
                start = probe_route['end_position']
                tile = probe_go['tile']
                baseline_command = [sys.executable, str(Path(__file__).resolve()), '--binary', str(binary),
                                    '--game-root', str(game), '--timeout', str(args.timeout), '--report-movements']
                if args.select_additional:
                    baseline_command.append('--select-additional')
                baseline_process = subprocess.run(baseline_command, capture_output=True, text=True,
                                                  timeout=2 * args.timeout + 30)
                if baseline_process.returncode:
                    raise RuntimeError(f'continuous window run failed: {baseline_process.stderr[-3000:]}')
                baseline = json.loads(baseline_process.stdout)
                continuous_positions = baseline['movement_positions']
                saved_indices = [i for i, position in enumerate(continuous_positions) if position == start]
                if len(saved_indices) != 1 or baseline['selected_tile'] != tile:
                    raise RuntimeError('saved route position is not unique in the continuous trajectory')
                prefix_count = saved_indices[0] + 1
                remaining = len(continuous_positions) - prefix_count
                prefix = actor_report(window_report)[1].get('movement_positions', [])[:prefix_count]
                if remaining <= 0 or len(prefix) != prefix_count or prefix[-1] != start:
                    raise RuntimeError('S was not captured after movement and before arrival')
                report = resume(remaining)
                if (not report.get('session_loaded') or report.get('resumed_tick') != save_tick or
                        report.get('ticks') != save_tick + remaining or
                        actor_report(report)[1].get('movement_callbacks') != remaining or
                        len(report.get('household_main', {}).get('additional_residents', [])) != 1):
                    raise RuntimeError('resume lost the saved tick or the second household resident')
            require_primary_still(window_report)
            require_primary_still(report)
            go_here, route = actor_report(report)
            tile = go_here.get('tile')
            if (report.get('host_failed') or
                    go_here.get('trigger') != ('session_restore' if args.save_resume and args.select_additional else 'window_input') or
                    go_here.get('route_status') != 'arrived' or go_here.get('result') is not True or
                    go_here.get('queue_active') or go_here.get('queue_size') != 0):
                raise RuntimeError(f'window Go Here did not complete: {go_here}; route={route}')
            expected_xy = [tile[0] * 16 + 8, tile[1] * 16 + 8] if tile else None
            if (not route.get('movement_callbacks') or route.get('active') or not expected_xy or
                    route.get('end_position', [])[:2] != expected_xy):
                raise RuntimeError(f'route did not end at the selected ground tile: {route}; tile={tile}')
            if args.save_resume:
                if prefix + route['movement_positions'] != continuous_positions:
                    raise RuntimeError('complete resumed window walk differs from continuous execution')
                full_path_match = True
            result = {
                'result': 'passed', 'input': 'X11 ground click then Space', 'actor_id': actor_id,
                'window': 'Xvfb 800x600, closed after the run', 'selected_tile': tile,
                'go_here': go_here['route_status'], 'movement_steps': route['movement_callbacks'],
                'queue_empty': True, 'destination': route['end_position'],
                'saved_while_walking': args.save_resume, 'saved_tick': save_tick,
                'zero_tick_resave_unchanged': True if args.save_resume else None,
                'continuous_steps': len(continuous_positions) if args.save_resume else len(route['movement_positions']),
                'saved_prefix_steps': prefix_count if args.save_resume else None,
                'full_continuous_path_match': full_path_match,
                'primary_remained_in_place': True if args.select_additional else None,
            }
            if args.report_movements:
                result['movement_positions'] = route['movement_positions']
        finally:
            if game_process is not None and game_process.poll() is None:
                game_process.terminate()
                try:
                    game_process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    game_process.kill()
                    game_process.communicate()
            if xvfb.poll() is None:
                xvfb.terminate()
                try:
                    xvfb.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    xvfb.kill()
                    xvfb.wait()

    after = {path: digest(path) for path in protected}
    if before != after:
        changed = [str(path) for path in protected if before[path] != after[path]]
        raise RuntimeError(f'commercial game inputs changed: {changed}')
    result['protected_game_files'] = len(protected)
    result['game_files_unchanged'] = True
    print(json.dumps(result, separators=(',', ':')))


if __name__ == '__main__':
    main()
