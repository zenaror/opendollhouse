#!/usr/bin/env python3
"""Run real House07 resident Init/entry8 before any Main tick, read-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


RESIDENTS = (
    (850010198, "User00000.iff"),
    (844195932, "User00001.iff"),
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--game-root", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    binary, game = args.binary.resolve(), args.game_root.resolve()
    global_archive = game / "GameData/Global/Global.far"
    object_archive = game / "GameData/Objects/Objects.far"
    animation_archive = game / "GameData/Animation/Animation.far"
    shared_archive = game / "ExpansionShared/ExpansionShared.far"
    lot = game / "UserData/Houses/House07.iff"
    zoning = game / "UserData/LotZoning.iff"
    neighborhood = game / "UserData/Neighborhood.iff"
    characters = [game / "UserData/Characters" / name for _, name in RESIDENTS]
    resources = [binary, global_archive, object_archive, animation_archive,
                 shared_archive, lot, zoning, neighborhood, *characters]
    missing = [str(path) for path in resources if not path.is_file()]
    if missing:
        raise SystemExit("required House07/startup resource missing: " + ", ".join(missing))
    before = {str(path.relative_to(game)) if path.is_relative_to(game) else str(path): sha256(path)
              for path in resources if path != binary}

    with tempfile.TemporaryDirectory(prefix="odh-house07-resident-startup-") as temp:
        env = os.environ.copy()
        for name in ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME"):
            location = Path(temp) / name
            location.mkdir()
            env[name] = str(location)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        results = []
        for guid, filename in RESIDENTS:
            character = game / "UserData/Characters" / filename
            command = [
                str(binary), "--avatar-file", str(character), "128",
                "--semiglobal", str(global_archive), "PersonGlobals.iff",
                "--global", str(global_archive), "Global.iff",
                "--lot", str(lot), "7", "--zoning", str(zoning),
                "--neighborhood", str(neighborhood), "--activate-family",
                "--object-definitions", str(object_archive),
                "--object-definition-file", str(characters[0]),
                "--object-definition-file", str(characters[1]),
                "--animation-source", str(animation_archive),
                "--animation-source", str(shared_archive),
                "--initialize", "--ticks", "0", "--seed", "1",
            ]
            completed = subprocess.run(command, capture_output=True, text=True,
                                       env=env, timeout=args.timeout)
            if not completed.stdout.strip():
                raise RuntimeError(
                    f"{filename} exited {completed.returncode}: {completed.stderr[-1200:]}"
                )
            report = json.loads(completed.stdout)
            stages = report["startup_stages"]
            family = report["family_activation"]
            if (completed.returncode not in (0, 3) or report["host_failed"] or
                    report["ticks"] != 0 or report["avatar"]["guid"] != guid or
                    guid not in family["members"] or
                    family["current_global9"] != family["chunk_id"] or
                    not report["startup_requested"] or not report["body_initialized"] or
                    len(stages) != 2 or stages[0]["phase"] != "init" or
                    stages[0]["status"] != "returned_true" or stages[0]["steps"] == 0 or
                    stages[1]["phase"] != "dynamic_multitile" or
                    stages[1]["routine"] != 0 or stages[1]["steps"] != 0):
                raise RuntimeError(
                    f"{filename} did not pass the real startup profile: "
                    f"{[(stage.get('phase'), stage.get('status'), stage.get('steps')) for stage in stages]}"
                )
            if completed.returncode == 3 and not report["tick_budget_exhausted"]:
                raise RuntimeError(f"{filename} returned the bounded-run code for another reason")
            results.append({
                "guid": guid,
                "character": filename,
                "init": stages[0]["status"],
                "init_steps": stages[0]["steps"],
                "entry8_id": stages[1]["routine"],
                "entry8_skipped": stages[1]["steps"] == 0,
                "body_initialized": report["body_initialized"],
                "main_ticks": report["ticks"],
                "bounded_tick_exit": completed.returncode == 3,
            })

    after = {key: sha256(game / key) for key in before}
    if before != after:
        raise RuntimeError("one or more original House07/game resources changed")
    print(json.dumps({
        "evidence": "House07_real_resident_startup_separately_before_Main_tick",
        "shared_world": False,
        "resources_unchanged": len(before),
        "residents": results,
        "claim": "real character Init only; entry8 is skipped when its OBJD action is zero; no arrival or gameplay claim",
    }, indent=2))


if __name__ == "__main__":
    main()
