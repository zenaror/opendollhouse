#!/usr/bin/env python3
"""Headless Have A Snack integration check against an installed TS1 House07."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--game-root", required=True, type=Path)
    parser.add_argument("--avatar-id", type=int, default=105)
    parser.add_argument("--fridge-id", type=int, default=112)
    parser.add_argument("--ticks", type=int, default=1024)
    args = parser.parse_args()

    game = args.game_root.resolve()
    binary = args.binary.resolve()
    global_far = game / "GameData/Global/Global.far"
    required = [
        binary,
        game / "UserData/Characters/User00000.iff",
        game / "UserData/Characters/User00001.iff",
        game / "UserData/Houses/House07.iff",
        game / "UserData/LotZoning.iff",
        game / "UserData/Neighborhood.iff",
        global_far,
        game / "GameData/Objects/Objects.far",
        game / "GameData/Animation/Animation.far",
        game / "ExpansionShared/ExpansionShared.far",
        game / "GameData/floors.iff",
        game / "GameData/walls.iff",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        parser.error("required game resource missing: " + ", ".join(missing))
    if not 1 <= args.ticks <= 4096:
        parser.error("--ticks must be in 1..4096")

    command = [
        str(binary), "--avatar-file", str(game / "UserData/Characters/User00000.iff"), "128",
        "--semiglobal", str(global_far), "PersonGlobals.iff",
        "--global", str(global_far), "Global.iff",
        "--lot", str(game / "UserData/Houses/House07.iff"), "7",
        "--zoning", str(game / "UserData/LotZoning.iff"),
        "--neighborhood", str(game / "UserData/Neighborhood.iff"), "--activate-family",
        "--object-definitions", str(game / "GameData/Objects/Objects.far"),
        "--object-definition-file", str(game / "UserData/Characters/User00000.iff"),
        "--object-definition-file", str(game / "UserData/Characters/User00001.iff"),
        "--animation-source", str(game / "GameData/Animation/Animation.far"),
        "--animation-source", str(game / "ExpansionShared/ExpansionShared.far"),
        "--headless-main", "--render-saved-avatar", str(args.avatar_id),
        "--ticks", str(args.ticks),
        "--render-floors", str(game / "GameData/floors.iff"),
        "--render-walls", str(game / "GameData/walls.iff"),
        "--headless-interaction", str(args.fridge_id), "1",
    ]
    for path in sorted(game.rglob("*")):
        if path.is_file() and path.suffix.lower() in (".flr", ".wll"):
            command += ["--architecture-resource", str(path)]

    with tempfile.TemporaryDirectory(prefix="odh-house07-snack-") as temporary:
        temp = Path(temporary)
        env = os.environ.copy()
        for variable, name in (
            ("HOME", "home"), ("XDG_CONFIG_HOME", "config"),
            ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
            ("XDG_STATE_HOME", "state"),
        ):
            directory = temp / name
            directory.mkdir()
            env[variable] = str(directory)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        result = subprocess.run(command, text=True, capture_output=True,
                                env=env, timeout=180, check=False)

    if not result.stdout.strip():
        raise SystemExit(f"odh-person exited {result.returncode}: {result.stderr[-2000:]}")
    report = json.loads(result.stdout)
    interaction = report["interaction_runtime"]
    objects = report["create_object_instance"]
    food_action = [entry for entry in report["trace"]
                   if entry["routine"] == 4175 and entry["owner"] >= 0x80000000]
    create_index = next((index for index, entry in enumerate(food_action)
                         if entry["opcode"] == 42 and entry["operands"][4] == 6), None)
    if create_index is None or create_index + 1 >= len(food_action):
        raise SystemExit("Food BHAV4175 did not reach the OutOfWorld creation")
    create = food_action[create_index]
    next_step = food_action[create_index + 1]
    checks = {
        "host did not fault": not report["host_failed"],
        "interaction returned true": interaction["status"] == "returned_true" and interaction["result"] is True,
        "avatar queue drained": interaction["queue_size"] == 0 and not interaction["queue_active"],
        "Food followed the successful OutOfWorld branch": create["true"] == next_step["pc"] == 3,
        "temporary runtime objects were removed": not objects["runtime_objects"],
        "family was selected from this House07": report["family_activation"]["house_number"] == 7,
    }
    evidence = {
        "evidence": "headless_House07_Have_A_Snack",
        "process_exit": result.returncode,
        "ticks": report["ticks"],
        "tick_budget_exhausted": report["tick_budget_exhausted"],
        "checks": checks,
        "interaction": interaction,
        "create_object_instance": objects,
    }
    print(json.dumps(evidence, indent=2))
    if not all(checks.values()):
        raise SystemExit("House07 Have A Snack integration did not meet its runtime checks")


if __name__ == "__main__":
    main()
