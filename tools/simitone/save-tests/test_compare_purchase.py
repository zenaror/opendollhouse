"""Synthetic gates for the actual purchase/use/reopen log comparator.

Run from the repository root:
  python3 -m unittest discover -s tools/simitone/save-tests -p test_compare_purchase.py

No game resources, commercial save, VM runtime, or UI are used.
"""

import contextlib
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import compare_purchase as COMPARATOR  # noqa: E402


CHAIR_GUID = 0x26BFBB29
CHAIR_ID = 200
AVATAR_ID = 105


def state(version=62):
    return {
        'Family': {'Budget': 585},
        'SIMI': {'Version': version},
        'Entities': [
            {'Kind': 'avatar', 'ObjectID': AVATAR_ID, 'GUID': 1234,
             'MasterGUID': 0, 'Position': {'x': 128, 'y': 128, 'Level': 1},
             'Direction': None},
            {'Kind': 'object', 'ObjectID': CHAIR_ID, 'GUID': CHAIR_GUID,
             'MasterGUID': 0, 'Position': {'x': 160, 'y': 192, 'Level': 1},
             'Direction': 0},
        ],
        'Groups': [{'Objects': [AVATAR_ID], 'Price': 0},
                   {'Objects': [CHAIR_ID], 'Price': 80}],
    }


def save_record(phase, version=62):
    serialized = phase in ('after_serialize', 'save_complete',
                           'deserialize_before_load')
    return {
        'SchemaVersion': 1, 'Phase': phase,
        'SnapshotVersion': 38, 'Compressed': True,
        'SerializedPayloadLength': 128 if serialized else None,
        'SerializedPayloadSHA256': 'a' * 64 if serialized else None,
        'StateSHA256': 'b' * 64, 'RuntimeFamilySubset': [1234],
        'State': state(version),
    }


def log_line(prefix, record):
    return prefix + json.dumps(record, separators=(',', ':'))


def fixture_lines():
    request = {
        'SchemaVersion': 1, 'GUID': CHAIR_GUID,
        'x': 160, 'y': 192, 'level': 1, 'Direction': 0,
        'Mode': 1, 'Value': 80,
        'CallerObjectID': AVATAR_ID, 'CallerPersistID': 65537,
    }
    charged = dict(request, Phase='charged', FamilyBudgetBefore=665,
                   FamilyBudgetAfter=585)
    placed = dict(request, Phase='placed', FamilyBudgetAfter=585,
                  PlacementSucceeded=True, BeforeObjectIDs=[AVATAR_ID],
                  Pieces=[{
                      'ObjectID': CHAIR_ID, 'GUID': CHAIR_GUID,
                      'MasterGUID': 0,
                      'Position': {'x': 160, 'y': 192, 'Level': 1},
                      'Direction': 0,
                  }])
    baseline = {
        'Tick': 80, 'ObjectID': AVATAR_ID, 'Comfort': 40,
        'Posture': 0, 'ActiveQueueBlock': -1, 'Queue': [], 'Frames': [],
    }
    action = {
        'SchemaVersion': 1, 'ActorObjectID': AVATAR_ID,
        'CalleeID': CHAIR_ID, 'QueueUID': 7,
        'ActionName': 'Sit', 'RoutineID': 4097, 'Tick': 81,
    }
    active = {
        'Tick': 90, 'ObjectID': AVATAR_ID, 'Comfort': 45,
        'Posture': 1, 'ActiveQueueBlock': 0,
        'Queue': [{'UID': 7, 'Callee': CHAIR_ID,
                   'Name': 'Sit', 'Routine': 4097}],
        'Frames': [{'Callee': CHAIR_ID}],
    }
    first = [
        log_line('TS1 purchase event: ', charged),
        log_line('TS1 purchase event: ', placed),
        log_line('TS1 avatar state: ', baseline),
        log_line('TS1 action started: ', action),
        log_line('TS1 avatar state: ', active),
    ]
    first.extend(log_line('TS1 save state: ', save_record(phase))
                 for phase in ('before_save', 'after_serialize', 'save_complete'))
    second = [log_line('TS1 save state: ', save_record(phase, 64))
              for phase in ('deserialize_before_load', 'after_vm_load', 'after_ep2')]
    return first, second


def replace_line(lines, prefix, mutate):
    for index, line in enumerate(lines):
        if line.startswith(prefix):
            record = json.loads(line[len(prefix):])
            mutate(record)
            lines[index] = log_line(prefix, record)
            return
    raise AssertionError('fixture has no ' + prefix)


class ComparePurchaseTests(unittest.TestCase):
    def run_case(self, first, second, *extra, include_session=True,
                 session_mutator=None):
        with tempfile.TemporaryDirectory(prefix='simitone-purchase-synthetic-') as directory:
            first_path = Path(directory) / 'first.log'
            second_path = Path(directory) / 'second.log'
            session_path = Path(directory) / 'result.json'
            first_path.write_text('\n'.join(first) + '\n', encoding='utf-8')
            second_path.write_text('\n'.join(second) + '\n', encoding='utf-8')
            session = {
                'expected_house': 'UserData/Houses/House07.iff',
                'expected_house_after_first': {'sha256': 'c' * 64,
                    'fsov_chunks': [{'wrapper_length_valid': True,
                                     'serialized_payload_sha256': 'a' * 64}]},
                'expected_house_after_second': {'sha256': 'c' * 64,
                    'fsov_chunks': [{'wrapper_length_valid': True,
                                     'serialized_payload_sha256': 'a' * 64}]},
                'phase_summary': {
                    'first': {'log': 'first.log', 'vm_error_count': 0,
                              'fatal_error_count': 0,
                              'save_state_diagnostic_error_count': 0},
                    'second': {'log': 'second.log', 'vm_error_count': 0,
                               'fatal_error_count': 0,
                               'save_state_diagnostic_error_count': 0,
                               'process_alive_at_deadline': True},
                },
            }
            for key in (
                    'save_artifact_observed', 'save_artifacts_persisted',
                    'expected_house_has_fsov_after_first',
                    'expected_house_has_fsov_after_second',
                    'local_house_fsov_absent', 'profile_identity_same',
                    'process_ids_different', 'first_exit_clean',
                    'protected_files_unchanged', 'save_state_gate_passed',
                    'pre_ep2_state_exact_except_simi_upgrade',
                    'avatar_person_motive_exact_after_ep2'):
                session[key] = True
            if session_mutator is not None:
                session_mutator(session)
            session_path.write_text(json.dumps(session), encoding='utf-8')
            output, errors = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                arguments = [
                    '--firstlog', str(first_path), '--secondlog', str(second_path),
                    '--object-guid', hex(CHAIR_GUID)]
                if include_session:
                    arguments.extend(('--session-result', str(session_path)))
                status = COMPARATOR.main([*arguments, *extra])
        report = json.loads(output.getvalue() or errors.getvalue())
        return status, report

    def test_positive_chair_purchase_use_and_reopen_without_placed_prebudget(self):
        first, second = fixture_lines()
        status, report = self.run_case(first, second, '--require-action-name', 'Sit',
                                       '--require-posture', '1', '--require-comfort-gain')
        self.assertEqual(status, 0, report)
        self.assertTrue(report['FullPurchaseUsePersistenceVerified'])
        self.assertEqual(report['Purchase']['FamilyBudgetBefore'], 665)
        self.assertEqual(report['Purchase']['FamilyBudgetAfter'], 585)
        self.assertTrue(report['ActionProofPassed'])
        self.assertEqual(report['SaveLoadComparisons']['save_complete_to_deserialize']
                         ['AllowedFormatMetadataDifferenceCount'], 1)

    def test_wrong_action_target_rejected(self):
        first, second = fixture_lines()
        replace_line(first, 'TS1 action started: ',
                     lambda record: record.update(CalleeID=999))
        status, report = self.run_case(first, second)
        self.assertEqual(status, 1)
        self.assertFalse(report['ActionProofPassed'])

    def test_piece_id_was_not_new_rejected(self):
        first, second = fixture_lines()
        for index, line in enumerate(first):
            if '"Phase":"placed"' in line:
                record = json.loads(line.split(': ', 1)[1])
                record['BeforeObjectIDs'].append(CHAIR_ID)
                first[index] = log_line('TS1 purchase event: ', record)
                break
        status, report = self.run_case(first, second)
        self.assertEqual(status, 1)
        self.assertIn('existed before placement',
                      report['PurchasePairEvaluations'][0]['Errors'][0])

    def test_wrong_debit_rejected(self):
        first, second = fixture_lines()
        replace_line(first, 'TS1 purchase event: ',
                     lambda record: record.update(FamilyBudgetAfter=600))
        status, report = self.run_case(first, second)
        self.assertEqual(status, 1)
        self.assertIn('family budget did not decrease',
                      report['PurchasePairEvaluations'][0]['Errors'][0])

    def test_wrong_placed_position_rejected(self):
        first, second = fixture_lines()
        for index, line in enumerate(first):
            if '"Phase":"placed"' in line:
                record = json.loads(line.split(': ', 1)[1])
                record['Pieces'][0]['Position']['x'] = 176
                first[index] = log_line('TS1 purchase event: ', record)
                break
        status, report = self.run_case(first, second)
        self.assertEqual(status, 1)
        self.assertIn('requested position',
                      ' '.join(report['PurchasePairEvaluations'][0]['Errors']))

    def test_persisted_piece_or_budget_mutation_rejected(self):
        for field in ('position', 'budget', 'guid', 'price'):
            with self.subTest(field=field):
                first, second = fixture_lines()
                def mutate(record):
                    if record['Phase'] == 'after_vm_load':
                        if field == 'position':
                            record['State']['Entities'][1]['Position']['x'] = 176
                        elif field == 'budget':
                            record['State']['Family']['Budget'] = 600
                        elif field == 'price':
                            record['State']['Groups'][1]['Price'] = 81
                        else:
                            record['State']['Entities'][1]['GUID'] = 999
                for index, line in enumerate(second):
                    if '"Phase":"after_vm_load"' in line:
                        record = json.loads(line.split(': ', 1)[1])
                        mutate(record)
                        second[index] = log_line('TS1 save state: ', record)
                        break
                status, report = self.run_case(first, second)
                self.assertEqual(status, 1)
                self.assertFalse(report['FullPurchaseUsePersistenceVerified'])
                self.assertTrue(report['StatePieceChecks']['after_vm_load']['Errors'])
                if field == 'price':
                    self.assertEqual(report['StatePieceChecks']['after_vm_load']
                                     ['ActualGroupPrice'], 81)
                    self.assertIn('saved group Price differs from charged Value',
                                  report['StatePieceChecks']['after_vm_load']['Errors'])

    def test_required_posture_rejected_when_missing(self):
        first, second = fixture_lines()
        for index, line in enumerate(first):
            if line.startswith('TS1 avatar state: ') and '"Tick":90' in line:
                record = json.loads(line.split(': ', 1)[1])
                record['Posture'] = 0
                first[index] = log_line('TS1 avatar state: ', record)
                break
        status, report = self.run_case(first, second, '--require-posture', '1')
        self.assertEqual(status, 1)
        self.assertFalse(report['ActionProofPassed'])

    def test_only_queued_action_is_not_use_proof(self):
        first, second = fixture_lines()
        for index, line in enumerate(first):
            if line.startswith('TS1 avatar state: ') and '"Tick":90' in line:
                record = json.loads(line.split(': ', 1)[1])
                record['ActiveQueueBlock'] = -1
                record['Frames'] = []
                first[index] = log_line('TS1 avatar state: ', record)
                break
        status, report = self.run_case(first, second)
        self.assertEqual(status, 1)
        self.assertFalse(report['ActionProofPassed'])

    def test_later_different_queue_uid_cannot_prove_started_action(self):
        first, second = fixture_lines()
        for index, line in enumerate(first):
            if line.startswith('TS1 avatar state: ') and '"Tick":90' in line:
                record = json.loads(line.split(': ', 1)[1])
                record['Queue'][0]['UID'] = 8
                first[index] = log_line('TS1 avatar state: ', record)
                break
        status, report = self.run_case(first, second)
        self.assertEqual(status, 1)
        self.assertFalse(report['ActionProofPassed'])
        self.assertTrue(report['ActionSamples'][0]['FrameTargetsPiece'])

    def test_snapshot_phases_must_be_in_order(self):
        first, second = fixture_lines()
        first[-3], first[-2] = first[-2], first[-3]
        status, report = self.run_case(first, second)
        self.assertEqual(status, 1)
        self.assertFalse(report['SaveLoadPhaseOrder']['FirstSave'])

    def test_diagnostics_without_lifecycle_result_are_scoped(self):
        first, second = fixture_lines()
        status, report = self.run_case(first, second, include_session=False)
        self.assertEqual(status, 1)
        self.assertTrue(report['DiagnosticsPurchaseUseSaveLoadVerified'])
        self.assertFalse(report['FullPurchaseUsePersistenceVerified'])
        self.assertFalse(report['Lifecycle']['Passed'])

    def test_full_phases_take_precedence_over_compact_summary(self):
        first, second = fixture_lines()
        def add_full(session):
            session['phases'] = copy.deepcopy(session['phase_summary'])
            session['phase_summary']['first']['log'] = 'wrong-compact-log.txt'
        status, report = self.run_case(first, second, session_mutator=add_full)
        self.assertEqual(status, 0, report)
        self.assertTrue(report['Lifecycle']['Passed'])

    def test_lifecycle_artifact_or_protected_source_failure_rejected(self):
        first, second = fixture_lines()
        for mutate in (
                lambda session: session['expected_house_after_second'].update(sha256='d' * 64),
                lambda session: session.update(protected_files_unchanged=False)):
            with self.subTest(mutate=mutate):
                status, report = self.run_case(first, second, session_mutator=mutate)
                self.assertEqual(status, 1)
                self.assertTrue(report['DiagnosticsPurchaseUseSaveLoadVerified'])
                self.assertFalse(report['Lifecycle']['Passed'])


if __name__ == '__main__':
    unittest.main()
