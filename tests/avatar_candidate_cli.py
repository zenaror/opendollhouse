#!/usr/bin/env python3
"""Integrated saved-lot EP5/EP26 CLI test; synthetic resources only."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from linux_session_resume_cli import Bits, far, iff, saved_person, strings

BINARY = str(Path(sys.argv[1]).resolve())
ACTOR_GUID = 0x11111111
BLOCKER_GUID = 0x22222222
LATER_GUID = 0x33333333

def grid(stride, data):
    out = struct.pack("<5I", 0, 64, 64, stride, 0)
    for pos in range(0, len(data), 16384):
        part = data[pos:pos+16384]
        out += struct.pack("<H", len(part)) + part + bytes(len(part) % 2)
    return out

def material_map(ident, name, alias):
    encoded = name.encode("ascii")
    return (struct.pack("<4I", 0, 1, 0, 1) + encoded + b"\0" + bytes(not len(encoded) % 2) +
            struct.pack("<IB", 0, alias) + bytes(5 + 2 * ident))

def objd(guid, kind, ep5=0, ep26=0):
    data = bytearray(162)
    struct.pack_into("<I", data, 0, 138)
    struct.pack_into("<H", data, 10, 4096)
    struct.pack_into("<H", data, 18, kind)
    struct.pack_into("<H", data, 38, 200)
    struct.pack_into("<I", data, 28, guid)
    struct.pack_into("<H", data, 42, ep5)
    struct.pack_into("<H", data, 64, ep26)
    struct.pack_into("<H", data, 112, 0)  # LevelOffset
    struct.pack_into("<H", data, 134, 16) # TileWidth
    struct.pack_into("<H", data, 154, 0)  # FootprintMask
    return bytes(data)

def routine(rows):
    return struct.pack("<HHBBHHH", 0x8002, len(rows), 0, 4, 0, 0, 0) + b"".join(
        struct.pack("<HBB", op, yes, no) + operands for op, yes, no, operands in rows)

def objf(actions):
    entries = list(actions) + [0] * (33 - len(actions))
    return (struct.pack("<II4sI", 0, 1, b"fJBO", len(entries)) +
            b"".join(struct.pack("<HH", 0, action) for action in entries))

def ep5_bhav(guid):
    return routine([
        (32, 1, 255, struct.pack("<I H B B", guid, 0, 10, 0)),
        (2, 254, 255, bytes([16, 0, 1, 0, 0, 2, 1, 7])),
    ])

def object_block(object_id, x, y, flags, attr16):
    b = Bits()
    for value in (0, 0, 0, 0, x, y, 1): b.integer(value)
    b.word(); b.word(17)
    for n in range(17): b.word(attr16 if n == 16 else 0)
    for _ in range(8): b.word()
    for n in range(68):
        b.word(object_id if n == 11 else -1 if n == 3 else flags if n == 8 else attr16 if n == 16 else 0)
    for _ in range(5): b.word()
    for value in (0, 0, -1, 0): b.integer(value)
    b.word(); b.word(); b.word(); b.integer()
    return b.data()

def objm_and_objt(entities):
    ids = Bits()
    for object_id, type_id, *_ in entities:
        ids.word(object_id); ids.word(type_id)
    ids.word()
    payload = struct.pack("<II4sB", 0, 62, b"MjbO", 1) + ids.data()
    person_single, _ = saved_person(1, 1)
    person = person_single[22:]  # helper's generated Person record after its offset word
    blocks = [person if row[1] == 1 else object_block(row[0], row[2], row[3], row[4], row[5])
              for row in entities]
    for block in blocks:
        payload += struct.pack("<I", len(payload) + 4 + len(block) - 12) + block
    # OBJT uses type IDs 1=Person, 2=regular object, 3=second regular object.
    table = struct.pack("<II4s", 0, 2, b"tjbo")
    definitions = [(ACTOR_GUID, 1, 2, "person"),
                   (BLOCKER_GUID, 2, 4, "blocker"),
                   (LATER_GUID, 3, 4, "later")]
    for guid, type_id, serialized_type, name in definitions:
        table += struct.pack("<I6H", guid, 0, 0, 0, 0, type_id, serialized_type)
        encoded = name.encode() + b"\0"
        table += encoded + bytes(len(encoded) % 2)
    return payload, table

def make_lot(path, later=False, blocker_attribute16=0, early_stop=False):
    entities = [(1, 1)]
    entities.append((2, 2, 520, 504, 2, blocker_attribute16))
    if later:
        entities.append((3, 3, 520, 504, 2, 0))
    objm, objt = objm_and_objt(entities)
    floor = bytearray(4096)
    floor[31 * 64 + 31] = 42
    floor[31 * 64 + 32] = 42
    wall = bytes(4096 * 8)
    chunks = [("SIMI", 1, struct.pack("<II4s", 0, 0x3e, b"IMIS") + bytes(64 + 34 + 24)),
              ("ObjM", 1, objm), ("objt", 0, objt)]
    for ident, stride, data in [
        (1, 1, floor), (101, 1, bytes(4096)), (2, 8, wall), (102, 8, wall),
        (8, 1, bytes(4096)), (9, 1, bytes(4096)), (10, 1, bytes(4096)),
        (0, 4, bytes(4096 * 4))]:
        chunks.append(("Arry", ident, grid(stride, data)))
    chunks += [("FLRm", 1, material_map(1, "tile.flr", 42)),
               ("WALm", 1, material_map(1, "paint.wll", 43))]
    path.write_bytes(iff(chunks))

def write_resources(root, later, early_stop, incomplete_blocker=False):
    actor = root / "actor.iff"
    actor_chunks = [("OBJD", 128, objd(ACTOR_GUID, 2, 4108)),
                    ("GLOB", 128, b"\x0dFixturePerson\xa3"),
                    ("STR#", 200, strings(["adult"] + [""] * 11 + ["male", "27"] + [""] * 13)),
                    ("BHAV", 4096, routine([(0, 254, 255, bytes(8))])),
                    ("BHAV", 4108, ep5_bhav(BLOCKER_GUID))]
    actor.write_bytes(iff(actor_chunks))
    semiglobal = root / "semi.far"
    global_file = root / "global.far"
    far(semiglobal, [("FixturePerson.iff", iff([]))])
    far(global_file, [("Global.iff", iff([("STR#", 150, strings(["", "walk"])), ("STR#", 158, strings(["", "swim"]))]))])
    definitions = root / "definitions.far"
    block_ep5 = 4097 if later else 0
    first = bytearray(objd(BLOCKER_GUID, 4, 0, 273 if early_stop else 0))
    blocker_chunks = [("OBJD", 128, first)]
    if incomplete_blocker:
        # Keep EntryPoints in OBJf so the saved object remains loadable while
        # its OBJD stops after LevelOffset, before collision metadata.
        struct.pack_into("<H", first, 44, 1)  # UsesFnTable
        first = bytes(first[:114])
        blocker_chunks = [("OBJD", 128, first), ("OBJf", 128, objf([0] * 33))]
    else:
        first = bytes(first)
    second = objd(LATER_GUID, 4, block_ep5, 0)
    far(definitions, [("blocker.iff", iff(blocker_chunks)),
                      ("later.iff", iff([("OBJD", 129, second)])),
                      ("actor.iff", iff([("OBJD", 128, objd(ACTOR_GUID, 2, 4108)), ("BHAV", 4108, ep5_bhav(BLOCKER_GUID))]))])
    floors = root / "floors.iff"
    floors.write_bytes(iff([]))
    tile = root / "tile.flr"
    sprite = struct.pack("<4I2H3I", 1000, 1, 7, 16, 2, 2, 1, 0, 0)
    sprite += struct.pack("<HH2s", 6, (6 << 13) | 2, b"\0\0") * 2 + struct.pack("<H", 5 << 13)
    palette = struct.pack("<4I", 1, 1, 0, 0) + bytes([180, 180, 180])
    tile.write_bytes(iff([("PALT", 7, palette), ("SPR2", 513, sprite)]))
    return actor, semiglobal, global_file, definitions, floors, tile

def run_case(root, blocker_attribute16, later=False, early_stop=False,
             incomplete_blocker=False):
    lot = root / "House07.iff"
    make_lot(lot, later=later, blocker_attribute16=blocker_attribute16, early_stop=early_stop)
    actor, semi, glob, definitions, floors, tile = write_resources(
        root, later, early_stop, incomplete_blocker)
    cmd = [BINARY, "--avatar-file", str(actor), "128", "--semiglobal", str(semi), "FixturePerson.iff",
           "--global", str(glob), "Global.iff", "--lot", str(lot), "7", "--headless-main",
           "--render-saved-avatar", "1", "--render-floors", str(floors),
           "--object-definitions", str(definitions), "--placement-candidate", "520", "504", "1", "0",
           "--architecture-resource", str(tile)]
    env = os.environ.copy()
    for name in ("DISPLAY", "WAYLAND_DISPLAY"): env.pop(name, None)
    for name in ("XDG_STATE_HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME"): env[name] = str(root / name)
    result = subprocess.run(cmd, text=True, capture_output=True, env=env, timeout=30)
    assert result.returncode in (0, 3), (result.returncode, result.stdout, result.stderr)
    report = json.loads(result.stdout)
    assert not report["host_failed"], report["host_diagnostic"]
    return report

def main():
    with tempfile.TemporaryDirectory(prefix="odh-avatar-candidate-cli-") as directory:
        root = Path(directory)
        clear = run_case(root, blocker_attribute16=1)
        clear_probe = clear["placement_candidate_probe"]
        assert clear_probe["candidate_probe_moves_avatar"] is False
        assert clear_probe["entity_overlap_policy_audit"]["status"] == "allowed", clear_probe
        assert clear_probe["bhav_executed"] is True, clear_probe
        assert clear_probe["room_info_status"] == "complete", clear_probe
        assert any(item["status"] == "evaluated_true"
                   for item in clear_probe["entry_point5_bhav_evidence"]), clear_probe
        blocked = run_case(root, blocker_attribute16=0)
        blocked_probe = blocked["placement_candidate_probe"]
        assert blocked_probe["entity_overlap_policy_audit"]["status"] == "blocked", blocked_probe
        assert blocked_probe["bhav_executed"] is True, blocked_probe
        assert any(item["status"] == "evaluated_false"
                   for item in blocked_probe["entry_point5_bhav_evidence"]), blocked_probe
        stop = run_case(root, blocker_attribute16=0, later=True, early_stop=True)
        stop_probe = stop["placement_candidate_probe"]
        audit = stop_probe["entity_overlap_policy_audit"]
        # The aggregate EP5 log also contains independent route probes. The
        # overlap audit itself proves it stopped after blocker 2 via this pair count.
        assert {item["owner_id"] for item in stop_probe["intersections"]} == {2, 3}, stop_probe
        assert (audit["status"] == "blocked" and audit["final_blocker_id"] == 2 and
                audit["early_stop_known"] is True and audit["pair_classifications"] == 1), audit
        incomplete = run_case(root, blocker_attribute16=1, incomplete_blocker=True)
        incomplete_probe = incomplete["placement_candidate_probe"]
        assert incomplete_probe["room_info_status"] == "unsupported", incomplete_probe
        assert incomplete_probe["room_info_reason"] == "inventory_incomplete", incomplete_probe
        assert incomplete_probe["intersections"] == [], incomplete_probe
        incomplete_audit = incomplete_probe["entity_overlap_policy_audit"]
        assert (incomplete_audit["status"] == "unsupported" and
                incomplete_audit["reason"] == "room_info_incomplete" and
                incomplete_audit["pair_classifications"] == 0), incomplete_audit
        assert incomplete_probe["architecture_walkability_audit"]["status"] == "unsupported", incomplete_probe
    print("synthetic saved-lot CLI: EP5 true/false, EP26 early stop, and incomplete collision inventory passed")


if __name__ == "__main__":
    main()
