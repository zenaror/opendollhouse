"""Managed occupancy checkpoints through the real native CLI, synthetic data only.

The initial sidecar is explicit test authority, not a claim that UI relocation
or portable household extraction is enabled. Installed fixture resources remain
unchanged throughout; each profile and archive belongs to TemporaryDirectory.
"""
import hashlib
import json
import os
import shutil
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from linux_household_session_resume_cli import GUIDS, archive_sections, rewrite_archive
from linux_lot_travel_cli import make_two_occupied_lots


def family_identity():
    # Exact family_overlay_identity byte sequence: chunk u16; FamilyNumber,
    # original house, budget, member count, and ordered GUIDs as little-endian u32.
    return hashlib.sha256(struct.pack('<HIIII2I', 201, 900, 7, 100, 2, *GUIDS)).digest()


def seed_managed_profile(root, env, binary=None):
    identity = hashlib.sha256((root / 'UserData/Neighborhood.iff').read_bytes()).digest()
    family = family_identity()

    def operation(action, destination, expected_house):
        first = struct.pack('<H32sHiiI', 201, family, destination, 0, 100, action)
        state = struct.pack('<HHQiii32s', expected_house, 0, 1, 100, 0, 0, bytes(32))
        payload = first + state
        assert len(payload) == 104
        return struct.pack('<HI', 5, len(payload)) + payload

    # Source occupancy epoch1 -> unhoused -> same house epoch2. No session
    # ticket exists, so the first launch must build a fresh managed session.
    encoded = b'ODHF' + struct.pack('<HH', 6, 0) + identity + struct.pack('<I', 2)
    encoded += operation(1, 0, 7) + operation(0, 7, 0)
    path = Path(env['XDG_CONFIG_HOME']) / 'opendollhouse/player-state' / ('neighborhood-' + identity.hex() + '.odhp')
    path.parent.mkdir(parents=True)
    if binary is not None:
        # Adopt the SAME original occupancy. Closing epoch1 and inventing a
        # fresh epoch around old OBJM actors would now require reconstruction.
        run = subprocess.run([binary, '--game-root', str(root), '--mode', '1', '--house', '7',
                              '--resident-id', '11', '--language', '1', '--headless-ticks', '0',
                              '--save-session-on-exit'], capture_output=True, text=True, env=env, timeout=90)
        assert run.returncode == 0, (run.stdout, run.stderr)
        record = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')][-1]
        source = Path(record['saved_session_path']).read_bytes()
        lease = struct.pack('<HHQ', 1, 201, 1) + identity + family
        archive = rewrite_archive(source, lambda sections: sections + [(b'FAML', 1, lease)])
        digest = hashlib.sha256(archive).digest()
        saved = path.parent / 'sessions' / identity.hex() / (digest.hex() + '.odhs')
        saved.parent.mkdir(parents=True)
        saved.write_bytes(archive)
        saved.chmod(0o600)
        payload = struct.pack('<H32sHiiI', 201, family, 7, 0, 100, 2)
        payload += struct.pack('<HHQiii32s', 7, 1, 1, 100, 0, 0, digest)
        encoded = b'ODHF' + struct.pack('<HH', 6, 0) + identity + struct.pack('<I', 1)
        encoded += struct.pack('<HI', 5, len(payload)) + payload
    path.write_bytes(encoded)
    return path, identity, family


def checkpoint_digest(overlay):
    assert overlay[:8] == b'ODHF\x06\0\0\0'
    count = struct.unpack_from('<I', overlay, 40)[0]
    position = 44
    digest = None
    for _ in range(count):
        tag, length = struct.unpack_from('<HI', overlay, position)
        position += 6
        payload = overlay[position:position + length]
        assert len(payload) == length
        if tag == 5 and struct.unpack_from('<I', payload, 44)[0] == 2:
            assert length == 104 and struct.unpack_from('<H', payload, 50)[0] == 1
            digest = payload[72:104]
        position += length
    assert position == len(overlay)
    return digest


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='odh-managed-family-cli-') as directory:
        base = Path(directory)
        root = base / 'game'
        make_two_occupied_lots(root)
        protected = {p.relative_to(root): hashlib.sha256(p.read_bytes()).digest()
                     for p in root.rglob('*') if p.is_file()}

        def profile(name):
            env = os.environ.copy()
            for variable in ('XDG_CONFIG_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME', 'ODH_STATE_DIR'):
                env[variable] = str(base / name / variable)
            for variable in ('DISPLAY', 'WAYLAND_DISPLAY'):
                env.pop(variable, None)
            return env

        def run(arguments, env, success=True):
            result = subprocess.run([binary, '--game-root', str(root)] + arguments,
                                    capture_output=True, text=True, env=env, timeout=90)
            visits = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            assert result.returncode == (0 if success else 2), (result.returncode, result.stdout, result.stderr)
            if success:
                assert visits and all(not visit['host_failed'] for visit in visits), visits
                assert all(visit['household_main']['scheduler_entities'] == 2 for visit in visits), visits
            return visits, result

        env = profile('continuation')
        overlay, identity, family = seed_managed_profile(root, env, binary)
        fresh = ['--mode', '1', '--house', '7', '--resident-id', '11', '--language', '1']
        first = run(fresh + ['--headless-ticks', '2', '--save-session-on-exit'], env)[0][0]
        assert first['session_loaded'] and first['resumed_tick'] == 0 and first['ticks'] == 2, first
        assert 'headless_checkpoints' not in first and 'headless_checkpoint_interval' not in first, first
        first_path = Path(first['saved_session_path'])
        first_bytes = first_path.read_bytes()
        digest = hashlib.sha256(first_bytes).digest()
        assert first_path.name == digest.hex() + '.odhs'
        assert first_path.parent == overlay.parent / 'sessions' / identity.hex()
        assert checkpoint_digest(overlay.read_bytes()) == digest
        sections = {ident: (flags, body) for ident, flags, body in archive_sections(first_bytes)}
        assert sections[b'FAML'] == (1, struct.pack('<HHQ', 1, 201, 1) + identity + family)
        assert b'HHRD' in sections and b'GEXP' in sections

        before_zero = overlay.read_bytes()
        zero = run(fresh + ['--headless-ticks', '0', '--save-session-on-exit'], env)[0][0]
        assert zero['session_loaded'] and zero['resumed_tick'] == 2 and zero['ticks'] == 2, zero
        assert Path(zero['saved_session_path']) == first_path
        assert first_path.read_bytes() == first_bytes and overlay.read_bytes() == before_zero, 'zero-tick save changed checkpoint'

        continued = run(fresh + ['--headless-ticks', '2', '--save-session-on-exit'], env)[0][0]
        assert continued['session_loaded'] and continued['resumed_tick'] == 2 and continued['ticks'] == 4, continued
        current_path = Path(continued['saved_session_path'])
        assert current_path != first_path and first_path.read_bytes() == first_bytes
        current_bytes = current_path.read_bytes()
        current_overlay = overlay.read_bytes()
        assert checkpoint_digest(current_overlay) == hashlib.sha256(current_bytes).digest()

        def checkpoint_profile(name):
            copied = profile(name)
            for variable in ('XDG_CONFIG_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME', 'ODH_STATE_DIR'):
                source = Path(env[variable])
                if source.exists():
                    shutil.copytree(source, Path(copied[variable]), dirs_exist_ok=True)
            copied_input = Path(copied['XDG_CONFIG_HOME']) / current_path.relative_to(Path(env['XDG_CONFIG_HOME']))
            return copied, copied_input

        run(['--load-session', str(current_path), '--headless-ticks', '2',
             '--save-session-on-exit', '--headless-checkpoint-interval', '1'], profile('checkpoint-no-authority'), False)
        run(['--load-session', str(first_path), '--headless-ticks', '2',
             '--save-session-on-exit', '--headless-checkpoint-interval', '1'], env, False)
        assert overlay.read_bytes() == current_overlay and current_path.read_bytes() == current_bytes

        checkpoint_env, checkpoint_input = checkpoint_profile('periodic')
        periodic = run(['--load-session', str(checkpoint_input), '--headless-ticks', '3',
                        '--save-session-on-exit', '--headless-checkpoint-interval', '1'], checkpoint_env)[0][0]
        receipts = periodic['headless_checkpoints']
        assert periodic['headless_checkpoint_interval'] == 1 and periodic['resumed_tick'] == 4 and periodic['ticks'] == 7
        assert [item['tick'] for item in receipts] == [5, 6, 7], receipts
        assert [item['elapsed_ticks'] for item in receipts] == [1, 2, 3], receipts
        for item in receipts:
            saved = Path(item['path'])
            assert saved.name == item['sha256'] + '.odhs' and hashlib.sha256(saved.read_bytes()).hexdigest() == item['sha256']
            assert item['family_chunk_id'] == 201 and item['occupancy_epoch'] == 1
            assert item['neighborhood_identity'] == identity.hex() and item['family_identity'] == family.hex()
        latest = Path(receipts[-1]['path'])
        assert latest == Path(periodic['saved_session_path'])
        # A new process rotates the shared log path. Inspect the producer's
        # receipts before the cold reopen creates its own session log.
        log = Path(periodic['session_log']).read_text()
        assert log.count('headless checkpoint accepted tick=') == 3, log
        assert all('tick=' + str(item['tick']) in log and item['sha256'] in log for item in receipts)
        cold = run(['--load-session', str(latest), '--headless-ticks', '0'], checkpoint_env)[0][0]
        assert cold['resumed_tick'] == 7 and cold['ticks'] == 7 and not cold['host_failed']

        cap_env, cap_input = checkpoint_profile('periodic-cap')
        capped = run(['--headless-checkpoint-interval', '1', '--load-session', str(cap_input),
                      '--headless-info-dialog-ok-limit', '1', '--no-audio',
                      '--headless-ticks', '32', '--save-session-on-exit'], cap_env)[0][0]
        assert len(capped['headless_checkpoints']) == 32 and capped['ticks'] == 36
        assert capped['headless_checkpoints'][-1]['elapsed_ticks'] == 32

        sparse_env, sparse_input = checkpoint_profile('periodic-sparse')
        sparse = run(['--load-session', str(sparse_input), '--headless-ticks', '2',
                      '--save-session-on-exit', '--headless-checkpoint-interval', '8'], sparse_env)[0][0]
        assert len(sparse['headless_checkpoints']) == 1 and sparse['headless_checkpoints'][0]['tick'] == 6
        assert sparse['headless_checkpoints'][0]['elapsed_ticks'] == 2

        # Preserve valid input/authority while making ONLY the copied archive
        # publication directory unusable. Capture must propagate its I/O error,
        # create no accepted receipt, and retain the exact previous overlay.
        failure_env, failure_input = checkpoint_profile('periodic-failure')
        external_input = base / 'periodic-failure-input.odhs'
        external_input.write_bytes(failure_input.read_bytes())
        failure_overlay = Path(failure_env['XDG_CONFIG_HOME']) / overlay.relative_to(Path(env['XDG_CONFIG_HOME']))
        failure_before = failure_overlay.read_bytes()
        blocked_sessions = failure_input.parent
        shutil.rmtree(blocked_sessions)
        blocked_sessions.write_bytes(b'preserve this non-directory')
        failed = run(['--load-session', str(external_input), '--headless-ticks', '3',
                      '--save-session-on-exit', '--headless-checkpoint-interval', '1'], failure_env, False)[1]
        assert not failed.stdout and failure_overlay.read_bytes() == failure_before
        assert blocked_sessions.read_bytes() == b'preserve this non-directory'
        assert external_input.read_bytes() == current_bytes
        failure_logs = list(Path(failure_env['ODH_STATE_DIR']).rglob('odh-person.log'))
        assert failure_logs and all('headless checkpoint accepted tick=' not in item.read_text() for item in failure_logs)
        assert overlay.read_bytes() == current_overlay and current_path.read_bytes() == current_bytes

        run(['--load-session', str(first_path), '--headless-ticks', '0', '--save-session-on-exit'], env, False)
        assert overlay.read_bytes() == current_overlay and current_path.read_bytes() == current_bytes
        duplicate = base / 'current-checkpoint-copy.odhs'
        duplicate.write_bytes(current_bytes)
        copied = run(['--load-session', str(duplicate), '--headless-ticks', '0'], env)[0][0]
        assert copied['session_loaded'] and copied['resumed_tick'] == 4 and copied['ticks'] == 4, copied
        assert overlay.read_bytes() == current_overlay
        run(['--load-session', str(duplicate), '--headless-ticks', '0'], profile('no-authority'), False)
        run(fresh + ['--single-resident-session', '--headless-ticks', '0'], env, False)
        assert overlay.read_bytes() == current_overlay and current_path.read_bytes() == current_bytes

        travel_env = profile('travel')
        travel_overlay, _, _ = seed_managed_profile(root, travel_env, binary)
        visits = run(fresh + ['--headless-travel-house', '8', '--headless-travel-house', '7',
                              '--headless-ticks', '2', '--instruction-budget', '25000',
                              '--save-session-on-exit'], travel_env)[0]
        assert [v['integrated_session_context']['house_number'] for v in visits] == [7, 8, 7], visits
        assert [v['session_loaded'] for v in visits] == [True, False, True], visits
        assert [v['resumed_tick'] for v in visits] == [0, 0, 1], visits
        final_path = Path(visits[-1]['saved_session_path'])
        assert checkpoint_digest(travel_overlay.read_bytes()) == hashlib.sha256(final_path.read_bytes()).digest()
        assert first_path.read_bytes() == first_bytes and current_path.read_bytes() == current_bytes
        assert protected == {p.relative_to(root): hashlib.sha256(p.read_bytes()).digest()
                             for p in root.rglob('*') if p.is_file()}, 'managed sessions changed source resources'
    print('Managed native CLI: fresh immutable checkpoint, fresh-process continuation, stale-save rejection, and travel authority reload (synthetic data)')


if __name__ == '__main__':
    main()
