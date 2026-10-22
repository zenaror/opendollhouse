#!/usr/bin/env python3
"""Probe the real House03 stair landings with a synthetic waiting Main/avatar."""
import argparse
import hashlib
import json
import math
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
              portal_id: int, start: tuple[int, int, int], expected: tuple[int, int, int],
              age: int, render_tick: int | None = None,
              render_output: pathlib.Path | None = None,
              render_level: int | None = None,
              occupant_avatar: tuple[int, int, int, int, int] | None = None,
              expect_occupied_landing: bool = True):
    global_far = game / "GameData/Global/Global.far"
    animation_far = game / "GameData/Animation/Animation.far"
    expansion_far = game / "ExpansionShared/ExpansionShared.far"
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
        "--age", str(age), "--animation-source", str(animation_far),
        "--animation-source", str(expansion_far), "--ticks", "300"]
    if occupant_avatar is not None:
        command += ["--headless-fixture-occupant-avatar",
            *(str(value) for value in occupant_avatar)]
    if (render_tick is None) != (render_output is None):
        raise ValueError("render tick and output path must be provided together")
    if render_tick is not None:
        command += ["--render-animation-far", str(animation_far),
            "--render-textures", str(game / "GameData/Textures/Textures.far"),
            "--render-walls", str(game / "GameData/walls.iff"),
            "--render-skeleton", "adult-skeleton.cmx.bcf", "adult",
            # Show the lower floor so the upper floor does not cover the
            # animated actor; this changes only the view, not the World.
            "--render-level", str(render_level or 1),
            "--render-tick", str(render_tick), "--render-frame", str(render_output)]
    for resource in architecture_resources(game):
        command += ["--architecture-resource", str(resource)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if not result.stdout:
        raise RuntimeError(result.stderr.strip() or f"odh-person exited {result.returncode} without JSON")
    data = json.loads(result.stdout)
    portal = data["portal_runtime"]
    route = data["route_runtime"]
    end = portal["actor_position"]
    snap = next((entry for entry in reversed(data["trace"])
                 if entry["opcode"] == 46), None)
    if route["movement_callbacks"] <= 0:
        raise AssertionError(f"portal {portal_id} did not walk: {portal}; route={route}")
    if occupant_avatar is not None and expect_occupied_landing:
        stair_snaps = [entry for entry in data["trace"]
                       if entry["routine"] == 4113 and entry["opcode"] == 46]
        continued_to_pc23 = any(entry["routine"] == 4113 and entry["pc"] == 23 and
                                entry["step"] > stair_snaps[0]["step"]
                                for entry in data["trace"]) if stair_snaps else False
        if (portal["status"] != "returned_false" or portal["actor_level_changed"] or
                end[:3] != portal["portal_position"][:3] or
                not any(entry["operands"][4] == 2 for entry in stair_snaps) or not continued_to_pc23):
            raise AssertionError(f"second avatar at stair arrival did not stop safely: {portal}; snaps={stair_snaps}")
        return {"portal_id": portal_id, "age": age,
            "occupant_avatar": occupant_avatar, "start": portal["actor_start_position"],
            "end": end, "movement_callbacks": route["movement_callbacks"],
            "portal_status": portal["status"], "level_changed": portal["actor_level_changed"],
            "snap_attempts": [{"step": entry["step"], "flag": entry["operands"][4]}
                              for entry in stair_snaps],
            "continued_to_pc23_after_blocked_landing": continued_to_pc23}
    if end[:3] != list(expected):
        raise AssertionError(f"portal {portal_id} ended at {end}, expected stair landing {expected}")
    if not portal["actor_level_changed"]:
        raise AssertionError(f"the Sim reached the stair but did not change floors: {portal}")
    if portal["status"] != "returned_true" or portal["diagnostic"]:
        raise AssertionError(f"the stair action did not finish cleanly: {portal}")
    if not snap or snap["routine"] != 4113 or snap["operands"] != [0, 0, 3, 0, 2, 0, 0, 0]:
        raise AssertionError(f"could not confirm the expected stair Snap instruction: {snap}")
    next_primitive = next((entry for entry in data["trace"]
                           if entry["opcode"] == 44 and entry["step"] > snap["step"]), None)
    if not next_primitive:
        raise AssertionError(f"could not confirm that the VM continued after the stair Snap: {data['trace'][-8:]}")
    animation_events = data.get("animation_events", [])
    if data.get("animation_resolution_diagnostic") or not animation_events:
        raise AssertionError(f"the stair animation did not resolve and produce events: {data.get('animation_resolution_diagnostic')}")
    if data["actor_state"]["age"] != age:
        raise AssertionError(f"fixture age mismatch: expected {age}, got {data['actor_state']['age']}")
    render = data.get("render")
    if render_tick is not None:
        if not render or render.get("output") != str(render_output):
            raise AssertionError(f"no frame was captured at tick {render_tick}: {render}")
        if render.get("actor") != 30000 or render.get("tick") != render_tick:
            raise AssertionError(f"captured frame has the wrong actor/tick: {render}")
        if render.get("shaded_samples", 0) <= 0:
            raise AssertionError(f"captured frame has no shaded pixels: {render}")
        if render.get("actor_raster_pixels", 0) <= 0:
            raise AssertionError(f"the stair animation produced no actor pixels: {render}")
        if render.get("actor_composited_pixels", 0) <= 0:
            raise AssertionError(f"the Sim is fully covered in the composed lot image: {render}")
        if "stair" not in render.get("clip", "").lower():
            raise AssertionError(f"captured frame is not a stair animation: {render}")
        anchor = portal.get("portal_position")
        camera_center = render.get("camera_center_tile")
        if (not anchor or not camera_center or
                not math.isclose(camera_center[0], anchor[0] / 16 - 0.5, abs_tol=0.01) or
                not math.isclose(camera_center[1], anchor[1] / 16 - 0.5, abs_tol=0.01)):
            raise AssertionError(f"camera is not centered on the live stair object: "
                f"camera={camera_center}, object={anchor}")
        if not render.get("lot_composited") or not render.get("lot_objects_included"):
            raise AssertionError(f"captured frame does not include the full lot object pass: {render}")
        lot = render.get("lot") or {}
        if not lot.get("objects_rendered") or lot.get("object_layers", 0) <= 0:
            raise AssertionError(f"captured frame did not draw any lot objects: {lot}")
        if lot.get("wall_layers", 0) <= 0:
            raise AssertionError(f"captured frame did not draw any lot walls: {lot}")
        image = render_output.read_bytes()
        if len(image) <= 320 * 240 * 3:
            raise AssertionError(f"captured PPM is incomplete: {render_output}")
        render["image_sha256"] = hashlib.sha256(image).hexdigest()
    return {"portal_id": portal_id, "age": age, "occupant_avatar": occupant_avatar,
            "start": portal["actor_start_position"],
            "end": end, "movement_callbacks": route["movement_callbacks"],
            "portal_status": portal["status"], "animation_events": len(animation_events),
            "snap_instruction": {"routine": snap["routine"], "pc": snap["pc"],
                "operands": snap["operands"]},
            "level_changed": portal["actor_level_changed"],
            "next_instruction": {"opcode": next_primitive["opcode"],
                "routine": next_primitive["routine"], "pc": next_primitive["pc"]},
            "render": render}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--game-root", required=True, type=pathlib.Path)
    parser.add_argument("--capture-tick", type=int, action="append",
        help="also capture the live stair Sim at this playback tick; repeat to compare frames")
    parser.add_argument("--capture-output", type=pathlib.Path,
        help="new PPM path for --capture-tick")
    parser.add_argument("--capture-portal", type=int, choices=(34, 38), default=34,
        help="stair landing used for optional captures (34 up, 38 down)")
    parser.add_argument("--capture-level", type=int, choices=(1, 2),
        help="optional floor shown in the capture; defaults to floor 1 to reveal the stair pose")
    args = parser.parse_args()
    if bool(args.capture_tick) != (args.capture_output is not None):
        parser.error("--capture-tick and --capture-output must be used together")
    game = args.game_root.resolve()
    required = [args.binary, game / "GameData/Global/Global.far",
        game / "GameData/Objects/Objects.far", game / "GameData/floors.iff",
        game / "GameData/Animation/Animation.far", game / "ExpansionShared/ExpansionShared.far",
        game / "UserData/Characters/User00000.iff", game / "UserData/Characters/User00001.iff",
        game / "UserData/Houses/House03.iff", game / "UserData/LotZoning.iff",
        game / "UserData/Neighborhood.iff"]
    if args.capture_tick:
        required.append(game / "GameData/walls.iff")
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("required game/test file missing: " + ", ".join(missing))
    with tempfile.TemporaryDirectory(prefix="odh-house03-stair-") as directory:
        avatar = pathlib.Path(directory) / "User00000-idle-main.iff"
        patch_idle_main(game / "UserData/Characters/User00000.iff", avatar)
        results = [
            run_probe(args.binary.resolve(), game, avatar, portal_id, start, end, age)
            for age in (0, 21)
            for portal_id, start, end in (
                (34, (320, 424, 1), (312, 344, 2)),
                (38, (320, 344, 2), (312, 424, 1)))
        ]
        occupant_case = run_probe(args.binary.resolve(), game, avatar, 34,
            (320, 424, 1), (312, 344, 2), 21,
            occupant_avatar=(30001, 312, 344, 2, 0))
        results.append(occupant_case)
        nearby_avatar_case = run_probe(args.binary.resolve(), game, avatar, 34,
            (320, 424, 1), (312, 344, 2), 21,
            occupant_avatar=(30001, 328, 344, 2, 0), expect_occupied_landing=False)
        nearby_avatar_case["same_room_avatar_away_from_arrival"] = nearby_avatar_case.pop("occupant_avatar")
        results.append(nearby_avatar_case)
        captures = []
        for index, tick in enumerate(args.capture_tick or []):
            if len(args.capture_tick) == 1:
                output = args.capture_output.resolve()
            else:
                output = args.capture_output.with_name(
                    f"{args.capture_output.stem}-tick{tick}{args.capture_output.suffix}").resolve()
            portal_id, start, end = ((34, (320, 424, 1), (312, 344, 2))
                if args.capture_portal == 34 else
                (38, (320, 344, 2), (312, 424, 1)))
            result = run_probe(args.binary.resolve(), game, avatar, portal_id,
                start, end, 21, tick, output, args.capture_level)
            results.append(result)
            captures.append(result["render"])
        if len(captures) > 1:
            if len({capture["frame"] for capture in captures}) != len(captures):
                raise AssertionError(f"requested ticks did not sample distinct stair frames: {captures}")
            if len({capture["image_sha256"] for capture in captures}) != len(captures):
                raise AssertionError(f"distinct stair frames produced identical images: {captures}")
    print(json.dumps({"evidence": "real House03 save; synthetic avatar and waiting Main",
        "results": results,
        "claim": "walked to each stair landing, executed its stair animation, changed floors in both directions, and completed the portal action for adult and child fixture ages; an additional synthetic-avatar case blocks the occupied arrival square and confirms BHAV4113 continues to PC23 before returning false; it does not move the blocker or claim equivalence to the original game; optional captures compose the live stair pose with saved lot floors, walls, and objects"}, indent=2))


if __name__ == "__main__":
    main()
