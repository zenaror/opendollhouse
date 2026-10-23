#!/usr/bin/env python3
"""Opt-in integrated household gate; reads external House07 resources only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from house07_resident_main_smoke import RESIDENTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--ticks', type=int, default=32)
    parser.add_argument('--timeout', type=int, default=240)
    args = parser.parse_args()
    if not 1 <= args.ticks <= 4096:
        parser.error('--ticks must be in 1..4096')
    binary, game = args.binary.resolve(), args.game_root.resolve()
    resources = [game / 'UserData/Houses/House07.iff', game / 'UserData/Neighborhood.iff']
    resources += [game / resident[2] for resident in RESIDENTS]
    def fingerprints():
        return {str(path.relative_to(game)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in resources}
    before = fingerprints()
    positions = {105: [392, 440, 1, 3], 137: [424, 440, 1, 3]}
    with tempfile.TemporaryDirectory(prefix='odh-household-runtime-') as directory:
        env = os.environ.copy()
        for key in ('XDG_CONFIG_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME'):
            env[key] = str(Path(directory) / key)
        for key in ('DISPLAY', 'WAYLAND_DISPLAY'):
            env.pop(key, None)
        reports = []
        for resident_id in (48, 47):
            command = [str(binary), '--game-root', str(game), '--mode', '1', '--house', '7',
                       '--resident-id', str(resident_id), '--language', '1',
                       '--headless-ticks', str(args.ticks)]
            result = subprocess.run(command, text=True, capture_output=True, env=env, timeout=args.timeout)
            if not result.stdout.strip():
                raise RuntimeError(f'integrated household exit {result.returncode}: {result.stderr}')
            report = json.loads(result.stdout)
            household = report['household_main']
            assert (result.returncode == 0 and report['tick_budget_exhausted'] and
                    not report['host_failed'] and report['ticks'] == args.ticks and
                    household['mode'] == 'whole_household' and household['scheduler_entities'] == 2 and
                    household['entities_before'] == household['entities_after'] and
                    len(household['additional_residents']) == 1), {key: report.get(key) for key in ('ticks', 'status', 'host_failed', 'host_diagnostic', 'tick_budget_exhausted', 'instruction_budget_exhausted', 'household_main')}
            primary = report['avatar']
            extra = household['additional_residents'][0]
            for object_id, guid, asset in RESIDENTS:
                member = next(m for m in report['family_world_reconciliation']['members'] if m['guid'] == guid)
                assert member['object_id'] == object_id and member['presence'] == 'present_with_position', member
                traces = [step for step in report['trace'] if step['owner'] == object_id]
                assert traces, f'no own-resource trace for resident {object_id}'
                if object_id == primary['owner']:
                    assert primary['guid'] == guid and primary['file'] == str(game / asset), primary
                    assert primary['root'] == primary['main'] == 4096, primary
                    assert report['status'] in ('waiting', 'yielded'), report['diagnostic']
                    assert report['route_runtime']['start_position'] == report['route_runtime']['end_position'] == positions[object_id], report['route_runtime']
                else:
                    assert (extra['entity_id'] == object_id and extra['guid'] == guid and
                            extra['file'] == str(game / asset) and extra['routine'] == 4096 and
                            extra['status'] in ('waiting', 'yielded') and extra['steps'] > 0 and
                            extra['start_position'] == extra['position'] == positions[object_id]), extra
            reports.append((command, result, report, household, primary))

        assert [item[4]['owner'] for item in reports] == [105, 137]
        # Save rejection occurs before launch and cannot write a partial archive.
        command = reports[0][0]
        rejected = subprocess.run(command + ['--save-session-on-exit'], text=True, capture_output=True,
                                  env=env, timeout=args.timeout)
        assert rejected.returncode == 2 and not rejected.stdout.strip(), rejected
        assert 'explicit --single-resident-session' in rejected.stderr, rejected.stderr
        assert not list((Path(directory) / 'XDG_DATA_HOME').rglob('*.odhs')), 'unexpected partial save'
    after = fingerprints()
    assert before == after, 'original game resources changed'
    print(json.dumps({'evidence': 'integrated_House07_whole_household_own_Main_one_scheduler',
        'resident_orders': [{'primary_owner': item[4]['owner'], 'ticks': item[2]['ticks'],
                             'scheduler_entities': item[3]['scheduler_entities']} for item in reports],
        'multi_resident_save_rejected': True,
        'fingerprinted_original_inputs_unchanged': True, 'input_sha256': after,
        'visual_window_validated': False, 'full_gameplay': False}, indent=2))


if __name__ == '__main__':
    main()
