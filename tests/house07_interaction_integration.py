#!/usr/bin/env python3
"""Run a resource-backed Sit interaction against an external TS1 install."""
import argparse
import json
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--game-root", required=True, type=pathlib.Path)
    parser.add_argument("--avatar-id", type=int, default=105)
    parser.add_argument("--avatar-file", default="UserData/Characters/User00000.iff")
    parser.add_argument("--object-id", type=int, default=128)
    parser.add_argument("--ticks", type=int, default=512)
    args = parser.parse_args()

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
        "--object-definitions", str(game / "GameData/Objects/Objects.far"),
        "--object-definition-file", str(game / "UserData/Characters/User00000.iff"),
        "--object-definition-file", str(game / "UserData/Characters/User00001.iff"),
        "--animation-source", str(game / "GameData/Animation/Animation.far"),
        "--animation-source", str(expansion_shared),
        "--headless-main", "--render-saved-avatar", str(args.avatar_id),
        "--headless-interaction", str(args.object_id), "1",
        "--ticks", str(args.ticks), "--render-floors", str(game / "GameData/floors.iff"),
        "--render-walls", str(game / "GameData/walls.iff"),
    ]
    for path in architecture:
        command += ["--architecture-resource", str(path)]

    result = subprocess.run(command, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if not result.stdout.strip():
        raise RuntimeError(f"odh-person exited {result.returncode}: {result.stderr.strip()}")
    report = json.loads(result.stdout)
    interaction = report["interaction_runtime"]
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
    actor = interaction["actor"]
    evidence = {
        "process_exit": result.returncode,
        "main_status": report["status"],
        "main_diagnostic": report["diagnostic"],
        "ticks": report["ticks"],
        "interaction": interaction,
        "completion": report["completion"],
        "action_routines": sorted(action_routines),
        "action_opcodes": sorted(action_opcodes),
        "main_same_tile_queries": main_entity_next,
        "main_run_tree_by_name": main_run_tree_by_name,
        "run_tree_false_continued": run_tree_false_continued,
        "main_trace_tail": main_trace_tail,
    }
    print(json.dumps(evidence, indent=2))

    completed_sit = (
        interaction["trigger"] == "headless_schedule" and interaction["enqueued"] and
        interaction["status"] == "returned_true" and interaction["result"] is True and
        interaction["queue_size"] == 0 and not interaction["queue_active"] and
        interaction["action"] == 4097 and interaction["scheduled_target"] == args.object_id and
        actor["attachment"] is not None and actor["attachment"]["container"] == args.object_id and
        actor["attachment"]["positioned"] and
        report["completion"] is not None and report["completion"]["result"] is True and
        {4097, 4100, 4099}.issubset(action_routines) and {44, 45, 46, 49}.issubset(action_opcodes) and
        bool(main_entity_next) and report["ticks"] == args.ticks and
        bool(main_run_tree_by_name) and run_tree_false_continued and
        report["status"] in ("waiting", "yielded") and
        (args.ticks < 512 or report["status"] == "waiting") and
        not report["host_failed"]
    )
    if not completed_sit:
        raise SystemExit("House07 Sit did not complete with the avatar attached to its target slot")


if __name__ == "__main__":
    main()
