#!/usr/bin/env python3
"""Check one TS1 purchase, use, and save/reopen sequence from existing logs.

This is an evidence checker. It does not run Simitone or infer a pre-purchase
baseline from the original game's House IFF. A purchase event supplies the
budget and object-ID baseline at the actual transaction and placement points.
"""

import argparse
import json
from pathlib import Path
import sys

from compare_save_states import compare


PREFIXES = {
    'purchase': 'TS1 purchase event: ',
    'action': 'TS1 action started: ',
    'avatar': 'TS1 avatar state: ',
    'save': 'TS1 save state: ',
    'compatibility': 'Saved code compatibility: ',
}
SAVE_PHASES = ('before_save', 'after_serialize', 'save_complete')
LOAD_PHASES = ('deserialize_before_load', 'after_vm_load', 'after_ep2')


def parse_guid(value):
    try:
        number = int(value, 0)
    except ValueError:
        try:
            number = int(value, 16)
        except ValueError as error:
            raise argparse.ArgumentTypeError('GUID must be decimal or hexadecimal') from error
    if not 0 <= number <= 0xffffffff:
        raise argparse.ArgumentTypeError('GUID must be a 32-bit unsigned value')
    return number


def read_log(path):
    records = {kind: [] for kind in PREFIXES}
    with path.open(encoding='utf-8', errors='replace') as source:
        for line_number, line in enumerate(source, 1):
            for kind, prefix in PREFIXES.items():
                if not line.startswith(prefix):
                    continue
                try:
                    record = json.loads(line[len(prefix):])
                except json.JSONDecodeError as error:
                    raise ValueError('%s:%d: malformed %s JSON: %s' % (
                        path, line_number, kind, error)) from error
                if not isinstance(record, dict):
                    raise ValueError('%s:%d: %s record is not an object' % (
                        path, line_number, kind))
                records[kind].append((line_number, record))
                break
    return records


def request_key(record):
    fields = ('GUID', 'x', 'y', 'level', 'Direction', 'Mode', 'Value',
              'CallerObjectID', 'CallerPersistID')
    missing = [name for name in fields if name not in record]
    if missing:
        raise ValueError('purchase event missing ' + ', '.join(missing))
    return tuple(record[name] for name in fields)


def purchase_pairs(records, object_guid):
    pending = {}
    pairs = []
    issues = []
    for line, event in records:
        if event.get('SchemaVersion') != 1:
            issues.append({'Line': line, 'Reason': 'purchase schema is not 1'})
            continue
        if object_guid is not None and event.get('GUID') != object_guid:
            continue
        key = request_key(event)
        if event.get('Phase') == 'charged':
            pending.setdefault(key, []).append((line, event))
        elif event.get('Phase') == 'placed':
            charges = pending.get(key, [])
            if len(charges) != 1:
                issues.append({'Line': line, 'Reason': 'placed event has %d pending matching charges' % len(charges)})
                continue
            pairs.append((charges.pop(), (line, event)))
        else:
            issues.append({'Line': line, 'Reason': 'unknown purchase phase'})
    for charges in pending.values():
        for line, _ in charges:
            issues.append({'Line': line, 'Reason': 'charged event has no placed result'})
    return pairs, issues


def select_phase(records, phase, occurrence):
    matching = [(line, record) for line, record in records
                if record.get('Phase') == phase]
    index = None
    if occurrence is None and len(matching) == 1:
        index = 0
    elif occurrence is not None and -len(matching) <= occurrence < len(matching):
        index = occurrence if occurrence >= 0 else len(matching) + occurrence
    selected = matching[index] if index is not None else None
    valid = bool(selected and selected[1].get('SchemaVersion') == 1
                 and isinstance(selected[1].get('State'), dict)
                 and 'DiagnosticError' not in selected[1])
    return selected if valid else None, {
        'Phase': phase, 'Count': len(matching), 'SelectedIndex': index,
        'Valid': valid,
    }


def position(value):
    if not isinstance(value, dict):
        return None
    return (value.get('x'), value.get('y'), value.get('Level'))


def validate_purchase(charge, placed):
    errors = []
    before = charge.get('FamilyBudgetBefore')
    after = charge.get('FamilyBudgetAfter')
    price = charge.get('Value')
    if charge.get('Mode') not in (1, 'Normal'):
        errors.append('purchase mode is not Normal')
    if (type(before) is not int or type(after) is not int
            or type(price) is not int or price <= 0 or before - after != price):
        errors.append('family budget did not decrease by a positive charged Value')
    if placed.get('PlacementSucceeded') is not True:
        errors.append('placement did not succeed')
    if placed.get('FamilyBudgetAfter') != after:
        errors.append('placed event final budget does not match charged event')
    pieces = placed.get('Pieces')
    old_ids = placed.get('BeforeObjectIDs')
    if not isinstance(pieces, list) or not pieces:
        errors.append('placed event has no pieces')
        pieces = []
    if not isinstance(old_ids, list):
        errors.append('placed event has no pre-placement object IDs')
        old_ids = []
    ids = [piece.get('ObjectID') for piece in pieces]
    if len(ids) != len(set(ids)) or any(type(value) is not int for value in ids):
        errors.append('piece object IDs are missing or repeated')
    if any(value in old_ids for value in ids):
        errors.append('a supposedly new piece ID existed before placement')
    requested = (placed.get('x'), placed.get('y'), placed.get('level'))
    if not any(position(piece.get('Position')) == requested for piece in pieces):
        errors.append('no created piece occupies the requested position')
    for piece in pieces:
        where = position(piece.get('Position'))
        if (where is None or any(type(axis) is not int for axis in where)
                or where[2] < 1):
            errors.append('a created piece is not placed in the world')
        if (piece.get('GUID') != placed.get('GUID')
                and piece.get('MasterGUID') != placed.get('GUID')):
            errors.append('a created piece does not belong to the requested GUID')
    return errors


def state_piece_checks(record, pieces, budget, price):
    state = record['State']
    entities = {entity.get('ObjectID'): entity
                for entity in state.get('Entities', [])}
    groups = state.get('Groups', [])
    ids = {piece['ObjectID'] for piece in pieces}
    errors = []
    actual = {}
    for piece in pieces:
        identity = piece['ObjectID']
        entity = entities.get(identity)
        if entity is None:
            errors.append('piece %s is missing' % identity)
            continue
        actual[str(identity)] = {
            'GUID': entity.get('GUID'),
            'MasterGUID': entity.get('MasterGUID'),
            'Position': entity.get('Position'),
            'Direction': entity.get('Direction'),
        }
        if entity.get('Kind') != 'object':
            errors.append('piece %s is not an object' % identity)
        if (entity.get('GUID') != piece.get('GUID')
                or entity.get('MasterGUID') != piece.get('MasterGUID')):
            errors.append('piece %s GUID/master differs from placement' % identity)
        if position(entity.get('Position')) != position(piece.get('Position')):
            errors.append('piece %s position differs from placement' % identity)
        if entity.get('Direction') != piece.get('Direction'):
            errors.append('piece %s direction differs from placement' % identity)
    matching_groups = [group for group in groups
                       if len(group.get('Objects', [])) == len(ids)
                       and set(group.get('Objects', [])) == ids]
    if len(matching_groups) != 1:
        errors.append('created pieces do not form exactly one saved group')
    group_price = matching_groups[0].get('Price') if len(matching_groups) == 1 else None
    if len(matching_groups) == 1 and group_price != price:
        errors.append('saved group Price differs from charged Value')
    family = state.get('Family')
    actual_budget = family.get('Budget') if isinstance(family, dict) else None
    if actual_budget != budget:
        errors.append('saved family budget differs from charged budget')
    return {'Phase': record.get('Phase'), 'PieceFacts': actual,
            'FamilyBudget': actual_budget, 'ActualGroupPrice': group_price,
            'Errors': errors}


def active_target(sample, event):
    frames = sample.get('Frames') or []
    frame = any(item.get('Callee') == event.get('CalleeID') for item in frames)
    queue = sample.get('Queue') or []
    index = sample.get('ActiveQueueBlock')
    active_queue = type(index) is int and 0 <= index < len(queue)
    if active_queue:
        item = queue[index]
        active_queue = (item.get('UID') == event.get('QueueUID')
                        and item.get('Callee') == event.get('CalleeID')
                        and (item.get('Name') is None or
                             item['Name'] == event.get('ActionName'))
                        and (item.get('Routine') is None or
                             item['Routine'] == event.get('RoutineID')))
    return frame, active_queue


def action_proof(actions, samples, piece_ids, placed_line, save_line,
                 required_name, required_posture, require_comfort_gain):
    candidates = [(line, event) for line, event in actions
                  if placed_line < line < save_line
                  and event.get('SchemaVersion') == 1
                  and event.get('CalleeID') in piece_ids
                  and type(event.get('ActorObjectID')) is int
                  and type(event.get('QueueUID')) is int
                  and type(event.get('Tick')) is int
                  and (required_name is None or
                       str(event.get('ActionName', '')).casefold()
                       == required_name.casefold())]
    reports = []
    for action_line, event in candidates:
        actor = event.get('ActorObjectID')
        before = [sample for line, sample in samples
                  if line < action_line and sample.get('ObjectID') == actor]
        after = [(line, sample) for line, sample in samples
                 if action_line < line < save_line
                 and sample.get('ObjectID') == actor
                 and sample.get('Tick', -1) >= event.get('Tick', -1)]
        baseline = before[-1] if before else None
        for sample_line, sample in after:
            frame, active_queue = active_target(sample, event)
            posture_ok = (required_posture is None
                          or sample.get('Posture') == required_posture)
            comfort_delta = (sample.get('Comfort') - baseline.get('Comfort')
                             if baseline is not None
                             and type(sample.get('Comfort')) is int
                             and type(baseline.get('Comfort')) is int else None)
            comfort_ok = not require_comfort_gain or (
                comfort_delta is not None and comfort_delta > 0)
            reports.append({
                'ActionLine': action_line, 'SampleLine': sample_line,
                'ActorObjectID': actor, 'CalleeID': event.get('CalleeID'),
                'QueueUID': event.get('QueueUID'),
                'ActionName': event.get('ActionName'),
                'RoutineID': event.get('RoutineID'),
                'StartedTick': event.get('Tick'), 'SampleTick': sample.get('Tick'),
                'FrameTargetsPiece': frame,
                'ActiveQueueTargetsPiece': active_queue,
                'Posture': sample.get('Posture'),
                'ComfortDelta': comfort_delta,
                'Passed': active_queue and posture_ok and comfort_ok,
            })
    return reports


def lifecycle_checks(path, firstlog, secondlog, save_complete, deserialize):
    if path is None:
        return {'Provided': False, 'Passed': False,
                'Errors': ['a session result is required to bind diagnostics to the saved House FSOV and isolated lifecycle']}
    session = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(session, dict):
        raise ValueError('session result is not a JSON object')
    errors = []
    phases = session.get('phases') or session.get('phase_summary') or {}
    for name, log in (('first', firstlog), ('second', secondlog)):
        phase = phases.get(name) or {}
        reported = phase.get('log')
        if not isinstance(reported, str) or (path.parent / reported).resolve() != log.resolve():
            errors.append('%s log path is not the session result log' % name)
        if phase.get('vm_error_count') != 0 or phase.get('fatal_error_count') != 0:
            errors.append('%s phase has VM or fatal errors' % name)
        if phase.get('save_state_diagnostic_error_count') != 0:
            errors.append('%s phase has save-state diagnostic errors' % name)
    required = (
        'save_artifact_observed', 'save_artifacts_persisted',
        'expected_house_has_fsov_after_first',
        'expected_house_has_fsov_after_second',
        'local_house_fsov_absent', 'profile_identity_same',
        'process_ids_different', 'first_exit_clean',
        'protected_files_unchanged', 'save_state_gate_passed',
        'pre_ep2_state_exact_except_simi_upgrade',
        'avatar_person_motive_exact_after_ep2',
    )
    for name in required:
        if session.get(name) is not True:
            errors.append('session result does not pass ' + name)
    if (phases.get('second') or {}).get('process_alive_at_deadline') is not True:
        errors.append('second process was not alive at the observation deadline')
    for label, phase, record in (
            ('first', 'expected_house_after_first', save_complete),
            ('second', 'expected_house_after_second', deserialize)):
        house = session.get(phase) or {}
        hashes = {chunk.get('serialized_payload_sha256') for chunk in
                  house.get('fsov_chunks', []) if chunk.get('wrapper_length_valid')}
        if record is None or record.get('SerializedPayloadSHA256') not in hashes:
            errors.append('%s selected diagnostic payload does not match House FSOV' % label)
    first_house = session.get('expected_house_after_first') or {}
    second_house = session.get('expected_house_after_second') or {}
    if (not first_house.get('sha256')
            or first_house.get('sha256') != second_house.get('sha256')):
        errors.append('House IFF SHA differs between save and reopen')
    return {
        'Provided': True, 'Path': str(path), 'Passed': not errors,
        'ExpectedHouse': session.get('expected_house'),
        'HouseSHA256First': first_house.get('sha256'),
        'HouseSHA256Second': second_house.get('sha256'),
        'Errors': errors,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firstlog', type=Path, required=True)
    parser.add_argument('--secondlog', type=Path, required=True)
    parser.add_argument('--session-result', type=Path,
                        help='session_smoke result.json binding logs to the saved House FSOV')
    parser.add_argument('--object-guid', type=parse_guid,
                        help='optional decimal or 0x-prefixed catalog GUID')
    parser.add_argument('--event-index', type=int,
                        help='zero-based successful purchase pair; -1 selects last')
    parser.add_argument('--save-record-index', type=int)
    parser.add_argument('--load-record-index', type=int)
    parser.add_argument('--require-action-name')
    parser.add_argument('--require-posture', type=int)
    parser.add_argument('--require-comfort-gain', action='store_true')
    args = parser.parse_args(argv)
    try:
        first = read_log(args.firstlog)
        second = read_log(args.secondlog)
        pairs, purchase_issues = purchase_pairs(first['purchase'], args.object_guid)
        evaluations = [{'ChargedLine': charge[0], 'PlacedLine': placed[0],
                        'Errors': validate_purchase(charge[1], placed[1])}
                       for charge, placed in pairs]
        valid_pairs = [pair for pair, evaluation in zip(pairs, evaluations)
                       if not evaluation['Errors']]
        pair_index = None
        if args.event_index is None and len(valid_pairs) == 1:
            pair_index = 0
        elif (args.event_index is not None
              and -len(valid_pairs) <= args.event_index < len(valid_pairs)):
            pair_index = (args.event_index if args.event_index >= 0
                          else len(valid_pairs) + args.event_index)
        pair = valid_pairs[pair_index] if pair_index is not None else None

        selected = {}
        selections = {}
        for phase in SAVE_PHASES:
            selected[phase], selections[phase] = select_phase(
                first['save'], phase, args.save_record_index)
        for phase in LOAD_PHASES:
            selected[phase], selections[phase] = select_phase(
                second['save'], phase, args.load_record_index)
        phase_order = {
            'FirstSave': (all(selected[phase] is not None for phase in SAVE_PHASES)
                          and selected['before_save'][0] < selected['after_serialize'][0]
                          < selected['save_complete'][0]),
            'SecondLoad': (all(selected[phase] is not None for phase in LOAD_PHASES)
                           and selected['deserialize_before_load'][0]
                           < selected['after_vm_load'][0] < selected['after_ep2'][0]),
        }

        report = {
            'SchemaVersion': 1,
            'FirstLog': str(args.firstlog), 'SecondLog': str(args.secondlog),
            'RequestedGUID': args.object_guid,
            'PurchasePairCount': len(pairs),
            'ValidPurchasePairCount': len(valid_pairs),
            'SelectedEventIndex': pair_index,
            'PurchaseIssues': purchase_issues,
            'PurchasePairEvaluations': evaluations,
            'SaveRecordSelection': selections,
            'SaveLoadPhaseOrder': phase_order,
            'SecondNativeCompatibilitySummaryCount': sum(
                record.get('Kind') == 'summary'
                for _, record in second['compatibility']),
            'FullPurchaseUsePersistenceVerified': False,
            'DiagnosticsPurchaseUseSaveLoadVerified': False,
            'Limits': [
                'Checks the selected object group and one action, not every object on the lot.',
                'An action-start event plus a later active avatar sample proves execution reached that interaction; it does not prove every intended animation or effect completed.',
                'The full gate requires session_smoke result.json to bind the diagnostics to the House FSOV, protected source hashes, profile, process lifecycle, and VM errors.',
            ],
        }
        report['Lifecycle'] = lifecycle_checks(
            args.session_result, args.firstlog, args.secondlog,
            selected['save_complete'][1] if selected['save_complete'] else None,
            selected['deserialize_before_load'][1]
            if selected['deserialize_before_load'] else None)
        if pair is not None:
            (charge_line, charge), (placed_line, placed) = pair
            pieces = placed['Pieces']
            ids = {piece['ObjectID'] for piece in pieces}
            report['Purchase'] = {
                'ChargedLine': charge_line, 'PlacedLine': placed_line,
                'GUID': placed['GUID'], 'Value': charge['Value'],
                'FamilyBudgetBefore': charge['FamilyBudgetBefore'],
                'FamilyBudgetAfter': charge['FamilyBudgetAfter'],
                'PieceIDs': sorted(ids),
                'Pieces': pieces,
            }
            state_checks = {}
            for phase in SAVE_PHASES + LOAD_PHASES:
                record = selected[phase]
                state_checks[phase] = (state_piece_checks(record[1], pieces,
                                       charge['FamilyBudgetAfter'], charge['Value'])
                                       if record is not None else None)
            report['StatePieceChecks'] = state_checks

            before_save = selected['before_save']
            report['ActionSamples'] = (action_proof(
                first['action'], first['avatar'], ids, placed_line,
                before_save[0], args.require_action_name,
                args.require_posture, args.require_comfort_gain)
                if before_save is not None else [])
            report['ActionStartedEventsForPieces'] = [
                {'Line': line, 'ActorObjectID': event.get('ActorObjectID'),
                 'CalleeID': event.get('CalleeID'),
                 'ActionName': event.get('ActionName'),
                 'Tick': event.get('Tick')}
                for line, event in first['action']
                if placed_line < line < (before_save[0] if before_save else float('inf'))
                and event.get('CalleeID') in ids]
            report['ActionProofPassed'] = any(
                item['Passed'] for item in report['ActionSamples'])
            boundaries = (
                ('before_save_to_after_serialize', 'before_save', 'after_serialize'),
                ('after_serialize_to_save_complete', 'after_serialize', 'save_complete'),
                ('save_complete_to_deserialize', 'save_complete', 'deserialize_before_load'),
                ('deserialize_to_after_vm_load', 'deserialize_before_load', 'after_vm_load'),
            )
            comparisons = {}
            for name, left, right in boundaries:
                comparisons[name] = (compare(selected[left][1], selected[right][1])
                                     if selected[left] is not None
                                     and selected[right] is not None else None)
            report['SaveLoadComparisons'] = comparisons
            report['DiagnosticsPurchaseUseSaveLoadVerified'] = (
                all(record is not None for record in selected.values())
                and all(check is not None and not check['Errors']
                        for check in state_checks.values())
                and report['ActionProofPassed']
                and all(comparison is not None
                        and comparison['UnexpectedDifferenceCount'] == 0
                        for comparison in comparisons.values())
                and report['SecondNativeCompatibilitySummaryCount'] == 0
                and all(phase_order.values())
                and before_save is not None
                and placed_line < before_save[0])
            report['FullPurchaseUsePersistenceVerified'] = (
                report['DiagnosticsPurchaseUseSaveLoadVerified']
                and report['Lifecycle']['Passed'])
        print(json.dumps(report, indent=2, ensure_ascii=False))
        return 0 if report['FullPurchaseUsePersistenceVerified'] else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({'SchemaVersion': 1, 'InputError': str(error)}),
              file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
