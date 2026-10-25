#!/usr/bin/env python3
"""Opt-in commercial-data probe: saved PhoneLine subroutine, not complete Pizza gameplay."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--game-root", required=True, type=Path)
    args = parser.parse_args()
    game = args.game_root.resolve()
    far = game / "GameData/Global/Global.far"
    protected = [game / path for path in (
        "UserData/Houses/House07.iff", "UserData/LotZoning.iff", "UserData/Neighborhood.iff",
        "UserData/Characters/User00000.iff", "UserData/Characters/User00001.iff")]

    def hashes():
        return {str(path.relative_to(game)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in protected}

    before = hashes()
    command = [str(args.binary.resolve()), "--avatar-file", str(protected[3]), "128",
        "--semiglobal", str(far), "PersonGlobals.iff", "--global", str(far), "Global.iff",
        "--lot", str(protected[0]), "7", "--zoning", str(protected[1]),
        "--neighborhood", str(protected[2]), "--activate-family",
        "--object-definitions", str(game / "GameData/Objects/Objects.far"),
        "--object-definition-file", str(protected[3]), "--object-definition-file", str(protected[4]),
        "--animation-source", str(game / "GameData/Animation/Animation.far"),
        "--animation-source", str(game / "ExpansionShared/ExpansionShared.far"),
        "--headless-main", "--render-saved-avatar", "105", "--ticks", "640",
        "--instruction-budget", "1000000", "--render-floors", str(game / "GameData/floors.iff"),
        "--render-walls", str(game / "GameData/walls.iff"),
        "--headless-window-interaction", "136", "--headless-window-menu-open", "0",
        "--headless-window-menu-select", "0",
        "--render-animation-far", str(game / "GameData/Animation/Animation.far"),
        "--render-textures", str(game / "GameData/Textures/Textures.far"),
        "--render-skeleton", "adult-skeleton.cmx.bcf", "adult", "--render-size", "320", "240"]
    for path in sorted(game.rglob("*")):
        if path.is_file() and path.suffix.lower() in (".flr", ".wll"):
            command += ["--architecture-resource", str(path)]
    with tempfile.TemporaryDirectory(prefix="odh-phone-subroutine-") as directory:
        command += ["--render-frame", str(Path(directory) / "frame.ppm")]
        result = subprocess.run(command, capture_output=True, text=True, timeout=300)
    after = hashes()
    if before != after:
        raise RuntimeError("commercial save/character inputs changed")
    if not result.stdout.strip():
        raise RuntimeError(f"probe produced no JSON: exit {result.returncode}: {result.stderr}")
    report = json.loads(result.stdout)
    phone = report["saved_phone_subroutines"]
    calls = phone["calls"]
    if len(calls) != 1:
        raise RuntimeError("expected one saved PhoneLine update call")
    call = calls[0]
    if (phone["target_id"] != 207 or phone["main_status"] != "dormant" or phone["main_steps"] != 0 or
            phone["main_scheduled"] or call["status"] != "returned_true" or call["steps"] != 23 or
            call["arguments"] != [2, 0, 0, 0] or call["caller"] != 207 or call["callee"] != 207 or
            call["stack_object"] != 207 or call["owner"] != 2933422533 or
            [(frame["routine"], frame["pc"]) for frame in phone["main_frames"]] != [(4096, 0), (280, 0)]):
        raise RuntimeError("saved PhoneLine context, result or dormant Main differs from the audited fixture")
    if (call["temps_before"][0] != 0 or call["temps_after"][0] != 1 or
            sum(event["routine"] == 4098 and event["pc"] == 0 for event in call["trace"]) != 2 or
            any(event["opcode"] == 49 for event in call["trace"])):
        raise RuntimeError("PhoneLine dependency/Temp/Notify trace differs")
    writes = phone["memory_writes"]
    if len(writes) != 1 or any(writes[0][key] != value for key, value in {
            "attribute_entity_id": 207, "scope": 0, "index": 0,
            "before": 0, "value": 2, "applied": True}.items()):
        raise RuntimeError("PhoneLine attribute write differs")
    relations = [row for row in report["relationship_records"] if row["caller"] == 207]
    if len(relations) != 2 or any(row["source"] != 207 or row["target"] != 136 or
            row["stack_object"] != 136 or row["value_before"] != 1 or not row["success"] for row in relations):
        raise RuntimeError("child relationship lookup did not use PhoneLine caller and telephone StackObject")
    call_index = next((index for index, event in enumerate(report["trace"])
                       if event["routine"] == 8197 and event["pc"] == 4), None)
    if (call_index is None or call_index + 1 >= len(report["trace"]) or
            (report["trace"][call_index + 1]["routine"], report["trace"][call_index + 1]["pc"]) != (8203, 7)):
        raise RuntimeError("calling avatar did not advance beyond RunTreeByName")
    # Deliberately assert the next unsupported boundary, never call this a
    # completed interaction or suppress the host fault in the emitted evidence.
    if (result.returncode != 3 or not report["host_failed"] or report["status"] != "fault" or
            report["ticks"] != 515 or "unsupported primitive 41" not in report["host_diagnostic"]):
        raise RuntimeError("partial Pizza boundary changed; review the complete trace")
    print(json.dumps({"evidence": "House07_saved_PhoneLine_subroutine_partial_Pizza",
        "full_gameplay": False, "command": command, "process_exit": result.returncode,
        "game_file_sha256_before": before, "game_file_sha256_after": after,
        "game_files_unchanged": True, "status": report["status"], "ticks": report["ticks"],
        "host_failed": report["host_failed"], "host_diagnostic": report["host_diagnostic"],
        "saved_phone_subroutines": phone, "phone_relationship_reads": relations,
        "phone_trace": [event for event in report["trace"] if 8192 <= event["routine"] <= 8400]}, indent=2))


if __name__ == "__main__":
    main()
