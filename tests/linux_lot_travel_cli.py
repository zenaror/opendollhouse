"""Exercise real integrated-runtime lot travel using synthetic game resources."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from linux_household_session_resume_cli import GUIDS, archive_sections, make_household, household_object_records
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


def rebase_house(source, destination, guids, neighbor_ids, family_id):
    # GUID-only OBJT rebasing leaves the source avatars' PD31/61 behind.
    # Reuse the same authoring helper, not a compressed-bit patch or runtime override.
    replacements = dict(zip((b'ObjM', b'objt'), household_object_records(guids, neighbor_ids, family_id)))
    data = source.read_bytes()
    out = bytearray(data[:64]); seen = set()
    for kind, ident, payload, size in chunks(data):
        if kind in replacements:
            assert kind not in seen and ident == (1 if kind == b'ObjM' else 0)
            seen.add(kind); body = replacements[kind]
            header = bytearray(data[payload - 76:payload])
            struct.pack_into('>I', header, 4, 76 + len(body))
            out += header + body
        else:
            out += data[payload - 76:payload + size]
    assert seen == set(replacements)
    destination.write_bytes(out)


def make_two_occupied_lots(root):
    make_household(root)
    user = root / "UserData"

    # Reset the Sleep argument on every cycle. The shared short-run fixture
    # branches back to Sleep itself, which spins once its argument goes negative
    # at tick 5. Revisit pc0 to update each resident's independent counter too.
    for name in ("User00000.iff", "User00001.iff"):
        character = user / "Characters" / name
        data = bytearray(character.read_bytes())
        mains = [item for item in chunks(data) if item[0] == b"BHAV" and item[1] == 4096]
        assert len(mains) == 1
        payload, size = mains[0][2:]
        sleep_instruction = payload + 12 + 2 * 12
        assert size >= 48 and struct.unpack_from("<H", data, sleep_instruction)[0] == 0
        assert data[sleep_instruction + 2] == 2
        data[sleep_instruction + 2] = 0
        character.write_bytes(data)

    # Give House08 its own two Sims and household, while retaining the same
    # complete serialized lot/World structure as the existing House07 fixture.
    family8 = struct.pack("<II4siiiiiiI", 0, 9, b"IMAF", 8, 901, 100, 0, 0, 0, 2)
    extension=struct.pack("<I6h",0,0,0,0,0,0,0)
    family8 += struct.pack("<2I", *SECOND_GUIDS)+extension
    neighbors = struct.pack("<II4sI", 0, 62, b"SRBN", 4)
    # Keep the first household's roster equivalent to its source fixture and
    # append a second, independently identified household.
    first_records = bytearray()
    for index, guid in enumerate(GUIDS):
        bank = [0] * 80
        bank[31], bank[58], bank[61] = 11 + index, 27, 201
        first_records += struct.pack("<II", 1, 4) + f"Sim{index}".encode() + b"\0\0"
        first_records += struct.pack("<ii", index, 1) + struct.pack("<80h", *bank)
        first_records += struct.pack("<hIii", 11 + index, guid, -1, 0)
    # Rebuild the roster in neighbor-ID order (11, 12, 13, 14).
    second_records = bytearray()
    for index, guid in enumerate(SECOND_GUIDS, start=2):
        bank = [0] * 80
        bank[31], bank[58], bank[61] = 11 + index, 27, 202
        name = f"Travel{index - 2}".encode()
        second_records += struct.pack("<II", 1, 4) + name + b"\0"
        if len(name) % 2 == 0:
            second_records += b"\0"
        second_records += struct.pack("<ii", index, 1) + struct.pack("<80h", *bank)
        second_records += struct.pack("<hIii", 11 + index, guid, -1, 0)
    neighborhood = iff([
        ("FAMI", 201, struct.pack("<II4siiiiiiI", 0, 9, b"IMAF", 7, 900, 100, 0, 0, 0, 2)
         + struct.pack("<2I", *GUIDS)+extension),
        ("FAMI", 202, family8),
        ("NBRS", 1, neighbors + first_records + second_records),
    ])
    (user / "Neighborhood.iff").write_bytes(neighborhood)

    house07 = user / "Houses" / "House07.iff"
    rebase_house(house07, user / "Houses" / "House08.iff", SECOND_GUIDS, (13, 14), 202)
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
        source_hashes = {path.relative_to(root): hashlib.sha256(path.read_bytes()).digest()
                         for path in root.rglob("*") if path.is_file()}
        env = os.environ.copy()
        for variable in ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "ODH_STATE_DIR"):
            env[variable] = str(base / variable)
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        command = [binary, "--game-root", str(root), "--mode", "1", "--house", "7",
                   "--resident-id", "11", "--language", "1",
                   "--headless-travel-house", "8", "--headless-travel-house", "7",
                   "--headless-ticks", "2", "--instruction-budget", "25000"]
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=90)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        visits = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
        assert len(visits) == 3, (len(visits), result.stdout, result.stderr)
        assert [visit["integrated_session_context"]["house_number"] for visit in visits] == [7, 8, 7], visits
        assert [visit["session_loaded"] for visit in visits] == [False, False, True], visits
        assert [visit["resumed_tick"] for visit in visits] == [0, 0, 1], visits
        assert [visit["runtime_instruction_budget"] for visit in visits] == [25000, 25000, 25000], visits
        assert [visit["neighborhood_resume_runtime"]["tick_before"] for visit in visits[:2]] == [1, 1], visits
        assert all(not visit["neighborhood_resume_runtime"]["selected_same_lot"] for visit in visits[:2]), visits
        assert all(not visit["host_failed"] for visit in visits), visits
        namespace="neighborhood-"+hashlib.sha256((root/"UserData/Neighborhood.iff").read_bytes()).hexdigest()
        saves = base / "XDG_DATA_HOME/opendollhouse/saves" / namespace
        assert (saves / "mode-1-house-7-family-900.odhs").is_file(), "House07 save was not retained"
        assert (saves / "mode-1-house-8-family-901.odhs").is_file(), "House08 save was not retained"

        def profile_env(name):
            isolated = env.copy()
            for variable in ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "ODH_STATE_DIR"):
                isolated[variable] = str(base / name / variable)
            isolated["SDL_AUDIODRIVER"] = "dummy"
            return isolated

        def run(arguments, isolated, expected_exit=0):
            result = subprocess.run([binary, "--game-root", str(root)] + arguments,
                                    capture_output=True, text=True, env=isolated, timeout=90)
            records = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
            summary = [(record.get("integrated_session_context", {}).get("house_number"),
                        record.get("resumed_tick"), record.get("ticks"), record.get("host_failed"))
                       for record in records]
            assert result.returncode == expected_exit, (result.returncode, summary, result.stderr)
            if expected_exit == 0:
                assert all(not record["host_failed"] for record in records), records
                assert all(record["household_main"]["scheduler_entities"] == 2 for record in records), records
            return records

        delayed_env = profile_env("delayed")
        delayed = run(["--mode", "1", "--house", "7", "--resident-id", "11", "--language", "1",
                       "--headless-travel-house", "8", "--headless-travel-house", "7",
                       "--headless-travel-house", "8", "--headless-travel-tick", "3",
                       "--headless-ticks", "4", "--instruction-budget", "25000",
                       "--audio", "--save-session-on-exit"], delayed_env)
        assert len(delayed) == 4, delayed
        assert [visit["integrated_session_context"]["house_number"] for visit in delayed] == [7, 8, 7, 8], delayed
        assert [visit["session_loaded"] for visit in delayed] == [False, False, True, True], delayed
        assert [visit["resumed_tick"] for visit in delayed] == [0, 0, 3, 3], delayed
        assert [visit["neighborhood_resume_runtime"]["tick_before"] for visit in delayed[:3]] == [3, 3, 6], delayed
        assert delayed[-1]["ticks"] == 7, delayed[-1]
        for visit in delayed:
            audio = visit["session_audio"]
            assert audio["requested"] and audio["backend"] == "dummy", audio
            assert not audio["startup_error"], audio
        for visit in delayed[:3]:
            neighborhood = visit["neighborhood_resume_runtime"]
            assert not neighborhood["selected_same_lot"], neighborhood
            assert all(neighborhood[key] for key in
                       ("tick_held", "vm_held", "world_held", "relationship_counts_held")), neighborhood
        delayed_saves = Path(delayed_env["XDG_DATA_HOME"]) / "opendollhouse/saves" / namespace
        assert (delayed_saves / "mode-1-house-7-family-900.odhs").is_file()
        delayed_save = Path(delayed[-1]["saved_session_path"]).read_bytes()
        assert Path(delayed[-1]["saved_session_path"]) == delayed_saves / "mode-1-house-8-family-901.odhs"

        # The final House08 has simulated seven ticks across two visits. Compare
        # every persisted section with an uninterrupted seven-tick household run,
        # including World banks, both residents' VMs/controllers and simulation.
        control = run(["--mode", "1", "--house", "8", "--resident-id", "13", "--language", "1",
                       "--headless-ticks", "7", "--instruction-budget", "25000",
                       "--audio", "--save-session-on-exit"], profile_env("control"))
        assert len(control) == 1 and control[0]["ticks"] == 7, control
        assert not control[0]["session_loaded"], control
        control_save = Path(control[0]["saved_session_path"]).read_bytes()
        delayed_sections = {ident: (flags, body) for ident, flags, body in archive_sections(delayed_save)}
        control_sections = {ident: (flags, body) for ident, flags, body in archive_sections(control_save)}
        assert delayed_sections.keys() == control_sections.keys(), (delayed_sections.keys(), control_sections.keys())
        for ident in delayed_sections:
            assert delayed_sections[ident] == control_sections[ident], ("travel changed saved section", ident)

        invalid_schedules = [
            ["--headless-travel-house", "8", "--headless-travel-tick", "0"],
            ["--headless-travel-tick", "3"],
            ["--headless-travel-house", "8", "--headless-travel-tick", "4"],
        ]
        for index, schedule in enumerate(invalid_schedules):
            rejected_env = profile_env(f"rejected-{index}")
            records = run(["--mode", "1", "--house", "7", "--language", "1",
                           "--headless-ticks", "4", "--save-session-on-exit"] + schedule,
                          rejected_env, expected_exit=2)
            assert not records, records
            rejected_data = Path(rejected_env["XDG_DATA_HOME"])
            assert not list(rejected_data.rglob("*.odhs")), "rejected schedule published a session save"
        assert source_hashes == {path.relative_to(root): hashlib.sha256(path.read_bytes()).digest()
                                 for path in root.rglob("*") if path.is_file()}, "travel modified source game resources"
        print("Integrated runtime lot travel: House07 → House08 → restored House07 (synthetic data)")
        print("Delayed travel: House07 → House08 → House07 → House08; household state matches continuous run; audio retained")


if __name__ == "__main__":
    main()
