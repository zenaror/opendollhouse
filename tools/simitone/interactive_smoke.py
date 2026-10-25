#!/usr/bin/env python3
"""Drive a bounded, isolated Simitone UI session using JSON lines on stdin.

Commands: capture, click(x,y), key(value), text(value), wait(seconds), restart,
stop_game, exit. Multiple complete JSON lines may be sent together.
Only the private Xvfb receives input. Restart retains the same copied profile.
This tool records observations; it does not declare gameplay correct.
"""
import argparse
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time

from session_smoke import MANAGED, capture, log_facts, save_inventory
from window_smoke import NATIVE_LIBRARIES, digest, fingerprint, stop


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', type=Path, required=True)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--max-seconds', type=int, default=1200)
    args = parser.parse_args()
    if not 1 <= args.max_seconds <= 1800:
        parser.error('--max-seconds must be 1..1800')
    game = args.game_root.resolve(strict=True)
    binary = (args.runtime_dir / 'Simitone').resolve(strict=True)
    out = args.report_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    result = {'schema': 1, 'kind': 'isolated interactive UI observation',
              'runner_sha256': digest(Path(__file__)), 'gameplay_verified': False,
              'protected_before': fingerprint(game),
              'managed_sha256': {n: digest(binary.parent / n) for n in MANAGED},
              'native_sha256': {n: digest(binary.parent / n) for n in NATIVE_LIBRARIES},
              'actions': [], 'processes': []}
    xvfb = process = log = None
    root = None
    display = None

    def emit(value):
        print(json.dumps(value), flush=True)

    def finish_process():
        nonlocal process, log
        if process is None:
            return
        phase = result['processes'][-1]
        maps = Path('/proc') / str(process.pid) / 'maps'
        try:
            phase['native_library_mappings'] = ([row for row in maps.read_text().splitlines()
                if any(n in row for n in NATIVE_LIBRARIES)] if maps.exists() else [])
        except OSError as error:
            phase['mapping_error'] = str(error)
        finally:
            phase['exit_before_stop'] = process.poll()
            stop(process)
            phase['exit_after_stop'] = process.returncode
            phase['pid_absent'] = not Path('/proc', str(process.pid)).exists()
            log.close()
            process = log = None
        facts = log_facts(out / phase['log'], root / 'profile')
        phase['log_facts'] = {k: v for k, v in facts.items() if k not in (
            'avatar_samples', 'save_states', 'native_recoveries', 'native_person_recoveries')}
        phase['save_state_count'] = len(facts['save_states'])
        phase['user_data'] = save_inventory(root / 'profile')
        phase['log_sha256'] = digest(out / phase['log'])

    def launch():
        nonlocal process, log
        phase = {'index': len(result['processes']), 'log': 'process-%d.log' % len(result['processes'])}
        log = (out / phase['log']).open('w')
        process = subprocess.Popen([str(binary), '-path' + str(root / 'The Sims') + '/', '-nosound'],
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        phase['pid'] = process.pid
        result['processes'].append(phase)
        emit({'event': 'launched', 'pid': process.pid, 'phase': phase['index'],
              'profile': str(root / 'profile'), 'display': display})

    try:
        with tempfile.TemporaryDirectory(prefix='odh-family-session-') as temporary:
            root = Path(temporary)
            result['temporary_root'] = str(root)
            shutil.copytree(game, root / 'The Sims', ignore=shutil.ignore_patterns(
                '*.exe', '*.EXE', '*.bak', '*.iso', '*.ISO'))
            xvfb = subprocess.Popen(['Xvfb', '-displayfd', '1', '-screen', '0',
                                      '1024x768x24', '-nolisten', 'tcp'],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            try:
                if not select.select([xvfb.stdout], [], [], 10)[0]:
                    raise RuntimeError('Xvfb startup timeout')
                number = xvfb.stdout.readline().strip()
                xvfb.stdout.close()
                if not number.isdigit():
                    raise RuntimeError('Invalid Xvfb display')
                display = ':' + number
                result['display'] = display
                result['xvfb_pid'] = xvfb.pid
                env = os.environ.copy()
                env.update(DISPLAY=display, ODH_USER_DIR=str(root / 'profile'),
                           XDG_DATA_HOME=str(root / 'data'), XDG_CONFIG_HOME=str(root / 'config'),
                           XDG_STATE_HOME=str(root / 'state'), SDL_AUDIODRIVER='dummy',
                           ALSOFT_DRIVERS='null', LIBGL_ALWAYS_SOFTWARE='1', ODH_VM_DIAGNOSTICS='1')
                env.pop('WAYLAND_DISPLAY', None)
                launch()
                deadline = time.monotonic() + args.max_seconds
                pending = b''
                while time.monotonic() < deadline:
                    if b'\n' not in pending:
                        if not select.select([sys.stdin], [], [], min(1, max(0, deadline-time.monotonic())))[0]:
                            continue
                        incoming = os.read(sys.stdin.fileno(), 4096)
                        if not incoming:
                            result['end_reason'] = 'stdin_closed'
                            break
                        pending += incoming
                        if len(pending) > 65536:
                            raise ValueError('command input exceeds buffer limit')
                        if b'\n' not in pending:
                            continue
                    line, pending = pending.split(b'\n', 1)
                    request = json.loads(line)
                    command = request.get('command')
                    if command in ('click', 'key', 'text') and (process is None or process.poll() is not None):
                        emit({'event': 'rejected', 'reason': 'game is not running', 'request': request})
                        continue
                    action = {'index': len(result['actions']), 'phase': len(result['processes'])-1,
                              'request': request, 'at_monotonic': time.monotonic()}
                    result['actions'].append(action)
                    if command == 'exit':
                        result['end_reason'] = 'requested_exit'
                        break
                    if command == 'restart':
                        finish_process()
                        launch()
                    elif command == 'stop_game':
                        finish_process()
                    elif command == 'capture':
                        name = 'capture-%03d.png' % action['index']
                        action['capture'] = name
                        action['sha256'] = capture(display, out / name)
                    elif command == 'wait':
                        seconds = request['seconds']
                        if not isinstance(seconds, (int, float)) or not 0 <= seconds <= 10:
                            raise ValueError('wait must be 0..10 seconds')
                        time.sleep(min(seconds, max(0, deadline-time.monotonic())))
                    elif command == 'click':
                        x, y = request['x'], request['y']
                        if type(x) is not int or type(y) is not int or not (0 <= x < 1024 and 0 <= y < 768):
                            raise ValueError('click outside private display')
                        # --sync waits for motion and can hang when already at x,y.
                        subprocess.run(['xdotool', 'mousemove', str(x), str(y),
                            'sleep', '.2', 'mousedown', '1', 'sleep', '.15', 'mouseup', '1'],
                            env=env, check=True, timeout=5)
                    elif command in ('key', 'text'):
                        value = request['value']
                        if not isinstance(value, str) or not 1 <= len(value) <= 128 or any(c in value for c in '\r\n\x00'):
                            raise ValueError('invalid key/text payload')
                        if command == 'key':
                            argv = ['xdotool', 'key', '--clearmodifiers', value]
                        else:
                            argv = ['xdotool', 'type', '--clearmodifiers', '--delay', '35', '--', value]
                        subprocess.run(argv, env=env, check=True, timeout=10)
                    else:
                        raise ValueError('unknown command')
                    emit({'event': 'action', **action})
                    (out / 'actions.json').write_text(json.dumps(result['actions'], indent=2)+'\n')
                else:
                    result['end_reason'] = 'deadline'
            finally:
                finish_process()
                stop(xvfb)
    finally:
        stop(process)
        if log is not None:
            log.close()
        stop(xvfb)
        result['protected_after'] = fingerprint(game)
        result['protected_files_unchanged'] = result['protected_before'] == result['protected_after']
        result['temporary_root_removed'] = root is not None and not root.exists()
        result['xvfb_stopped'] = xvfb is None or xvfb.poll() is not None
        (out / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
        emit({'event': 'finished', 'originals_unchanged': result['protected_files_unchanged'],
              'temporary_root_removed': result['temporary_root_removed'], 'xvfb_stopped': result['xvfb_stopped']})
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
