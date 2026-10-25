"""Reject hidden desktop dependencies in a proposed data-only module.

These use synthetic restore metadata, not SDK restores or console builds.
"""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit_dependencies import inventory


class DependencyAuditTests(unittest.TestCase):
    def fixture(self, root, packages=None, frameworks=None):
        project = root / 'Module/Module.csproj'
        project.parent.mkdir()
        project.write_text('<Project><PropertyGroup><TargetFramework>netstandard2.0</TargetFramework></PropertyGroup></Project>')
        assets = project.parent / 'obj/project.assets.json'
        assets.parent.mkdir()
        data = {
            'libraries': {name: {'type': 'package'} for name in (packages or [])},
            'targets': {'netstandard2.0': {}},
            'project': {
                'restore': {'projectPath': str(project), 'frameworks': {
                    'netstandard2.0': {'projectReferences': {}}}},
                'frameworks': {'netstandard2.0': {
                    'frameworkReferences': {name: {} for name in (frameworks or [])}}},
            },
        }
        assets.write_text(json.dumps(data))
        return project, assets, data

    def test_clean_module_does_not_claim_console_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, _, _ = self.fixture(root, ['NETStandard.Library/2.0.3'])
            result = inventory(root, project, 'netstandard2.0')
            self.assertTrue(result['data_core_dependency_gate_passed'])
            self.assertIs(result['shared_console_runtime_proven'], False)

    def test_transitive_only_desktop_package_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, _, _ = self.fixture(root, ['MonoGame.Framework.DesktopGL/3.8.4'])
            result = inventory(root, project, 'netstandard2.0')
            self.assertFalse(result['data_core_dependency_gate_passed'])
            self.assertTrue(any('evaluated restore' in x['reason'] for x in result['data_core_blockers']))

    def test_imported_framework_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, _, _ = self.fixture(root, frameworks=['Microsoft.AspNetCore.App'])
            result = inventory(root, project, 'netstandard2.0')
            self.assertFalse(result['data_core_dependency_gate_passed'])

    def test_foreign_restore_cannot_approve_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, assets, data = self.fixture(root)
            data['project']['restore']['projectPath'] = str(root / 'Other.csproj')
            assets.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'different project'):
                inventory(root, project, 'netstandard2.0')


if __name__ == '__main__':
    unittest.main()
