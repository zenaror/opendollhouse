"""Optional real probe scheduling and SDL dummy output, synthetic assets only."""
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]
disabled = len(sys.argv) == 3 and sys.argv[2] == "disabled"
assert os.environ.get("SDL_AUDIODRIVER") == "dummy", "refusing physical audio"
u = lambda n: struct.pack("<I", n)


def far(entries):
    data = b"".join(value for _, value in entries)
    index = u(len(entries))
    offset = 16
    for name, value in entries:
        index += struct.pack("<IIII", len(value), len(value), offset, len(name)) + name
        offset += len(value)
    return b"FAR!byAZ" + u(1) + u(16 + len(data)) + data + index


def chunk(kind, identifier, payload):
    return kind + struct.pack(">IHH", 76 + len(payload), identifier, 0) + bytes(64) + payload


def iff(chunks):
    return b"IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1".ljust(60, b"\0") + bytes(4) + b"".join(chunks)


with tempfile.TemporaryDirectory(prefix="odh-bhav-audio-") as folder:
    p = Path(folder)
    owner, global_file, animation = (p / name for name in ("owner.far", "global.far", "animation.far"))
    hsm, hot, hit, xa = (p / name for name in ("sound.hsm", "sound.hot", "sound.hit", "fixture.xa"))
    record = struct.pack("<HIIIiiiIi", 3, 0, 0, 0, 1, 0, 0, 0x55, -1) + struct.pack("<iiiiiIiii", 16, 16, 16, 0, 0, 0x3E400000, 0, -2, 16)
    storage = struct.pack("<HIIIiiiIi", 0, 0, 0, 0, 0, 0, 0, 0, -1) + struct.pack("<iiiiiIiii", 16, 16, 16, 100, 0, 0x3E400000, 7, -2, 16)
    route = struct.pack("<HIIIiiiIi", 3, 0, 0, 0, 0, 0, 0, 0, 0) + struct.pack("<iiiiiIiii", 16, 16, 16, 0, 0, 0x3E400000, 0, -2, 16)
    slots = struct.pack("<II4sI", 0, 10, b"TOLS", 2)
    prefix = bytearray(42)
    struct.pack_into("<I", prefix, 0, 138)
    struct.pack_into("<H", prefix, 26, 129)
    struct.pack_into("<I", prefix, 28, 0x12345678)
    struct.pack_into("<H", prefix, 40, 128)
    global_file.write_bytes(far([(b"global.iff", iff([chunk(b"SLOT", 100, slots + record * 2)]))]))
    animation.write_bytes(far([(b"empty.bcf", u(0) * 3)]))
    hsm.write_text("none 0\n")
    hot_text = '[TrackData]\n0x9=0x10\n[EventMapping]\nfixture=1,7,0,0,0,0\n[Track]\n7=0,entry,1024,0,0,0,0,@none,9\n[Patch]\n9=0,fixture,"fixture.xa",0,0\n'
    hot.write_text(hot_text)
    hit_header = b"HIT!" + struct.pack("<II", 1, 8) + b"TRAX"
    hit.write_bytes(hit_header + bytes([2, 5, 11, 12]))
    xa.write_bytes(b"XAI\0" + struct.pack("<IHHIIHH", 112, 1, 1, 8000, 16000, 2, 16) + (b"\0" + b"\x11" * 14) * 2)

    def write_owner(flags=12, yes=254, event_ids=(1,), second_sound=False):
        instructions = b"".join(struct.pack("<HBB", 23, n + 1 if n + 1 < len(event_ids) else yes, 255) + bytes([event, 0, 0, 0, flags, 0, 0, 0]) for n, event in enumerate(event_ids))
        routine = struct.pack("<HHBBHHH", 0x8002, len(event_ids), 0, 4, 0, 0, 0) + instructions
        owner.write_bytes(far([(b"owner.iff", iff([
            chunk(b"BHAV", 4096, routine), chunk(b"SLOT", 128, slots + storage + route),
            chunk(b"OBJD", 16808, prefix), chunk(b"FWAV", 1, b"fixture\0"),
            *([chunk(b"FWAV", 2, b"bad\0")] if second_sound else []),
        ]))]))

    write_owner()
    base = [binary, str(owner), "owner.iff", "4096", "--run", "--world-probe", "--route-probe", "--snap-probe", "--object-def", "16808", "--global", str(global_file), "global.iff", "--animation-probe", str(animation), "empty.bcf", "--sound-probe"]
    resources = ["--audio-probe", str(hsm), str(hot), str(hit)]
    profile = ["--audio-args", "1", "1024", "0", "--audio-jump", "literal32", "--audio-gain-q15", "32768", "--audio-pan-q15", "0", "--audio-choose-first", "--audio-xa", str(xa), "--audio-rate", "8000"]

    def run(extra=()):
        return subprocess.run(base + list(extra), capture_output=True, text=True, timeout=10)

    control = run()
    assert control.returncode == 0 and not control.stderr, control.stderr
    silent = json.loads(control.stdout)
    assert silent["sound_backend"] == "none" and silent["sound_requests"][0]["status"] == "skipped_no_backend"
    if disabled:
        result = run(resources + profile)
        assert result.returncode == 2 and not result.stdout and "requires Linux SDL audio build" in result.stderr
        print("BHAV audio disabled: silent probe retained and audio profile rejected before device access")
        sys.exit(0)
    identities = {path: hashlib.sha256(path.read_bytes()).digest() for path in (owner, global_file, animation, hsm, hot, hit, xa)}
    result = run(resources + profile + ["--audio-drain-ms", "1000"])
    assert result.returncode == 0 and not result.stderr, (result.returncode, result.stdout, result.stderr)
    report = json.loads(result.stdout)
    audio = report["audio_probe"]
    assert report["status"] == silent["status"] == "returned_true" and report["steps"] == silent["steps"]
    assert report["simulation_ticks"] == silent["simulation_ticks"] == audio["simulation_ticks"] == 0
    assert report["sound_requests"][0]["status"] == "submitted"
    assert audio["profile"]["args"] == [1, 1024, 0] and audio["profile"]["chooser"] == "first"
    assert audio["requests"] == {"submitted": 1, "rejected": 0, "submit_errors": 0}
    assert audio["pcm"]["rendered"] >= 56 and audio["pcm"]["rendered"] == audio["pcm"]["submitted"] == audio["pcm"]["consumed"]
    assert not audio["pcm"]["failed"] and audio["host_drain"]["output_empty"]
    assert audio["playback_state"] == "pcm_drained_hit_pending"
    assert audio["host_drain"]["scope"] == "pcm_only" and audio["host_drain"]["requested"]
    assert audio["scheduler_ticks"] == 1
    # Drain pumps the existing transport only, never simulates a HIT completion.
    assert audio["threads"]["completed"] == 0 and audio["threads"]["active"] == 1
    assert all(hashlib.sha256(path.read_bytes()).digest() == digest for path, digest in identities.items())
    assert "native_controls" not in audio["profile"] and "hit_native_controls" not in report["sound_requests"][0]
    assert audio["profile"]["argument_mode"] == "explicit_tuple"
    assert report["sound_requests"][0]["hit_arguments"] == [1, 1024, 0]
    caller_profile = ["--audio-caller-args", *profile[4:]]
    for flags, source in [(12, 1), (14, 2)]:
        write_owner(flags=flags)
        result = run(resources + caller_profile + ["--audio-drain-ms", "1000"])
        report = json.loads(result.stdout)
        assert result.returncode == 0 and report["sound_requests"][0]["source"] == source
        assert report["sound_requests"][0]["hit_arguments"] == [source, 0, 0]
        assert report["audio_probe"]["profile"]["argument_mode"] == "play_sound_caller"
        assert report["audio_probe"]["profile"]["args"] is None
    hot.write_text(hot_text.replace("fixture=1,7,0,0,0,0", "fixture=1,7,0,-7,2147483647,0"))
    result = run(resources + caller_profile)
    report = json.loads(result.stdout)
    assert result.returncode == 0 and report["sound_requests"][0]["hit_arguments"] == [2, -7, 2147483647]
    hot.write_text(hot_text.replace("fixture=1,7,0,0,0,0", "fixture=1,7,5,0,0,0"))
    result = run(resources + caller_profile)
    report = json.loads(result.stdout)
    assert result.returncode == 0 and report["sound_requests"][0]["hit_arguments"] == [5, 0, 0]
    assert report["sound_requests"][0]["source"] == 2  # Override does not change ownership.
    hit.write_bytes(hit_header + bytes([72, 6, 1, 2, 2, 5, 11, 12]))
    result = run(resources + caller_profile)
    report = json.loads(result.stdout)
    assert result.returncode == 3 and report["audio_probe"]["threads"]["faulted"] == 1
    assert report["audio_probe"]["pcm"]["rendered"] == 0  # No alias back to owner/source2.
    hit.write_bytes(hit_header + bytes([2, 5, 11, 12]))
    hot.write_text(hot_text.replace("fixture=1,7,0,0,0,0", "fixture=1,7,0,missing,0,0"))
    result = run(resources + caller_profile)
    report = json.loads(result.stdout)
    assert result.returncode == 3 and report["sound_requests"][0]["hit_arguments"] is None
    assert report["audio_probe"]["requests"]["submit_errors"] == 1 and report["audio_probe"]["pcm"]["rendered"] == 0
    hot.write_text(hot_text)
    write_owner(event_ids=(1, 2))
    result = run(resources + caller_profile)
    report = json.loads(result.stdout)
    assert result.returncode == 0 and report["sound_requests"][0]["hit_arguments"] == [1, 0, 0]
    assert report["sound_requests"][1]["status"] == "missing_resource" and report["sound_requests"][1]["hit_arguments"] is None
    # Native controls are opt-in, raw, and separate from unchanged DSP Q15.
    native_profile = caller_profile + ["--audio-native-controls", "1024", "512"]
    native_hot = hot_text.replace("7=0,entry,1024,0,0,0,0,@none,9", "7=0,entry,1024,2,10,0,0,@none,9")
    hot.write_text(native_hot)
    write_owner(flags=14, event_ids=(1, 2))
    result = run(resources + native_profile + ["--audio-drain-ms", "1000"])
    report = json.loads(result.stdout)
    assert result.returncode == 0, (result.stdout, result.stderr)
    first, missing = report["sound_requests"]
    expected = {"17": 32, "18": 1024, "19": 1024, "20": 512, "21": 3600, "25": 10, "46": 1, "47": 2, "50": 9}
    assert first["hit_native_controls"] == expected and first["hit_arguments"] == [2, 0, 0]
    assert first["source"] == 2 and first["status"] == "submitted"
    assert missing["status"] == "missing_resource" and all(v is None for v in missing["hit_native_controls"].values())
    audio = report["audio_probe"]
    assert audio["profile"]["native_controls"] == {"profile": "fresh_argstyle2_partial", "extvol": 1024, "pan": 512}
    assert audio["profile"]["gain_q15"] == 32768 and audio["profile"]["pan_q15"] == 0
    assert audio["playback_state"] == "pcm_drained_hit_pending" and audio["simulation_ticks"] == 0
    assert audio["scheduler_ticks"] == 1 and not audio["pcm"]["failed"]
    # Raw patch local50 remains zero when the chooser selects patch9 from a list.
    list_hot = native_hot.replace("0x9=0x10", "0x1=0x10").replace("@none,9", "@1,0") + "[GlobalHitList]\n1=9\n"
    hot.write_text(list_hot)
    write_owner(flags=14)
    result = run(resources + native_profile)
    report = json.loads(result.stdout)
    assert result.returncode == 0 and report["audio_probe"]["pcm"]["rendered"] >= 56
    assert report["sound_requests"][0]["hit_native_controls"] == dict(expected, **{"50": 0})
    hot.write_text(native_hot)
    # Full signed int32 inputs are raw controls, never clamped as Q15.
    write_owner()
    result = run(resources + caller_profile + ["--audio-native-controls", "-2147483648", "2147483647"])
    report = json.loads(result.stdout)
    assert result.returncode == 0 and report["sound_requests"][0]["hit_native_controls"]["19"] == -2147483648
    assert report["sound_requests"][0]["hit_native_controls"]["20"] == 2147483647
    # A later submit that fails the profile gate must not reuse a receipt.
    hot.write_text(native_hot.replace("[Track]", "bad=1,8,0,0,0,0\n[Track]\n8=0,entry,1024,1,10,0,0,@none,9"))
    write_owner(event_ids=(1, 2), second_sound=True)
    result = run(resources + native_profile)
    report = json.loads(result.stdout)
    first, rejected = report["sound_requests"]
    assert result.returncode == 3 and first["status"] == "submitted" and first["hit_native_controls"]["19"] == 1024
    assert rejected["status"] == "backend_rejected" and all(v is None for v in rejected["hit_native_controls"].values())
    write_owner(event_ids=(1, 1))
    # Unsupported ArgStyle rejects every request before first tick/decode.
    hot.write_text(native_hot.replace(",1024,2,10,", ",1024,1,10,"))
    result = run(resources + native_profile)
    report = json.loads(result.stdout)
    assert result.returncode == 3 and report["audio_probe"]["requests"]["submit_errors"] == 2
    assert report["audio_probe"]["pcm"]["rendered"] == 0 and report["audio_probe"]["threads"]["active"] == 0
    assert all(all(v is None for v in event["hit_native_controls"].values()) for event in report["sound_requests"])
    assert "ArgStyle 2" in report["audio_probe"]["diagnostic"]
    hot.write_text(hot_text)
    write_owner()
    result = run(resources + profile)
    audio = json.loads(result.stdout)["audio_probe"]
    assert result.returncode == 0 and audio["playback_state"] == "pending_output"
    assert not audio["host_drain"]["requested"] and not audio["host_drain"]["output_empty"]
    assert audio["host_drain"]["polls"] == 0 and audio["scheduler_ticks"] == 1
    for bad in [resources + profile + ["--audio-native-controls", "0"], resources + profile + ["--audio-native-controls", "2147483648", "0"], resources + profile + ["--audio-native-controls", "0", "0", "--audio-native-controls", "0", "0"], resources, profile, resources + profile + ["--audio-rate", "7999"], resources + profile + ["--audio-drain-ms", "5001"], resources + profile + ["--audio-args", "0", "0", "0"], resources + profile + ["--audio-caller-args"], resources + caller_profile + ["--audio-args", "0", "0", "0"]]:
        result = run(bad)
        assert result.returncode == 2 and not result.stdout, (bad, result.returncode, result.stdout, result.stderr)
    hit.write_bytes(hit_header + bytes([69, 1, 2, 5, 11, 12]))
    result = run(resources + profile)
    report = json.loads(result.stdout)
    assert result.returncode == 3 and report["status"] == "returned_true"
    assert report["audio_probe"]["threads"]["faulted"] == 1 and report["audio_probe"]["pcm"]["rendered"] == 0
    hit.write_bytes(hit_header + bytes([2, 5, 11, 12]))
    hot.write_text(hot_text.replace('"fixture.xa"', '"absent.xa"'))
    result = run(resources + profile)
    report = json.loads(result.stdout)
    assert result.returncode == 3 and report["audio_probe"]["threads"]["faulted"] == 1
    hot.write_text(hot_text)
    write_owner(flags=13)
    result = run(resources + profile)
    report = json.loads(result.stdout)
    assert result.returncode == 3 and report["audio_probe"]["requests"]["rejected"] == 1
    assert report["sound_requests"][0]["loop"] and report["sound_requests"][0]["status"] == "backend_rejected"
    assert report["audio_probe"]["pcm"]["rendered"] == 0
    # A bounded drain timeout reports incomplete output without advancing simulation.
    write_owner()
    xa.write_bytes(b"XAI\0" + struct.pack("<IHHIIHH", 32000, 1, 1, 8000, 16000, 2, 16) + (b"\0" + b"\x11" * 14) * ((16000 + 27) // 28))
    result = run(resources + profile + ["--audio-drain-ms", "1"])
    report = json.loads(result.stdout)
    assert result.returncode == 3 and report["status"] == "returned_true" and report["simulation_ticks"] == 0
    audio = report["audio_probe"]
    assert audio["playback_state"] == "drain_timeout" and audio["host_drain"]["timed_out"]
    assert audio["scheduler_ticks"] == 1 and audio["host_drain"]["polls"] <= 5001
print("BHAV audio CLI: explicit profile, real scheduling/dummy output, unchanged simulation, visible faults, immutable inputs passed")
