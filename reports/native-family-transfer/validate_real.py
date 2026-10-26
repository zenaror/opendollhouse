"""Opt-in native CAS -> two vacant installed lots -> return validation.

Uses real installed resources read-only and two genuinely created private adults.
Only initial CAS creation uses a test driver; relocation uses production native
headless commands. This is not graphical UI, original-game or hardware proof.
Reports retain metadata and hashes only; all characters/saves stay in owned
TemporaryDirectory profiles and are removed when every child has been waited.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

PERSISTENT = (2,3,4,5,6,7,10,11,12,15,17,18,36,46,47,48,49,50,51,52,53,56,57,
              58,60,63,65,68,69,70,75,76,80,81,82,83,84,85,86)
NEEDS = (5,6,7,8,9,14,15)


class Reader:
    def __init__(self, data):
        self.data, self.position = data, 0

    def read(self, pattern):
        size = struct.calcsize('<' + pattern)
        assert size <= len(self.data) - self.position, 'truncated metadata reader input'
        result = struct.unpack_from('<' + pattern, self.data, self.position)
        self.position += size
        return result[0] if len(result) == 1 else result

    def skip(self, size):
        assert 0 <= size <= len(self.data) - self.position
        self.position += size

    def words(self):
        count = self.read('I')
        assert count <= 4096
        return tuple(self.read('h') for _ in range(count))

    def text(self):
        count = self.read('I')
        assert count <= 4096
        value = self.data[self.position:self.position + count]
        self.skip(count)
        return value


def world_people(data):
    r = Reader(data)
    version = r.read('H')
    assert version == 4, 'runner parser requires documented current WRLD v4'
    r.read('B'); r.read('Q'); r.read('Q'); r.read('Q')
    count = r.read('I')
    assert count <= 4096
    result = {}
    for _ in range(count):
        runtime, kind, generation = r.read('hBQ')
        attributes, person, slots = r.words(), r.words(), r.words()
        r.skip(r.read('I') * 4)  # ObjectData index/value pairs.
        if r.read('B'):
            r.skip(11)  # container, slot uint64, positioned.
        if r.read('B'):
            r.skip(11)  # x/y int32, level int16, direction.
        guid = None
        if r.read('B'):
            guid = r.read('I')
            if r.read('B'):
                r.skip(4)
        motives = r.words()
        r.skip(r.read('I') * 4)  # DefinitionData index/value pairs.
        if r.read('B'):
            r.skip(r.read('I') * 4)
        if r.read('B'):
            r.skip(73 * 2)  # Preserved raw object banks.
        r.skip(4)  # unattached_slot int16, static flag, UI ghost flag.
        if r.read('B'):
            r.skip(4)
        r.skip(4)  # radian direction float.
        if r.read('B'):
            r.skip(16 * 12)  # per-hour int16, max int16, fractional float64.
        appearances = tuple(r.text() for _ in range(r.read('I')))
        r.skip(8)  # dynamic sprite flags.
        if kind == 1:
            assert guid not in result and guid is not None
            result[guid] = dict(runtime=runtime,generation=generation,attributes=attributes,
                                person=person,motives=motives,appearances=appearances,slots=slots)
    assert r.position == len(data)
    return result


def member_projection(sections):
    h = Reader(sections[b'HHRD'][1])
    assert h.read('H') == 1
    count = h.read('H')
    roster, runtime_to_guid = {}, {}
    for _ in range(count):
        runtime, generation, guid, neighbor, objd = h.read('hQIhH')
        size = h.read('H')
        asset = h.data[h.position:h.position + size].decode('utf8'); h.skip(size)
        fingerprint = h.data[h.position:h.position + 32].hex(); h.skip(32)
        roster[guid] = dict(neighbor_id=neighbor,object_definition_id=objd,character_asset=asset,character_sha256=fingerprint)
        runtime_to_guid[runtime] = guid
    assert h.position == len(h.data) and count == 2
    s = Reader(sections[b'SIMS'][1])
    assert s.read('H') == 1
    s.skip(18)
    count = s.read('H')
    fractions = {}
    for _ in range(count):
        runtime, bucket = s.read('hB')
        fractions[runtime_to_guid[runtime]] = tuple(s.read('h') for _ in range(7))
        assert 0 <= bucket < 30
    assert s.position == len(s.data)
    people = world_people(sections[b'WRLD'][1])
    result = {}
    for guid, identity in roster.items():
        actor = people[guid]
        assert len(actor['person']) == 88 and len(actor['motives']) == 16
        assert actor['person'][31] == identity['neighbor_id']
        result[guid] = dict(identity=identity,person=tuple(actor['person'][i] for i in PERSISTENT),
                            needs=tuple(actor['motives'][i] for i in NEEDS),fractions=fractions[guid])
    return result


def overlay_state(path, family, initial_budget):
    data = path.read_bytes()
    assert data[:8] == b'ODHF\x06\0\0\0'
    position = 44
    state = dict(house=0,epoch=0,budget=initial_budget,arch=0,head=None)
    for _ in range(struct.unpack_from('<I',data,40)[0]):
        tag, size = struct.unpack_from('<HI',data,position); position += 6
        body = data[position:position+size]; position += size
        if tag == 2:
            continue
        assert tag == 5 and size == 104 and struct.unpack_from('<H',body)[0] == family
        house, price, budget, action = struct.unpack_from('<HiiI',body,34)
        before_house, flags, epoch, before_budget, before_arch, arch = struct.unpack_from('<HHQiii',body,48)
        assert (before_house,epoch,before_budget,before_arch) == (state['house'],state['epoch'],state['budget'],state['arch'])
        if action == 0:
            assert before_house == 0 and budget == before_budget-price
            state['epoch'] += 1
        elif action == 1:
            assert house == 0 and budget == before_budget+before_arch
        else:
            assert action == 2 and flags == 1 and house == before_house
            state['head'] = body[72:104].hex()
        state.update(house=house,budget=budget,arch=arch)
    assert position == len(data)
    return state


def main():
    checkout = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('checkpoint_validation_helpers',checkout/'reports/native-family-checkpoint/validate_real.py')
    helpers = importlib.util.module_from_spec(spec); spec.loader.exec_module(helpers)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=checkout/'build/linux-checkpoint/odh-person')
    parser.add_argument('--cas-driver',type=Path,default=checkout/'build/linux-checkpoint/odh-cas-movein-e2e')
    parser.add_argument('--game-root',type=Path,default=checkout/'../referencias/The Sims')
    parser.add_argument('--output',type=Path,default=Path(__file__).parent/'real-1/result.json')
    args = parser.parse_args()
    binary, driver, game, output = (p.resolve() for p in (args.binary,args.cas_driver,args.game_root,args.output))
    assert not output.exists(), 'refusing to overwrite evidence'
    prior = json.loads((checkout/'reports/native-residential-switch/real-1/result.json').read_text())
    protected_paths = set(prior['protected_before'])
    for directory in ('UserData/Characters','UserData/Houses'):
        protected_paths.update(str(path.relative_to(game)) for path in (game/directory).rglob('*') if path.is_file())
    report = dict(evidence_type='native Linux real-data headless CAS creation and deferred family relocation',
                  limits=['No graphical move-in/move-out confirmation tested.','No original-game comparison or hardware proof.',
                          'Reports retain only commands, identities, counts and hashes; no commercial assets or archives.'],
                  binary_sha256=helpers.sha(binary),cas_driver_sha256=helpers.sha(driver),runs=[],cases={},passed=False)
    report['protected_before'] = {name:helpers.sha(game/name) for name in sorted(protected_paths)}
    try:
        with tempfile.TemporaryDirectory(prefix='odh-real-family-transfer-') as temporary:
            profile = Path(temporary)/'profile'
            env = os.environ.copy()
            for variable in ('DISPLAY','WAYLAND_DISPLAY'):
                env.pop(variable,None)
            for variable, suffix in (('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_STATE_HOME','state'),
                                     ('XDG_CACHE_HOME','cache'),('ODH_STATE_DIR','logs')):
                env[variable] = str(profile/suffix)

            def run(label, command, exit_code=0):
                record = dict(label=label,command=command,expected_exit=exit_code,executable_sha256_before=helpers.sha(Path(command[0])))
                report['runs'].append(record)
                child = subprocess.Popen(command,cwd=checkout,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                record['pid'] = child.pid
                try:
                    stdout,stderr = child.communicate(timeout=180)
                except subprocess.TimeoutExpired:
                    child.kill();stdout,stderr = child.communicate();record['timed_out'] = True
                records = [json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
                record.update(exit=child.returncode,pid_absent=not Path('/proc',str(child.pid)).exists(),
                              process_waited=child.poll() is not None,record_count=len(records),
                              stdout_sha256=hashlib.sha256(stdout.encode()).hexdigest(),
                              stderr_sha256=hashlib.sha256(stderr.encode()).hexdigest(),
                              visits=[{key:item.get(key) for key in ('status','ticks','resumed_tick','session_loaded','host_failed')}
                                      for item in records])
                record['executable_sha256_after'] = helpers.sha(Path(command[0]))
                assert record['executable_sha256_before']==record['executable_sha256_after'], 'executable changed during child run'
                if Path(command[0])==binary:
                    assert record['executable_sha256_before']==report['binary_sha256'], 'host binary changed between stages'
                assert child.returncode == exit_code,(label,child.returncode,stderr[-3000:])
                return records

            created = run('create-two-adults-unhoused',[str(driver),str(game),str(profile),'--create-only'])[0]
            assert created['create_only'] and created['member_count'] == 2 and created['second_house']
            family = created['family_chunk_id']; initial_budget = created['initial_budget']
            first_house, second_house = created['house'],created['second_house']
            first_price, second_price = created['purchase_price'],created['second_purchase_price']
            neighbor = created['members'][0]['neighbor_id']
            report['created'] = {key:created[key] for key in ('family_chunk_id','family_number','house','second_house',
                                'purchase_price','second_purchase_price','initial_budget','members')}
            identity = helpers.sha(game/'UserData/Neighborhood.iff')
            overlay = profile/'config/opendollhouse/player-state'/('neighborhood-'+identity+'.odhp')
            # The create-only driver must leave the disk overlay genuinely unhoused.
            initial_overlay = overlay.read_bytes()
            assert struct.unpack_from('<I',initial_overlay,40)[0] == 1
            private = profile/'data/opendollhouse/Characters'
            private_before = {path.name:helpers.sha(path) for path in private.glob('*.iff')}
            report['private_characters_before'] = private_before
            assert len(private_before) == 2
            common = [str(binary),'--game-root',str(game),'--mode','1','--language','14','--no-audio']

            def app(label, house, options, exit_code=0):
                return run(label,common+['--house',str(house)]+options,exit_code)

            def checkpoint(record, house, epoch, budget):
                path = Path(record['saved_session_path'])
                parts = helpers.sections(path)
                digest = helpers.sha(path)
                state = overlay_state(overlay,family,initial_budget)
                assert state['house'] == house and state['epoch'] == epoch and state['budget'] == budget
                assert state['head'] == digest and path.name == digest+'.odhs'
                faml = parts[b'FAML'][1]
                assert len(faml)==76 and struct.unpack_from('<HHQ',faml,0)==(1,family,epoch)
                assert faml[12:44].hex() == identity and struct.unpack_from('<H',parts[b'HOST'][1],11)[0] == house
                return path, member_projection(parts)

            first = app('native-first-purchase-two-ticks',first_house,['--resident-id',str(neighbor),'--headless-move-in-family',str(family),
                         '--headless-ticks','2','--save-session-on-exit'])[0]
            assert not first['session_loaded'] and first['ticks'] == 2
            first_path, original_projection = checkpoint(first,first_house,1,initial_budget-first_price)
            assert set(original_projection)=={member['guid'] for member in created['members']}
            for member in created['members']:
                assert original_projection[member['guid']]['identity']=={key:value for key,value in member.items() if key!='guid'}
            first_digest = helpers.sha(first_path)
            report['first_checkpoint_sha256'] = first_digest
            def attribute_hashes(path):
                people = world_people(helpers.sections(path)[b'WRLD'][1])
                return {str(guid):hashlib.sha256(json.dumps(people[guid]['attributes']).encode()).hexdigest()
                        for guid in original_projection}
            first_attribute_hashes = attribute_hashes(first_path)
            report['attribute_policy'] = 'Source attributes remain in retained archive; destination attributes come from fresh Init/EP8, matching audited Simitone newVMAvatar + InheritNeighbor behavior.'
            report['source_attribute_hashes'] = first_attribute_hashes
            report['portable_projection_sha256'] = hashlib.sha256(json.dumps(original_projection,sort_keys=True).encode()).hexdigest()
            report['cases']['first_deferred_purchase'] = True
            retained = [(first_path,first_digest)]

            def move_out(label,house,budget):
                result = app(label,house,['--headless-move-out-family',str(family),'--headless-ticks','0'])
                assert len(result)==1 and result[0]['status']=='moved_out'
                state = overlay_state(overlay,family,initial_budget)
                assert state['house']==0 and state['budget']==budget and state['arch']==0
                assert all(helpers.sha(path)==digest for path,digest in retained)

            budget = initial_budget-first_price
            move_out('native-first-move-out',first_house,budget)
            for label,house,price,epoch in (('second-vacant-lot',second_house,second_price,2),('return-original-vacant-lot',first_house,first_price,3)):
                before_purchase = overlay.read_bytes()
                try:
                    moved = app('native-move-in-'+label,house,['--resident-id',str(neighbor),'--headless-move-in-family',str(family),
                                '--headless-ticks','0','--save-session-on-exit'])[0]
                except BaseException:
                    state = overlay_state(overlay,family,initial_budget)
                    report['cases']['rejected_purchase_preservation'] = dict(
                        overlay_unchanged=overlay.read_bytes()==before_purchase,
                        retained_archives_unchanged=all(helpers.sha(path)==digest for path,digest in retained),
                        unhoused=state['house']==0,budget_unchanged=state['budget']==budget)
                    report['private_characters_after'] = {path.name:helpers.sha(path) for path in private.glob('*.iff')}
                    raise
                budget -= price
                assert not moved['session_loaded'] and moved['resumed_tick']==0 and moved['ticks']==0
                path,projection = checkpoint(moved,house,epoch,budget)
                assert projection==original_projection,'portable raw fields/needs/fractions/identity changed during relocation'
                retained.append((path,helpers.sha(path)))
                before = overlay.read_bytes()
                zero = app('fresh-process-zero-'+label,house,['--resident-id',str(neighbor),'--headless-ticks','0','--save-session-on-exit'])[0]
                assert zero['session_loaded'] and zero['resumed_tick']==0 and zero['ticks']==0
                assert helpers.sha(Path(zero['saved_session_path']))==helpers.sha(path) and overlay.read_bytes()==before
                report['cases'][label] = dict(portable_projection_equal=True,zero_tick_archive_equal=True,overlay_unchanged=True,
                                             epoch=epoch,budget=budget,checkpoint_sha256=helpers.sha(path),
                                             source_attribute_hashes=first_attribute_hashes,
                                             destination_attribute_hashes=attribute_hashes(path),
                                             attribute_policy='fresh destination Init/EP8; source attributes retained only in source archive')
                if epoch==2:
                    move_out('native-second-move-out',house,budget)
            before = overlay.read_bytes()
            run('reject-original-full-archive',common+['--load-session',str(first_path),'--headless-ticks','0'],2)
            assert overlay.read_bytes()==before and all(helpers.sha(path)==digest for path,digest in retained)
            report['cases']['old_archives_retained_full_resume_rejected'] = True
            report['private_characters_after'] = {path.name:helpers.sha(path) for path in private.glob('*.iff')}
            assert report['private_characters_after']==private_before
            report['temporary_profiles_removed_on_exit'] = True
        report['passed'] = True
    except BaseException as error:
        report['failure'] = str(error)
        raise
    finally:
        report['binary_sha256_after'] = helpers.sha(binary)
        report['binary_unchanged'] = report['binary_sha256_after']==report['binary_sha256']
        if not report['binary_unchanged']:
            report['passed']=False
        report['protected_after'] = {name:helpers.sha(game/name) for name in sorted(protected_paths)}
        report['protected_unchanged'] = report['protected_before']==report['protected_after']
        if not report['protected_unchanged']:
            report['passed']=False
        output.parent.mkdir(parents=True,exist_ok=True)
        output.write_text(json.dumps(report,indent=2)+'\n')
    assert report['protected_unchanged']
    print(json.dumps(dict(passed=report['passed'],report=str(output))))


if __name__=='__main__':
    main()
