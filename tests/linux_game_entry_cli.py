"""Synthetic integrated Linux entry checks; never copies commercial resources."""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]
checks = 0


def check(value, reason):
    global checks
    checks += 1
    assert value, f"check {checks} failed: {reason}"


def chunk(kind, ident, data):
    return kind + struct.pack(">IHH", 76 + len(data), ident, 0) + bytes(64) + data


def iff(chunks):
    signature = b"IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1"
    return signature.ljust(60, bytes(1)) + bytes(4) + b"".join(chunks)


def make_game(root):
    user = root / "UserData"
    game = root / "GameData"
    (user / "Houses").mkdir(parents=True)
    (user / "Characters").mkdir()
    (game / "Global").mkdir(parents=True)

    locations = chunk(b"STR#", 1, struct.pack("<hH", -3, 1) + b"\1" +
                      b"7,10,20\0\0")
    family = chunk(b"FAMI", 201, struct.pack(
        "<II4siiiiiiI", 0, 9, b"IMAF", 7, 900, 100, 0, 0, 0, 1) +
        struct.pack("<I", 0x12345678))
    name = b"Sim0"
    neighbor = struct.pack("<II", 1, 4) + name + b"\0\0" + struct.pack("<ii", 0, 1)
    neighbor += struct.pack("<80h", *([0] * 80))
    neighbor += struct.pack("<hIii", 11, 0x12345678, -1, 0)
    neighbors = chunk(b"NBRS", 1,
                      struct.pack("<II4sI", 0, 62, b"SRBN", 1) + neighbor)
    simi_data = bytearray(12 + 32 * 2 + 34 + 6 * 4)
    struct.pack_into("<II", simi_data, 0, 0, 0x33)
    simi_data[8:12] = b"IMIS"
    simi = chunk(b"SIMI", 1, simi_data)
    objd = bytearray(32)
    struct.pack_into("<I", objd, 0, 138)
    struct.pack_into("<H", objd, 18, 2)
    struct.pack_into("<I", objd, 28, 0x12345678)

    (user / "LotLocations.iff").write_bytes(iff([locations]))
    (user / "Neighborhood.iff").write_bytes(iff([family, neighbors]))
    (user / "LotZoning.iff").write_bytes(b"synthetic zoning")
    (user / "Houses" / "House07.iff").write_bytes(iff([simi]))
    (user / "Characters" / "User00000.iff").write_bytes(iff([chunk(b"OBJD", 128, objd)]))
    for relative in [
        "Global/Global.far", "Objects/Objects.far", "Animation/Animation.far",
        "Textures/Textures.far", "floors.iff", "walls.iff",
    ]:
        path = game / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"synthetic resource")
    for relative in ["UIGraphics/UIGraphics.far", "ExpansionShared/ExpansionShared.far"]:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"synthetic resource")
    for pack in range(1, 8):
        directory = "ExpansionPack" if pack == 1 else f"ExpansionPack{pack}"
        path = root / directory / f"{directory}.far"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"synthetic expansion resource")
    (root / "Downloads" / "Sample").mkdir(parents=True)
    (root / "Downloads" / "Sample" / "Sample.far").write_bytes(b"synthetic download archive")
    (root / "Downloads" / "Loose.iff").write_bytes(b"synthetic download IFF")


with tempfile.TemporaryDirectory(prefix="odh-linux-game-entry-") as directory:
    base = Path(directory)
    root = base / "game"
    make_game(root)
    player_characters = base / "data" / "opendollhouse" / "Characters"
    player_characters.mkdir(parents=True)
    (root / "UserData" / "Characters" / "User00000.iff").replace(
        player_characters / "PlayerCreated.iff")
    env = os.environ.copy()
    env["XDG_CONFIG_HOME"] = str(base / "config")
    env["XDG_DATA_HOME"] = str(base / "data")
    env["XDG_STATE_HOME"] = str(base / "state")
    env.pop("ODH_STATE_DIR", None)

    command = [binary, "--game-root", str(root), "--mode", "1", "--house", "7",
               "--language", "15", "--plan-only"]
    run = subprocess.run(command, capture_output=True, text=True, env=env)
    check(run.returncode == 0, run.stderr)
    result = json.loads(run.stdout)
    check(result["status"] == "ready" and result["resident_count"] == 1,
          "synthetic family and lot resolve")
    check(result["resident_execution"] == "whole_household",
          "normal integrated launch selects the full household runtime")
    session_log_dir = base / "state" / "opendollhouse" / "logs"
    session_logs = list(session_log_dir.glob("*.log"))
    check(len(session_logs) == 1 and session_logs[0].name == "odh-person.log",
          "integrated plan uses one local session log")
    session_log = session_logs[0].read_text()
    check("stage=launcher-startup" in session_log and "stage=runtime-plan-ready" in session_log and
          "language=15" in session_log and "expansion_mask=127" in session_log and
          "session ended exit_code=0" in session_log and "Sim0" not in session_log and
          "family" not in session_log.lower(),
          "headless plan log records bounded launcher stage, selected locale, known expansion mask, and clean finish")
    check(not list(session_log_dir.glob("active-*")),
          "integrated plan removes its active marker on clean finish")
    budget_without_ticks = subprocess.run(
        [binary, "--game-root", str(root), "--mode", "1", "--house", "7",
         "--instruction-budget", "25000"], capture_output=True, text=True, env=env)
    check(budget_without_ticks.returncode == 2 and "requires a bounded whole-household run with --headless-ticks" in budget_without_ticks.stderr,
          "larger diagnostic instruction budgets require explicit headless ticks")
    budget_single_resident = subprocess.run(
        [binary, "--game-root", str(root), "--mode", "1", "--house", "7",
         "--single-resident-session", "--headless-ticks", "10", "--instruction-budget", "25000"],
        capture_output=True, text=True, env=env)
    check(budget_single_resident.returncode == 2 and "whole-household" in budget_single_resident.stderr,
          "larger diagnostic budgets are reserved for the complete household")
    budget_over_limit = subprocess.run(
        [binary, "--game-root", str(root), "--mode", "1", "--house", "7",
         "--headless-ticks", "10", "--instruction-budget", "1000001"],
        capture_output=True, text=True, env=env)
    check(budget_over_limit.returncode == 2 and "invalid instruction budget" in budget_over_limit.stderr,
          "integrated diagnostic budget has a one-million instruction ceiling")
    single = subprocess.run(command + ["--single-resident-session"], capture_output=True, text=True, env=env)
    check(single.returncode == 0 and
          json.loads(single.stdout)["resident_execution"] == "single_resident_session",
          "single resident continuation is an explicit diagnostic profile")
    # Execution and complete household persistence use the richer session CLI fixture.
    identity = result["resource_identity"]
    check(identity["language"] == 15 and identity["expansion_mask"] == 0x7f and
          len(identity["sha256"]) == 64 and identity["resource_count"] == len(identity["files"]) and
          identity["resource_count"] >= 18 and
          "UserData/LotLocations.iff" in identity["files"] and
          all(any(path.startswith(("ExpansionPack" if pack == 1 else f"ExpansionPack{pack}") + "/")
                  for path in identity["files"]) for pack in range(1, 8)),
          "plan fingerprints every installed expansion archive and sets the matching flags")
    check("--language" in result["command"] and "15" in result["command"],
          "selected language reaches runtime arguments")
    graphics_resource = next((path for path in identity["files"]
                              if path.endswith("/UIGraphics.far")), None)
    if graphics_resource:
        ui_index = result["command"].index("--ui-graphics")
        check(Path(result["command"][ui_index + 1]).resolve() ==
              (root / "UIGraphics" / "UIGraphics.far").resolve(),
              "interactive Linux runtime receives the selected installation's UI archive")
    else:
        check("--ui-graphics" not in result["command"],
              "headless/non-X11 plans do not pass a window-only UI archive")
    check(result["command"].count("--object-definitions") == 10 and
          result["command"].count("--animation-source") == 10 and
          result["command"].count("--object-definition-file") == 2 and
          "Downloads/Sample/Sample.far" in identity["files"] and
          "Downloads/Loose.iff" in identity["files"],
          "base, shared, expansion, and optional download resources reach runtime loaders")
    stand_without_profile = subprocess.run(
        [binary, "--game-root", str(root), "--mode", "1", "--house", "7", "--language", "15",
         "--headless-sit-object", "1", "--headless-stand-tick", "3", "--headless-ticks", "4"],
        capture_output=True, text=True, env=env)
    check(stand_without_profile.returncode == 2 and "single-resident Sit run" in stand_without_profile.stderr,
          "same-session Stand scheduling requires the explicit single-resident profile")
    stand_outside_run = subprocess.run(
        [binary, "--game-root", str(root), "--mode", "1", "--house", "7", "--language", "15",
         "--single-resident-session", "--headless-sit-object", "1", "--headless-stand-tick", "4",
         "--headless-ticks", "3"], capture_output=True, text=True, env=env)
    check(stand_outside_run.returncode == 2 and "in-range tick" in stand_outside_run.stderr,
          "same-session Stand tick must fit within the bounded run")
    (root / "ExpansionPack4" / "ExpansionPack4.far").unlink()
    partial = subprocess.run(command, capture_output=True, text=True, env=env)
    check(partial.returncode == 0, partial.stderr)
    partial_result = json.loads(partial.stdout)
    check(partial_result["resource_identity"]["expansion_mask"] == 0x77 and
          "ExpansionPack4/ExpansionPack4.far" not in partial_result["resource_identity"]["files"] and
          partial_result["command"].count("--object-definitions") == 9 and
          partial_result["command"].count("--animation-source") == 9 and
          partial_result["command"].count("--object-definition-file") == 2,
          "missing optional pack is omitted from both runtime loaders and identity")
    shutil.rmtree(root / "Downloads")
    without_downloads = subprocess.run(command, capture_output=True, text=True, env=env)
    check(without_downloads.returncode == 0 and
          not any(path.startswith("Downloads/") for path in
                  json.loads(without_downloads.stdout)["resource_identity"]["files"]),
          "Downloads remains optional for installations without user content")
    for argument in result["command"]:
        if argument.startswith("/"):
            path = Path(argument).resolve()
            check(path.is_relative_to(root.resolve()) or
                  path == (player_characters / "PlayerCreated.iff").resolve(),
                  f"runtime resource stays in the game or explicit player character catalog: {argument}")
    check(any(argument.endswith("/PlayerCreated.iff") for argument in result["command"]) and
          not any("PlayerCreated.iff" in path for path in identity["files"]),
          "player-created IFF reaches the character loader without becoming a commercial game resource")

    config = base / "config" / "opendollhouse" / "config"
    config.parent.mkdir(parents=True)
    config.write_text("invalid saved settings\n")
    override = subprocess.run(command, capture_output=True, text=True, env=env)
    check(override.returncode == 0, "explicit game root recovers malformed saved config")
    check(config.read_text() == "invalid saved settings\n",
          "plan-only never rewrites user configuration")
    objects_far = root / "GameData" / "Objects" / "Objects.far"
    original_objects = objects_far.read_bytes()
    objects_far.write_bytes(original_objects + b" changed")
    changed_plan = subprocess.run(command, capture_output=True, text=True, env=env)
    check(changed_plan.returncode == 0 and
          json.loads(changed_plan.stdout)["resource_identity"]["sha256"] != identity["sha256"],
          "changing an actual launch resource changes the planned session identity")
    objects_far.write_bytes(original_objects)
    implicit = subprocess.run([binary, "--mode", "1", "--house", "7", "--plan-only"],
                              capture_output=True, text=True, env=env)
    check(implicit.returncode == 2 and "pass --game-root" in implicit.stderr,
          "malformed config asks for an explicit repair path")

    global_far = root / "GameData" / "Global" / "Global.far"
    global_far.unlink()
    outside = base / "outside.far"
    outside.write_bytes(b"outside resource")
    global_far.symlink_to(outside)
    escaped = subprocess.run(command, capture_output=True, text=True, env=env)
    check(escaped.returncode == 2 and "outside the selected installation" in escaped.stderr,
          "symlinked resource outside selected installation is rejected")

print(f"Integrated Linux entry CLI: {checks} checks passed (synthetic only)")
