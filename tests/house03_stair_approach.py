#!/usr/bin/env python3
"""Probe the real House03 stair landings with a synthetic waiting Main/avatar."""
import argparse
import json
import pathlib
import struct
import subprocess
import tempfile


def patch_idle_main(source: pathlib.Path, destination: pathlib.Path) -> None:
    data = source.read_bytes()
    if len(data) < 64:
        raise ValueError("User00000 IFF is shorter than its header")
    instruction = struct.pack("<HBB8s", 17, 0, 0, bytes([0, 0, 1, 0, 0, 0, 0, 0]))
    bhav = struct.pack("<HH8s", 0x8002, 1, bytes(8)) + instruction
    output = bytearray(data[:64])
    offset = 64
    replaced = 0
    while offset < len(data):
        if offset + 76 > len(data):
            raise ValueError(f"truncated IFF chunk header at {offset}")
        length = struct.unpack_from(">I", data, offset + 4)[0]
        if length < 76 or offset + length > len(data):
            raise ValueError(f"invalid IFF chunk length {length} at {offset}")
        chunk = bytearray(data[offset:offset + length])
        if chunk[:4] == b"BHAV" and struct.unpack_from(">H", chunk, 8)[0] == 4096:
            chunk = bytearray(chunk[:76] + bhav)
            struct.pack_into(">I", chunk, 4, len(chunk))
            replaced += 1
        output.extend(chunk)
        offset += length
    if replaced != 1:
        raise ValueError(f"expected one avatar Main BHAV4096, found {replaced}")
    destination.write_bytes(output)


def architecture_resources(game: pathlib.Path):
    roots = [game / "GameData/Floors", game / "GameData/Walls"]
    for root in [game / "Deluxe", game / "ExpansionShared", *game.glob("ExpansionPack*")]:
        roots.extend(root / rel for rel in (
            "Floors", "Walls", "GameData/Floors", "GameData/Walls"))
    paths = {path for root in roots if root.is_dir() for path in root.iterdir()
             if path.is_file() and path.suffix.lower() in {".flr", ".wll"}}
    return sorted(paths)


def run_probe(binary: pathlib.Path, game: pathlib.Path, avatar: pathlib.Path,
              portal_id: int, start: tuple[int, int, int], expected: tuple[int, int, int]):
    global_far = game / "GameData/Global/Global.far"
    command = [str(binary), "--avatar-file", str(avatar), "128",
        "--semiglobal", str(global_far), "PersonGlobals.iff",
        "--global", str(global_far), "Global.iff",
        "--lot", str(game / "UserData/Houses/House03.iff"), "3",
        "--zoning", str(game / "UserData/LotZoning.iff"),
        "--neighborhood", str(game / "UserData/Neighborhood.iff"),
        "--object-definitions", str(game / "GameData/Objects/Objects.far"),
        "--object-definition-file", str(game / "UserData/Characters/User00000.iff"),
        "--object-definition-file", str(game / "UserData/Characters/User00001.iff"),
        "--headless-main", "--render-floors", str(game / "GameData/floors.iff"),
        "--headless-fixture-avatar", "30000", str(start[0]), str(start[1]), str(start[2]), "0",
        "--headless-fixture-main-arg0", "64", "--headless-portal", str(portal_id), "1",
        "--ticks", "80"]
    for resource in architecture_resources(game):
        command += ["--architecture-resource", str(resource)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if not result.stdout:
        raise RuntimeError(result.stderr.strip() or f"odh-person exited {result.returncode} without JSON")
    data = json.loads(result.stdout)
    portal = data["portal_runtime"]
    route = data["route_runtime"]
    end = portal["actor_position"]
    if route["movement_callbacks"] <= 0:
        raise AssertionError(f"portal {portal_id} did not walk: {portal}; route={route}")
    if end[:3] != list(expected):
        raise AssertionError(f"portal {portal_id} ended at {end}, expected x/y/floor {expected}")
    if portal["actor_level_changed"]:
        raise AssertionError(f"approach probe unexpectedly changed floor: {portal}")
    if "Snap resource/profile unsupported" not in portal["diagnostic"]:
        raise AssertionError(f"expected the known Snap limit after reaching the landing: {portal}")
    return {"portal_id": portal_id, "start": portal["actor_start_position"],
            "end": end, "movement_callbacks": route["movement_callbacks"],
            "portal_status": portal["status"], "next_limit": portal["diagnostic"],
            "level_changed": portal["actor_level_changed"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--game-root", required=True, type=pathlib.Path)
    args = parser.parse_args()
    game = args.game_root.resolve()
    required = [args.binary, game / "GameData/Global/Global.far",
        game / "GameData/Objects/Objects.far", game / "GameData/floors.iff",
        game / "UserData/Characters/User00000.iff", game / "UserData/Characters/User00001.iff",
        game / "UserData/Houses/House03.iff", game / "UserData/LotZoning.iff",
        game / "UserData/Neighborhood.iff"]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("required game/test file missing: " + ", ".join(missing))
    with tempfile.TemporaryDirectory(prefix="odh-house03-stair-") as directory:
        avatar = pathlib.Path(directory) / "User00000-idle-main.iff"
        patch_idle_main(game / "UserData/Characters/User00000.iff", avatar)
        lower = run_probe(args.binary.resolve(), game, avatar, 34, (320, 424, 1), (312, 424, 1))
        upper = run_probe(args.binary.resolve(), game, avatar, 38, (320, 344, 2), (312, 344, 2))
    print(json.dumps({"evidence": "real House03 save; synthetic avatar and waiting Main",
        "results": [lower, upper],
        "claim": "walked to each stair landing on its starting floor; no floor transition was proven"}, indent=2))


if __name__ == "__main__":
    main()
