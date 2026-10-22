"""Synthetic argument/resource failures, without opening an X11 window."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
binary = sys.argv[1]
checks = 0

def check(value):
    global checks
    checks += 1
    assert value, checks

def run(*args):
    return subprocess.run([binary, *args], capture_output=True, text=True, timeout=10)

with tempfile.TemporaryDirectory(prefix='odh-map-cli-') as folder:
    root = Path(folder)
    args = ['--user-data', str(root), '--ui-graphics', str(root/'UIGraphics.far')]
    result = run(*args, '--mode', '6')
    check(result.returncode == 0 and json.loads(result.stdout) == {
        'evidence': 'map_selection_only', 'mode': 6, 'status': 'unsupported', 'selected_house': None})
    for extra in [[], ['--mode'], ['--mode','0'], ['--mode','8'], ['--mode','-1'],
                  ['--mode','1x'], ['--mode',''], ['--mode','1','--mode','1'],
                  ['--mode','1','--house','7'], ['--mode','1','--user-data',str(root)]]:
        result = run(*args, *extra)
        check(result.returncode == 2 and not result.stdout)
    # An empty FAR1 archive is structurally valid but has no exact map resource.
    (root/'UIGraphics.far').write_bytes(b'FAR!byAZ'+struct.pack('<III',1,16,0))
    for mode in [1,2,3,4,5,7]:
        result = run(*args, '--mode', str(mode))
        check(result.returncode == 2 and not result.stdout and 'exact archive entry not found' in result.stderr)
print(f'Neighborhood window CLI: {checks} checks passed (synthetic, no GUI)')
