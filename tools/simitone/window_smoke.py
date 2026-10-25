#!/usr/bin/env python3
"""Observe a published Simitone in isolated Xvfb with copied game data.

This is a startup observation, not proof of gameplay. Inspect the screenshot
and log; a live process may be displaying an error dialog.
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


PROTECTED = (
    'UserData/Houses/House07.iff', 'UserData/Neighborhood.iff',
    'UserData/Characters/User00000.iff', 'GameData/Animation/Animation.far',
    'GameData/Global/Global.far', 'GameData/Objects/Objects.far',
)

NATIVE_LIBRARIES = ('libodh_data.so', 'libodh_simulation.so')


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def fingerprint(game):
    return {name: digest(game / name) for name in PROTECTED}


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=8)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', type=Path, required=True)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--vm-diagnostics', action='store_true',
                        help='record complete VM exception traces in the temporary profile log')
    parser.add_argument('--require-clean-vm', action='store_true',
                        help='fail on VM/fatal errors in the log; screenshots still require inspection')
    parser.add_argument('--click', action='append', default=[], metavar='SECOND:X:Y',
                        help='send a scheduled click only inside the isolated display')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error('--seconds must be between 1 and 60')
    if args.require_clean_vm and not args.vm_diagnostics:
        parser.error('--require-clean-vm requires --vm-diagnostics')
    clicks = []
    for spec in args.click:
        try:
            second, x, y = spec.split(':')
            second, x, y = float(second), int(x), int(y)
            if not (0 <= second < args.seconds and 0 <= x < 1024 and 0 <= y < 768):
                raise ValueError()
            clicks.append((second, x, y))
        except ValueError:
            parser.error('--click requires SECOND:X:Y within the observation and screen')
    clicks.sort()
    game = args.game_root.resolve(strict=True)
    binary = (args.runtime_dir / 'Simitone').resolve(strict=True)
    out = args.report_dir.resolve()
    before = fingerprint(game)
    out.mkdir(parents=True, exist_ok=False)
    result = {'protected_before': before, 'audio': 'disabled',
              'binary_sha256': digest(binary), 'actions': [],
              'managed_sha256': {name: digest(binary.parent / name) for name in (
                  'Simitone.dll', 'Simitone.Client.dll', 'OpenDollhouse.Simulation.dll', 'OpenDollhouse.Data.Interop.dll', 'FSO.SimAntics.dll',
                  'FSO.HIT.dll', 'FSO.Content.dll', 'FSO.Files.dll',
                  'Simitone.runtimeconfig.json')},
              'native_sha256': {name: digest(binary.parent / name)
                                for name in NATIVE_LIBRARIES},
              'runtime': str(binary), 'gameplay_verified': False}
    try:
        with tempfile.TemporaryDirectory(prefix='odh-simitone-window-') as temporary:
            root = Path(temporary)
            data = root / 'The Sims'
            shutil.copytree(game, data, ignore=shutil.ignore_patterns(
                '*.exe', '*.EXE', '*.bak', '*.iso', '*.ISO'))
            xvfb = subprocess.Popen(
                ['Xvfb', '-displayfd', '1', '-screen', '0', '1024x768x24',
                 '-nolisten', 'tcp'], stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL, text=True)
            process = None
            try:
                if not select.select([xvfb.stdout], [], [], 10)[0]:
                    raise RuntimeError('Xvfb startup timeout')
                number = xvfb.stdout.readline().strip()
                if not number.isdigit():
                    raise RuntimeError('Invalid Xvfb display number')
                xvfb.stdout.close()
                display = ':' + number
                env = os.environ.copy()
                env.update(DISPLAY=display, ODH_USER_DIR=str(root / 'profile'),
                           XDG_DATA_HOME=str(root / 'data'),
                           XDG_CONFIG_HOME=str(root / 'config'),
                           XDG_STATE_HOME=str(root / 'state'),
                           SDL_AUDIODRIVER='dummy', ALSOFT_DRIVERS='null',
                           LIBGL_ALWAYS_SOFTWARE='1')
                env.pop('WAYLAND_DISPLAY', None)
                # Do not inherit an unrelated diagnostic setting into this run.
                env.pop('ODH_VM_DIAGNOSTICS', None)
                if args.vm_diagnostics:
                    env['ODH_VM_DIAGNOSTICS'] = '1'
                argv = [str(binary), '-path' + str(data) + '/', '-nosound']
                result['argv'] = argv
                result['vm_diagnostics'] = args.vm_diagnostics
                result['environment'] = {key: env[key] for key in (
                    'DISPLAY', 'ODH_USER_DIR', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME',
                    'XDG_STATE_HOME', 'SDL_AUDIODRIVER', 'ALSOFT_DRIVERS',
                    'LIBGL_ALWAYS_SOFTWARE')}
                def screenshot(name):
                    shot = subprocess.run(
                        ['xwd', '-display', display, '-root', '-silent'],
                        capture_output=True, check=True, timeout=10)
                    subprocess.run(['convert', 'xwd:-', str(out / name)],
                                   input=shot.stdout, check=True, timeout=10)
                with (out / 'startup.log').open('w') as log:
                    process = subprocess.Popen(
                        argv,
                        stdout=log, stderr=subprocess.STDOUT, env=env)
                    started = time.monotonic()
                    deadline = started + args.seconds
                    while process.poll() is None and time.monotonic() < deadline:
                        if clicks and time.monotonic() - started >= clicks[0][0]:
                            second, x, y = clicks.pop(0)
                            screenshot('before-click-%d.png' % len(result['actions']))
                            subprocess.run(['xdotool', 'mousemove', '--sync',
                                            str(x), str(y), 'sleep', '0.3',
                                            'mousedown', '1',
                                            'sleep', '0.15', 'mouseup', '1'],
                                           env=env, check=True, timeout=5)
                            result['actions'].append({'second': second, 'x': x, 'y': y})
                        time.sleep(.2)
                    result['process_alive'] = process.poll() is None
                    result['exit_before_stop'] = process.poll()
                    result['windows'] = subprocess.run(
                        ['xwininfo', '-display', display, '-root', '-tree'],
                        capture_output=True, text=True, check=True, timeout=5).stdout
                    if result['process_alive']:
                        status = Path('/proc') / str(process.pid) / 'status'
                        if status.exists():
                            result['memory_status'] = [line for line in
                                status.read_text().splitlines()
                                if line.startswith(('VmRSS:', 'VmHWM:'))]
                    screenshot('startup.png')
                    result['screenshot_sha256'] = digest(out / 'startup.png')
                    result['profile_isolated'] = (
                        'User data directory: ' + str(root / 'profile')
                        in (out / 'startup.log').read_text())
                    lines = (out / 'startup.log').read_text().splitlines()
                    result['vm_error_count'] = sum('SimAntics exception:' in line for line in lines)
                    result['fatal_error_count'] = sum(
                        'Unhandled exception.' in line or '=== FATAL ERROR ===' in line for line in lines)
                    result['native_recoveries'] = [json.loads(line.split(': ', 1)[1])
                        for line in lines if line.startswith('Native object recovery: ')]
                    result['native_person_recoveries'] = [json.loads(line.split(': ', 1)[1])
                        for line in lines if line.startswith('Native person recovery: ')]
                    result['avatar_samples'] = [json.loads(line.split(': ', 1)[1])
                        for line in lines if line.startswith('TS1 avatar state: ')]
                    result['compatibility_summaries'] = [record for line in lines
                        if line.startswith('Saved code compatibility: ')
                        for record in [json.loads(line.split(': ', 1)[1])]
                        if record.get('Kind') == 'summary']
            finally:
                stop(process)
                stop(xvfb)
    finally:
        result['protected_after'] = fingerprint(game)
        result['protected_files_unchanged'] = before == result['protected_after']
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items()
                      if key not in ('protected_before', 'protected_after',
                                     'managed_sha256', 'windows')}, indent=2))
    clean = not args.require_clean_vm or (
        result.get('vm_error_count') == 0 and result.get('fatal_error_count') == 0)
    return 0 if (result.get('process_alive') and result.get('profile_isolated')
                 and result['protected_files_unchanged'] and clean) else 1


if __name__ == '__main__':
    raise SystemExit(main())
