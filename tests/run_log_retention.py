#!/usr/bin/env python3
"""Verify that the last run survives cache deletion, without launching a game."""
import hashlib
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from preserve_run_log import preserve_run_log

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    cache, report = root / 'cache', root / 'runtime-last'
    cache.mkdir(); report.mkdir()
    content = b'final-only action and committed transaction\n' * 32000
    source = cache / 'opendollhouse.log'
    source.write_bytes(content)
    receipt = preserve_run_log(source, report)
    assert receipt == {'path': 'product.log', 'bytes': len(content),
                       'sha256': hashlib.sha256(content).hexdigest()}
    try:
        preserve_run_log(source, report)
    except FileExistsError:
        pass
    else:
        raise AssertionError('overwrote unique preserved log')
    shutil.rmtree(cache)
    assert (report / receipt['path']).read_bytes() == content
    try:
        preserve_run_log(source, report)
    except FileNotFoundError:
        pass
    else:
        raise AssertionError('missing final log accepted')
print('PASS last-run log survives cache deletion; missing input and overwrite refused')
