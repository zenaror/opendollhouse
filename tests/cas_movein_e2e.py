#!/usr/bin/env python3
"""Opt-in real-data CAS -> vacant-lot -> session-entry integration gate."""
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
        changed = [str(p) for p, sha in before.items() if digest(p) != sha]
        if changed:
            raise RuntimeError('installed game data changed: ' + ', '.join(changed[:8]))
        private = profile / 'data/opendollhouse/Characters'
        overlay_files = list((profile / 'config/opendollhouse/player-state').glob('*.odhp'))
        if not private.is_dir() or len(list(private.glob('*.iff'))) != 1 or len(overlay_files) != 1:
            raise RuntimeError('expected exactly one private character and one player overlay in temporary profile')
        print(json.dumps({**report, 'launcher_plan': 'passed',
                          'launcher_resource_files': len(plan_report.get('resource_identity', {}).get('files', [])),
                          'commercial_source_files_sha256_checked': len(before),
                          'neighborhood_sha256': before[game / 'UserData/Neighborhood.iff'],
                          'game_data_unchanged': True,
                          'private_character_count': 1, 'player_overlay_count': 1}, separators=(',', ':')))


if __name__ == '__main__':
    main()
