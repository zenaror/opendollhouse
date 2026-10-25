"""Synthetic raw IFF coverage for the read-only family profile inspector."""

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import family_profile_report as report  # noqa: E402


GUID = 0x12345678


def chunk(kind, number, payload):
    return kind + struct.pack('>IHH', 76 + len(payload), number, 0) + bytes(64) + payload


def iff(*chunks):
    signature = b'IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1'
    return signature.ljust(60, b'\0') + bytes(4) + b''.join(chunks)


def neighborhood(include_family=True, duplicate=False, person_data_61=200,
                 writer_early_eof=False, truncate_record=False):
    fami = struct.pack('<II4siiiiiiII', 0, 9, b'IMAF', 9, 3, 16500,
                       0, 0, 24, 1, GUID) + bytes(16)
    fams = struct.pack('<hH', -3, 1) + b'\1Household\0\0'
    person_data = [0] * 256
    person_data[61] = person_data_61
    name = b'Alex\0\0'  # even-length name has one padding byte after NUL.
    neighbor = struct.pack('<iii', 1, 10, 9) + name + struct.pack('<ii', 0, 9)
    neighbor += struct.pack('<256h', *person_data)
    neighbor += struct.pack('<hIii', 11, GUID, -1, 0)
    if truncate_record:
        neighbor = neighbor[:-1]
    nbrs = struct.pack('<II4sI', 0, 73, b'SRBN', 2 if writer_early_eof else 1) + neighbor
    members = ([chunk(b'FAMI', 200, fami), chunk(b'FAMs', 200, fams)]
               if include_family else [])
    if duplicate:
        members.append(chunk(b'FAMI', 200, fami))
    return iff(*(members + [chunk(b'NBRS', 1, nbrs)]))


def strings(*rows):
    return struct.pack('<hH', -3, len(rows)) + b''.join(
        b'\1' + row.encode('ascii') + b'\0\0' for row in rows)


def character():
    objd = bytearray(32)
    struct.pack_into('<I', objd, 0, 138)
    struct.pack_into('<H', objd, 18, 2)
    struct.pack_into('<I', objd, 28, GUID)
    return iff(chunk(b'OBJD', 128, objd))


class FamilyReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiled = tempfile.TemporaryDirectory()
        cls.extractor = Path(cls.compiled.name) / 'family_profile_extract'
        report.compile_extractor(cls.extractor, 'g++')

    @classmethod
    def tearDownClass(cls):
        cls.compiled.cleanup()

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.profile = Path(self.folder.name)
        self.user_data = self.profile / 'UserData'
        (self.user_data / 'Characters').mkdir(parents=True)
        (self.user_data / 'Houses').mkdir()
        (self.user_data / 'Neighborhood.iff').write_bytes(neighborhood())
        (self.user_data / 'LotLocations.iff').write_bytes(iff(
            chunk(b'STR#', 4, strings('9,538,303')),
            chunk(b'STR#', 7, strings('4,120,200'))))
        (self.user_data / 'LotZoning.iff').write_bytes(iff(
            chunk(b'STR#', 1, strings('9, residential', '4, community'))))
        (self.user_data / 'Characters/User00050.iff').write_bytes(character())
        fsov = struct.pack('<II', 1, 3) + b'abc'
        (self.user_data / 'Houses/House09.iff').write_bytes(iff(chunk(b'FSOV', 1, fsov)))

    def tearDown(self):
        self.folder.cleanup()

    def test_persisted_identity_money_house_and_hashes(self):
        paths = sorted(path for path in self.user_data.rglob('*.iff'))
        before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        facts = report.inspect(self.profile, self.user_data, self.extractor)
        family = facts['families'][0]
        self.assertEqual((family['chunk_id'], family['family_number'],
                          family['house_number'], family['budget']), (200, 3, 9, 16500))
        self.assertEqual(family['family_name']['name_latin1'], 'Household')
        binding = family['member_bindings'][0]
        self.assertEqual(binding['guid'], GUID)
        self.assertEqual(binding['neighbors'][0]['neighbor_id'], 11)
        self.assertEqual(binding['neighbors'][0]['person_data_61'], 200)
        self.assertTrue(binding['person_data_61_matches_chunk_id'])
        self.assertEqual(binding['characters'][0]['path'], 'UserData/Characters/User00050.iff')
        self.assertEqual(facts['houses'][0]['fsov_chunks'][0]['serialized_payload_sha256'],
                         hashlib.sha256(b'abc').hexdigest())
        self.assertEqual(facts['lot_locations']['modes'][0]['lots'][0],
                         {'house_number': 9, 'x': 538, 'y': 303})
        self.assertEqual(facts['lot_zoning']['lots'][0],
                         {'house_number': 9, 'zone': 'residential', 'code': 0})
        self.assertEqual(facts['neighborhood']['nbrs_decode_profile'], 'strict')
        self.assertEqual(before, {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in paths})

    def test_delete_inventory_keeps_neighbor_and_character_visible(self):
        (self.user_data / 'Neighborhood.iff').write_bytes(neighborhood(include_family=False))
        facts = report.inspect(self.profile, self.user_data, self.extractor)
        self.assertEqual(facts['families'], [])
        self.assertEqual(facts['family_name_chunks'], [])
        self.assertEqual(facts['neighbors'][0]['guid'], GUID)
        self.assertEqual(facts['characters'][0]['person_definitions'][0]['guid'], GUID)

    def test_duplicate_family_rejected(self):
        (self.user_data / 'Neighborhood.iff').write_bytes(neighborhood(duplicate=True))
        with self.assertRaisesRegex(ValueError, 'duplicate FAMI'):
            report.inspect(self.profile, self.user_data, self.extractor)

    def test_case_insensitive_file_inventory_and_wrong_family_slot(self):
        (self.user_data / 'Neighborhood.iff').write_bytes(neighborhood(person_data_61=201))
        (self.user_data / 'Characters/User00050.iff').rename(
            self.user_data / 'Characters/USER00050.IFF')
        (self.user_data / 'Houses/House09.iff').rename(
            self.user_data / 'Houses/house09.IFF')
        facts = report.inspect(self.profile, self.user_data, self.extractor)
        self.assertEqual(facts['characters'][0]['path'], 'UserData/Characters/USER00050.IFF')
        self.assertEqual(facts['houses'][0]['path'], 'UserData/Houses/house09.IFF')
        self.assertEqual(facts['families'][0]['member_bindings'][0]['neighbors'][0]['person_data_61'], 201)
        self.assertFalse(facts['families'][0]['member_bindings'][0]['person_data_61_matches_chunk_id'])

    def test_exact_early_eof_writer_profile_is_labeled(self):
        (self.user_data / 'Neighborhood.iff').write_bytes(neighborhood(writer_early_eof=True))
        facts = report.inspect(self.profile, self.user_data, self.extractor)
        self.assertEqual(facts['neighborhood']['nbrs_declared_entry_count'], 2)
        self.assertEqual(facts['neighborhood']['nbrs_entry_count'], 1)
        self.assertEqual(facts['neighborhood']['nbrs_decode_profile'],
                         'freeso_active_only_early_eof')

    def test_partial_last_record_still_rejected(self):
        (self.user_data / 'Neighborhood.iff').write_bytes(neighborhood(
            writer_early_eof=True, truncate_record=True))
        with self.assertRaisesRegex(ValueError, 'truncated'):
            report.inspect(self.profile, self.user_data, self.extractor)


if __name__ == '__main__':
    unittest.main()
