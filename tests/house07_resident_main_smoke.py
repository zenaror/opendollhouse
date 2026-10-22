#!/usr/bin/env python3
"""Run each saved House07 resident's own Main for a bounded Linux smoke check."""
import argparse
import json
import pathlib
import subprocess


RESIDENTS = (
    (105, 850010198, "UserData/Characters/User00000.iff"),
    (137, 844195932, "UserData/Characters/User00001.iff"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--game-root", required=True, type=pathlib.Path)
    parser.add_argument("--ticks", type=int, default=32)
    args = parser.parse_args()
    if not 1 <= args.ticks <= 4096:
        parser.error("--ticks must be in 1..4096")

    game = args.game_root.resolve()
    global_archive = game / "GameData/Global/Global.far"
    character_files = [game / path for _, _, path in RESIDENTS]
    required = [
        args.binary.resolve(), global_archive,
        game / "GameData/Objects/Objects.far",
        game / "GameData/Animation/Animation.far",
        game / "ExpansionShared/ExpansionShared.far",
        game / "UserData/Houses/House07.iff",
        game / "UserData/LotZoning.iff",
        game / "UserData/Neighborhood.iff",
        game / "GameData/floors.iff", game / "GameData/walls.iff",
        *character_files,
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("required House07/game resource missing: " + ", ".join(missing))

    architecture = sorted(game.rglob("*.flr")) + sorted(game.rglob("*.wll"))
    residents = []
    for object_id, guid, relative_character in RESIDENTS:
        command = [
            str(args.binary.resolve()), "--avatar-file",
            str(game / relative_character), "128",
            "--semiglobal", str(global_archive), "PersonGlobals.iff",
            "--global", str(global_archive), "Global.iff",
            "--lot", str(game / "UserData/Houses/House07.iff"), "7",
            "--zoning", str(game / "UserData/LotZoning.iff"),
            "--neighborhood", str(game / "UserData/Neighborhood.iff"),
            "--activate-family",
            "--object-definitions", str(game / "GameData/Objects/Objects.far"),
            "--object-definition-file", str(character_files[0]),
            "--object-definition-file", str(character_files[1]),
            "--animation-source", str(game / "GameData/Animation/Animation.far"),
            "--animation-source", str(game / "ExpansionShared/ExpansionShared.far"),
            "--headless-main", "--render-saved-avatar", str(object_id),
            "--ticks", str(args.ticks), "--render-floors", str(game / "GameData/floors.iff"),
            "--render-walls", str(game / "GameData/walls.iff"),
        ]
        for path in architecture:
            command += ["--architecture-resource", str(path)]

        result = subprocess.run(command, check=False, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if not result.stdout.strip():
            raise RuntimeError(f"resident {object_id} exited {result.returncode}: {result.stderr.strip()}")
        report = json.loads(result.stdout)
        avatar = report["avatar"]
        traces = [entry for entry in report["trace"] if entry["owner"] == object_id]
        household = report["family_world_reconciliation"]
        member = next((entry for entry in household["members"] if entry["guid"] == guid), None)
        if (avatar["guid"] != guid or avatar["owner"] != object_id or
                avatar["file"] != str(game / relative_character) or
                avatar["root"] != avatar["main"] or not traces or
                member is None or member["object_id"] != object_id or
                member["presence"] != "present_with_position" or
                report["status"] not in ("waiting", "yielded") or
                report["ticks"] != args.ticks or report["host_failed"]):
            raise SystemExit(f"resident {object_id} did not pass its own Main smoke check")
        residents.append({
            "object_id": object_id,
            "guid": guid,
            "character_file": relative_character,
            "main_routine": avatar["main"],
            "trace_owner": object_id,
            "trace_steps": len(traces),
            "status": report["status"],
            "ticks": report["ticks"],
            "process_exit": result.returncode,
            "tick_budget_exhausted": report["tick_budget_exhausted"],
            "fault": False,
        })

    print(json.dumps({
        "evidence": "House07_real_saved_residents_run_separately_with_their_own_character_files",
        "simultaneous_execution": False,
        "residents": residents,
    }, indent=2))


if __name__ == "__main__":
    main()
