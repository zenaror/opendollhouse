#!/usr/bin/env python3
"""Opt-in bounded House05 household runtime gate using external data read-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--ticks', type=int, default=4096)
    parser.add_argument('--instruction-budget', type=int, default=50000)
    parser.add_argument('--timeout', type=int, default=240)
    args = parser.parse_args()
    if not 1 <= args.ticks <= 4096:
        parser.error('--ticks must be in 1..4096')
    if not 10000 < args.instruction_budget <= 1000000:
        parser.error('--instruction-budget must be in 10001..1000000')
    binary, game = args.binary.resolve(), args.game_root.resolve()
    command = [str(binary), '--game-root', str(game), '--mode', '1', '--house', '5',
               '--resident-id', '34', '--language', '1', '--headless-ticks', str(args.ticks),
               '--instruction-budget', str(args.instruction_budget)]
    plan = subprocess.run(command[:-4] + ['--plan-only'], capture_output=True, text=True, timeout=args.timeout)
    if plan.returncode != 0:
        raise RuntimeError(f'House05 resource plan failed: {plan.stderr}')
    paths = json.loads(plan.stdout)['resource_identity']['files']

    def fingerprints():
        return {relative: hashlib.sha256((game / relative).read_bytes()).hexdigest()
                for relative in paths}

    before = fingerprints()
    with tempfile.TemporaryDirectory(prefix='odh-house05-household-budget-') as directory:
        env = os.environ.copy()
        for key in ('XDG_CONFIG_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME'):
            env[key] = str(Path(directory) / key)
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=args.timeout)
    if not result.stdout.strip():
        raise RuntimeError(f'House05 runtime exit {result.returncode}: {result.stderr}')
    report = json.loads(result.stdout)
    household = report['household_main']
    residents = {resident['entity_id']: resident for resident in household['additional_residents']}
    assert result.returncode == 0 and report['status'] == 'waiting', (result.returncode, report.get('status'))
    assert report['runtime_instruction_budget'] == args.instruction_budget
    assert report['ticks'] == args.ticks and report['tick_budget_exhausted']
    assert not report['instruction_budget_exhausted'] and not report['host_failed'], {
        key: report.get(key) for key in ('steps', 'ticks', 'instruction_budget_exhausted', 'host_failed', 'host_diagnostic')}
    assert household['mode'] == 'whole_household' and household['scheduler_entities'] == 3
    resident16 = residents[16]
    assert resident16['status'] == 'waiting' and not resident16['diagnostic']
    assert resident16['position'] != resident16['start_position']
    assert resident16['routing']['movement_callbacks'] > 0
    assert residents[18]['status'] == 'waiting' and not residents[18]['diagnostic']
    after = fingerprints()
    assert before == after, 'game resources changed during the read-only runtime gate'
    print(json.dumps({
        'status': 'passed', 'ticks': report['ticks'], 'steps': report['steps'],
        'instruction_budget': report['runtime_instruction_budget'],
        'resident16_start': resident16['start_position'], 'resident16_end': resident16['position'],
        'resident16_movement_callbacks': resident16['routing']['movement_callbacks'],
        'resident18_position': residents[18]['position'], 'game_resources_unchanged': len(before),
    }, separators=(',', ':')))


if __name__ == '__main__':
    main()
