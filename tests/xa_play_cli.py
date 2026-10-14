"""Bounded XA-to-Linux queue integration, exclusively silent SDL dummy."""
import hashlib, json, os, struct, subprocess, sys, tempfile
from pathlib import Path

binary = sys.argv[1]
env = dict(os.environ, SDL_AUDIODRIVER='dummy')
def run(args, environment=env):
    return subprocess.run([binary, *args], env=environment, capture_output=True,
                          text=True, timeout=10)

with tempfile.TemporaryDirectory(prefix='odh-xa-play-') as folder:
    path = Path(folder) / 'fixture.xa'
    for channels, frames, rate in [(1, 3, 22050), (2, 70001, 192000), (1, 0, 8000)]:
        header = b'XAI\0' + struct.pack('<IHHIIHH', frames*channels*2, 1, channels,
                                       rate, rate*channels*2, channels*2, 16)
        # Predictor zero, zero samples; final declared frame trims block padding.
        data = header + bytes(((frames+27)//28)*15*channels) + b'tail'
        path.write_bytes(data)
        before = hashlib.sha256(data).digest()
        # A deliberately invalid driver proves inspection never opens output.
        inspected = run([str(path)], dict(env, SDL_AUDIODRIVER='odh-invalid'))
        assert inspected.returncode == 0, inspected.stderr
        report = json.loads(inspected.stdout)
        assert report['frames'] == frames and report['channels'] == channels
        assert report['remaining_bytes'] == 4 and report['pcm_bytes'] == frames*channels*2
        assert report['delivered_frames'] == 0 and report['driver'] == ''
        played = run([str(path), '--play'])
        assert played.returncode == 0, played.stderr
        result = json.loads(played.stdout)
        assert result == dict(report, delivered_frames=frames, driver='dummy')
        assert not result['audibility_verified']
        assert hashlib.sha256(path.read_bytes()).digest() == before
    failed_driver = run([str(path), '--play'], dict(env, SDL_AUDIODRIVER='odh-invalid'))
    assert failed_driver.returncode == 2 and failed_driver.stderr and not failed_driver.stdout
    path.write_bytes(b'bad')
    for args in [[str(path)], [str(path), '--play'], [], [str(path), '--bad']]:
        rejected = run(args)
        assert rejected.returncode == 2 and rejected.stderr and not rejected.stdout
    assert run(['--help']).returncode == 0
print('XA Linux dummy decode/queue/partial-prefix/input-preservation checks passed')
