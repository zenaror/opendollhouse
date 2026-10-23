"""Exercise real integrated-runtime lot travel using synthetic game resources."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from linux_household_session_resume_cli import GUIDS, make_household
from linux_session_resume_cli import iff, strings


SECOND_GUIDS = (0x22345678, 0x22345679)


def chunks(data):
    """Yield (type, id, payload offset, payload size) from a TS1 IFF file."""
    position = 64
    while position < len(data):
        if position + 76 > len(data):
            raise AssertionError("truncated IFF chunk header")
        size = struct.unpack_from(">I", data, position + 4)[0]
        if size < 76 or position + size > len(data):
            raise AssertionError("invalid IFF chunk size")
        yield (data[position:position + 4], struct.unpack_from(">H", data, position + 8)[0],
               position + 76, size - 76)
        position += size
    assert position == len(data)


def rebase_character(source, destination, guid):
    data = bytearray(source.read_bytes())
    objd = [item for item in chunks(data) if item[0] == b"OBJD"]
    assert len(objd) == 1
    payload, size = objd[0][2:]
    assert size >= 32
    struct.pack_into("<I", data, payload + 28, guid)
    destination.write_bytes(data)


def rebase_house(source, destination, guids):
    data = bytearray(source.read_bytes())
    objt = [item for item in chunks(data) if item[0] == b"objt"]
    assert len(objt) == 1
    payload, size = objt[0][2:]
    # The objt header is 12 bytes and its two person entries are 24 bytes each.
    assert size >= 60
    struct.pack_into("<I", data, payload + 12, guids[0])
    struct.pack_into("<I", data, payload + 36, guids[1])
    destination.write_bytes(data)


def make_two_occupied_lots(root):
    make_household(root)
    user = root / "UserData"

    # Give House08 its own two Sims and household, while retaining the same
    # complete serialized lot/World structure as the existing House07 fixture.
    family8 = struct.pack("<II4siiiiiiI", 0, 9, b"IMAF", 8, 901, 100, 0, 0, 0, 2)
    family8 += struct.pack("<2I", *SECOND_GUIDS)
    neighbors = struct.pack("<II4sI", 0, 62, b"SRBN", 4)
    # Keep the first household's roster equivalent to its source fixture and
    # append a second, independently identified household.
    first_records = bytearray()
    for index, guid in enumerate(GUIDS):
        bank = [0] * 80
        bank[61] = 201
        first_records += struct.pack("<II", 1, 4) + f"Sim{index}".encode() + b"\0\0"
        first_records += struct.pack("<ii", index, 1) + struct.pack("<80h", *bank)
        first_records += struct.pack("<hIii", 11 + index, guid, -1, 0)
    # Rebuild the roster in neighbor-ID order (11, 12, 13, 14).
    second_records = bytearray()
    for index, guid in enumerate(SECOND_GUIDS, start=2):
        bank = [0] * 80
        bank[61] = 201
        name = f"Travel{index - 2}".encode()
        second_records += struct.pack("<II", 1, 4) + name + b"\0"
        if len(name) % 2 == 0:
            second_records += b"\0"
        second_records += struct.pack("<ii", index, 1) + struct.pack("<80h", *bank)
        second_records += struct.pack("<hIii", 11 + index, guid, -1, 0)
    neighborhood = iff([
        ("FAMI", 201, struct.pack("<II4siiiiiiI", 0, 9, b"IMAF", 7, 900, 100, 0, 0, 0, 2)
         + struct.pack("<2I", *GUIDS)),
        ("FAMI", 202, family8),
        ("NBRS", 1, neighbors + first_records + second_records),
    ])
    (user / "Neighborhood.iff").write_bytes(neighborhood)

    house07 = user / "Houses" / "House07.iff"
    rebase_house(house07, user / "Houses" / "House08.iff", SECOND_GUIDS)
    rebase_character(user / "Characters/User00000.iff", user / "Characters/User00002.iff", SECOND_GUIDS[0])
    rebase_character(user / "Characters/User00001.iff", user / "Characters/User00003.iff", SECOND_GUIDS[1])

    locations = strings(["7,10,20", "8,30,40"])
    (user / "LotLocations.iff").write_bytes(iff([("STR#", 1, locations)]))
    (user / "LotZoning.iff").write_bytes(iff([("STR#", 1, strings(["7, residential", "8, residential"]))]))


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="odh-lot-travel-") as directory:
        base = Path(directory)
        root = base / "game"
        make_two_occupied_lots(root)
        env = os.environ.copy()
        for variable in ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "ODH_STATE_DIR"):
            env[variable] = str(base / variable)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        command = [binary, "--game-root", str(root), "--mode", "1", "--house", "7",
                   "--resident-id", "11", "--language", "1",
                   "--headless-travel-house", "8", "--headless-travel-house", "7",
                   "--headless-ticks", "2"]
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=90)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        visits = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
        assert len(visits) == 3, (len(visits), result.stdout, result.stderr)
        assert [visit["integrated_session_context"]["house_number"] for visit in visits] == [7, 8, 7], visits
        assert [visit["session_loaded"] for visit in visits] == [False, False, True], visits
        assert [visit["resumed_tick"] for visit in visits] == [0, 0, 1], visits
        assert [visit["neighborhood_resume_runtime"]["tick_before"] for visit in visits[:2]] == [1, 1], visits
        assert all(not visit["neighborhood_resume_runtime"]["selected_same_lot"] for visit in visits[:2]), visits
        assert all(not visit["host_failed"] for visit in visits), visits
        saves = base / "XDG_DATA_HOME/opendollhouse/saves"
        assert (saves / "mode-1-house-7-family-900.odhs").is_file(), "House07 save was not retained"
        assert (saves / "mode-1-house-8-family-901.odhs").is_file(), "House08 save was not retained"
        print("Integrated runtime lot travel: House07 → House08 → restored House07 (synthetic data)")


if __name__ == "__main__":
    main()
