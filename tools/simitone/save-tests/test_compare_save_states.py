"""Pure synthetic regression cases for the actual save-state comparator.

Run from the repository root:
  python3 -m unittest discover -s tools/simitone/save-tests -p test_compare_save_states.py

No game resources, engine, commercial fixture or serializer imitation is used.
Fixture fingerprints intentionally remain unchanged during bank mutations:
the comparator must inspect complete fields instead of trusting a summary hash.
"""

import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


HELPER_PATH = Path(__file__).resolve().parents[1] / "compare_save_states.py"
SPEC = importlib.util.spec_from_file_location("actual_compare_save_states", HELPER_PATH)
COMPARATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPARATOR)


def fixture():
    return {
        "SchemaVersion": 1,
        "Phase": "synthetic",
        "SnapshotVersion": 38,
        "Compressed": True,
        "SerializedPayloadLength": None,
        "SerializedPayloadSHA256": None,
        "StateSHA256": "0" * 64,
        "RuntimeFamilySubset": [123, 456],
        "State": {
            "Clock": {"Ticks": 50, "MinuteFractions": 20, "Hours": 7, "Minutes": 15},
            "GlobalState": [7, 17, 1],
            "Family": {"ID": 1, "Budget": 100, "FamilyGUIDs": [123, 456]},
            "SIMI": {"Version": 62, "GlobalData": [7, 17, 1]},
            "Architecture": {"BinarySHA256": "a" * 64},
            "Entities": [{
                "ObjectID": 2,
                "GUID": 123,
                "ObjectData": [0, 1],
                "Attributes": [5],
                "Position": {"x": 128, "y": 128, "Level": 1},
                "Avatar": {"PersonData": [1, 2], "MotiveData": [30, 40]},
                "BinarySHA256": "b" * 64,
                "Thread": {"TempRegisters": [0, 1], "TempXL": [0, 0], "Queue": [], "Stack": []}
            }],
            "Groups": [{"Objects": [2], "BinarySHA256": "c" * 64}]
        }
    }


class CompareSaveStatesTests(unittest.TestCase):
    def assert_unexpected(self, left, right, path):
        report = COMPARATOR.compare(left, right)
        self.assertFalse(report["StrictFieldsEqual"])
        self.assertEqual(report["UnexpectedDifferenceCount"], 1)
        self.assertEqual(report["AllowedFormatMetadataDifferenceCount"], 0)
        unexpected = [item for item in report["Differences"] if item["Classification"] == "unexpected"]
        self.assertEqual(unexpected[0]["Path"], path)
        return report

    def run_cli(self, left, right, *arguments):
        with tempfile.TemporaryDirectory(prefix="simitone-save-comparator-") as directory:
            left_path = Path(directory) / "left.json"
            right_path = Path(directory) / "right.json"
            left_path.write_text(json.dumps(left), encoding="utf-8")
            right_path.write_text(json.dumps(right), encoding="utf-8")
            output, errors = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                status = COMPARATOR.main([str(left_path), str(right_path), *arguments])
        return status, json.loads(output.getvalue() or errors.getvalue())

    def test_identical_complete_records_pass(self):
        record = fixture()
        before = copy.deepcopy(record)
        report = COMPARATOR.compare(record, record)
        self.assertTrue(report["StrictFieldsEqual"])
        self.assertEqual(report["Differences"], [])
        self.assertEqual(record, before, "comparison must not modify captured state")

    def test_entity_bank_mutation_cannot_pass_with_unchanged_fingerprints(self):
        left = fixture()
        for bank, path in (("ObjectData", "/State/Entities/2/ObjectData/0"),
                           ("Attributes", "/State/Entities/2/Attributes/0"),
                           ("PersonData", "/State/Entities/2/Avatar/PersonData/0"),
                           ("MotiveData", "/State/Entities/2/Avatar/MotiveData/0")):
            with self.subTest(bank=bank):
                right = copy.deepcopy(left)
                entity = right["State"]["Entities"][0]
                (entity["Avatar"] if bank in ("PersonData", "MotiveData") else entity)[bank][0] += 1
                self.assert_unexpected(left, right, path)
        status, report = self.run_cli(left, right, "--strict")
        self.assertEqual(status, 1)
        self.assertFalse(report["StrictFieldsEqual"])
        status, report = self.run_cli(left, right)
        self.assertEqual(status, 0, "default mode reports differences without acting as a gate")
        self.assertFalse(report["StrictFieldsEqual"])

    def test_global_or_family_funds_mutation_is_not_ignored(self):
        left = fixture()
        right = copy.deepcopy(left)
        right["State"]["GlobalState"][0] += 1
        self.assert_unexpected(left, right, "/State/GlobalState/0")
        right = copy.deepcopy(left)
        right["State"]["Family"]["Budget"] += 1
        self.assert_unexpected(left, right, "/State/Family/Budget")

    def test_clock_tick_or_fraction_drift_is_not_tolerated(self):
        left = fixture()
        for field in ("Ticks", "MinuteFractions"):
            with self.subTest(field=field):
                right = copy.deepcopy(left)
                right["State"]["Clock"][field] += 1
                self.assert_unexpected(left, right, "/State/Clock/" + field)

    def test_thread_temp_banks_are_compared(self):
        left = fixture()
        for bank in ("TempRegisters", "TempXL"):
            with self.subTest(bank=bank):
                right = copy.deepcopy(left)
                right["State"]["Entities"][0]["Thread"][bank][0] += 1
                self.assert_unexpected(left, right, "/State/Entities/2/Thread/" + bank + "/0")

    def test_architecture_digest_mutation_is_not_ignored(self):
        left = fixture()
        right = copy.deepcopy(left)
        right["State"]["Architecture"]["BinarySHA256"] = "d" * 64
        self.assert_unexpected(left, right, "/State/Architecture/BinarySHA256")

    def test_group_digest_mutation_is_not_ignored(self):
        left = fixture()
        right = copy.deepcopy(left)
        right["State"]["Groups"][0]["BinarySHA256"] = "d" * 64
        self.assert_unexpected(left, right, "/State/Groups/2/BinarySHA256")

    def test_added_actor_is_not_hidden_by_matching_existing_actors(self):
        left = fixture()
        right = copy.deepcopy(left)
        added = copy.deepcopy(right["State"]["Entities"][0])
        added["ObjectID"] = 3
        right["State"]["Entities"].append(added)
        report = self.assert_unexpected(left, right, "/State/Entities/3")
        self.assertEqual(report["Differences"][0]["Kind"], "missing_left")

    def test_runtime_family_subset_is_explicitly_reported_outside_persisted_gate(self):
        left = fixture()
        right = copy.deepcopy(left)
        right["RuntimeFamilySubset"] = []
        report = COMPARATOR.compare(left, right)
        self.assertTrue(report["StrictFieldsEqual"])
        self.assertEqual(report["RuntimeOnlyDifferenceCount"], 2)
        self.assertTrue(all(item["Classification"] == "runtime_only" for item in report["Differences"]))

    def test_multiple_matching_records_require_explicit_selection(self):
        first = fixture()
        second = copy.deepcopy(first)
        second["State"]["Clock"]["Ticks"] += 1
        status, error = self.run_cli([first, second], first, "--left-phase", "synthetic")
        self.assertEqual(status, 2)
        self.assertIn("explicit occurrence index", error["InputError"])
        status, report = self.run_cli([first, second], second, "--left-index", "-1", "--strict")
        self.assertEqual(status, 0)
        self.assertEqual(report["Left"]["MatchingRecordCount"], 2)
        self.assertEqual(report["Left"]["SelectedIndex"], -1)

    def test_duplicate_actor_or_group_identity_is_an_input_error(self):
        for collection in ("Entities", "Groups"):
            with self.subTest(collection=collection):
                record = fixture()
                record["State"][collection].append(copy.deepcopy(record["State"][collection][0]))
                with self.assertRaisesRegex(ValueError, "duplicate"):
                    COMPARATOR.compare(record, record)
                status, error = self.run_cli(record, record, "--strict")
                self.assertEqual(status, 2)
                self.assertIn("duplicate", error["InputError"])

    def test_legacy_simi_versions_promote_with_visible_warning(self):
        for version in (0, 62, 63):
            with self.subTest(version=version):
                left = fixture()
                right = copy.deepcopy(left)
                left["State"]["SIMI"]["Version"] = version
                right["State"]["SIMI"]["Version"] = 64
                status, report = self.run_cli(left, right, "--strict")
                self.assertEqual(status, 0)
                self.assertEqual(report["AllowedFormatMetadataDifferenceCount"], 1)
                self.assertEqual(report["UnexpectedDifferenceCount"], 0)
                difference = report["Differences"][0]
                self.assertEqual(difference["Path"], "/State/SIMI/Version")
                self.assertEqual((difference["Left"], difference["Right"]), (version, 64))
                self.assertIn("max(inputVersion, 0x40)", difference["Warning"])

    def test_simi_downgrades_other_promotions_and_invalid_types_fail(self):
        for old, new in ((69, 64), (64, 65), (-1, 64), (False, 64)):
            with self.subTest(old=old, new=new):
                left = fixture()
                right = copy.deepcopy(left)
                left["State"]["SIMI"]["Version"] = old
                right["State"]["SIMI"]["Version"] = new
                self.assert_unexpected(left, right, "/State/SIMI/Version")

    def test_allowed_simi_promotion_never_masks_other_simi_data_changes(self):
        left = fixture()
        right = copy.deepcopy(left)
        right["State"]["SIMI"]["Version"] = 64
        right["State"]["SIMI"]["GlobalData"][0] += 1
        status, report = self.run_cli(left, right, "--strict")
        self.assertEqual(status, 1)
        self.assertEqual(report["AllowedFormatMetadataDifferenceCount"], 1)
        self.assertEqual(report["UnexpectedDifferenceCount"], 1)
        unexpected = [item for item in report["Differences"] if item["Classification"] == "unexpected"]
        self.assertEqual(unexpected[0]["Path"], "/State/SIMI/GlobalData/0")


if __name__ == "__main__":
    unittest.main()
