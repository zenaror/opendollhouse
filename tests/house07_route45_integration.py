#!/usr/bin/env python3
"""Run a Route45 Main-host integration probe against an external TS1 install."""
import argparse
import json
import os
import pathlib
import sys
import struct
import subprocess
import tempfile


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--game-root", required=True, type=pathlib.Path)
    parser.add_argument("--avatar-id", type=int, default=105)
    parser.add_argument("--avatar-file", default="UserData/Characters/User00000.iff")
    parser.add_argument("--stack-object-id", type=int, default=18)
    parser.add_argument("--mutation-entity-id", type=int, default=106)
    parser.add_argument("--ticks", type=int, default=214)
    parser.add_argument("--literal-slot", action="store_true",
                        help="route through the selected live StackObject's OBJD SLOT")
    parser.add_argument("--goto-relative", action="store_true",
                        help="diagnose opcode27 against the restored StackObject and live route grid")
    parser.add_argument("--portal-id", type=int,
                        help="schedule saved Portal15 on a restored House07 portal, using the original Main")
    parser.add_argument("--go-here", nargs=3, type=int, metavar=("TILE_X", "TILE_Y", "LEVEL"),
                        help="schedule saved Destination GUID 0x7C4 BHAV4096 at a ground tile, using the original Main")
    parser.add_argument("--go-here-expect-status",
                        choices=("returned_true", "returned_false", "tick_limit", "host_failed", "vm_fault"),
                        default="returned_true",
                        help="expected terminal state for --go-here diagnostics")
    parser.add_argument("--go-here-expect-route-failure", action="store_true",
                        help="assert that saved Go Here runs its failure callback and leaves the avatar still")
    parser.add_argument("--second-route-room-refresh", action="store_true",
                        help="run synthetic Main: reach StackObject26, then validate two Route45 SLOT orders on room 1's refreshed grid")
    parser.add_argument("--cross-room-entrypoint", action="store_true",
                        help="move the saved avatar to room 1, then use Main and Door26's real EntryPoint15 to route back to room 3")
    parser.add_argument("--expect-route-goal", nargs=2, type=int, metavar=("X", "Y"),
                        help="assert that Route45 reached this XY before the scheduled portal BHAV continues")
    parser.add_argument("--architecture-resource", action="append", type=pathlib.Path, default=[],
                        help="additional loose .flr/.wll resource; may be repeated")
    return parser.parse_args()


def patch_root_with_route45(source, destination, routine_id, literal_slot=False,
                            goto_relative=False, second_route_room_refresh=False,
                            cross_room_entrypoint=False):
    data = source.read_bytes()
    if len(data) < 64:
        raise ValueError("avatar IFF is shorter than its header")
    chunks = []
    position = 64
    while position < len(data):
        if position + 76 > len(data):
            raise ValueError(f"truncated IFF chunk header at {position}")
        length = struct.unpack_from(">I", data, position + 4)[0]
        if length < 76 or position + length > len(data):
            raise ValueError(f"invalid IFF chunk length {length} at {position}")
        chunks.append(bytearray(data[position:position + length]))
        position += length

    # Synthetic Main instructions only replace the temporary avatar copy;
    # the commercial IFF is read-only and remains untouched.
    metadata = bytes([0, 4, 0, 0, 0, 0, 0, 0])
    if second_route_room_refresh:
        instructions = [
            (27, 1, 255, [0, 0, 254, 0, 0, 0, 2, 0]),
            (45, 2, 255, [0, 0, 1, 0, 1, 0, 0, 0]),
            (45, 254, 255, [0, 0, 1, 0, 1, 0, 0, 0]),
        ]
    elif cross_room_entrypoint:
        # Set Main argument 0, then Sleep one tick so the host can place the
        # avatar in room 1 before Route45 enters Door26's real EntryPoint15.
        instructions = [
            (2, 1, 255, [0, 0, 1, 0, 0, 5, 9, 7]),
            (0, 2, 255, [0, 0, 0, 0, 0, 0, 0, 0]),
            (45, 254, 255, [2, 0, 1, 0, 1, 0, 0, 0]),
        ]
    else:
        opcode = 27 if goto_relative else 45
        operands = ([0, 0, 254, 0, 0, 0, 2, 0] if goto_relative else
                    [1, 0, 1, 0, 1, 0, 0, 0] if literal_slot else
                    [1, 0, 2, 0, 0, 0, 0, 0])
        instructions = [(opcode, 254, 255, operands)]
    bhav = struct.pack("<HH8s", 0x8002, len(instructions), metadata) + b"".join(
        struct.pack("<HBB8s", opcode, on_true, on_false, bytes(operands))
        for opcode, on_true, on_false, operands in instructions)
    replaced = 0
    output = bytearray(data[:64])
    for chunk in chunks:
        if (chunk[:4] == b"BHAV" and
                struct.unpack_from(">H", chunk, 8)[0] == routine_id):
            chunk = bytearray(chunk[:76] + bhav)
            struct.pack_into(">I", chunk, 4, len(chunk))
            replaced += 1
        output.extend(chunk)
    if replaced != 1:
        raise ValueError(f"expected one Main BHAV {routine_id}, replaced {replaced}")
    destination.write_bytes(output)


def main():
    args = parse_args()
    game = args.game_root.resolve()
    original_avatar = game / args.avatar_file
    object_archive = game / "GameData/Objects/Objects.far"
    global_archive = game / "GameData/Global/Global.far"
    floor_file = game / "GameData/floors.iff"
    house = game / "UserData/Houses/House07.iff"
    zoning = game / "UserData/LotZoning.iff"
    neighborhood = game / "UserData/Neighborhood.iff"
    required = [args.binary, original_avatar, object_archive, global_archive,
                floor_file, house, zoning, neighborhood]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("required game/test file missing: " + ", ".join(missing))
    if not 1 <= args.avatar_id <= 32767 or not 1 <= args.stack_object_id <= 32767:
        raise SystemExit("avatar and StackObject IDs must be in 1..32767")
    if not 1 <= args.ticks <= 4096:
        raise SystemExit("ticks must be in 1..4096")
    if sum((args.literal_slot, args.goto_relative, args.portal_id is not None,
            args.second_route_room_refresh, args.cross_room_entrypoint,
            args.go_here is not None)) > 1:
        raise SystemExit("route diagnostic modes are mutually exclusive")
    if (args.second_route_room_refresh or args.cross_room_entrypoint) and args.stack_object_id != 26:
        raise SystemExit("the House07 portal route modes require --stack-object-id 26")
    if args.expect_route_goal is not None and args.portal_id is None:
        raise SystemExit("--expect-route-goal requires --portal-id")
    if args.expect_route_goal is not None and args.go_here is not None:
        raise SystemExit("--expect-route-goal cannot be combined with --go-here")

    object_files = [game / "UserData/Characters/User00000.iff",
                    game / "UserData/Characters/User00001.iff"]
    animations = [game / "GameData/Animation/Animation.far",
                  game / "ExpansionShared/ExpansionShared.far"]
    missing = [str(path) for path in object_files + animations if not path.is_file()]
    if missing:
        raise SystemExit("required character/animation file missing: " + ", ".join(missing))

    command = [str(args.binary.resolve()), "--avatar-file", str(original_avatar), "128",
               "--semiglobal", str(global_archive), "PersonGlobals.iff",
               "--global", str(global_archive), "Global.iff",
               "--lot", str(house), "7", "--zoning", str(zoning),
               "--neighborhood", str(neighborhood),
               "--object-definitions", str(object_archive)]
    for path in object_files:
        command += ["--object-definition-file", str(path)]
    for path in animations:
        command += ["--animation-source", str(path)]
    command += ["--headless-main", "--render-saved-avatar", str(args.avatar_id),
                "--ticks", str(args.ticks), "--render-floors", str(floor_file)]
    profile = os.environ.get("ODH_PROFILE_STARTUP")
    architecture_dirs = [game / "GameData" / "Floors", game / "GameData" / "Walls"]
    for root in [game / "Deluxe", game / "ExpansionShared", *game.glob("ExpansionPack*")]:
        architecture_dirs.extend(root / relative for relative in (
            pathlib.Path("Floors"), pathlib.Path("Walls"),
            pathlib.Path("GameData/Floors"), pathlib.Path("GameData/Walls")))
    if profile:
        print("[house07-probe] scanning canonical architecture directories",
              file=sys.stderr, flush=True)
    architecture = {path for directory in architecture_dirs if directory.is_dir()
                    for path in directory.glob("*")
                    if path.is_file() and path.suffix.lower() in {".flr", ".wll"}}
    architecture.update(args.architecture_resource)
    architecture = sorted(architecture)
    if profile:
        print(f"[house07-probe] architecture catalog inputs: {len(architecture)} files",
              file=sys.stderr, flush=True)
    for path in architecture:
        command += ["--architecture-resource", str(path)]

    def run(avatar_path, stack_object_id=None, scheduled_move=None):
        invocation = command.copy()
        invocation[invocation.index("--avatar-file") + 1] = str(avatar_path)
        if stack_object_id is not None:
            invocation += ["--headless-stack-object", str(stack_object_id)]
            if args.cross_room_entrypoint:
                invocation += ["--headless-move-entity", str(args.avatar_id),
                               "320", "320", "1", "3", "1"]
        if scheduled_move is not None:
            entity_id, position, tick = scheduled_move
            invocation += ["--headless-move-entity", str(entity_id),
                           *(str(value) for value in position), str(tick)]
        if args.portal_id is not None:
            invocation += ["--headless-portal", str(args.portal_id), "1"]
        if args.go_here is not None:
            invocation += ["--headless-go-here", *(str(value) for value in args.go_here), "1"]
        profile = os.environ.get("ODH_PROFILE_STARTUP")
        if profile:
            print(f"[house07-probe] starting odh-person with ODH_PROFILE_STARTUP={profile}",
                  file=sys.stderr, flush=True)
        result = subprocess.run(invocation, check=False, text=True,
                                stdout=subprocess.PIPE,
                                stderr=None if profile else subprocess.PIPE)
        if not result.stdout.strip():
            diagnostic = result.stderr.strip() if result.stderr else ""
            raise RuntimeError(f"odh-person exited {result.returncode}: {diagnostic}")
        return result.returncode, json.loads(result.stdout)

    baseline_code, baseline = run(original_avatar)
    root_id = baseline["avatar"]["main"]
    if args.go_here is not None:
        go_here = baseline.get("go_here_runtime") or {}
        route = baseline.get("route_runtime") or {}
        destination = [args.go_here[0] * 16 + 8, args.go_here[1] * 16 + 8,
                       args.go_here[2]]
        end = route.get("end_position") or []
        failure_tree_trace = [record for record in baseline.get("trace", [])
                              if record.get("routine") == 398]
        print(json.dumps({
            "probe_exit": baseline_code,
            "vm_status": baseline.get("status"),
            "host_failed": baseline.get("host_failed"),
            "go_here": go_here,
            "route_runtime": {key: route.get(key) for key in (
                "state", "start_position", "end_position", "active",
                "movement_callbacks", "diagnostic")},
            "goal_validation_diagnostic": route.get("goal_validation_diagnostic"),
            "global_398_trace": failure_tree_trace,
            "evidence": "House07 restored World and original Main; saved Destination BHAV4096; temporary UI-only marker",
        }, indent=2))
        expected_status = args.go_here_expect_status
        successful = expected_status == "returned_true"
        route_failure = args.go_here_expect_route_failure
        if (route_failure and expected_status != "returned_true"):
            raise SystemExit("--go-here-expect-route-failure requires action status returned_true")
        if (go_here.get("status") != expected_status or
                go_here.get("marker_alive") is not False or
                go_here.get("action") != 4096 or
                (successful and not route_failure and
                                 (baseline_code != 0 or baseline.get("host_failed") or
                                 go_here.get("result") is not True or
                                 go_here.get("route_status") != "arrived" or
                                 go_here.get("end_position", [])[:3] != destination or
                                 route.get("active") or route.get("movement_callbacks", 0) <= 0 or
                                 len(end) < 3 or end[:3] != destination or
                                 route.get("diagnostic") is not None)) or
                (route_failure and
                 (baseline.get("host_failed") or go_here.get("result") is not True or
                  go_here.get("route_status") != "failed" or
                  go_here.get("end_position") != go_here.get("start_position") or
                  go_here.get("queue_size") != 0 or go_here.get("queue_active") is not False or
                  route.get("active") or route.get("movement_callbacks", 0) != 0 or
                  route.get("diagnostic") != "GotoRelativePosition target rejected by host room/clearance validator" or
                  route.get("goal_validation_diagnostic") !=
                    "candidate lies outside the prepared room grid or inside an obstacle" or
                  not failure_tree_trace)) or
                (expected_status == "returned_false" and
                 (go_here.get("result") is not False or
                  go_here.get("end_position") != go_here.get("start_position") or
                  go_here.get("queue_size") != 0 or go_here.get("queue_active") is not False or
                  route.get("active") or route.get("movement_callbacks", 0) != 0 or
                  not failure_tree_trace)) or
                (expected_status == "tick_limit" and
                 (baseline_code == 0 or go_here.get("end_position") != go_here.get("start_position") or
                  route.get("movement_callbacks", 0) != 0))):
            raise SystemExit(f"House07 Go Here did not reach expected state {expected_status}")
        return
    if args.portal_id is not None:
        portal_definition = next((record for record in baseline.get("saved_definition_records", [])
                                  if record.get("object_id") == args.portal_id), None)
        portal_runtime = baseline.get("portal_runtime") or {}
        portal_owner = portal_runtime.get("code_owner")
        portal_trace = [record for record in baseline["trace"]
                        if record["owner"] == portal_owner]
        print(json.dumps({
            "probe_exit": baseline_code,
            "vm_status": baseline["status"],
            "vm_diagnostic": baseline["diagnostic"],
            "ticks": baseline["ticks"],
            "trace_tail": baseline["trace"][-12:],
            "portal_trace_count": len(portal_trace),
            "portal_trace": portal_trace[-256:],
            "portal_trace_truncated": len(portal_trace) > 256,
            "portal_trace_tail": portal_trace[-24:],
            "entity_next": baseline.get("entity_next"),
            "route_runtime": baseline.get("route_runtime"),
            "portal_runtime": baseline.get("portal_runtime"),
            "portal_definition": portal_definition,
            "evidence": "House07 restored World, live saved door, original avatar Main, and host-scheduled diagnostic Portal15",
        }, indent=2))
        if args.expect_route_goal is not None:
            route = baseline.get("route_runtime") or {}
            expected = args.expect_route_goal
            end = route.get("end_position") or []
            if (route.get("state") != "ready_dynamic_single_room_grid" or
                    route.get("active") or route.get("movement_callbacks", 0) <= 0 or
                    len(end) < 2 or end[:2] != expected or
                    route.get("diagnostic") is not None):
                raise SystemExit("House07 portal diagnostic did not reach the expected Route45 destination")
        return
    with tempfile.TemporaryDirectory(prefix="odh-house07-route45-") as temp:
        probe_avatar = pathlib.Path(temp) / "User00000-route45.iff"
        patch_root_with_route45(original_avatar, probe_avatar, root_id,
                                args.literal_slot, args.goto_relative,
                                args.second_route_room_refresh,
                                args.cross_room_entrypoint)
        exit_code, report = run(probe_avatar, args.stack_object_id)
        if args.cross_room_entrypoint:
            route = report.get("route_runtime") or {}
            main_trace = [record for record in report.get("trace", [])
                          if record.get("phase") == "root_probe" and
                          record.get("routine") == root_id]
            actual_instructions = [(record.get("pc"), record.get("opcode"))
                                   for record in main_trace]
            expected_instructions = [(0, 2), (1, 0), (2, 45)]
            reached_door = any(position[:2] == [408, 408]
                               for position in route.get("movement_positions", []))
            scheduled_move = route.get("scheduled_world_mutation") or {}
            route_summary = {key: route.get(key) for key in (
                "state", "room_id", "start_position", "end_position", "active",
                "movement_callbacks", "snapshot_refreshes", "cross_room_portal_execution",
                "portal_planned_calls", "portal_returns", "portal_room_transitions",
                "diagnostic")}
            print(json.dumps({
                "main_routine": root_id,
                "probe_exit": exit_code,
                "vm_status": report.get("status"),
                "vm_diagnostic": report.get("diagnostic"),
                "host_failed": report.get("host_failed"),
                "host_diagnostic": report.get("host_diagnostic"),
                "main_instructions": actual_instructions,
                "route_runtime": route_summary,
                "scheduled_start_position": scheduled_move.get("position"),
                "reached_door_approach": reached_door,
                "evidence": "House07 saved World; synthetic Main and starting position; original Door26 EntryPoint15 through ordinary Route45 room scheduler",
            }, indent=2))
            if (exit_code != 0 or report.get("status") != "returned_true" or
                    report.get("host_failed") or report.get("host_diagnostic") or
                    actual_instructions != expected_instructions or
                    route.get("active") or route.get("diagnostic") is not None or
                    route.get("portal_planned_calls", 0) < 1 or
                    route.get("portal_returns", 0) < 1 or
                    route.get("portal_room_transitions", 0) < 1 or
                    route.get("cross_room_portal_execution") is not True or
                    route.get("room_id") != 3 or
                    route.get("snapshot_refreshes", 0) < 1 or
                    route.get("movement_callbacks", 0) <= 0 or
                    not route.get("scheduled_world_mutation_applied") or
                    scheduled_move.get("entity") != args.avatar_id or
                    scheduled_move.get("position", [])[:2] != [320, 320] or
                    not reached_door):
                raise SystemExit("House07 ordinary EntryPoint15 room-crossing probe failed")
            return
        if args.second_route_room_refresh:
            route = report.get("route_runtime") or {}
            main_trace = [record for record in report.get("trace", [])
                          if record.get("phase") == "root_probe" and
                          record.get("routine") == root_id]
            actual_instructions = [(record.get("pc"), record.get("opcode"))
                                   for record in main_trace]
            expected_instructions = [(0, 27), (1, 45), (2, 45)]
            reached_door = any(position[:2] == [408, 408]
                               for position in route.get("movement_positions", []))
            print(json.dumps({
                "main_routine": root_id,
                "probe_exit": exit_code,
                "vm_status": report.get("status"),
                "vm_diagnostic": report.get("diagnostic"),
                "host_failed": report.get("host_failed"),
                "host_diagnostic": report.get("host_diagnostic"),
                "main_instructions": actual_instructions,
                "route_runtime": route,
                "reached_door_goal": reached_door,
                "evidence": "House07 saved World and live StackObject26; synthetic Main; no commercial resource writes",
            }, indent=2))
            if (exit_code != 0 or report.get("status") != "returned_true" or
                    report.get("host_failed") or report.get("host_diagnostic") or
                    actual_instructions != expected_instructions or not reached_door or
                    route.get("active") or route.get("diagnostic") is not None or
                    route.get("room_id") != 1 or route.get("grid_windowed") is not True or
                    route.get("stackobject_slot_resolutions", 0) < 2):
                raise SystemExit("House07 second-route room-grid validation probe failed")
            return
        if args.goto_relative:
            route = report["route_runtime"]
            print(json.dumps({
                "main_routine": root_id,
                "probe_exit": exit_code,
                "vm_status": report["status"],
                "vm_diagnostic": report["diagnostic"],
                "ticks": report["ticks"],
                "trace_tail": report["trace"][-12:],
                "route_runtime": route,
                "route_scope": "GotoRelativePosition OnTopOf/SameDirection/NoFailureTrees",
                "evidence": "restored House07 World and live room grid; injected Main is synthetic",
            }, indent=2))
            return
        movement_positions = report["route_runtime"]["movement_positions"]
        if len(movement_positions) < 2:
            raise SystemExit("successful House07 route produced fewer than two movement samples")
        scheduled_move = (args.mutation_entity_id, movement_positions[1], 2)
        blocked_code, blocked_report = run(probe_avatar, args.stack_object_id, scheduled_move)

    route = report["route_runtime"]
    start, end = route["start_position"], route["end_position"]
    blocked_route = blocked_report["route_runtime"]
    evidence = {
        "main_routine": root_id,
        "baseline_exit": baseline_code,
        "probe_exit": exit_code,
        "vm_status": report["status"],
        "ticks": report["ticks"],
        "route_runtime": route,
        "obstacle_mutation_exit": blocked_code,
        "obstacle_mutation_vm_status": blocked_report["status"],
        "obstacle_mutation": blocked_route,
        "route_scope": "StackObject Literal" if args.literal_slot else "Global",
    }
    print(json.dumps(evidence, indent=2))
    if (exit_code != 0 or report["status"] != "returned_true" or
            route["state"] != "ready_dynamic_single_room_grid" or
            route["movement_callbacks"] <= 0 or
            route["snapshot_refreshes"] <= 0 or
            route["snapshot_refresh_policy"] != "on_world_spatial_revision_change" or
            not route["dynamic_obstacle_refresh"] or route["active"] or start == end):
        raise SystemExit("House07 Route45 integration probe failed its movement/ticket checks")
    if args.literal_slot and (route["stackobject_slot_resolutions"] != 1 or
                              route["stackobject_slot_loads"] != 1):
        raise SystemExit("House07 StackObject Literal probe did not resolve one live OBJD SLOT")
    if (blocked_code != 0 or blocked_report["status"] != "returned_false" or
            blocked_route["movement_callbacks"] != 1 or
            blocked_route["snapshot_refreshes"] < 2 or
            not blocked_route["scheduled_world_mutation_applied"] or
            blocked_route["active"] or
            blocked_route["end_position"] != blocked_route["movement_positions"][0] or
            blocked_route["diagnostic"] != "next segment blocked during route"):
        raise SystemExit("House07 moving-obstacle probe failed to refresh and cancel the blocked route")


if __name__ == "__main__":
    main()
