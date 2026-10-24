#!/usr/bin/env python3
"""Opt-in House07 host check for one additional resident Go Here request.

This script requires an explicitly supplied TS1 game root and is deliberately
not registered with CTest. Its output is ODH host evidence, not GUI or game
equivalence evidence.
"""
import argparse
import json
from pathlib import Path
import subprocess

from house07_resident_main_smoke import RESIDENTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--resident-id', required=True, type=int,
        help='additional household resident ID; cannot be the primary resident')
    parser.add_argument('--destination-tile', nargs=2, type=int, default=(25, 27),
        metavar=('X', 'Y'))
    parser.add_argument('--tick', type=int, default=1)
    parser.add_argument('--ticks', type=int, default=512)
    parser.add_argument('--require-portal', action='store_true',
        help='require this request to complete at least one saved room portal transition')
    args = parser.parse_args()
    if not 1 <= args.tick <= args.ticks <= 4096:
        parser.error('require 1 <= --tick <= --ticks <= 4096')
    game = args.game_root.resolve()
    residents = list(RESIDENTS)
    selected = next((resident for resident in residents if resident[0] == args.resident_id), None)
    if selected is None:
        parser.error('--resident-id must identify a listed House07 household resident')
    resident_id = selected[0]
    primary_neighbor_id = 48 if resident_id == 137 else 47
    command = [str(args.binary.resolve()), '--game-root', str(game), '--mode', '1',
        '--house', '7', '--resident-id', str(primary_neighbor_id), '--language', '1',
        '--headless-ticks', str(args.ticks), '--headless-resident-go-here', str(resident_id),
        str(args.destination_tile[0]), str(args.destination_tile[1]), '1', str(args.tick)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=240)
    if not result.stdout.strip():
        raise RuntimeError(f'odh-person exited {result.returncode}: {result.stderr.strip()}')
    report = json.loads(result.stdout)
    member = next((item for item in report.get('household_main', {}).get('additional_residents', [])
        if item.get('entity_id') == resident_id), None)
    session = member.get('go_here') if member else None
    route = member.get('routing') if member else None
    target = [args.destination_tile[0] * 16 + 8, args.destination_tile[1] * 16 + 8, 1]
    crossed_portal = bool(route and route.get('portal_planned_calls', 0) > 0 and
        route.get('portal_returns', 0) > 0 and route.get('portal_room_transitions', 0) > 0)
    if (result.returncode != 0 or report.get('host_failed') or not session or
        session.get('status') != 'returned_true' or session.get('result') is not True or
        session.get('marker_alive') is not False or session.get('code_owner') is None or
        session.get('marker_id') is None or session.get('target_position', [])[:3] != target or
        session.get('start_position', [])[:3] == target or member.get('position', [])[:3] != target or
        not route or route.get('movement_callbacks', 0) <= 0 or
        (args.require_portal and not crossed_portal)):
        raise AssertionError(json.dumps({'returncode': result.returncode,
            'host_failed': report.get('host_failed'), 'resident': member,
            'diagnostic': report.get('host_diagnostic')}, indent=2))
    print(json.dumps({'evidence': 'ODH_House07_resident_Go_Here_host_CLI',
        'primary_neighbor_id': primary_neighbor_id, 'resident_id': resident_id, 'request': session,
        'resident_position': member['position'], 'routing': route, 'ticks': report.get('ticks'),
        'claim': 'Headless host request reached its target for this supplied House07 install' +
            (' after saved room-portal transition' if args.require_portal else '') +
            '; no GUI or original-game equivalence claim'}, indent=2))


if __name__ == '__main__':
    main()
