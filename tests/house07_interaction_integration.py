#!/usr/bin/env python3
"""Run resource-backed Sit then Stand through one saved-world session."""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--game-root", required=True, type=pathlib.Path)
    parser.add_argument("--avatar-id", type=int, default=105)
    parser.add_argument("--avatar-file", default="UserData/Characters/User00000.iff")
    parser.add_argument("--object-id", type=int, default=128)
    parser.add_argument("--stand-tick", type=int, default=270)
    parser.add_argument("--ticks", type=int, default=512)
    parser.add_argument("--window-input", action="store_true",
                        help="exercise the production click/pick/Space path with queued inputs and no display")
    parser.add_argument("--move-avatar-to", nargs=4, type=int,
                        metavar=("X", "Y", "LEVEL", "DIRECTION"),
                        help="schedule the saved avatar at this World position on tick 1")
    args = parser.parse_args()

    if args.move_avatar_to:
        x, y, level, direction = args.move_avatar_to
        if not (0 <= x <= 32767 and 0 <= y <= 32767 and
                1 <= level <= 2 and 0 <= direction <= 7):
            parser.error("--move-avatar-to is outside the supported World profile")

    game = args.game_root.resolve()
    expansion_shared = game / "ExpansionShared" / "ExpansionShared.far"
    architecture = sorted(game.rglob("*.flr")) + sorted(game.rglob("*.wll"))
    required = [
        args.binary.resolve(), game / args.avatar_file,
        game / "GameData/Global/Global.far",
        game / "GameData/Objects/Objects.far",
        game / "GameData/Animation/Animation.far", expansion_shared,
        game / "UserData/Houses/House07.iff", game / "UserData/LotZoning.iff",
        game / "UserData/Neighborhood.iff", game / "GameData/floors.iff",
        game / "GameData/walls.iff",
        game / "UserData/Characters/User00001.iff",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("required House07/game resource missing: " + ", ".join(missing))
    if not 1 <= args.ticks <= 4096:
        raise SystemExit("--ticks must be in 1..4096")

    global_archive = game / "GameData/Global/Global.far"
    command = [
        str(args.binary.resolve()), "--avatar-file", str(game / args.avatar_file), "128",
        "--semiglobal", str(global_archive), "PersonGlobals.iff",
        "--global", str(global_archive), "Global.iff",
        "--lot", str(game / "UserData/Houses/House07.iff"), "7",
        "--zoning", str(game / "UserData/LotZoning.iff"),
        "--neighborhood", str(game / "UserData/Neighborhood.iff"),
        "--activate-family",
        "--object-definitions", str(game / "GameData/Objects/Objects.far"),
        "--object-definition-file", str(game / "UserData/Characters/User00000.iff"),
        "--object-definition-file", str(game / "UserData/Characters/User00001.iff"),
        "--animation-source", str(game / "GameData/Animation/Animation.far"),
        "--animation-source", str(expansion_shared),
        "--headless-main", "--render-saved-avatar", str(args.avatar_id),
        "--ticks", str(args.ticks), "--render-floors", str(game / "GameData/floors.iff"),
        "--render-walls", str(game / "GameData/walls.iff"),
    ]
    frame_path = None
    if args.window_input:
        frame_fd, frame_name = tempfile.mkstemp(prefix="odh-house07-click-", suffix=".ppm")
        os.close(frame_fd)
        frame_path = pathlib.Path(frame_name)
        frame_path.unlink()
        command += ["--headless-window-interaction", str(args.object_id),
                    "--render-animation-far", str(game / "GameData/Animation/Animation.far"),
                    "--render-textures", str(game / "GameData/Textures/Textures.far"),
                    "--render-skeleton", "adult-skeleton.cmx.bcf", "adult",
                    "--render-frame", str(frame_path)]
    else:
        command += ["--headless-interaction", str(args.object_id), "1",
                    "--headless-stand", str(args.object_id), str(args.stand_tick)]
    if args.move_avatar_to:
        command += ["--headless-move-entity", str(args.avatar_id),
                    *(str(value) for value in args.move_avatar_to), "1"]
    for path in architecture:
        command += ["--architecture-resource", str(path)]

    result = subprocess.run(command, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if frame_path is not None:
        frame_path.unlink(missing_ok=True)
    if not result.stdout.strip():
        raise RuntimeError(f"odh-person exited {result.returncode}: {result.stderr.strip()}")
    report = json.loads(result.stdout)
    interaction = report["interaction_runtime"]
    stand = report["stand_runtime"]
    trace = report["trace"]
    action_trace = [entry for entry in trace if entry["owner"] >= 0x80000000]
    main_trace = [entry for entry in trace if entry["owner"] < 0x80000000]
    main_trace_tail = main_trace[-12:]
    main_run_tree_by_name = [entry for entry in main_trace if entry["opcode"] == 28]
    run_tree_false_continued = any(
        entry["false"] == following["pc"] and entry["routine"] == following["routine"]
        and entry["owner"] == following["owner"]
        for index, entry in enumerate(main_trace) if entry["opcode"] == 28
        for following in main_trace[index + 1:index + 2]
    )
    main_entity_next = [entry for entry in report["entity_next"]
                        if entry["owner"] < 0x80000000 and entry["mode"] == 8]
    action_routines = {entry["routine"] for entry in action_trace}
    action_opcodes = {entry["opcode"] for entry in action_trace}
    action_route_calls = [
        {key: entry[key] for key in ("step", "routine", "pc", "operands", "true", "false")}
        for entry in action_trace if entry["opcode"] == 45
    ]
    actor = interaction["actor"]
    route = report["route_runtime"]
    household = report["family_world_reconciliation"]
    reconciled = {
        member["guid"]: (member["object_id"], member["position"])
        for member in household["members"]
    }
    prefixes = report["saved_object_prefixes"]["records"]
    common_records = report["saved_common_records"]
    tail_records = report["saved_tail_records"]
    restore_records_consistent = (
        len(prefixes) == len(common_records) == len(tail_records) and
        all(tail["status"] == "decoded_known_reference_schema" for tail in tail_records) and
        report["saved_objm_container"]["known_reference_schema_decoded"] and
        report["saved_objm_container"]["world_entities_restored"]
    )
    evidence = {
        "process_exit": result.returncode,
        "main_status": report["status"],
        "main_diagnostic": report["diagnostic"],
        "ticks": report["ticks"],
        "route_runtime": report["route_runtime"],
        "route_movement_callbacks": route["movement_callbacks"],
        "route_start_position": route["start_position"],
        "route_end_position": route["end_position"],
        "saved_restore_records": {
            "prefixes": len(prefixes),
            "common": len(common_records),
            "tails": len(tail_records),
            "consistent": restore_records_consistent,
        },
        "family_world_reconciliation": household,
        "interaction": interaction,
        "stand": stand,
        "completion": report["completion"],
        "action_routines": sorted(action_routines),
        "action_opcodes": sorted(action_opcodes),
        "action_route_calls": action_route_calls,
        "main_same_tile_queries": main_entity_next,
        "main_run_tree_by_name": main_run_tree_by_name,
        "run_tree_false_continued": run_tree_false_continued,
        "main_trace_tail": main_trace_tail,
    }
    print(json.dumps(evidence, indent=2))

    if args.window_input:
        selection = report["render"]["object_selection"]
        completed_sit = (
            selection["clicks"] == 1 and selection["id"] == args.object_id and
            selection["interaction_requested"] and selection["interaction_action"] == 4097 and
            interaction["trigger"] == "headless_window_input" and interaction["enqueued"] and
            interaction["status"] == "returned_true" and interaction["result"] is True and
            actor["id"] == args.avatar_id and actor["attachment"] is not None and
            actor["attachment"]["container"] == args.object_id and
            interaction["queue_size"] == 0 and not interaction["queue_active"] and
            report["completion"] is not None and report["completion"]["result"] is True and
            report["status"] in ("waiting", "yielded") and not report["host_failed"]
        )
    else:
        completed_sit = (
        household["evidence"] == "restored_World_exact_avatar_GUID_match_only" and
        household["creates_or_moves_Sims"] is False and
        reconciled == {
            850010198: (105, {"x": 392, "y": 440, "level": 1, "direction": 3}),
            844195932: (137, {"x": 424, "y": 440, "level": 1, "direction": 3}),
        } and
        interaction["trigger"] in ("headless_schedule", "headless_sit_schedule") and interaction["enqueued"] and
        interaction["status"] == "returned_true" and interaction["result"] is True and
        interaction["queue_size"] == 0 and not interaction["queue_active"] and
        interaction["action"] == 4097 and interaction["scheduled_target"] == args.object_id and
        stand["precondition_posture"] == 1 and
        stand["precondition_attachment"] == args.object_id and
        report["completion"] is not None and report["completion"]["result"] is True and
        restore_records_consistent and
        {4097, 4100, 4099}.issubset(action_routines) and {44, 45, 46, 49}.issubset(action_opcodes) and
        bool(main_entity_next) and report["ticks"] == args.ticks and
        stand["precondition_met"] and stand["entry_point"] == 27 and
        stand["action"] == 4102 and stand["status"] == "returned_true" and
        stand["result"] is True and stand["actor_posture"] == 0 and
        not stand["actor_attached"] and 4102 in action_routines and
        report["status"] in ("waiting", "yielded") and
        (args.ticks < 512 or report["status"] == "waiting") and
        (not args.move_avatar_to or route["movement_callbacks"] > 0) and
        not report["host_failed"]
        )
    if not completed_sit:
        raise SystemExit("House07 interaction sequence did not pass its requested input path")


if __name__ == "__main__":
    main()
