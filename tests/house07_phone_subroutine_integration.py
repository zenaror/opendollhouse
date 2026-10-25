#!/usr/bin/env python3
"""Opt-in commercial-data probe: saved PhoneLine subroutine, not complete Pizza gameplay."""
import argparse
import hashlib
import json
import os
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
        "--headless-main", "--render-saved-avatar", "105", "--ticks", "1200",
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
        env = os.environ.copy()
        for variable, name in (("HOME", "home"), ("XDG_CONFIG_HOME", "config"),
                               ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
                               ("XDG_STATE_HOME", "state")):
            path = Path(directory) / name
            path.mkdir()
            env[variable] = str(path)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        command += ["--render-frame", str(Path(directory) / "frame.ppm")]
        waiting_result = subprocess.run(command, capture_output=True, text=True, timeout=300, env=env)
        if not waiting_result.stdout.strip():
            raise RuntimeError(f"waiting probe produced no JSON: exit {waiting_result.returncode}: {waiting_result.stderr}")
        waiting_report = json.loads(waiting_result.stdout)
        response_command = command[:-2] + ["--render-frame",
            str(Path(directory) / "response-frame.ppm"), "--headless-window-dialog-ok"]
        response_result = subprocess.run(response_command, capture_output=True, text=True, timeout=300, env=env)
    after = hashes()
    if before != after:
        raise RuntimeError("commercial save/character inputs changed")
    if not response_result.stdout.strip():
        raise RuntimeError(f"response probe produced no JSON: exit {response_result.returncode}: {response_result.stderr}")
    report = json.loads(response_result.stdout)
    phone = report["saved_phone_subroutines"]
    calls = phone["calls"]
    if not calls:
        raise RuntimeError("expected a saved PhoneLine update call")
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
    if (len(calls) != 2 or calls[1]["status"] != "fault" or calls[1]["steps"] != 22 or
            "NotifyOutOfIdle requires a scheduled Main" not in calls[1]["diagnostic"] or
            not calls[1]["trace"] or calls[1]["trace"][-1]["opcode"] != 49):
        raise RuntimeError("resumed phone flow did not stop at the audited NotifyOutOfIdle scheduler boundary")
    writes = phone["memory_writes"]
    if not writes or any(writes[0][key] != value for key, value in {
            "attribute_entity_id": 207, "scope": 0, "index": 0,
            "before": 0, "value": 2, "applied": True}.items()):
        raise RuntimeError("PhoneLine attribute write differs")
    relations = [row for row in report["relationship_records"] if row["caller"] == 207]
    if len(relations) < 2 or any(row["source"] != 207 or row["target"] != 136 or
            row["stack_object"] != 136 or row["value_before"] != 1 or not row["success"] for row in relations):
        raise RuntimeError("child relationship lookup did not use PhoneLine caller and telephone StackObject")
    call_index = next((index for index, event in enumerate(report["trace"])
                       if event["routine"] == 8197 and event["pc"] == 4), None)
    if (call_index is None or call_index + 1 >= len(report["trace"]) or
            (report["trace"][call_index + 1]["routine"], report["trace"][call_index + 1]["pc"]) != (8203, 7)):
        raise RuntimeError("calling avatar did not advance beyond RunTreeByName")
    headline = report["headless_headline_events"]
    expected_headline = [{"target": 105, "icon": 40, "caller": 105,
        "stack_object": 40, "owner": 2147483648, "routine": 8206, "pc": 3,
        "index": 1, "group": 7, "duration": 40, "type": 2, "flags": 1}]
    if not headline or any(event != expected_headline[0] for event in headline):
        raise RuntimeError("House07 SetBalloonHeadline context or parameters differ: " +
                           json.dumps(headline))
    dialog = next((event for event in report["trace"]
        if event["routine"] == 8312 and event["pc"] == 11), None)
    if (dialog is None or dialog["opcode"] != 36 or
            dialog["operands"] != [0, 0, 44, 0, 0, 0, 45, 4]):
        raise RuntimeError("House07 Pizza private-message request differs")
    waiting_dialog = waiting_report.get("blocking_dialog")
    if (waiting_result.returncode != 0 or waiting_report["host_failed"] or
            waiting_report["status"] != "waiting" or not waiting_report["queue_active"] or
            waiting_dialog is None or waiting_dialog["routine"] != 8312 or waiting_dialog["pc"] != 11 or
            waiting_dialog["title"] != "SimCity Pizza" or "$" not in waiting_dialog["message"]):
        raise RuntimeError("House07 private message did not remain open while the VM waited")
    resumed_past_dialog = any(event["routine"] == 8312 and event["step"] > dialog["step"]
                              for event in report["trace"])
    if (response_result.returncode != 3 or report["status"] != "fault" or report["ticks"] != 823 or
            not report["host_failed"] or "NotifyOutOfIdle requires a scheduled Main" not in report["host_diagnostic"] or
            report["blocking_dialog_responses"] != 1 or not report["headless_dialog_ok_sent"] or
            not resumed_past_dialog or not report["queue_active"] or report["queue_size"] != 1):
        raise RuntimeError("explicit dialog response did not resume the House07 phone action: " +
            json.dumps({"exit": response_result.returncode, "status": report["status"],
                "ticks": report["ticks"], "host_failed": report["host_failed"],
                "host_diagnostic": report["host_diagnostic"],
                "dialog_responses": report["blocking_dialog_responses"],
                "ok_sent": report["headless_dialog_ok_sent"],
                "resumed_past_dialog": resumed_past_dialog,
                "queue_active": report["queue_active"], "queue_size": report["queue_size"]}))
    print(json.dumps({"evidence": "House07_saved_PhoneLine_private_message_wait_and_response",
        "full_gameplay": False,
        "waiting_process_exit": waiting_result.returncode, "response_process_exit": response_result.returncode,
        "game_file_sha256_before": before, "game_file_sha256_after": after,
        "game_files_unchanged": True,
        "waiting": {"status": waiting_report["status"], "ticks": waiting_report["ticks"],
            "host_failed": waiting_report["host_failed"], "dialog": waiting_dialog},
        "after_response": {"status": report["status"], "ticks": report["ticks"],
            "host_failed": report["host_failed"], "host_diagnostic": report["host_diagnostic"],
            "dialog_responses": report["blocking_dialog_responses"], "next_dialog": report["blocking_dialog"]},
        "saved_phone_update": {"target_id": phone["target_id"], "main_status": phone["main_status"],
            "calls": [{"status": call["status"], "steps": call["steps"], "arguments": call["arguments"],
                "owner": call["owner"], "caller": call["caller"], "callee": call["callee"],
                "diagnostic": call["diagnostic"], "trace": call["trace"]}
                for call in calls],
            "memory_writes": phone["memory_writes"]}, "phone_relationship_reads": relations,
        "headless_headline_events": headline, "private_message_request": dialog,
        "phone_dialog_routine_trace": [event for event in report["trace"]
            if event["routine"] == 8312]}, indent=2))


if __name__ == "__main__":
    main()
