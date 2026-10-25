#!/usr/bin/env python3
"""Inventory an existing evaluated restore graph without restoring or building.

A successful inventory is not a platform compatibility check. --require-data-core
rejects known presentation/server dependencies in a proposed shared data core;
even a clean graph still needs runtime, compiler, resource and hardware proof.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


FORBIDDEN_PREFIXES = (
    'MonoGame.', 'Eto.', 'Microsoft.AspNetCore.', 'System.Data.SQLite',
    'Ninject', 'SharpDX',
)
FORBIDDEN_PROJECTS = ('FSO.LotView', 'FSO.HIT', 'FSO.Client', 'FSO.UI', 'FSO.Server')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(base, entry, framework):
    pending = [entry]
    projects = {}
    while pending:
        project = pending.pop().resolve(strict=True)
        key = project.relative_to(base).as_posix()
        if key in projects:
            continue
        assets = project.parent / 'obj/project.assets.json'
        restored = json.loads(assets.read_text())
        if restored['project']['restore']['projectPath'] != str(project):
            raise ValueError('restore belongs to a different project: ' + key)
        frameworks = restored['project']['restore']['frameworks']
        selected_framework = framework if framework in frameworks else (
            next(iter(frameworks)) if len(frameworks) == 1 else None)
        if selected_framework is None:
            raise ValueError('ambiguous restore frameworks in ' + key)
        restored_framework = frameworks[selected_framework]
        references = [Path(path).resolve(strict=True)
                      for path in restored_framework.get('projectReferences', {})]
        # This is the last evaluated restore, not a claim it is current after edits.
        tree = ET.parse(project).getroot()
        declared = [{'name': e.attrib['Include'], 'version': e.get('Version'),
                     'conditions': [v for v in (group.get('Condition'), e.get('Condition')) if v]}
                    for group in tree for e in group
                    if e.tag == 'PackageReference' and 'Include' in e.attrib]
        framework_refs = [e.attrib['Include'] for group in tree for e in group
                          if e.tag == 'FrameworkReference' and 'Include' in e.attrib]
        targets = [e.text for group in tree for e in group
                   if e.tag in ('TargetFramework', 'TargetFrameworks')]
        projects[key] = {
            'project_sha256': digest(project), 'assets_sha256': digest(assets),
            'restored_targets': sorted(restored['targets']),
            'restored_library_count': len(restored['libraries']),
            'restored_packages': sorted(name.split('/')[0] for name, lib in restored['libraries'].items() if lib['type'] == 'package'),
            'restored_framework_references': sorted(restored['project']['frameworks'][selected_framework].get('frameworkReferences', {})),
            'declared_frameworks': targets, 'selected_restore_framework': selected_framework,
            'declared_packages': declared, 'declared_framework_references': framework_refs,
            'restored_references': sorted(path.relative_to(base).as_posix()
                                          for path in references),
        }
        pending.extend(references)
    blockers = []
    for path, project in sorted(projects.items()):
        for package in project['declared_packages']:
            if package['name'].startswith(FORBIDDEN_PREFIXES):
                blockers.append({'project': path, 'dependency': package['name'],
                                 'reason': 'presentation, server, database or dynamic injection dependency'})
        for package in project['restored_packages']:
            if package.startswith(FORBIDDEN_PREFIXES):
                blockers.append({'project': path, 'dependency': package,
                                 'reason': 'forbidden package present in evaluated restore closure'})
        for reference in project['restored_framework_references']:
            if reference.startswith('Microsoft.AspNetCore.'):
                blockers.append({'project': path, 'dependency': reference,
                                 'reason': 'web framework present in evaluated restore'})
        for reference in project['restored_references']:
            if Path(reference).stem.startswith(FORBIDDEN_PROJECTS):
                blockers.append({'project': path, 'dependency': reference,
                                 'reason': 'presentation, audio or server project in data-core closure'})
        for reference in project['declared_framework_references']:
            if reference.startswith('Microsoft.AspNetCore.'):
                blockers.append({'project': path, 'dependency': reference,
                                 'reason': 'web application framework in data-core closure'})
    return {
        'schema_version': 1, 'entry': entry.relative_to(base).as_posix(),
        'evaluated_framework': framework, 'evidence_level': 'static source and last evaluated restore',
        'limitations': ['No platform build or runtime proof.',
                       'Restore graph may be stale; source and asset hashes are recorded.',
                       'Declared package conditions are not evaluated by this tool.',
                       'No static allowlist can establish compatibility with a missing runtime.'],
        'project_count': len(projects), 'projects': dict(sorted(projects.items())),
        'data_core_blockers': blockers, 'data_core_dependency_gate_passed': not blockers,
        'shared_console_runtime_proven': False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--entry', type=Path, required=True,
                        help='project relative to source-root')
    parser.add_argument('--framework', default='net9.0')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-data-core', action='store_true')
    args = parser.parse_args()
    try:
        base = args.source_root.resolve(strict=True)
        entry = (base / args.entry).resolve(strict=True)
        result = inventory(base, entry, args.framework)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'projects': result['project_count'],
                          'data_core_dependency_gate_passed': result['data_core_dependency_gate_passed'],
                          'shared_console_runtime_proven': False,
                          'output': str(args.output)}))
        return int(args.require_data_core and not result['data_core_dependency_gate_passed'])
    except (OSError, ValueError, KeyError, ET.ParseError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
