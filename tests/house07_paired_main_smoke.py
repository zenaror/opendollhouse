#!/usr/bin/env python3
"""Bounded paired Main startup in one restored House07 World; no gameplay claim."""
import argparse
import json
from pathlib import Path
import subprocess

from house07_resident_main_smoke import RESIDENTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--ticks', type=int, default=32)
    parser.add_argument('--reverse', action='store_true', help='swap the primary and second saved residents')
    args = parser.parse_args()
    if not 1 <= args.ticks <= 4096:
        parser.error('--ticks must be in 1..4096')
    game = args.game_root.resolve()
    global_far = game / 'GameData/Global/Global.far'
    residents = RESIDENTS[::-1] if args.reverse else RESIDENTS
    primary_id, primary_guid, _ = residents[0]
    second_id, second_guid, _ = residents[1]
    positions = {105: [392, 440, 1, 3], 137: [424, 440, 1, 3]}
    files = [game / entry[2] for entry in residents]
    command = [str(args.binary.resolve()), '--avatar-file', str(files[0]), '128',
        '--semiglobal', str(global_far), 'PersonGlobals.iff',
        '--global', str(global_far), 'Global.iff',
        '--lot', str(game / 'UserData/Houses/House07.iff'), '7',
        '--zoning', str(game / 'UserData/LotZoning.iff'),
        '--neighborhood', str(game / 'UserData/Neighborhood.iff'), '--activate-family',
        '--object-definitions', str(game / 'GameData/Objects/Objects.far'),
        '--object-definition-file', str(files[0]), '--object-definition-file', str(files[1]),
        '--animation-source', str(game / 'GameData/Animation/Animation.far'),
        '--animation-source', str(game / 'ExpansionShared/ExpansionShared.far'),
        '--headless-main', '--render-saved-avatar', str(primary_id),
        '--headless-second-saved-main', str(second_id), '4096',
        '--headless-second-avatar-file', str(files[1]), '128',
        '--ticks', str(args.ticks), '--render-floors', str(game / 'GameData/floors.iff'),
        '--render-walls', str(game / 'GameData/walls.iff')]
    for path in sorted(game.rglob('*.flr')) + sorted(game.rglob('*.wll')):
        command += ['--architecture-resource', str(path)]
    wrong_resource = command.copy()
    wrong_resource[wrong_resource.index('--headless-second-avatar-file') + 1] = str(files[0])
    rejected = subprocess.run(wrong_resource, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (rejected.returncode != 2 or rejected.stdout.strip() or
            'second saved World avatar does not match exact positioned person resource' not in rejected.stderr):
        raise SystemExit('mismatching second saved resident resource was not rejected')
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if not result.stdout.strip():
        raise SystemExit(f'paired probe exited {result.returncode}: {result.stderr.strip()}')
    report = json.loads(result.stdout)
    second = report['second_main']
    paired = report['paired_saved_main']
    for object_id, guid, _ in RESIDENTS:
        member = next((m for m in report['family_world_reconciliation']['members']
                       if m['guid'] == guid), None)
        traces = [t for t in report['trace'] if t['owner'] == object_id]
        if not traces or member is None or member['object_id'] != object_id or member['presence'] != 'present_with_position':
            raise SystemExit(f'missing exact resident identity or trace for {object_id}')
    if (result.returncode != 3 or not report['tick_budget_exhausted'] or report['host_failed'] or
            report['ticks'] != args.ticks or report['status'] not in ('waiting', 'yielded') or
            report['avatar']['owner'] != primary_id or report['avatar']['guid'] != primary_guid or
            report['avatar']['file'] != str(files[0]) or report['avatar']['root'] != report['avatar']['main'] or
            second['entity_id'] != second_id or second['guid'] != second_guid or
            second['status'] not in ('waiting', 'yielded') or
            paired['second_file'] != str(files[1]) or paired['entities_before'] != paired['entities_after'] or
            paired['primary_start_position'] != positions[primary_id] or
            paired['primary_position'] != paired['primary_start_position'] or
            paired['second_start_position'] != positions[second_id] or
            paired['second_position'] != paired['second_start_position']):
        raise SystemExit(json.dumps({'failure': 'paired Main startup', 'primary_status': report['status'],
            'primary_diagnostic': report['diagnostic'], 'second': second,
            'host_diagnostic': report['host_diagnostic'], 'ticks': report['ticks']}))
    print(json.dumps({'evidence': 'House07_two_saved_residents_own_Main_one_World_one_clock',
        'ticks': report['ticks'], 'process_exit': result.returncode, 'tick_budget_exhausted': True,
        'full_gameplay': False, 'primary_id': primary_id, 'primary_status': report['status'], 'second': second,
        'mismatching_resource_rejected': True,
        'saved_world': paired,
        'trace_steps': {str(id): sum(t['owner'] == id for t in report['trace'])
                        for id, _, _ in RESIDENTS}}, indent=2))


if __name__ == '__main__':
    main()
