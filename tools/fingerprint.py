#!/usr/bin/env python3
"""Fingerprint exactly the file classes considered by odh-inspect.

JSON on stdout. Reads resources without extracting or modifying them. Run while
the game is closed: detected concurrent changes abort instead of recording them.
The output contains relative filenames and should stay in ignored reports/.
"""
import hashlib
import json
import sys
from pathlib import Path

EXTENSIONS = {".far", ".iff", ".stx", ".spf", ".wll", ".flr"}


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 tools/fingerprint.py GAME_DIRECTORY")
    root = Path(sys.argv[1]).resolve(strict=True)
    if not root.is_dir():
        raise SystemExit("Game directory not found")
    paths = sorted(
        (p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in EXTENSIONS),
        key=lambda p: p.relative_to(root).as_posix(),
    )
    if not paths:
        raise SystemExit("No supported input files found")
    files = []
    for path in paths:
        before = path.stat()
        digest = hashlib.sha256()
        read_size = 0
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
                read_size += len(block)
        after = path.stat()
        identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
        if identity(before) != identity(after) or read_size != after.st_size:
            raise SystemExit(f"Input changed while hashing: {path.relative_to(root)}")
        files.append({"path": path.relative_to(root).as_posix(), "size": read_size, "sha256": digest.hexdigest()})
    encoded = json.dumps(files, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode("ascii")
    report = {
        "schema_version": 1,
        "profile": "odh-inspect-inputs-v1",
        "manifest_encoding": "files as ASCII JSON, ensure_ascii=true, sort_keys=true, separators=(',',':')",
        "manifest_sha256": hashlib.sha256(encoded).hexdigest(),
        "file_count": len(files),
        "bytes": sum(row["size"] for row in files),
        "files": files,
    }
    print(json.dumps(report, ensure_ascii=True, sort_keys=True, indent=2))


if __name__ == "__main__":
    main()
