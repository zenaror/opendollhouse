"""Synthetic archive integration: explicit table states and ambiguous selection."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]


def chunk(payload):
    return b"TTAB" + struct.pack(">IHH", 76 + len(payload), 128, 0) + bytes(64) + payload


def archive(payload, duplicate_entry=False, duplicate_chunk=False):
    header = b"IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1"
    asset = header.ljust(60, b"\0") + bytes(4) + chunk(payload)
    if duplicate_chunk:
        asset += chunk(payload)
    name = b"test.iff"
    entry = struct.pack("<IIII", len(asset), len(asset), 16, len(name)) + name
    return (b"FAR!byAZ" + struct.pack("<II", 1, 16 + len(asset)) + asset +
            struct.pack("<I", 2 if duplicate_entry else 1) + entry * (2 if duplicate_entry else 1))


def require(value, message):
    if not value:
        raise RuntimeError(message)


with tempfile.TemporaryDirectory(prefix="odh-ttab-") as folder:
    path = Path(folder) / "synthetic.far"

    def run(payload, chunk_id="128", **options):
        path.write_bytes(archive(payload, **options))
        return subprocess.run([binary, str(path), "test.iff", chunk_id], capture_output=True, text=True)

    raw = struct.pack("<HHHHIIIIIII", 1, 8, 0x1001, 0x1002, 0, 0, 0, 0, 0, 0, 0xFFFFFFFF)
    for identifier in ["128", "0x80", "0128"]:
        result = run(raw, identifier)
        require(result.returncode == 0, "valid TTAB selector rejected")
        row = json.loads(result.stdout)["interactions"][0]
        require(row["action"] == 0x1001 and row["test"] == 0x1002 and row["joining_index"] == -1,
                "decoded fields mismatch")
    result = run(b"")
    require(result.returncode == 3 and json.loads(result.stdout)["status"] == "absent_payload",
            "absent payload was silently accepted")
    result = run(struct.pack("<HH", 1, 3))
    require(result.returncode == 3 and json.loads(result.stdout)["status"] == "legacy_unsupported",
            "legacy table was silently accepted")
    result = run(bytes(2))
    require(result.returncode == 0 and json.loads(result.stdout)["status"] == "empty", "empty count failed")
    for options in [{"duplicate_entry": True}, {"duplicate_chunk": True}]:
        result = run(raw, **options)
        require(result.returncode == 2 and "ambiguous" in result.stderr and not result.stdout,
                "ambiguous selection did not fail")
    for identifier in ["-1", "65536", "128garbage", "129"]:
        require(run(raw, identifier).returncode == 2, "invalid or absent selector accepted")

print("TTAB CLI checks passed (synthetic fixtures only)")
