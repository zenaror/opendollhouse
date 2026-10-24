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
from linux_household_session_resume_cli import archive_sections, negative_archives


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--ticks', type=int, default=32)
    parser.add_argument('--timeout', type=int, default=240)
    parser.add_argument('--resident-sit-stand', action='store_true',
                        help='also exercise Sit and Stand for the non-primary House07 resident')
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
        resident_actions = []
        for resident_id in (48, 47):
            command = [str(binary), '--game-root', str(game), '--mode', '1', '--house', '7',
                       '--resident-id', str(resident_id), '--language', '1',
                       '--headless-ticks', str(args.ticks), '--save-session-on-exit']
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
            path = Path(report['saved_session_path'])
            saved = path.read_bytes()
            sections = {ident: body for ident, _, body in archive_sections(saved)}
            assert sections[b'HOST'][8:10] == b'\x04\0'
            assert int.from_bytes(sections[b'HHRD'][2:4], 'little') == 2
            resumed_result = subprocess.run([str(binary), '--game-root', str(game), '--load-session', str(path),
                '--headless-ticks', str(args.ticks), '--save-session-on-exit'], text=True, capture_output=True,
                env=env, timeout=args.timeout)
            assert resumed_result.returncode == 0, (resumed_result.returncode, resumed_result.stdout[-2000:], resumed_result.stderr)
            resumed = json.loads(resumed_result.stdout)
            assert (resumed['session_loaded'] and resumed['resumed_tick'] == args.ticks and resumed['ticks'] == 2*args.ticks and
                    not resumed['host_failed'] and resumed['avatar']['owner'] == primary['owner'] and
                    resumed['household_main']['scheduler_entities'] == 2), resumed
            resumed_bytes = Path(resumed['saved_session_path']).read_bytes()
            direct_command = list(command)
            direct_command[direct_command.index('--headless-ticks')+1] = str(2*args.ticks)
            direct_result = subprocess.run(direct_command, text=True, capture_output=True,env=env, timeout=args.timeout)
            assert direct_result.returncode == 0, (direct_result.returncode,direct_result.stdout[-2000:],direct_result.stderr)
            direct = json.loads(direct_result.stdout)
            assert resumed_bytes == Path(direct['saved_session_path']).read_bytes(), 'resumed household differs from uninterrupted execution'
            if args.resident_sit_stand:
                extra_actor_id = {48: 137, 47: 105}[resident_id]
                interaction_ticks, stand_tick = 512, 270
                action_command = [str(binary), '--game-root', str(game), '--mode', '1', '--house', '7',
                    '--resident-id', str(resident_id), '--language', '1', '--headless-ticks', str(interaction_ticks),
                    '--headless-resident-sit', str(extra_actor_id), '128', str(stand_tick), '--save-session-on-exit']
                action_result = subprocess.run(action_command, text=True, capture_output=True,
                    env=env, timeout=args.timeout)
                if not action_result.stdout.strip():
                    raise RuntimeError(f'resident Sit/Stand exit {action_result.returncode}: {action_result.stderr}')
                action_report = json.loads(action_result.stdout)
                interaction, stand = action_report['interaction_runtime'], action_report['stand_runtime']
                assert (action_result.returncode == 0 and action_report['ticks'] == interaction_ticks and
                    action_report['tick_budget_exhausted'] and not action_report['host_failed'] and
                    interaction['trigger'] == 'headless_sit_schedule' and interaction['actor']['id'] == extra_actor_id and
                    interaction['action'] == 4097 and interaction['status'] == 'returned_true' and
                    interaction['result'] is True and interaction['queue_size'] == 0 and not interaction['queue_active'] and
                    interaction['actor']['attachment'] is None and stand['actor_id'] == extra_actor_id and
                    stand['scheduled_tick'] == stand_tick and stand['precondition_met'] and
                    stand['precondition_posture'] == 1 and stand['precondition_attachment'] == 128 and
                    stand['action'] == 4102 and stand['status'] == 'returned_true' and stand['result'] is True and
                    stand['actor_posture'] == 0 and not stand['actor_attached']), action_report
                resident_actions.append({'evidence': 'House07_additional_resident_Sit_Stand',
                    'primary_neighbor_id': resident_id, 'actor_id': extra_actor_id,
                    'object_id': 128, 'sit_action': interaction['action'], 'sit_result': interaction['result'],
                    'stand_action': stand['action'], 'stand_result': stand['result'],
                    'final_posture': stand['actor_posture'], 'final_attached': stand['actor_attached']})
                action_path = Path(action_report['saved_session_path'])
                action_saved = action_path.read_bytes()
                action_resumed_result = subprocess.run([str(binary), '--game-root', str(game),
                    '--load-session', str(action_path), '--headless-ticks', str(interaction_ticks),
                    '--save-session-on-exit'], text=True, capture_output=True, env=env, timeout=args.timeout)
                assert action_resumed_result.returncode == 0, (action_resumed_result.returncode,
                    action_resumed_result.stdout[-2000:], action_resumed_result.stderr)
                action_resumed = json.loads(action_resumed_result.stdout)
                assert (action_resumed['session_loaded'] and action_resumed['resumed_tick'] == interaction_ticks and
                    action_resumed['ticks'] == 2*interaction_ticks and not action_resumed['host_failed'] and
                    action_resumed['household_main']['scheduler_entities'] == 2), action_resumed
                action_resumed_bytes = Path(action_resumed['saved_session_path']).read_bytes()
                action_direct_command = list(action_command)
                action_direct_command[action_direct_command.index('--headless-ticks')+1] = str(2*interaction_ticks)
                action_direct_result = subprocess.run(action_direct_command, text=True, capture_output=True,
                    env=env, timeout=args.timeout)
                assert action_direct_result.returncode == 0, (action_direct_result.returncode,
                    action_direct_result.stdout[-2000:], action_direct_result.stderr)
                action_direct = json.loads(action_direct_result.stdout)
                assert action_resumed_bytes == Path(action_direct['saved_session_path']).read_bytes(), \
                    'post-Sit/Stand household resume differs from uninterrupted execution'
                resident_actions[-1]['post_stand_saved_tick'] = interaction_ticks
                resident_actions[-1]['post_stand_resumed_tick'] = action_resumed['resumed_tick']
                resident_actions[-1]['post_stand_final_tick'] = action_resumed['ticks']
                resident_actions[-1]['post_stand_resume_matches_continuous'] = True
                resident_actions[-1]['post_stand_archive_sha256'] = hashlib.sha256(action_saved).hexdigest()
                saved_before_blocked_attempt = path.read_bytes()
                blocked_save = subprocess.run([str(binary), '--game-root', str(game), '--mode', '1', '--house', '7',
                    '--resident-id', str(resident_id), '--language', '1', '--headless-ticks', str(stand_tick),
                    '--headless-resident-interaction', str(extra_actor_id), '128', '--save-session-on-exit'],
                    text=True, capture_output=True, env=env, timeout=args.timeout)
                assert (blocked_save.returncode == 2 and
                    'household session cannot yet restore a seated or attached additional resident' in blocked_save.stderr), (
                        blocked_save.returncode, blocked_save.stdout, blocked_save.stderr)
                assert path.read_bytes() == saved_before_blocked_attempt, 'blocked seated-resident save replaced the existing checkpoint'
                resident_actions[-1]['save_while_seated_rejected'] = True
                resident_actions[-1]['previous_save_preserved'] = True
            previous = path.read_bytes()
            for name, malformed in negative_archives(saved):
                bad = Path(directory)/(str(resident_id)+'-'+name+'.odhs');bad.write_bytes(malformed)
                rejected = subprocess.run([str(binary),'--game-root',str(game),'--load-session',str(bad),
                    '--headless-ticks','1'],text=True,capture_output=True,env=env,timeout=args.timeout)
                assert rejected.returncode == 2, (name,rejected.returncode,rejected.stdout[-2000:],rejected.stderr)
                assert bad.read_bytes() == malformed and path.read_bytes() == previous, 'rejected restore modified archive'
            reports.append((command, result, report, household, primary))

        assert [item[4]['owner'] for item in reports] == [105, 137]
    after = fingerprints()
    assert before == after, 'original game resources changed'
    print(json.dumps({'evidence': 'integrated_House07_whole_household_own_Main_one_scheduler',
        'resident_orders': [{'primary_owner': item[4]['owner'], 'ticks': item[2]['ticks'],
                             'scheduler_entities': item[3]['scheduler_entities']} for item in reports],
        'resident_sit_stand': resident_actions,
        'whole_household_save_resume_matches_continuous': True,
        'missing_partial_mismatched_rosters_rejected': True,
        'fingerprinted_original_inputs_unchanged': True, 'input_sha256': after,
        'visual_window_validated': False, 'full_gameplay': False}, indent=2))


if __name__ == '__main__':
    main()
