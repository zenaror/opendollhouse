#!/usr/bin/env python3
"""Open a selected neighborhood house in the current Linux lot preview host."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


class LaunchError(Exception):
    pass


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise LaunchError(f"required game file is missing: {path}")
    return path.resolve()


def run_json(command, label):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        raise LaunchError(f"{label} failed: {detail}")
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise LaunchError(f"{label} returned invalid JSON: {error}") from error
    if not isinstance(value, dict):
        raise LaunchError(f"{label} returned an unexpected JSON value")
    return value


def build_person_command(args, game, house, session):
    userdata = game / "UserData"
    gamedata = game / "GameData"
    global_far = require_file(gamedata / "Global/Global.far")
    objects_far = require_file(gamedata / "Objects/Objects.far")
    animation_far = require_file(gamedata / "Animation/Animation.far")
    textures_far = require_file(gamedata / "Textures/Textures.far")
    floors = require_file(gamedata / "floors.iff")
    walls = require_file(gamedata / "walls.iff")
    zoning = require_file(userdata / "LotZoning.iff")
    neighborhood = require_file(userdata / "Neighborhood.iff")
    house_file = require_file(userdata / "Houses" / f"House{house:02d}.iff")
    shared_archives = sorted(game.rglob("ExpansionShared.far"))
    if not shared_archives:
        raise LaunchError("no ExpansionShared.far was found in the selected game data")

    members = session.get("members")
    if not isinstance(members, list) or not members:
        raise LaunchError("selected family has no resolved members")
    if args.resident_id is None:
        resident = members[0]
    else:
        resident = next((m for m in members if m.get("neighbor_id") == args.resident_id), None)
        if resident is None:
            raise LaunchError(f"resident {args.resident_id} is not in the selected family")

    characters = (userdata / "Characters").resolve()
    character_files = []
    for member in members:
        asset = member.get("character_asset")
        if not isinstance(asset, str) or not asset:
            raise LaunchError("family member has no character resource name")
        character = require_file(characters / asset)
        if character.parent != characters:
            raise LaunchError("character resource escaped UserData/Characters")
        if character not in character_files:
            character_files.append(character)
    selected_character = require_file(characters / resident["character_asset"])
    object_definition_id = resident.get("object_definition_id")
    neighbor_id = resident.get("neighbor_id")
    resident_guid = resident.get("guid")
    if not isinstance(object_definition_id, int) or not 1 <= object_definition_id <= 65535:
        raise LaunchError("selected resident has an invalid object definition ID")
    if not isinstance(neighbor_id, int) or not 1 <= neighbor_id <= 32767:
        raise LaunchError("selected resident has an invalid neighborhood ID")
    if not isinstance(resident_guid, int) or not 1 <= resident_guid <= 0xFFFFFFFF:
        raise LaunchError("selected resident has an invalid identity GUID")

    command = [str(args.binary_dir / "odh-person"),
        "--avatar-file", str(selected_character), str(object_definition_id),
        "--semiglobal", str(global_far), "PersonGlobals.iff",
        "--global", str(global_far), "Global.iff",
        "--lot", str(house_file), str(house),
        "--zoning", str(zoning), "--neighborhood", str(neighborhood),
        "--activate-family", "--object-definitions", str(objects_far)]
    for character in character_files:
        command += ["--object-definition-file", str(character)]
    command += ["--animation-source", str(animation_far)]
    for archive in shared_archives:
        command += ["--animation-source", str(archive.resolve())]
    command += ["--saved-world-main", "--render-window",
        "--render-saved-guid", str(resident_guid),
        "--render-animation-far", str(animation_far),
        "--render-textures", str(textures_far),
        "--render-skeleton", "adult-skeleton.cmx.bcf", "adult",
        "--render-floors", str(floors), "--render-walls", str(walls),
        "--render-level", "1", "--render-wall-mode", "up",
        "--render-terrain-theme", "0"]
    for path in sorted(game.rglob("*")):
        if path.is_file() and path.suffix.lower() in (".flr", ".wll"):
            command += ["--architecture-resource", str(path.resolve())]
    return command, resident


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", required=True, type=Path)
    parser.add_argument("--binary-dir", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build")
    parser.add_argument("--mode", required=True, type=int, choices=range(1, 8))
    parser.add_argument("--language", type=int, default=1, choices=range(1, 21))
    parser.add_argument("--house", type=int, choices=range(1, 99),
                        help="open this lot directly instead of showing the map")
    parser.add_argument("--resident-id", type=int,
                        help="neighborhood member ID to start; defaults to the first family member")
    parser.add_argument("--plan-only", action="store_true",
                        help="resolve the selected household and print the launch command")
    args = parser.parse_args()
    try:
        if args.resident_id is not None and not 1 <= args.resident_id <= 32767:
            raise LaunchError("--resident-id must be in 1..32767")
        game = args.game_root.resolve()
        binary_dir = args.binary_dir.resolve()
        args.binary_dir = binary_dir
        for name in ("odh-neighborhood-window", "odh-session-entry", "odh-person"):
            executable = binary_dir / name
            if not executable.is_file():
                raise LaunchError(f"required OpenDollhouse program is missing: {executable}")
        userdata = require_file(game / "UserData/Neighborhood.iff").parent
        if args.house is None:
            graphics = require_file(game / "UIGraphics/UIGraphics.far")
            selected = run_json([
                str(binary_dir / "odh-neighborhood-window"),
                "--user-data", str(userdata), "--ui-graphics", str(graphics),
                "--mode", str(args.mode)], "neighborhood picker")
            if selected.get("status") == "cancelled":
                print(json.dumps({"status": "cancelled", "stage": "neighborhood"}))
                return 0
            if selected.get("status") != "selected" or selected.get("mode") != args.mode:
                raise LaunchError("neighborhood picker did not return a selected house for this mode")
            house = selected.get("selected_house")
        else:
            house = args.house
        if not isinstance(house, int) or not 1 <= house <= 98:
            raise LaunchError("selected house is outside 1..98")
        session = run_json([
            str(binary_dir / "odh-session-entry"), "--user-data", str(userdata),
            "--mode", str(args.mode), "--house", str(house),
            "--language", str(args.language)], "selected household validation")
        if (session.get("evidence") != "session_entry_resolved_only" or
                session.get("house_number") != house or session.get("mode") != args.mode):
            raise LaunchError("selected household validation returned an unexpected lot")
        command, resident = build_person_command(args, game, house, session)
        if args.plan_only:
            print(json.dumps({"status": "ready", "house": house,
                "family_chunk_id": session["family_chunk_id"],
                "resident_guid": resident["guid"],
                "resident_neighbor_id": resident["neighbor_id"],
                "resident_asset": resident["character_asset"],
                "resident_count": len(session["members"]), "interactive": False,
                "command": command}, indent=2))
            return 0
        print(json.dumps({"status": "starting_lot_preview", "house": house,
            "family_chunk_id": session["family_chunk_id"],
            "resident_guid": resident["guid"],
            "resident_neighbor_id": resident["neighbor_id"],
            "resident_count": len(session["members"]),
            "limitations": ["map and lot use separate windows", "only the selected resident Main runs",
                            "the session is not saved when the window closes",
                            "family editing and return-to-neighborhood are not implemented"]}), flush=True)
        return subprocess.run(command, check=False).returncode
    except (OSError, LaunchError, KeyError, TypeError) as error:
        print(f"odh-play-linux: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
