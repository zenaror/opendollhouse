#!/usr/bin/env python3
"""Opt-in real-data CAS -> vacant-lot -> runtime -> save/resume integration gate."""
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


def source_paths(game):
    paths = {game / 'UserData/Neighborhood.iff', game / 'UserData/LotLocations.iff',
             game / 'GameData/Objects/Objects.far'}
    # The CAS scanner catalogs all FAR archives and loose assets below these
    # metadata roots, and walks expansion Skins directories.
    roots = [game / 'GameData/Skins', game / 'ExpansionShared/SkinsBuy', game / 'Downloads']
    roots += [p / 'Skins' for p in game.iterdir() if p.is_dir() and p.name.lower().startswith('expansion')]
    for root in roots:
        if root.is_dir():
            paths.update(p for p in root.rglob('*') if p.is_file())
    paths.update(p for p in game.rglob('*.far') if p.is_file())
    paths.update(p for p in (game / 'UserData/Characters').rglob('*') if p.is_file())
    paths.update(p for p in (game / 'UserData/Houses').glob('House*.iff') if p.is_file())
    return sorted(paths)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path, help='built odh-cas-movein-e2e executable')
    parser.add_argument('--person', required=True, type=Path, help='built odh-person executable')
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    game = args.game_root.resolve(strict=True)
    runner, person = args.runner.resolve(strict=True), args.person.resolve(strict=True)
    inputs = source_paths(game)
    before = {p: digest(p) for p in inputs}
    with tempfile.TemporaryDirectory(prefix='odh-cas-movein-e2e-') as temp:
        profile = Path(temp)
        env = os.environ.copy()
        env['HOME'] = str(profile / 'home')
        env['XDG_CONFIG_HOME'] = str(profile / 'config')
        env['XDG_STATE_HOME'] = str(profile / 'state')
        env['XDG_DATA_HOME'] = str(profile / 'data')
        env['ODH_STATE_DIR'] = str(profile / 'state/opendollhouse')
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        result = subprocess.run([str(runner), str(game), str(profile)], env=env,
                                capture_output=True, text=True, timeout=args.timeout)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or f'integration runner exited {result.returncode}')
        report = json.loads(result.stdout)
        plan = subprocess.run([str(person), '--game-root', str(game), '--mode', '1',
                               '--house', str(report['house']), '--resident-id', str(report['neighbor_id']),
                               '--plan-only'], env=env, capture_output=True, text=True,
                              timeout=args.timeout)
        if plan.returncode:
            raise RuntimeError(plan.stderr.strip() or f'odh-person --plan-only exited {plan.returncode}')
        plan_report = json.loads(plan.stdout)
        if plan_report.get('status') != 'ready' or plan_report.get('resident_guid') != report['guid']:
            raise RuntimeError(f'launcher did not return a successful resource plan: {plan.stdout[:1000]}')
        selection = [str(person), '--game-root', str(game), '--mode', '1',
                     '--house', str(report['house']), '--resident-id', str(report['neighbor_id'])]

        def run_session(arguments, expected_ticks, expected_resume):
            session_result = subprocess.run(arguments, env=env, capture_output=True, text=True,
                                             timeout=args.timeout)
            if session_result.returncode:
                raise RuntimeError(session_result.stderr.strip() or
                                   f'game session exited {session_result.returncode}')
            session = json.loads(session_result.stdout)
            if session.get('host_failed') or session.get('ticks') != expected_ticks:
                raise RuntimeError(f'bounded game session did not reach tick {expected_ticks}: '
                                   f'{session_result.stdout[:1200]}')
            if session.get('session_loaded') is not expected_resume:
                raise RuntimeError(f'game session resume state was unexpected: {session_result.stdout[:1200]}')
            if not session.get('tick_budget_exhausted'):
                raise RuntimeError('bounded game session did not stop at its requested tick budget')
            household = session.get('household_main', {})
            if household.get('scheduler_entities') != report['member_count']:
                raise RuntimeError(f'full CAS household did not enter the runtime: {session_result.stdout[:1600]}')
            additional = household.get('additional_residents', [])
            if (len(additional) != report['member_count'] - 1 or
                    any(member.get('status') != 'waiting' or member.get('diagnostic') or
                        member.get('position') is None for member in additional)):
                raise RuntimeError(f'additional CAS resident did not reach its idle Main: {session_result.stdout[:2000]}')
            return session

        first_session = run_session(selection + ['--headless-ticks', '16', '--save-session-on-exit'],
                                    16, False)
        archive = Path(first_session['saved_session_path']).resolve(strict=True)
        try:
            archive.relative_to(profile / 'data/opendollhouse/saves')
        except ValueError as error:
            raise RuntimeError('session archive escaped the temporary player profile') from error
        resumed_session = run_session([str(person), '--game-root', str(game), '--load-session', str(archive),
                                       '--headless-ticks', '16', '--save-session-on-exit'], 32, True)
        resumed_bytes = archive.read_bytes()
        direct_session = run_session(selection + ['--headless-ticks', '32', '--save-session-on-exit'],
                                     32, False)
        direct_archive = Path(direct_session['saved_session_path']).resolve(strict=True)
        if direct_archive != archive or direct_archive.read_bytes() != resumed_bytes:
            raise RuntimeError('fresh-process save/resume differs from uninterrupted 32-tick execution')
        changed = [str(p) for p, sha in before.items() if digest(p) != sha]
        if changed:
            raise RuntimeError('installed game data changed: ' + ', '.join(changed[:8]))
        private = profile / 'data/opendollhouse/Characters'
        overlay_files = list((profile / 'config/opendollhouse/player-state').glob('*.odhp'))
        if (not private.is_dir() or len(list(private.glob('*.iff'))) != report['member_count'] or
                len(overlay_files) != 1):
            raise RuntimeError('expected the complete private CAS household and one player overlay in temporary profile')
        print(json.dumps({'status': 'passed', 'mode': report['mode'], 'house': report['house'],
                          'purchase_price': report['purchase_price'], 'household_members': report['member_count'],
                          'launcher_plan': 'passed',
                          'launcher_resource_files': len(plan_report.get('resource_identity', {}).get('files', [])),
                          'runtime_entry': 'passed', 'runtime_ticks': resumed_session['ticks'],
                          'additional_resident_statuses': [member['status'] for member in
                              resumed_session['household_main']['additional_residents']],
                          'fresh_process_resume': 'passed', 'resume_matches_continuous': True,
                          'commercial_source_files_sha256_checked': len(before),
                          'neighborhood_sha256': before[game / 'UserData/Neighborhood.iff'],
                          'game_data_unchanged': True,
                          'private_character_count': len(list(private.glob('*.iff'))),
                          'player_overlay_count': 1}, separators=(',', ':')))


if __name__ == '__main__':
    main()
