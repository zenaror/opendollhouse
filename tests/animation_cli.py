"""Synthetic selection, byte preservation and JSON regression for resource CLIs."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

strings, animation = sys.argv[1:]
def u(n): return struct.pack('<I', n)
def pas(s): return bytes([len(s)]) + s
def clip(name=b'clip'):
    return (pas(name) + pas(b'xskill') + b''.join(u(n) for n in
            [0x42c80000, 0, 0, 1, 1, 1]) + pas(b'ROOT') +
            b''.join(u(n) for n in [1, 0x42c80000, 1, 1, 0, 0, 0, 1, 1, 100, 1]) +
            pas(b'xevt') + pas(b'100'))
def far(entries):
    data = b''.join(value for _, value in entries)
    index = u(len(entries)); offset = 16
    for name, value in entries:
        index += struct.pack('<IIII', len(value), len(value), offset, len(name)) + name
        offset += len(value)
    return b'FAR!byAZ' + u(1) + u(16+len(data)) + data + index
payload = struct.pack('<hH', -3, 2) + b'\x01a"\xff\0comment\0\x02UK\0\0'
chunk = b'STR#' + struct.pack('>IHH', 76+len(payload), 129, 0) + bytes(64) + payload
iff = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60, b'\0') + bytes(4) + chunk
bcf = u(0) + u(0) + u(1) + clip()
cfp = (b'\xff' + u(0x3f800000)) * 7
with tempfile.TemporaryDirectory(prefix='odh-animation-') as folder:
    path = Path(folder)/'fixture.far'
    standalone_iff = Path(folder)/'UIText.iff'
    duplicate_iff = Path(folder)/'duplicate.iff'
    missing_iff = Path(folder)/'missing.iff'
    standalone_iff.write_bytes(iff)
    duplicate_iff.write_bytes(iff + chunk)
    def run(binary, *args, entries=None):
        path.write_bytes(far(entries or [(b'test.iff', iff), (b'test.bcf', bcf), (b'xskill.cfp', cfp)]))
        return subprocess.run([binary, str(path), *args], capture_output=True, text=True)
    def decoded(r):
        assert r.returncode == 0 and not r.stderr, r.stderr
        return json.loads(r.stdout)
    d = decoded(run(strings, 'test.iff', '129', '1', '0'))
    assert d['format'] == -3 and d['entries'][0]['value'] == 'a"\xff'
    assert d['entries'][0]['comment'] == 'comment'
    direct = subprocess.run([strings, '--iff', str(standalone_iff), '129', '1', '0'],
                            capture_output=True, text=True)
    direct_data = decoded(direct)
    assert direct_data == d
    direct = subprocess.run([strings, '--iff', str(standalone_iff), '129'],
                            capture_output=True, text=True)
    direct_data = decoded(direct)
    assert [entry['language'] for entry in direct_data['entries']] == [1, 2]
    direct = subprocess.run([strings, '--iff', str(duplicate_iff), '129'],
                            capture_output=True, text=True)
    assert direct.returncode == 2 and 'ambiguous STR# chunk' in direct.stderr and not direct.stdout
    direct = subprocess.run([strings, '--iff', str(missing_iff), '129'],
                            capture_output=True, text=True)
    assert direct.returncode == 2 and 'cannot open IFF file' in direct.stderr and not direct.stdout
    direct = subprocess.run([strings, '--iff', str(standalone_iff)],
                            capture_output=True, text=True)
    assert direct.returncode == 2 and 'Usage:' in direct.stderr and not direct.stdout
    d = decoded(run(animation, 'test.bcf', 'CLIP', '--cfp', 'xskill.cfp'))
    assert d['evidence'] == 'animation_metadata_only'
    assert d['cfp']['raw_float_planes'] == [[0x3f800000]]*7
    assert d['animations'][0]['motions'][0]['time_properties'][0][0] == {'id':100, 'pairs':[['xevt','100']]}
    for entry, selector in [(b'Animations\\XSKILL.CFP', r'Animations\XSKILL.CFP'),
                            (b'Animations/xskill.cfp', 'Animations/xskill.cfp')]:
        nested = decoded(run(animation, 'test.bcf', 'clip', '--cfp', selector,
                             entries=[(b'test.bcf', bcf), (entry, cfp)]))
        assert nested == d
    r = run(animation, 'test.bcf', 'clip', '--cfp', r'Animations\wrong.cfp',
            entries=[(b'test.bcf', bcf), (b'Animations\\xskill.cfp', cfp)])
    assert r.returncode == 2 and 'CFP entry must match' in r.stderr and not r.stdout
    r = run(animation, 'test.bcf', 'clip', '--cfp', 'Other/xskill.cfp',
            entries=[(b'test.bcf', bcf), (b'Animations\\xskill.cfp', cfp)])
    assert r.returncode == 2 and 'exact archive entry not found' in r.stderr and not r.stdout
    for binary, args in [(strings, ('test.iff','129','3','0')),
                         (strings, ('test.iff','129junk')),
                         (strings, ('--iff',str(standalone_iff),'999')),
                         (animation, ('test.bcf','missing')),
                         (animation, ('test.bcf','clip','--cfp','wrong.cfp'))]:
        r = run(binary, *args); assert r.returncode == 2 and not r.stdout
    duplicate = u(0)+u(0)+u(2)+clip()+clip(b'CLIP')
    r = run(animation, 'test.bcf', 'clip', entries=[(b'test.bcf',duplicate)])
    assert r.returncode == 2 and 'ambiguous' in r.stderr and not r.stdout
    r = run(animation, 'test.bcf', entries=[(b'test.bcf',bcf),(b'test.bcf',bcf)])
    assert r.returncode == 2 and 'ambiguous' in r.stderr and not r.stdout
    r = run(animation, 'test.bcf', entries=[(b'test.bcf',bcf[:-1])])
    assert r.returncode == 2 and not r.stdout
print('Animation/string CLI passed (synthetic fixtures only)')
