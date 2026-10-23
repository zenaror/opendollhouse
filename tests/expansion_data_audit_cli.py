"""Synthetic integration tests for the read-only expansion data audit."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

script = Path(sys.argv[1]).resolve()
binary = Path(sys.argv[2]).resolve()
checks = 0


def check(value, why):
    global checks
    checks += 1
    assert value, f"check {checks}: {why}"


def iff(chunks):
    return b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE\0 JAMIE DOORNBOS & MAXIS 1'.ljust(60, b'\0') + bytes(4) + b''.join(chunks)


def chunk(kind, ident, data):
    return kind + struct.pack('>IHH', 76 + len(data), ident, 0) + bytes(64) + data


def locations(ident, rows):
    return chunk(b'STR#', ident, struct.pack('<hH', -3, len(rows)) + b''.join(b'\1' + row.encode() + b'\0\0' for row in rows))


def family(ident, house, guids):
    header = struct.pack('<II4siiiiiiI', 0, 9, b'IMAF', house, 400 + house, 100, 0, 0, 0, len(guids))
    return chunk(b'FAMI', ident, header + struct.pack('<' + 'I' * len(guids), *guids))


with tempfile.TemporaryDirectory(prefix='odh-expansion-audit-') as tmp:
    root = Path(tmp)
    user_data = root / 'UserData'
    houses = user_data / 'Houses'
    houses.mkdir(parents=True)
    (root / 'TemplateVacation').mkdir()
    (root / 'TemplateVacation' / 'House40.iff').write_bytes(b'fixture')
    (root / 'fake.FAR').write_bytes(b'fixture')
    (houses / 'House07.iff').write_bytes(iff([]))
    (user_data / 'LotLocations.iff').write_bytes(iff([
        locations(1, ['7,10,20', '8,30,40', '99,0,0']),
        locations(2, ['7,1,2']), locations(3, ['9,3,4']), locations(4, ['10,5,6']),
        locations(5, ['11,7,8']), locations(6, ['12,9,10']), locations(7, ['13,11,12']),
    ]))
    (user_data / 'Neighborhood.iff').write_bytes(iff([family(200, 7, [0x80000000, 0xFFFFFFFF])]))
    result = subprocess.run([sys.executable, str(script), '--binary', str(binary), '--installation', str(root)], capture_output=True, text=True)
    check(result.returncode == 0, result.stderr)
    report = json.loads(result.stdout)
    check(report['evidence'] == 'data_catalog_coverage_only' and report['playable_coverage'] is False, 'explicit non-playable evidence label')
    check([row['mode'] for row in report['modes']] == list(range(1, 8)), 'all seven modes invoked')
    mode1 = report['modes'][0]
    check(mode1['catalog_lot_rows'] == 2 and mode1['house_files_present'] == 1, 'sentinel omitted and actual house presence counted')
    check(mode1['missing_referenced_house_files'] == [str(houses / 'House08.iff')], 'missing catalog-referenced house reported')
    check(mode1['fami_associations'] == 1 and mode1['fami_member_entries'] == 2, 'FAMI and member counts')
    check(report['modes'][5]['status'] == 'unsupported' and report['modes'][5]['catalog_lot_rows'] == 0, 'mode 6 counted as unsupported')
    check(report['expansion_template_directories'] == [{'directory': 'TemplateVacation', 'iff_file_count': 1}], 'template folder/file presence')
    check(report['far_archive_count'] == 1, 'FAR presence count')
    print(f'Expansion data audit CLI: {checks} checks passed (synthetic only)')
