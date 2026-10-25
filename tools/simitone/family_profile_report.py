#!/usr/bin/env python3
"""Read persisted TS1 family facts from one isolated profile, without editing it.

FAMI, FAMs, NBRS and character OBJD are decoded by the repository's C++
readers through family_profile_extract.cpp. This wrapper hashes the exact files
and binds each family GUID to raw neighbor and character resources.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from session_smoke import embedded_fsov


PROJECT_ROOT = Path(__file__).resolve().parents[2]
EXTRACTOR_SOURCE = PROJECT_ROOT / 'tools/simitone/family_profile_extract.cpp'
EXTRACTOR_SOURCES = (
    EXTRACTOR_SOURCE,
    PROJECT_ROOT / 'src/formats.cpp',
    PROJECT_ROOT / 'src/family.cpp',
    PROJECT_ROOT / 'src/neighborhood.cpp',
    PROJECT_ROOT / 'src/animation.cpp',
    PROJECT_ROOT / 'src/object_definition.cpp',
    PROJECT_ROOT / 'src/lot_locations.cpp',
)
HOUSE_NAME = re.compile(r'^House([0-9]{2})\.iff$', re.IGNORECASE)
USER_NAME = re.compile(r'^User.*\.iff$', re.IGNORECASE)


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def regular_inside(path, root):
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root):
        raise ValueError('profile input must be a regular file inside the selected profile: %s' % path)


def compile_extractor(target, compiler):
    command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I' + str(PROJECT_ROOT / 'include'), '-I' + str(PROJECT_ROOT / 'tools')]
    command.extend(str(path) for path in EXTRACTOR_SOURCES)
    command.extend(['-o', str(target)])
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)


def extract(executable, kind, path):
    run = subprocess.run([str(executable), '--' + kind, str(path)],
                         capture_output=True, text=True, timeout=30)
    if run.returncode:
        raise ValueError('%s: %s' % (path, run.stderr.strip()))
    return json.loads(run.stdout)


def unchanged(path, before):
    if digest(path) != before:
        raise ValueError('profile file changed during inspection: %s' % path)


def select_user_data(profile, name):
    if name:
        if '/' in name or name in ('.', '..'):
            raise ValueError('--user-data must be a directory name inside the profile')
        candidate = profile / name
        if candidate.is_symlink() or not candidate.is_dir():
            raise ValueError('selected UserData directory is absent or symlinked')
        return candidate
    matches = [path for path in profile.glob('UserData*')
               if path.is_dir() and not path.is_symlink() and (path / 'Neighborhood.iff').is_file()]
    if len(matches) != 1:
        raise ValueError('expected exactly one UserData* with Neighborhood.iff; specify --user-data')
    return matches[0]


def inspect(profile, user_data, extractor):
    neighborhood = user_data / 'Neighborhood.iff'
    regular_inside(neighborhood, profile)
    if neighborhood.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('Neighborhood.iff exceeds 64 MiB inspection bound')
    neighborhood_hash = digest(neighborhood)
    parsed = extract(extractor, 'neighborhood', neighborhood)
    unchanged(neighborhood, neighborhood_hash)

    def lot_resource(filename, kind):
        path = user_data / filename
        regular_inside(path, profile)
        before = digest(path)
        facts = extract(extractor, kind, path)
        unchanged(path, before)
        return {'path': str(path.relative_to(profile)), 'bytes': path.stat().st_size,
                'sha256': before, **facts}

    lot_locations = lot_resource('LotLocations.iff', 'locations')
    lot_zoning = lot_resource('LotZoning.iff', 'zoning')

    characters = []
    directory = user_data / 'Characters'
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Characters directory is absent or symlinked')
    user_files = sorted(path for path in directory.iterdir() if USER_NAME.fullmatch(path.name))
    if len(user_files) > 4096:
        raise ValueError('character inventory exceeds 4096 files')
    for path in user_files:
        regular_inside(path, profile)
        if path.stat().st_size > 64 * 1024 * 1024:
            raise ValueError('character IFF exceeds 64 MiB inspection bound: %s' % path)
        character_hash = digest(path)
        facts = extract(extractor, 'character', path)
        unchanged(path, character_hash)
        characters.append({'path': str(path.relative_to(profile)), 'bytes': path.stat().st_size,
                           'sha256': character_hash, **facts})
    houses = []
    directory = user_data / 'Houses'
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Houses directory is absent or symlinked')
    for path in sorted(directory.iterdir()):
        matched = HOUSE_NAME.fullmatch(path.name)
        if not matched:
            continue
        regular_inside(path, profile)
        house_hash = digest(path)
        fsov = embedded_fsov(path)
        unchanged(path, house_hash)
        if 'parse_error' in fsov:
            raise ValueError('%s: %s' % (path, fsov['parse_error']))
        houses.append({'house_number': int(matched.group(1)),
                       'path': str(path.relative_to(profile)), 'bytes': path.stat().st_size,
                       'sha256': house_hash, **fsov})

    neighbors_by_guid = {}
    for neighbor in parsed['neighbors']:
        neighbors_by_guid.setdefault(neighbor['guid'], []).append(neighbor)
    characters_by_guid = {}
    for asset in characters:
        for definition in asset['person_definitions']:
            characters_by_guid.setdefault(definition['guid'], []).append({
                'path': asset['path'], 'sha256': asset['sha256'],
                'objd_chunk_id': definition['chunk_id']})
    names = {entry['chunk_id']: entry for entry in parsed['family_names']}
    families = []
    for family in parsed['families']:
        chunk_id = family['chunk_id']
        bindings = []
        for guid in family['member_guids']:
            neighbors = neighbors_by_guid.get(guid, [])
            bindings.append({'guid': guid,
                             'neighbors': [{'neighbor_id': record['neighbor_id'],
                                            'person_data_61': record['person_data_61'],
                                            'name_latin1': record['name_latin1']}
                                           for record in neighbors],
                             'characters': characters_by_guid.get(guid, []),
                             'person_data_61_matches_chunk_id':
                                 len(neighbors) == 1 and neighbors[0]['person_data_61'] == chunk_id})
        families.append({**family, 'family_name': names.get(chunk_id), 'member_bindings': bindings})

    return {
        'schema_version': 1,
        'evidence': 'read_only_raw_profile_files; FSOV payload hashes bind separate save-state diagnostics',
        'profile': str(profile), 'user_data': str(user_data.relative_to(profile)),
        'neighborhood': {'path': str(neighborhood.relative_to(profile)),
                         'bytes': neighborhood.stat().st_size, 'sha256': neighborhood_hash,
                         'nbrs_version': parsed['nbrs_version'],
                         'nbrs_entry_count': parsed['nbrs_entry_count'],
                         'nbrs_declared_entry_count': parsed['nbrs_declared_entry_count'],
                         'nbrs_decode_profile': parsed['nbrs_decode_profile'],
                         'nbrs_remaining_bytes': parsed['nbrs_remaining_bytes']},
        'families': families, 'family_name_chunks': parsed['family_names'],
        'neighbors': parsed['neighbors'], 'characters': characters, 'houses': houses,
        'lot_locations': lot_locations, 'lot_zoning': lot_zoning,
        'limits': ['House FSOV avatars are not decoded here; compare payload-bound SaveState diagnostics',
                   'freeso_active_only_early_eof means NBRS declared more entries than its complete active-record payload; the omitted placeholder positions cannot be recovered',
                   'Files are individually checked for changes; pause or close the game for a coherent multi-file snapshot']
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', required=True, type=Path,
                        help='isolated ODH_USER_DIR root containing one UserData*')
    parser.add_argument('--user-data', help='specific UserData directory name within --profile')
    parser.add_argument('--output', required=True,
                        help='JSON report outside the profile, or - for stdout')
    parser.add_argument('--extractor', type=Path,
                        help='prebuilt family_profile_extract executable; otherwise compile to a temporary path')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    args = parser.parse_args(argv)
    try:
        profile = args.profile.resolve(strict=True)
        if not profile.is_dir():
            raise ValueError('--profile is not a directory')
        user_data = select_user_data(profile, args.user_data)
        if args.output != '-' and Path(args.output).resolve().is_relative_to(profile):
            raise ValueError('--output must be outside the inspected profile')
        if args.extractor:
            executable = args.extractor.resolve(strict=True)
            result = inspect(profile, user_data, executable)
        else:
            with tempfile.TemporaryDirectory(prefix='odh-family-inspector-') as directory:
                executable = Path(directory) / 'family_profile_extract'
                compile_extractor(executable, args.cxx)
                result = inspect(profile, user_data, executable)
                extractor_hash = digest(executable)
        if args.extractor:
            extractor_hash = digest(executable)
        sources = [Path(__file__).resolve(), PROJECT_ROOT / 'tools/simitone/session_smoke.py',
                   PROJECT_ROOT / 'tools/json.hpp', PROJECT_ROOT / 'include/odh/formats.hpp',
                   PROJECT_ROOT / 'include/odh/family.hpp', PROJECT_ROOT / 'include/odh/neighborhood.hpp',
                   PROJECT_ROOT / 'include/odh/animation.hpp', PROJECT_ROOT / 'include/odh/lot_locations.hpp',
                   PROJECT_ROOT / 'include/odh/object_definition.hpp', *EXTRACTOR_SOURCES]
        result['inspector'] = {
            'invocation': ['family_profile_report.py', *(sys.argv[1:] if argv is None else argv)],
            'extractor_mode': 'prebuilt' if args.extractor else 'compiled_from_listed_sources',
            'source_hash_semantics': ('Current source context only; a supplied prebuilt binary is bound by extractor_sha256, not proven to derive from these sources.'
                                      if args.extractor else 'Sources compiled for this report by the stated compiler.'),
            'compiler': None if args.extractor else args.cxx,
            'extractor_sha256': extractor_hash,
            'source_sha256': {str(path.relative_to(PROJECT_ROOT)): digest(path)
                              for path in sources}
        }
        serialized = json.dumps(result, indent=2, sort_keys=True) + '\n'
        if args.output == '-':
            sys.stdout.write(serialized)
        else:
            target = Path(args.output)
            if target.exists():
                raise ValueError('output already exists; use a distinct stage report path')
            target.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8',
                                             dir=target.parent, prefix=target.name + '.',
                                             suffix='.tmp', delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(serialized)
            try:
                os.replace(temporary, target)
            finally:
                temporary.unlink(missing_ok=True)
    except (OSError, ValueError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        parser.exit(2, 'family_profile_report: %s\n' % error)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
