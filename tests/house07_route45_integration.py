#!/usr/bin/env python3
"""Run a Route45 Main-host integration probe against an external TS1 install."""
import argparse
import json
import pathlib
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
    return parser.parse_args()


def patch_root_with_route45(source, destination, routine_id, literal_slot=False,
                            goto_relative=False):
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

    # One synthetic Main instruction calls Route45 for the selected live
    # StackObject. The original commercial IFF remains untouched.
    opcode = 27 if goto_relative else 45
    operands = ([0, 0, 254, 0, 0, 0, 2, 0] if goto_relative else
                [1, 0, 1, 0, 1, 0, 0, 0] if literal_slot else
                [1, 0, 2, 0, 0, 0, 0, 0])
    bhav = struct.pack("<HH8sHBB8s", 0x8002, 1,
                       bytes([0, 4, 0, 0, 0, 0, 0, 0]),
                       opcode, 254, 255, bytes(operands))
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
    if sum((args.literal_slot, args.goto_relative, args.portal_id is not None)) > 1:
        raise SystemExit("--literal-slot, --goto-relative, and --portal-id are mutually exclusive")

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
    architecture = sorted(game.rglob("*.flr")) + sorted(game.rglob("*.wll"))
    for path in architecture:
        command += ["--architecture-resource", str(path)]

    def run(avatar_path, stack_object_id=None, scheduled_move=None):
        invocation = command.copy()
        invocation[invocation.index("--avatar-file") + 1] = str(avatar_path)
        if stack_object_id is not None:
            invocation += ["--headless-stack-object", str(stack_object_id)]
        if scheduled_move is not None:
            entity_id, position, tick = scheduled_move
            invocation += ["--headless-move-entity", str(entity_id),
                           *(str(value) for value in position), str(tick)]
        if args.portal_id is not None:
            invocation += ["--headless-portal", str(args.portal_id), "1"]
        result = subprocess.run(invocation, check=False, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if not result.stdout.strip():
            raise RuntimeError(f"odh-person exited {result.returncode}: {result.stderr.strip()}")
        return result.returncode, json.loads(result.stdout)

    baseline_code, baseline = run(original_avatar)
    root_id = baseline["avatar"]["main"]
    if args.portal_id is not None:
        print(json.dumps({
            "probe_exit": baseline_code,
            "vm_status": baseline["status"],
            "vm_diagnostic": baseline["diagnostic"],
            "ticks": baseline["ticks"],
            "trace_tail": baseline["trace"][-12:],
            "portal_runtime": baseline.get("portal_runtime"),
            "evidence": "House07 restored World, live saved door, original avatar Main, and host-scheduled diagnostic Portal15",
        }, indent=2))
        return
    with tempfile.TemporaryDirectory(prefix="odh-house07-route45-") as temp:
        probe_avatar = pathlib.Path(temp) / "User00000-route45.iff"
        patch_root_with_route45(original_avatar, probe_avatar, root_id,
                                args.literal_slot, args.goto_relative)
        exit_code, report = run(probe_avatar, args.stack_object_id)
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
