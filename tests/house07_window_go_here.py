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

            xdotool('windowfocus', '--sync', window)
            # The lot renderer finishes a few seconds after the window title
            # starts showing the selected Sim.
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                title = xdotool('getwindowname', window, check=False).stdout
                if 'Sim 105' in title and 'Loading' not in title:
                    break
                if game_process.poll() is not None:
                    raise RuntimeError('game exited before the House07 lot loaded')
                time.sleep(0.2)
            else:
                raise RuntimeError(f'House07 lot did not finish loading: {title.strip()}')
            time.sleep(6)

            # This is a known walkable ground pixel in the initial House07
            # camera. The resulting report identifies the selected tile.
            xdotool('mousemove', '--window', window, '435', '328', 'click', '1')
            time.sleep(0.5)
            xdotool('windowfocus', '--sync', window, 'key', '--clearmodifiers', 'space')
            completion_deadline = time.monotonic() + args.timeout
            while time.monotonic() < completion_deadline:
                if game_process.poll() is not None:
                    break
                title = xdotool('getwindowname', window, check=False).stdout
                if ('Go Here arrived' in title or 'could not reach the selected ground' in title or
                        'Go Here cancelled by player' in title):
                    break
                time.sleep(0.1)
            if game_process.poll() is None:
                xdotool('key', '--clearmodifiers', 'Escape')
            stdout, stderr = game_process.communicate(timeout=30)
            if game_process.returncode != 0:
                raise RuntimeError(f'game exited {game_process.returncode}: {stderr[-3000:]}')
            try:
                report = json.loads(stdout)
            except json.JSONDecodeError as error:
                raise RuntimeError(f'game did not return a JSON report: {stdout[:2000]}') from error
            go_here = report.get('go_here_runtime') or {}
            route = report.get('route_runtime') or {}
            tile = go_here.get('tile')
            if (report.get('host_failed') or go_here.get('trigger') != 'window_input' or
                    go_here.get('route_status') != 'arrived' or go_here.get('result') is not True or
                    go_here.get('queue_active') or go_here.get('queue_size') != 0):
                raise RuntimeError(f'window Go Here did not complete: {go_here}; route={route}')
            expected_xy = [tile[0] * 16 + 8, tile[1] * 16 + 8] if tile else None
            if (not route.get('movement_callbacks') or route.get('active') or not expected_xy or
                    route.get('end_position', [])[:2] != expected_xy):
                raise RuntimeError(f'route did not end at the selected ground tile: {route}; tile={tile}')
            result = {
                'result': 'passed', 'input': 'X11 ground click then Space',
                'window': 'Xvfb 800x600, closed after the run', 'selected_tile': tile,
                'go_here': go_here['route_status'], 'movement_steps': route['movement_callbacks'],
                'queue_empty': True, 'destination': route['end_position'],
            }
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
