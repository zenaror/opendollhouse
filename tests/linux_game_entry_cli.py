"""Synthetic integrated Linux entry checks; never copies commercial resources."""
import json
import hashlib
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
        struct.pack("<I", 0x12345678) + struct.pack("<I6h", 0, 0, 0, 0, 0, 0, 0))
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
    # Neither NPC is a resident nor serialized in this lot. Distinct physical
    # files sharing one GUID must both reach discovery (ambiguity stays visible).
    npc_objd = bytearray(objd)
    struct.pack_into("<I", npc_objd, 28, 0x33445566)
    for name in ("User00001.iff", "User00002.IFF"):
        (user / "Characters" / name).write_bytes(iff([chunk(b"OBJD", 128, npc_objd)]))

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
    (root / "GameData" / "synthetic.wll").write_bytes(b"synthetic loose wall")
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
    env["XDG_CACHE_HOME"] = str(base / "cache")
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
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
    render_index = result["command"].index("--render-size")
    check(result["command"][render_index + 1:render_index + 3] == ["800", "600"],
          "default lot viewport remains unchanged")
    for width, height in ((320, 200), (800, 240), (1024, 768)):
        resized = subprocess.run(command + ["--lot-window-size", str(width), str(height)],
                                 capture_output=True, text=True, env=env)
        check(resized.returncode == 0, resized.stderr)
        args = json.loads(resized.stdout)["command"]
        index = args.index("--render-size")
        check(args[index + 1:index + 3] == [str(width), str(height)],
              "explicit lot viewport reaches the native renderer")
    for extra in (["--lot-window-size", "319", "240"],
                  ["--lot-window-size", "800", "199"],
                  ["--lot-window-size", "1025", "600"],
                  ["--lot-window-size", "800"],
                  ["--lot-window-size", "800", "240", "--lot-window-size", "800", "600"],
                  ["--lot-window-size", "800", "240", "--headless-ticks", "0"]):
        rejected = subprocess.run(command + extra, capture_output=True, text=True, env=env)
        check(rejected.returncode != 0, "invalid, duplicate or headless lot viewport accepted")
    dialog_base = [binary, "--game-root", str(root), "--mode", "1", "--house", "7", "--language", "15"]
    checkpoint_base = dialog_base + ["--load-session", str(root / "missing.odhs"),
                                     "--headless-ticks", "33", "--save-session-on-exit"]
    for option in ([], ["0"], ["-1"], ["bad"], ["65536"], ["1", "--headless-checkpoint-interval", "1"]):
        rejected = subprocess.run(checkpoint_base + ["--headless-checkpoint-interval"] + option,
                                  capture_output=True, text=True, env=env)
        check(rejected.returncode == 2 and not rejected.stdout,
              "invalid, absent or duplicate checkpoint interval accepted")
    for extra in ([], ["--headless-ticks", "0", "--save-session-on-exit"],
                  ["--headless-ticks", "2"],
                  ["--headless-ticks", "2", "--save-session-on-exit", "--plan-only"],
                  ["--headless-ticks", "2", "--save-session-on-exit", "--instruction-budget", "100"],
                  ["--headless-ticks", "2", "--save-session-on-exit", "--lot-window-size", "800", "600"]):
        rejected = subprocess.run(dialog_base + ["--load-session", str(root / "missing.odhs")] +
                                  extra + ["--headless-checkpoint-interval", "1"],
                                  capture_output=True, text=True, env=env)
        check(rejected.returncode == 2 and "--headless-checkpoint-interval requires" in rejected.stderr,
              "checkpoint mode conflict must reject before archive/resource access")
    for forbidden in (["--headless-move-entity", "1", "20", "24", "1", "2", "1"],
                      ["--headless-resident-go-here", "1", "20", "24", "1", "1"],
                      ["--headless-single-resident-go-here", "20", "24", "1", "1"],
                      ["--headless-stop-route-depth", "1"],
                      ["--headless-resident-interaction", "1", "2", "1"],
                      ["--audio"], ["--headless-dialog-ok"], ["--original-companion-protocol"]):
        ordinary = dialog_base + ["--load-session", str(root / "missing.odhs"),
                                  "--headless-ticks", "2", "--save-session-on-exit"]
        for ordered in (["--headless-checkpoint-interval", "1"] + forbidden,
                        forbidden + ["--headless-checkpoint-interval", "1"]):
            rejected = subprocess.run(ordinary + ordered, capture_output=True, text=True, env=env)
            check(rejected.returncode == 2 and "requires only ordinary managed HOME options" in rejected.stderr,
                  "checkpoint allowlist rejects every probe/input option in either argument order before archive access")
    for forbidden in (["--headless-portal", "1", "1"], ["--person-arg", "--headless-move-entity"]):
        rejected = subprocess.run(checkpoint_base + forbidden + ["--headless-checkpoint-interval", "1"],
                                  capture_output=True, text=True, env=env)
        check(rejected.returncode == 2 and not rejected.stdout,
              "unknown raw passthrough/portal argument must not escape the launcher parser")
    rejected = subprocess.run(checkpoint_base + ["--headless-checkpoint-interval", "1"],
                              capture_output=True, text=True, env=env)
    check(rejected.returncode == 2 and "exceeds 32 saves" in rejected.stderr,
          "33 total checkpoint captures must reject before loading the session")
    move = ["--headless-move-entity", "1", "20", "24", "1", "2", "3"]
    for extra in [[], ["--headless-ticks", "2"], ["--headless-ticks", "4"] + move]:
        rejected_move = subprocess.run(dialog_base + extra + move,
                                       capture_output=True, text=True, env=env)
        check(rejected_move.returncode == 2 and
              ("in-range tick" in rejected_move.stderr or "duplicate" in rejected_move.stderr),
              "scheduled obstacle mutation requires a finite run, an in-range tick and one request")
    go_here = ["--headless-resident-go-here", "1", "20", "24", "1", "3"]
    # Equal commercial bytes do not make the player catalogs interchangeable.
    for number in (3, 4):
        shutil.copytree(root / "UserData", root / f"UserData{number}")
    scoped_player = base / "data" / "opendollhouse" / "UserData3" / "Characters"
    scoped_player.mkdir(parents=True)
    shutil.copy2(player_characters / "PlayerCreated.iff", scoped_player / "PlayerCreated.iff")
    town3 = subprocess.run(command + ["--neighborhood", "3"], capture_output=True, text=True, env=env)
    check(town3.returncode == 0, town3.stderr)
    town3_plan = json.loads(town3.stdout)
    check("UserData3/Neighborhood.iff" in town3_plan["resource_identity"]["files"],
          "selected neighborhood contributes its own physical resource identity")
    town4 = subprocess.run(command + ["--neighborhood", "4"], capture_output=True, text=True, env=env)
    check(town4.returncode == 2, "town4 must not borrow town3 or town1 player-created residents")
    scoped_player4 = base / "data" / "opendollhouse" / "UserData4" / "Characters"
    scoped_player4.mkdir(parents=True)
    shutil.copy2(scoped_player / "PlayerCreated.iff", scoped_player4 / "PlayerCreated.iff")
    town4 = subprocess.run(command + ["--neighborhood", "4"], capture_output=True, text=True, env=env)
    check(town4.returncode == 0, town4.stderr)
    for invalid in ("0", "9", "-1"):
        rejected = subprocess.run(command + ["--neighborhood", invalid], capture_output=True, text=True, env=env)
        check(rejected.returncode == 2, "invalid neighborhood selector must be rejected")

    for extra, expected in [([], "--headless-resident-go-here requires an in-range tick"),
                            (["--headless-ticks", "2"], "--headless-resident-go-here requires an in-range tick"),
                            (["--headless-ticks", "4", "--plan-only"], "--headless-resident-go-here requires an in-range tick"),
                            (["--headless-ticks", "4", "--single-resident-session"], "single-resident Go Here requires a fresh saved")]:
        rejected_go_here = subprocess.run(dialog_base + extra + go_here,
                                          capture_output=True, text=True, env=env)
        check(rejected_go_here.returncode == 2 and expected in rejected_go_here.stderr and
              "unsupported FAR signature/version" not in rejected_go_here.stderr,
              "explicit actor Go Here retains bounded execution and household profile gates")
    for actor, level, tick in [("0", "1", "1"), ("1", "0", "1"), ("1", "1", "0"),
                               ("1", "1", "-1"), ("32768", "1", "1")]:
        invalid = subprocess.run(dialog_base + ["--headless-ticks", "4",
            "--headless-resident-go-here", actor, "20", "24", level, tick],
            capture_output=True, text=True, env=env)
        check(invalid.returncode == 2 and "unsupported FAR signature/version" not in invalid.stderr,
              "Go Here rejects malformed actor, level and relative tick")
    for interaction_actor in ("1", "2"):
        combined = subprocess.run(dialog_base + ["--headless-ticks", "4"] + go_here +
            ["--headless-resident-interaction", interaction_actor, "9999", "1"],
            capture_output=True, text=True, env=env)
        expected = ("require different actors" if interaction_actor == "1"
                    else "unsupported FAR signature/version")
        check(combined.returncode == 2 and expected in combined.stderr,
              "Go Here and selected-object input share a run only for different actors")
    # Legacy and explicit rows share one bounded schedule before resource loading.
    row_request = ["--headless-resident-interaction-row", "1", "2", "0", "1"]
    for suffix, expected in [
            (row_request[:-1], "missing resident interaction values"),
            (["--headless-resident-interaction-row", "1", "2", "65536", "1"], "resident interaction row"),
            (["--headless-resident-interaction", "1", "2", "2"] + row_request, "nondecreasing"),
            (row_request * 33, "thirty-two requests"),
            (["--headless-resident-interaction-row", "1", "2", "0", "5"], "fit within the bounded run")]:
        rejected = subprocess.run(dialog_base + ["--headless-ticks", "4"] + suffix,
                                  capture_output=True, text=True, env=env)
        check(rejected.returncode == 2 and expected in rejected.stderr,
              "explicit resident rows retain missing-value, range, mixed-order, count and tick bounds")
    mixed_rows = subprocess.run(dialog_base + ["--headless-ticks", "4",
        "--headless-resident-interaction", "1", "2", "1"] + row_request,
        capture_output=True, text=True, env=env)
    check(mixed_rows.returncode == 2 and "unsupported FAR signature/version" in mixed_rows.stderr,
          "equal-tick legacy and explicit rows reach resource loading in their original order")
    accepted_move = subprocess.run(dialog_base + ["--headless-ticks", "4"] + move,
                                  capture_output=True, text=True, env=env)
    check(accepted_move.returncode == 2 and "unsupported FAR signature/version" in accepted_move.stderr,
          "scheduled mutation reaches runtime parsing only with explicit bounded headless execution")
    for extra in [[], ["--headless-ticks", "0"], ["--plan-only"],
                  ["--headless-ticks", "1", "--plan-only"]]:
        rejected_dialog = subprocess.run(dialog_base + extra + ["--headless-dialog-ok"],
                                         capture_output=True, text=True, env=env)
        check(rejected_dialog.returncode == 2 and
              "--headless-dialog-ok requires execution with --headless-ticks 1..65535" in rejected_dialog.stderr,
              "explicit one-shot dialog response rejects absent/zero ticks and plan-only")
    oversized_horizon = subprocess.run(dialog_base + ["--headless-ticks", "65536"],
                                       capture_output=True, text=True, env=env)
    check(oversized_horizon.returncode == 2 and "headless tick count" in oversized_horizon.stderr,
          "continuous execution still rejects a horizon beyond the finite CLI bound")
    late_travel = subprocess.run(dialog_base + ["--headless-ticks", "65535",
        "--headless-travel-house", "8", "--headless-travel-tick", "65534"],
        capture_output=True, text=True, env=env)
    check(late_travel.returncode == 2 and "unsupported FAR signature/version" in late_travel.stderr,
          "late travel scheduling reaches resource loading within the extended finite horizon")
    endpoint_travel = subprocess.run(dialog_base + ["--headless-ticks", "43200",
        "--headless-travel-house", "8", "--headless-travel-tick", "43200"],
        capture_output=True, text=True, env=env)
    check(endpoint_travel.returncode == 2 and "tick below the per-visit limit" in endpoint_travel.stderr,
          "travel remains strictly before the end even in a full-day run")
    for ticks in ["1", "4096", "43200", "65535"]:
        accepted_dialog = subprocess.run(dialog_base + ["--headless-ticks", ticks, "--headless-dialog-ok"],
                                         capture_output=True, text=True, env=env)
        # These resources deliberately cannot execute a VM. Reaching the FAR
        # parser establishes accepted finite execution CLI, not dialog delivery.
        check(accepted_dialog.returncode == 2 and
              "unsupported FAR signature/version" in accepted_dialog.stderr,
              "valid explicit dialog flag reaches runtime resource parsing at both tick bounds")
    duplicate_dialog = subprocess.run(dialog_base + ["--headless-ticks", "1", "--headless-dialog-ok",
                                      "--headless-dialog-ok"], capture_output=True, text=True, env=env)
    check(duplicate_dialog.returncode == 2 and "--headless-dialog-ok" in duplicate_dialog.stderr and
          "unsupported FAR signature/version" not in duplicate_dialog.stderr,
          "one-shot dialog option cannot be repeated")
    for responses in (["--headless-dialog-response", "0", ""],
                      ["--headless-dialog-response", "2", "Não, obrigado"] * 32):
        response_run = subprocess.run(dialog_base + ["--headless-ticks", "1"] + responses,
                                capture_output=True, text=True, env=env)
        check(response_run.returncode == 2 and "unsupported FAR signature/version" in response_run.stderr,
              "bounded explicit dialog response queue reaches resource parsing without inventing a VM result")
    for extra in (["--headless-dialog-response", "3", ""],
                  ["--headless-dialog-response", "0"],
                  ["--headless-dialog-response", "0", "x" * 4097],
                  ["--headless-dialog-response", "0", ""] * 33,
                  ["--headless-dialog-ok", "--headless-dialog-response", "0", ""],
                  ["--headless-dialog-response", "0", "", "--headless-dialog-ok"]):
        response_run = subprocess.run(dialog_base + ["--headless-ticks", "1"] + extra,
                                capture_output=True, text=True, env=env)
        check(response_run.returncode == 2 and "unsupported FAR signature/version" not in response_run.stderr,
              "invalid or mixed dialog response sequences fail before resource access")
    for extra in ([], ["--headless-ticks", "0"], ["--headless-ticks", "1", "--plan-only"]):
        response_run = subprocess.run(dialog_base + extra + ["--headless-dialog-response", "0", ""],
                                capture_output=True, text=True, env=env)
        check(response_run.returncode == 2 and "requires bounded execution" in response_run.stderr,
              "dialog response queue requires an actual bounded execution")
    info_option = ["--headless-info-dialog-ok-limit"]
    for limit in ("1", "32"):
        info_run = subprocess.run(dialog_base + ["--headless-ticks", "2"] + info_option + [limit],
                                capture_output=True, text=True, env=env)
        check(info_run.returncode == 2 and "unsupported FAR signature/version" in info_run.stderr,
              "bounded informational input reaches resource loading at both count bounds")
    for extra in (info_option, info_option + ["0"], info_option + ["33"],
                  info_option + ["1"] + info_option + ["1"],
                  info_option + ["1", "--headless-dialog-ok"],
                  ["--headless-dialog-ok"] + info_option + ["1"],
                  info_option + ["1", "--headless-dialog-response", "0", ""],
                  ["--headless-dialog-response", "0", ""] + info_option + ["1"]):
        info_run = subprocess.run(dialog_base + ["--headless-ticks", "2"] + extra,
                                capture_output=True, text=True, env=env)
        check(info_run.returncode == 2 and "unsupported FAR signature/version" not in info_run.stderr,
              "invalid count, repetition or mixed dialog inputs fail before resource access")
    for extra in ([], ["--headless-ticks", "0"], ["--headless-ticks", "2", "--plan-only"]):
        info_run = subprocess.run(dialog_base + extra + info_option + ["1"],
                                capture_output=True, text=True, env=env)
        check(info_run.returncode == 2 and "requires bounded execution" in info_run.stderr,
              "informational input requires an executing finite nonzero horizon")
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
    metadata_paths = [Path(result["command"][i + 1]).resolve()
                      for i, arg in enumerate(result["command"])
                      if arg == "--character-definition-file"]
    material_paths = [Path(result["command"][i + 1]).resolve()
                      for i, arg in enumerate(result["command"])
                      if arg == "--architecture-resource"]
    check(all(root.resolve() / name in material_paths for name in (
              "GameData/Objects/Objects.far", "ExpansionShared/ExpansionShared.far",
              "ExpansionPack3/ExpansionPack3.far", "ExpansionPack5/ExpansionPack5.far",
              "Downloads/Sample/Sample.far")) and
          material_paths[-1] == root.resolve() / "GameData/synthetic.wll" and
          len(material_paths) == len(set(material_paths)),
          "installed material FARs precede loose files without duplicate source arguments")
    check(all(str(path.relative_to(root.resolve())) in identity["files"] for path in material_paths),
          "material consumers reuse sources protected by the resource identity manifest")
    expected_characters = sorted((root / "UserData" / "Characters" / name).resolve()
                                 for name in ("User00001.iff", "User00002.IFF"))
    check(metadata_paths == expected_characters and len(metadata_paths) == len(set(metadata_paths)),
          "all unused character files, including distinct duplicate GUIDs, enter metadata discovery exactly once")
    check(all(str(path.relative_to(root.resolve())) in identity["files"] for path in metadata_paths),
          "character metadata reuses files already protected by the existing identity manifest")
    # Independently reproduce unchanged manifest-v1 bytes. Runtime discovery
    # flags are not part of META and never add a second copy of a resource.
    digest = hashlib.sha256(b"ODH-RESOURCE-MANIFEST-V1\0")
    names = sorted(identity["files"])
    digest.update(struct.pack("<I", len(names)))
    for name in names:
        resource_bytes = (root / name).read_bytes()
        encoded = name.encode("utf-8")
        digest.update(struct.pack("<I", len(encoded)))
        digest.update(encoded)
        digest.update(struct.pack("<Q", len(resource_bytes)))
        digest.update(hashlib.sha256(resource_bytes).digest())
    check(digest.hexdigest() == identity["sha256"],
          "metadata discovery preserves the existing canonical manifest-v1 identity calculation")
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

# A real synthetic saved household exercises resumed CLI admission and argument
# forwarding. Its deliberately out-of-lot goal must fail in route context
# validation, after the launcher accepted the saved live actor; this is not a
# successful Go Here gameplay probe.
from linux_household_session_resume_cli import make_household
with tempfile.TemporaryDirectory(prefix="odh-linux-go-here-resume-") as directory:
    base = Path(directory)
    root = base / "game"
    make_household(root)
    env = os.environ.copy()
    for key in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"):
        env[key] = str(base / key)
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "ODH_STATE_DIR"):
        env.pop(key, None)
    launch = [binary, "--game-root", str(root)]
    # The installed synthetic household is unmanaged: saving ODHS alone does
    # not create a neighborhood overlay. Supply the regression's actual profile
    # precondition explicitly, using the existing empty ODHF/v2 fixture format
    # (44-byte header, real Neighborhood SHA256, zero assignment records).
    # It owns no character, family assignment, occupancy lease or fabricated Sim.
    neighborhood_identity = hashlib.sha256((root / "UserData" / "Neighborhood.iff").read_bytes()).digest()
    overlay_directory = Path(env["XDG_CONFIG_HOME"]) / "opendollhouse" / "player-state"
    overlay_directory.mkdir(parents=True)
    fixture_overlay = overlay_directory / f"neighborhood-{neighborhood_identity.hex()}.odhp"
    fixture_overlay.write_bytes(struct.pack("<4sHH32sI", b"ODHF", 2, 0, neighborhood_identity, 0))
    fresh = subprocess.run(launch + ["--mode", "1", "--house", "7", "--language", "1",
        "--headless-ticks", "1", "--save-session-on-exit"],
        capture_output=True, text=True, env=env, timeout=30)
    check(fresh.returncode == 0, fresh.stderr)
    path = Path(json.loads(fresh.stdout)["saved_session_path"])
    original = path.read_bytes()
    # This profile now has its prepared neighborhood overlay and real ODHS
    # checkpoint, without owning CAS/Make19 character files. Operations below
    # exercise the real launcher with that profile, not only a reader helper.
    overlays = list((Path(env["XDG_CONFIG_HOME"]) / "opendollhouse" /
                     "player-state").glob("neighborhood-*.odhp"))
    private_characters = Path(env["XDG_DATA_HOME"]) / "opendollhouse" / "Characters"
    check(len(overlays) == 1 and overlays[0] == fixture_overlay and not private_characters.exists(),
          "saved installed household has its real overlay and no private character catalog")
    overlay_before = overlays[0].read_bytes()
    resume = launch + ["--load-session", str(path), "--headless-ticks", "3"]
    # A valid integrated request reaches the ordinary menu consumer. This
    # fixture deliberately has no TTAB: the exact authored row in its rejection
    # proves forwarding without inventing an action or bypassing its test.
    explicit_row = subprocess.run(resume + ["--headless-resident-interaction-row",
        "1", "2", "65535", "1"], capture_output=True, text=True, env=env, timeout=30)
    check(explicit_row.returncode == 3, (explicit_row.returncode, explicit_row.stderr, explicit_row.stdout))
    explicit_report = json.loads(explicit_row.stdout)
    submissions = explicit_report["player_submissions"]
    check(len(submissions) == 1 and not submissions[0]["accepted"] and
          "requested TTAB row 65535 is absent" in submissions[0]["diagnostic"],
          "integrated saved-session input forwards the exact explicit row to the regular menu without fallback")
    variant = subprocess.run(resume + ["--headless-resident-interaction-variant", "1", "2", "65535", "-1", "14", "Nome observado", "1"],
        capture_output=True, text=True, env=env, timeout=30)
    check(variant.returncode == 3 and "requested TTAB row 65535 is absent" in
          json.loads(variant.stdout)["player_submissions"][0]["diagnostic"],
          "observed variant forwards exact authored row without falling back to another menu choice")
    query = subprocess.run(launch + ["--load-session", str(path), "--headless-ticks", "0", "--headless-menu-query", "1", "2", "--save-session-on-exit"],
        capture_output=True, text=True, env=env, timeout=30)
    check(query.returncode == 0, (query.returncode, query.stderr))
    query_report=json.loads(query.stdout)
    check(query_report["menu_query"]["world_unchanged"] and query_report["menu_query"]["random_unchanged"] and
          query_report["ticks"] == query_report["resumed_tick"] and not query_report["player_submissions"] and
          Path(query_report["saved_session_path"]).read_bytes() == original and
          overlays[0].read_bytes() == overlay_before and not private_characters.exists(),
          "zero-tick resumed legacy overlay without owned characters preserves source head/whole session and creates no catalog or actions")
    # Exercise the real resumed app and serialized session, including the batch
    # limit. Repeated same-target queries must preserve legacy output exactly.
    for count in (2, 32):
        batch = subprocess.run(launch + ["--load-session", str(path), "--headless-ticks", "0"] +
            ["--headless-menu-query", "1", "2"] * count + ["--save-session-on-exit"],
            capture_output=True, text=True, env=env, timeout=30)
        check(batch.returncode == 0, (count, batch.returncode, batch.stderr))
        batch_report = json.loads(batch.stdout)
        check(batch_report["menu_query"] == query_report["menu_query"] and
              len(batch_report["menu_queries"]) == count and
              all(item == query_report["menu_query"] for item in batch_report["menu_queries"]),
              f"{count} actual menu queries preserve legacy first result and ordered exact output")
        check(all(item["world_unchanged"] and item["random_unchanged"]
                  for item in batch_report["menu_queries"]) and
              batch_report["ticks"] == batch_report["resumed_tick"] == query_report["ticks"] and
              not batch_report["player_submissions"] and
              Path(batch_report["saved_session_path"]).read_bytes() == original and
              path.read_bytes() == original,
              f"{count} actual zero-tick queries preserve World/RNG, whole saved session and source bytes without actions")
    excessive_queries = subprocess.run(launch + ["--load-session", str(path), "--headless-ticks", "0"] +
        ["--headless-menu-query", "1", "2"] * 33 + ["--save-session-on-exit"],
        capture_output=True, text=True, env=env, timeout=30)
    check(excessive_queries.returncode == 2 and not excessive_queries.stdout.strip() and
          "at most thirty-two requests" in excessive_queries.stderr and len(excessive_queries.stderr) < 4096 and
          path.read_bytes() == original,
          "thirty-third query rejects before runtime output with bounded diagnostic and untouched source bytes")
    hex_variant = subprocess.run(resume + ["--headless-resident-interaction-variant-hex", "1", "2", "65535", "-1", "14", "4e6f6d6520e1", "1"],
        capture_output=True, text=True, env=env, timeout=30)
    check(hex_variant.returncode == 3 and "requested TTAB row 65535 is absent" in
          json.loads(hex_variant.stdout)["player_submissions"][0]["diagnostic"],
          "lossless hex variant accepts real non-UTF8 source bytes without replacing the authored row")
    for malformed in ("0", "xx", "00", "aa"*4097):
        rejected = subprocess.run(resume + ["--headless-resident-interaction-variant-hex", "1", "2", "0", "-1", "14", malformed, "1"],
            capture_output=True, text=True, env=env, timeout=30)
        check(rejected.returncode == 2 and "hex" in rejected.stderr,
              "hex menu names reject odd/nonhex/NUL/oversized source bytes")
    fresh_query = subprocess.run(launch + ["--mode", "1", "--house", "7", "--headless-ticks", "0", "--headless-menu-query", "1", "2"],
        capture_output=True, text=True, env=env, timeout=30)
    check(fresh_query.returncode == 2 and "zero-tick resumed" in fresh_query.stderr,
          "query refuses fresh bootstrap before simulation/resource preparation")
    for actor in ("1", "2"):
        action = ["--headless-resident-go-here", actor, "2046", "2046", "1", "3"]
        for other_action in ([], ["--headless-resident-interaction",
                                 "2" if actor == "1" else "1", "9999", "1"]):
            resumed = subprocess.run(resume + action + other_action,
                capture_output=True, text=True, env=env, timeout=30)
            check(resumed.returncode == 2 and
                  "resident Go Here target is outside the known route context" in resumed.stderr,
                  "saved primary and auxiliary Go Here reach live route validation, including another actor's object input")
    for actor, tick, expected in [("999", "1", "positioned live saved household member"),
                                  ("1", "4", "in-range tick"),
                                  ("1", "0", "outside the supported profile")]:
        rejected = subprocess.run(resume + ["--headless-resident-go-here", actor,
            "20", "24", "1", tick], capture_output=True, text=True, env=env, timeout=30)
        check(rejected.returncode == 2 and expected in rejected.stderr,
              "resumed Go Here rejects absent saved actors and invalid relative ticks")
    conflict = subprocess.run(resume + ["--headless-resident-go-here", "1", "20", "24", "1", "3",
        "--headless-resident-interaction", "1", "9999", "1"],
        capture_output=True, text=True, env=env, timeout=30)
    check(conflict.returncode == 2 and "require different actors" in conflict.stderr,
          "resumed same-actor action schedules are rejected")
    check(path.read_bytes() == original, "admission and route-context failures preserve the source checkpoint")

with tempfile.TemporaryDirectory(prefix="odh-property-command-admission-") as temp:
    isolated = os.environ.copy()
    for key, directory in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                           ("XDG_STATE_HOME", "state"), ("XDG_CACHE_HOME", "cache")):
        isolated[key] = str(Path(temp) / directory)
    launch = [binary, "--game-root", str(Path(temp) / "absent-game"),
              "--mode", "1", "--house", "7", "--headless-ticks", "0"]
    floor = ["--edit-floor", "7", "2", "3", "1"]
    cases = [
        (["--edit-floor", "7", "2", "3", "0"], "floor level must be 1 or 2"),
        (["--edit-floor", "7", "2", "3", "3"], "floor level"),
        (["--edit-floor", "7", "2"], "MATERIAL X Y LEVEL"),
        (["--edit-floor", "65536", "2", "3", "1"], "floor material"),
        (["--edit-floor", "7", "32768", "3", "1"], "floor tile X"),
        (["--edit-floor", "7", "2", "32768", "1"], "floor tile Y"),
        (floor + floor, "one MATERIAL X Y LEVEL"),
        (floor + ["--cancel-object-command"], "one property command"),
        (["--cancel-object-command"] + floor, "one MATERIAL X Y LEVEL"),
        (floor + ["--plan-only"], "property commands require"),
        (floor + ["--single-resident-session"], "property commands require"),
    ]
    for arguments, reason in cases:
        result = subprocess.run(launch + arguments, env=isolated,
                                capture_output=True, text=True, timeout=30)
        check(result.returncode == 2 and reason in result.stderr,
              (arguments, result.returncode, result.stderr))
    no_house = launch[:5] + launch[7:]
    result = subprocess.run(no_house + floor, env=isolated,
                            capture_output=True, text=True, timeout=30)
    check(result.returncode == 2 and "property commands require" in result.stderr,
          "floor editing requires a selected house or authenticated saved session")
    check(not (Path(temp) / "absent-game").exists(),
          "invalid property inputs never create or alter game data")

print(f"Integrated Linux entry CLI: {checks} checks passed (synthetic only)")
