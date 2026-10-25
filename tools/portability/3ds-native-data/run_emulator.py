#!/usr/bin/env python3
"""Observe the synthetic 3DS probe in an isolated Xvfb/RetroArch profile.

Requires an existing RetroArch Flatpak and an explicitly supplied libretro core.
Does not install software, touch physical input, or prove hardware compatibility.
A screenshot and exact debug marker are evidence; liveness alone is not success.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stop(process):
    if process is not None and process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=8)


def snapshot(directory):
    return {str(path.relative_to(directory)): digest(path)
            for path in directory.rglob('*') if path.is_file() and not path.is_symlink()}


def find_owned(profile, application):
    listing = subprocess.run(['flatpak', 'ps', '--columns=instance,application,pid,child-pid'],
                             capture_output=True, text=True, check=True).stdout
    owned = []
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) != 4 or fields[1] != application:
            continue
        pids = [int(value) for value in fields[2:] if value.isdigit()]
        for pid in pids:
            try:
                command = (Path('/proc') / str(pid) / 'cmdline').read_bytes()
            except FileNotFoundError:
                continue
            if str(profile).encode() in command:
                owned.append({'instance': fields[0], 'pids': pids})
                break
    return owned


def stop_instances(instances, launcher):
    for instance in instances:
        subprocess.run(['flatpak', 'kill', instance['instance']], capture_output=True)
    # Reap our wrapper before checking /proc: an unreaped zombie is terminal.
    stop(launcher)
    deadline = time.monotonic() + 8
    def live(pid):
        try:
            state = (Path('/proc') / str(pid) / 'stat').read_text().rsplit(')', 1)[1].split()[0]
            return state not in ('Z', 'X')
        except FileNotFoundError:
            return False
    while time.monotonic() < deadline:
        if all(not live(pid) for instance in instances for pid in instance['pids']):
            return True
        time.sleep(.05)
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=25)
    parser.add_argument('--flatpak-app', default='org.libretro.RetroArch')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error('--seconds must be 1..60')
    core, probe = args.core.resolve(strict=True), args.probe.resolve(strict=True)
    out = args.report_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    result = {'runner_sha256': digest(Path(__file__)),
              'core': str(core), 'core_sha256': digest(core),
              'probe': str(probe), 'probe_sha256': digest(probe),
              'hardware_verified': False, 'game_verified': False,
              'emulated_component_verified': False}
    game = xvfb = None
    profile = None
    instances = []
    personal_config = Path.home() / '.var/app' / args.flatpak_app / 'config'
    result['personal_config_before'] = snapshot(personal_config)
    try:
        with tempfile.TemporaryDirectory(prefix='profile-', dir=out) as temporary:
            profile = Path(temporary)
            config = profile / 'retroarch.cfg'
            settings = {
                'video_driver': 'glcore', 'video_fullscreen': 'false',
                'video_window_width': '1000', 'video_window_height': '800',
                'video_window_auto_width_max': '1000', 'video_window_auto_height_max': '800',
                'video_scale_integer': 'false', 'video_vsync': 'true',
                'audio_enable': 'false', 'audio_driver': 'null',
                'input_joypad_driver': 'null', 'input_autodetect_enable': 'false',
                'config_save_on_exit': 'false', 'log_verbosity': 'true',
                'frontend_log_level': '0', 'libretro_log_level': '0',
                'network_cmd_enable': 'false', 'network_remote_enable': 'false',
                'core_options_path': str(profile / 'core-options.cfg'),
                'global_core_options': 'true', 'core_info_cache_enable': 'false',
            }
            for key, suffix in (('system_directory', 'system'),
                                ('savefile_directory', 'saves'),
                                ('savestate_directory', 'states'),
                                ('cache_directory', 'cache'),
                                ('screenshot_directory', 'screenshots'),
                                ('rgui_config_directory', 'overrides'),
                                ('libretro_directory', 'cores'),
                                ('libretro_info_path', 'core-info')):
                path = profile / suffix
                path.mkdir()
                settings[key] = str(path)
            config.write_text(''.join('%s = "%s"\n' % item for item in settings.items()))
            (profile / 'core-options.cfg').write_text('citra_is_new_3ds = "enabled"\n')
            result['config'] = config.read_text()
            xvfb = subprocess.Popen(['Xvfb', '-displayfd', '1', '-screen', '0',
                                     '1280x960x24', '-nolisten', 'tcp'],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                    text=True, start_new_session=True)
            try:
                if not select.select([xvfb.stdout], [], [], 10)[0]:
                    raise RuntimeError('Xvfb startup timeout')
                number = xvfb.stdout.readline().strip()
                if not number.isdigit():
                    raise RuntimeError('invalid Xvfb number')
                xvfb.stdout.close()
                display = ':' + number
                result['display'] = display
                result['xvfb_pid'] = xvfb.pid
                env = os.environ.copy()
                env.update(DISPLAY=display)
                env.pop('WAYLAND_DISPLAY', None)
                argv = ['flatpak', 'run', '--nofilesystem=host', '--nofilesystem=home',
                        '--filesystem=' + str(out) + ':rw',
                        '--filesystem=' + str(personal_config) + ':ro',
                        '--filesystem=' + str(core.parent) + ':ro',
                        '--filesystem=' + str(probe.parent) + ':ro', '--socket=x11',
                        '--env=DISPLAY=' + display, '--env=LIBGL_ALWAYS_SOFTWARE=1',
                        '--env=SDL_AUDIODRIVER=dummy', '--env=ALSOFT_DRIVERS=null',
                        '--command=retroarch', args.flatpak_app, '--verbose',
                        '--config=' + str(config), '--sram-mode=noload-nosave',
                        '--nodevice=1', '--libretro=' + str(core), str(probe)]
                result['argv'] = argv
                result['profile'] = str(profile)
                def screenshot(name):
                    pixels = subprocess.run(['xwd', '-display', display, '-root', '-silent'],
                                            check=True, capture_output=True, timeout=10).stdout
                    subprocess.run(['convert', 'xwd:-', str(out / name)], input=pixels,
                                   check=True, timeout=10)
                    return digest(out / name)
                with (out / 'emulator.log').open('w') as log:
                    game = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                            env=env, start_new_session=True)
                    result['launcher_pid'] = game.pid
                    start = time.monotonic()
                    while game.poll() is None and time.monotonic() - start < args.seconds:
                        if not instances:
                            instances = find_owned(profile, args.flatpak_app)
                        time.sleep(.2)
                    result['alive_at_deadline'] = game.poll() is None
                    result['exit_before_stop'] = game.poll()
                    result['screenshot_sha256'] = screenshot('emulator.png')
            finally:
                result['owned_instances'] = instances
                result['owned_instances_stopped'] = stop_instances(instances, game) if instances else False
                stop(game)
                stop(xvfb)
        # Read and hash only after the actual sandbox processes have ended.
        log = (out / 'emulator.log').read_text(errors='replace')
        result['debug_pass_marker'] = 'ODH_3DS_NATIVE_DATA_V1:PASS' in log
        result['debug_fail_marker'] = 'ODH_3DS_NATIVE_DATA_V1:FAIL' in log
        result['log_sha256'] = digest(out / 'emulator.log')
    finally:
        stop(game)
        stop(xvfb)
        result['personal_config_after'] = snapshot(personal_config)
        result['personal_config_unchanged'] = result['personal_config_before'] == result['personal_config_after']
        result['profile_removed'] = profile is not None and not profile.exists()
        result['launcher_stopped'] = game is None or game.poll() is not None
        result['xvfb_stopped'] = xvfb is None or xvfb.poll() is not None
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result.get(k) for k in (
        'alive_at_deadline', 'debug_pass_marker', 'debug_fail_marker',
        'profile_removed', 'launcher_stopped', 'xvfb_stopped')}, indent=2))
    return 0 if (result['debug_pass_marker'] and not result['debug_fail_marker']
                 and result['personal_config_unchanged']
                 and result.get('owned_instances_stopped')
                 and result['profile_removed'] and result['launcher_stopped']
                 and result['xvfb_stopped']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
