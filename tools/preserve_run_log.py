#!/usr/bin/env python3
"""Preserve a closed test process's active log before cleaning its cache."""
import hashlib
from pathlib import Path


def preserve_run_log(source, report_directory):
    """Copy to a new product.log; return a receipt only for verified stable bytes.

    The caller must close the owned process first. This does not prove that
    its commands succeeded or that the saved log is already backed up remotely.
    """
    source, report_directory = Path(source), Path(report_directory)
    before = source.stat()
    target = report_directory / 'product.log'
    digest = hashlib.sha256()
    size = 0
    with source.open('rb') as original, target.open('xb') as preserved:
        for data in iter(lambda: original.read(1024 * 1024), b''):
            preserved.write(data)
            digest.update(data)
            size += len(data)
    after = source.stat()
    if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
            != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)
            or size != before.st_size):
        raise ValueError('active log changed while copying; do not clean its cache')
    if hashlib.sha256(target.read_bytes()).hexdigest() != digest.hexdigest():
        raise ValueError('preserved log differs; do not clean its cache')
    return {'path': target.name, 'bytes': size, 'sha256': digest.hexdigest()}
