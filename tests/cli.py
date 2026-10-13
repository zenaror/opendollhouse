"""Integration checks for census coverage, JSON escaping and failure status.

Input is synthetic; no game assets or saved games are read.
"""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary = sys.argv[1]


def run(root):
    return subprocess.run([binary, str(root)], capture_output=True, text=True, check=False)


def require(condition, description):
    if not condition:
        raise RuntimeError(description)


with tempfile.TemporaryDirectory(prefix="odh-cli-") as folder:
    root = Path(folder)
    require(run(root).returncode == 2, "empty corpus must not report successful inspection")
    header = b"IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1"
    header = header.ljust(60, b"\0") + struct.pack(">I", 0)
    # Unknown FourCCs are preserved; quote/backslash must round-trip through JSON.
    fourcc = b'A"\\B'
    chunk = fourcc + struct.pack(">IHH", 76, 0x1234, 0) + bytes(64)
    asset = root / "valid.iff"
    asset.write_bytes(header + chunk)
    result = run(root)
    require(result.returncode == 0 and not result.stderr, "valid container failed")
    report = json.loads(result.stdout)
    require(report["chunks"][fourcc.decode("ascii")] == 1, "FourCC JSON escaping failed")
    require(report["totals"]["iff_containers"] == 1, "valid coverage count failed")

    (root / "broken.iff").write_bytes(header + b"BHAV")
    result = run(root)
    require(result.returncode == 1, "malformed input must produce nonzero status")
    report = json.loads(result.stdout)
    require(report["totals"]["errors"] == 1, "missing failure count")
    require(report["totals"]["iff_containers"] == 1, "failed resource polluted successful counters")
    require("broken.iff" in result.stderr, "missing input provenance in diagnostic")
    require(report["evidence"] == "static_structure_only", "missing evidence boundary")
    require(report["totals"]["selected_input_files"] == 2, "missing input coverage count")

    raw_type = b'\xffXYZ'
    asset.write_bytes(header + raw_type + chunk[4:])
    result = run(root)
    report = json.loads(result.stdout)
    require(report["chunks"][raw_type.decode("latin-1")] == 1, "non-ASCII FourCC broke JSON encoding")

print("inspector CLI checks passed (synthetic fixtures only)")
