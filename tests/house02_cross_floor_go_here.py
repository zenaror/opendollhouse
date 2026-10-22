#!/usr/bin/env python3
"""Exercise saved House02 Go Here from the lower to the upper floor."""
import argparse
import json
import pathlib
import subprocess
import tempfile

from house03_stair_approach import architecture_resources, patch_idle_main


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--game-root", required=True, type=pathlib.Path)
    parser.add_argument("--destination-tile", nargs=2, type=int, default=(25, 24),
        metavar=("X", "Y"), help="upper-floor destination tile (default: near the stair)")
    args = parser.parse_args()
    game = args.game_root.resolve()
    required = [args.binary, game / "GameData/Global/Global.far",
        game / "GameData/Objects/Objects.far", game / "GameData/floors.iff",
        game / "GameData/Animation/Animation.far", game / "ExpansionShared/ExpansionShared.far",
        game / "UserData/Characters/User00000.iff", game / "UserData/Characters/User00001.iff",
        game / "UserData/Houses/House02.iff", game / "UserData/LotZoning.iff",
        game / "UserData/Neighborhood.iff"]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("required game/test file missing: " + ", ".join(missing))
    with tempfile.TemporaryDirectory(prefix="odh-house02-go-here-") as directory:
        avatar = pathlib.Path(directory) / "User00000-idle-main.iff"
        patch_idle_main(game / "UserData/Characters/User00000.iff", avatar)
        command = [str(args.binary.resolve()), "--avatar-file", str(avatar), "128",
            "--semiglobal", str(game / "GameData/Global/Global.far"), "PersonGlobals.iff",
            "--global", str(game / "GameData/Global/Global.far"), "Global.iff",
            "--lot", str(game / "UserData/Houses/House02.iff"), "2",
            "--zoning", str(game / "UserData/LotZoning.iff"),
            "--neighborhood", str(game / "UserData/Neighborhood.iff"),
            "--object-definitions", str(game / "GameData/Objects/Objects.far"),
            "--object-definition-file", str(game / "UserData/Characters/User00000.iff"),
            "--object-definition-file", str(game / "UserData/Characters/User00001.iff"),
            "--headless-main", "--render-floors", str(game / "GameData/floors.iff"),
            # [376,456] is adjacent to the saved stair, outside its footprint.
            "--headless-fixture-avatar", "30000", "376", "456", "1", "0",
            "--headless-fixture-main-arg0", "64",
            "--headless-go-here", str(args.destination_tile[0]), str(args.destination_tile[1]), "2", "1",
            "--age", "21", "--animation-source", str(game / "GameData/Animation/Animation.far"),
            "--animation-source", str(game / "ExpansionShared/ExpansionShared.far"), "--ticks", "1024"]
        for resource in architecture_resources(game):
            command += ["--architecture-resource", str(resource)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=240)
        if not result.stdout:
            raise RuntimeError(result.stderr.strip() or
                f"odh-person exited {result.returncode} without JSON")
        data = json.loads(result.stdout)
    route = data["route_runtime"]
    go_here = data["go_here_runtime"]
    if result.returncode != 0 or go_here["status"] != "returned_true" or go_here["result"] is not True:
        recent = [{key: item.get(key) for key in ("routine", "pc", "opcode", "operands", "owner")}
            for item in data.get("trace", [])[-16:]]
        raise AssertionError(f"Go Here did not complete: {go_here}; route={route}; trace={recent}")
    expected_position = [args.destination_tile[0] * 16 + 8,
        args.destination_tile[1] * 16 + 8, 2]
    if go_here["end_position"][:3] != expected_position:
        raise AssertionError(f"Go Here ended at the wrong floor/tile: {go_here}")
    stair_snap = any(item.get("routine") == 4113 and item.get("opcode") == 46
        for item in data.get("trace", []))
    if (not route["cross_room_portal_execution"] or route["portal_returns"] < 1 or
            route["portal_room_transitions"] < 1 or route["movement_callbacks"] < 2 or
            route["end_position"][2] != 2 or not stair_snap):
        raise AssertionError(f"route did not walk through the saved stair to floor 2: {route}")
    if not route["movement_positions_truncated"]:
        movement_levels = {position[2] for position in route["movement_positions"]}
        if movement_levels != {1, 2}:
            raise AssertionError(f"movement log does not show both floors: {route}")
    print(json.dumps({"evidence": "saved House02; synthetic avatar and idle Main",
        "start_position": go_here["start_position"], "end_position": go_here["end_position"],
        "status": go_here["status"], "movement_callbacks": route["movement_callbacks"],
        "portal_calls": route["portal_planned_calls"], "portal_returns": route["portal_returns"],
        "room_transitions": route["portal_room_transitions"], "stair_snap_executed": stair_snap,
        "movement_log_truncated": route["movement_positions_truncated"],
        "claim": "Go Here walked from the lower floor, executed the saved stair Snap, and reached the selected upper-floor tile in ODH; this is not proof of equivalence to the original executable"}, indent=2))


if __name__ == "__main__":
    main()
