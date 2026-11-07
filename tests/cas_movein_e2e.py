#!/usr/bin/env python3
"""Opt-in real-data CAS -> vacant-lot -> runtime -> save/resume integration gate."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def source_paths(game):
    paths = {game / 'UserData/Neighborhood.iff', game / 'UserData/LotLocations.iff',
             game / 'GameData/Objects/Objects.far'}
    # The CAS scanner catalogs all FAR archives and loose assets below these
    # metadata roots, and walks expansion Skins directories.
    roots = [game / 'GameData/Skins', game / 'ExpansionShared/SkinsBuy', game / 'Downloads']
    roots += [p / 'Skins' for p in game.iterdir() if p.is_dir() and p.name.lower().startswith('expansion')]
    for root in roots:
        if root.is_dir():
            paths.update(p for p in root.rglob('*') if p.is_file())
    paths.update(p for p in game.rglob('*.far') if p.is_file())
    paths.update(p for p in (game / 'UserData/Characters').rglob('*') if p.is_file())
    paths.update(p for p in (game / 'UserData/Houses').glob('House*.iff') if p.is_file())
    return sorted(paths)


def integrated_family_lot(args, game, runner, person):
    """One real product path; setup calls the same CAS writer as the screen."""
    import time
    checkout = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    def inside(path, parent):
        try:
            path.relative_to(parent)
            return True
        except ValueError:
            return False
    if output.exists() or inside(output, checkout) or inside(output, game):
        raise ValueError('integrated output must be new and outside checkout/game data')
    if not args.setup_only and (args.purchase is None or args.floor is None):
        raise ValueError('integrated flow requires --purchase GUID X Y LEVEL DIRECTION and --floor MATERIAL X Y LEVEL')
    resumed = None
    resume_document = None
    resume_step = None
    resume_notice = None
    resume_saved = None
    source_runner = source_person = None
    resume_receipt = args.resume_run or args.resume_setup
    resume_inventory = None
    resume_result_hash = None
    resume_profile = None
    def inventory(root):
        if root.is_symlink() or not root.is_dir():
            raise ValueError('profile must be a real directory, not a symlink')
        result = {}
        for path in sorted(root.rglob('*')):
            if path.is_symlink():
                raise ValueError('profile contains a symlink: '+str(path))
            if path.is_file():
                result[str(path.relative_to(root))] = digest(path)
        return result
    if args.resume_run is not None and args.resume_setup is not None:
        raise ValueError('choose --resume-run or --resume-setup')
    if args.respond_observed_notice is not None and args.resume_run is None:
        raise ValueError('--respond-observed-notice requires --resume-run')
    if args.second_move and args.resume_run is None:
        raise ValueError('--second-move requires a guarded completed family/lot --resume-run')
    if args.revise_pending_purchase and args.resume_run is None:
        raise ValueError('--revise-pending-purchase requires a guarded pre-purchase --resume-run')
    if args.observed_response_code is not None and args.respond_observed_notice is None:
        raise ValueError('--observed-response-code requires the exact --respond-observed-notice receipt')
    if resume_receipt is not None:
        if args.setup_only:
            raise ValueError('resume cannot be combined with --setup-only')
        receipt = resume_receipt.resolve(strict=True)
        resume_result_hash = digest(receipt)
        resumed = json.loads(receipt.read_text())
        if args.resume_run is None:
            if (resumed.get('status') != 'setup_ready' or resumed.get('passed') is not True or
                    resumed.get('setup_only') is not True or
                    resumed.get('source_guards') != {'commercial_unchanged':True,'binaries_unchanged':True}):
                raise ValueError('resume receipt is not a successful guarded CAS-only setup')
        elif (resumed.get('schema') != 'integrated_family_lot/v1' or
                resumed.get('status') not in ('failed','passed') or
                not resumed.get('source_guards') or not all(resumed['source_guards'].values())):
            raise ValueError('resume run requires an intact guarded integrated result')
        raw_profile = Path(resumed['profile'])
        if not raw_profile.is_absolute() or raw_profile.is_symlink():
            raise ValueError('resume profile path must be absolute and not a symlink')
        resume_profile = raw_profile.resolve(strict=True)
        if inside(output, resume_profile) or inside(resume_profile, output):
            raise ValueError('output and resume profile must be separate')
        resume_inventory = inventory(resume_profile)
        if not resume_inventory or resume_inventory != resumed.get('profile_files'):
            raise ValueError('resume profile is absent or differs from its setup receipt')
        if len(resumed.get('binaries',{})) != 2:
            raise ValueError('resume receipt lacks the original runner/engine binary provenance')
        if args.resume_run is None:
            creation = next((step for step in resumed.get('steps',[]) if step.get('stage') == 'create'), None)
            if not creation or creation.get('command',[])[1:] != [str(game),str(resume_profile),'--family-lot-flow']:
                raise ValueError('resume setup belongs to another game or scenario')
            source_runner=creation['command'][0]
            source_person=next((path for path in resumed['binaries'] if path!=source_runner),None)
            if source_runner not in resumed['binaries'] or source_person is None:
                raise ValueError('setup writer/engine provenance is incomplete')
        else:
            checkpoint = resumed.get('last_checkpoint')
            if checkpoint:
                stage_name = checkpoint['source_stage']
                resume_step = next((step for step in resumed['steps'] if step['stage']==stage_name),None)
            else:
                resume_step = next((step for step in reversed(resumed.get('steps',[]))
                    if '--save-session-on-exit' in step.get('command',[]) and step.get('exit')==0),None)
            if not resume_step:
                raise ValueError('resume run has no completed product checkpoint operation')
            command = resume_step['command']
            source_person=(resume_step.get('binary_provenance_path',command[0])
                if resume_step.get('reused_observation') else command[0])
            source_runner=next((path for path in resumed['binaries'] if path!=source_person),None)
            if source_person not in resumed['binaries'] or source_runner is None:
                raise ValueError('runtime engine/decoder provenance is incomplete')
            if '--game-root' not in command or command[command.index('--game-root')+1] != str(game):
                raise ValueError('resume run belongs to another game installation')
            source_stdout = receipt.parent/(resume_step['stage']+'-stdout.json')
            if digest(source_stdout) != resume_step['stdout_sha256']:
                raise ValueError('observed product stdout changed since the receipt')
            resume_document = json.loads(source_stdout.read_text())
            if (resume_document.get('host_failed') is not False or
                    resume_document.get('instruction_budget_exhausted') is not False or
                    resume_document.get('scheduler_budget_yields') != 0 or
                    resume_document.get('object_main_fault_count') != 0 or
                    resume_document.get('registry_first_spatial_invalidation') is not None or
                    resume_document.get('headless_dialog_ok_sent') is not False):
                raise ValueError('resume checkpoint did not stop at a clean product boundary')
            alias=resume_step.get('reused_observation')
            if alias:
                if (not args.second_move or not checkpoint or
                        checkpoint.get('relative_path')!=alias.get('archive_relative_path') or
                        checkpoint.get('sha256')!=alias.get('archive_sha256') or
                        Path(resume_document['saved_session_path']).stem!=alias.get('archive_sha256')):
                    raise ValueError('invalid relocated second-move observation provenance')
                saved=(resume_profile/alias['archive_relative_path']).resolve(strict=True)
            else:
                saved = Path(resume_document['saved_session_path']).resolve(strict=True)
            resume_saved=saved
            if not inside(saved,resume_profile) or digest(saved) != saved.stem:
                raise ValueError('saved checkpoint is not the intact content-addressed profile archive')
            resume_notice = resume_document.get('blocking_dialog')
            if args.respond_observed_notice is not None:
                if args.respond_observed_notice.resolve(strict=True) != source_stdout.resolve() or not resume_notice:
                    raise ValueError('notice must name the exact observed stdout of the resumed checkpoint')
                if (resume_notice.get('choices') != [] or not isinstance(resume_notice.get('title'),str) or
                        not resume_notice.get('message')):
                    raise ValueError('observed modal must preserve its exact original prompt without selector input')
                kind=resume_notice.get('type')
                if kind==0:
                    if args.observed_response_code not in (None,0):
                        raise ValueError('informational modal has only its actual OK button0')
                elif kind==1:
                    if args.observed_response_code not in (0,1):
                        raise ValueError('yes/no modal requires an explicitly selected observed response code0 or1')
                else:
                    raise ValueError('this continuation supports only observed informational or explicit yes/no dialogs')
            elif resume_notice is not None:
                raise ValueError('pending modal requires explicit --respond-observed-notice OBSERVED_STDOUT')
            for key,value in (('requested_purchase',args.purchase),('requested_floor',args.floor)):
                if key in resumed and resumed[key] != value:
                    if (key!='requested_purchase' or not args.revise_pending_purchase or
                            (resumed.get('last_checkpoint') or {}).get('logical_stage')!='move-in' or
                            any(stage in resumed.get('progress',{}) for stage in ('buy','edit','return')) or
                            not resumed[key] or not value or resumed[key][0]!=value[0]):
                        raise ValueError('resume cannot change committed transaction inputs; pending placement revision needs its explicit pre-purchase receipt')
    output.mkdir(parents=True)
    profile = output / 'profile'
    if resume_profile is None:
        profile.mkdir()
    else:
        shutil.copytree(resume_profile, profile, symlinks=False)
        if inventory(profile) != resume_inventory or inventory(resume_profile) != resume_inventory:
            raise ValueError('resume profile changed while being copied')
    env = os.environ.copy()
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'ODH_STATE_DIR'):
        env.pop(key, None)
    for key, child in (('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),
                       ('XDG_STATE_HOME','state'),('XDG_CACHE_HOME','cache')):
        env[key] = str(profile / child)
    def profile_tree():
        return {str(p.relative_to(profile)): digest(p) for p in sorted(profile.rglob('*')) if p.is_file()}
    source_inventory = sorted(str(p.relative_to(game)) for p in game.rglob('*') if p.is_file())
    protected = {str(p.relative_to(game)): digest(p) for p in source_paths(game)}
    binaries = {str(p): digest(p) for p in (runner, person)}
    archives = {str(profile/name):sha for name,sha in (resume_inventory or {}).items() if name.endswith('.odhs')}
    report = {'schema':'integrated_family_lot/v1', 'status':'running', 'passed':False,
              'evidence_scope':'real_CAS_services_and_product_CLI_not_GUI',
              'playable_milestone_accepted':False, 'profile':str(profile),
              'initial_budget':20000, 'steps':[], 'errors':[], 'binaries':binaries,
              'requested_purchase':args.purchase, 'requested_floor':args.floor,
              'move_in_goal_tick':(resumed or {}).get('move_in_goal_tick',16),
              'progress':json.loads(json.dumps((resumed or {}).get('progress',{})))}
    if args.revise_pending_purchase:
        report['pending_purchase_revision']={'previous':resumed.get('requested_purchase'),
            'requested':args.purchase,'source_receipt_sha256':resume_result_hash,
            'scope':'uncommitted placement only; authenticated pre-purchase head still required'}
    def checked(condition, text):
        if not condition:
            raise RuntimeError(text)
    def publish():
        (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    def run(name, command):
        record = {'stage':name, 'command':list(map(str, command))}
        report['steps'].append(record)
        publish()
        begin = time.monotonic()
        stdout, stderr = output/(name+'-stdout.json'), output/(name+'-stderr.log')
        with stdout.open('wb') as out, stderr.open('wb') as err:
            completed = subprocess.run(list(map(str, command)), env=env, cwd=checkout,
                                       stdout=out, stderr=err, timeout=args.timeout, check=False)
        record.update(exit=completed.returncode, elapsed_seconds=time.monotonic()-begin,
                      stdout_sha256=digest(stdout), stderr_sha256=digest(stderr))
        checked(completed.returncode == 0, name+' exited nonzero; inspect '+str(stderr))
        document = json.loads(stdout.read_text())
        checked(isinstance(document, dict), name+' did not return a JSON object')
        publish()
        return document
    common = [str(person), '--game-root', str(game), '--mode', '1', '--language', '14', '--no-audio']
    def product(name, selection, count, commands=(), logical_stage=None, expected_answers=0):
        document = run(name, common+list(selection)+list(commands)+[
            '--headless-ticks',str(count),'--save-session-on-exit'])
        checked(document.get('host_failed') is False, name+' reported a host failure')
        checked(document.get('instruction_budget_exhausted') is False and
                document.get('scheduler_budget_yields') == 0, name+' did not complete its gameplay work')
        checked(document.get('object_main_fault_count') == 0 and
                document.get('registry_first_spatial_invalidation') is None,
                name+' stopped at an object/spatial failure')
        checked(document.get('headless_dialog_responses_requested') == expected_answers and
                document.get('headless_dialog_responses_consumed') == expected_answers and
                document.get('headless_dialog_ok_sent') is False,
                name+' did not consume exactly its explicitly authorized response count')
        path = Path(document.get('saved_session_path','')).resolve(strict=True)
        checked(inside(path, profile.resolve()), name+' save escaped its complete isolated profile')
        saved_hash = digest(path)
        checked(str(path) not in archives or archives[str(path)] == saved_hash,
                name+' overwrote an earlier immutable checkpoint')
        archives[str(path)] = saved_hash
        projection = run(name+'-inspect', [runner,'--inspect-session',path])
        checked(projection['tick'] == document['ticks'], name+' inspector tick differs')
        checked(projection.get('blocking_dialog') == document.get('blocking_dialog'),
                name+' serialized modal differs from its actual product observation')
        report['last_checkpoint'] = {'logical_stage':logical_stage or name,'source_stage':name,
            'relative_path':str(path.relative_to(profile)), 'sha256':saved_hash,
            'tick':document['ticks'],'blocking_dialog':document.get('blocking_dialog')}
        publish()
        checked(document.get('blocking_dialog') is None,
                name+' stopped at an observed original modal; inspect this receipt before authorizing any response')
        return document, path, projection
    def remember(stage, value):
        document,path,projection=value
        report['progress'][stage]={'document':document,'relative_path':str(path.relative_to(profile)),
            'sha256':digest(path),'projection':projection}
        publish()
    def previous(stage):
        value=report['progress'].get(stage)
        if value is None:return None
        path=profile/value['relative_path']
        checked(path.is_file() and digest(path)==value['sha256'],stage+' prior checkpoint changed')
        projection=json.loads(json.dumps(value['projection']))
        if 'archive_path' in projection:projection['archive_path']=str(path.resolve())
        return value['document'],path,projection
    def roster(projection):
        return sorted((m['guid'],m['neighbor_id'],m['age'],m['gender'],m['body'])
                      for m in projection['members'])
    def ownership(projection):
        return (projection['house'],projection['family_chunk_id'],projection['family_number'],
                projection['occupancy_epoch'],roster(projection))
    try:
        if resumed is None:
            created = run('create', [runner, game, profile, '--family-lot-flow'])
        else:
            created = resumed['created_family']
            report['resumed_run' if args.resume_run else 'resumed_setup'] = {'result':str(resume_receipt.resolve()),
                'sha256':resume_result_hash, 'profile':str(resume_profile),
                'profile_files':resume_inventory,
                'setup_runner':{'path':source_runner,'sha256':resumed['binaries'][source_runner]},
                'current_runner':{'path':str(runner),'sha256':digest(runner)},
                'runner_changed_since_source_receipt':resumed['binaries'][source_runner] != digest(runner),
                'CAS_writer_reexecuted':False,
                'person_binary':{'source_path':source_person,'current_path':str(person),
                    'source_sha256':resumed['binaries'][source_person],
                    'current_sha256':digest(person),
                    'changed_since_source_receipt':resumed['binaries'][source_person] != digest(person),
                    'source_receipt_was_CAS_only':args.resume_run is None}}
        checked(created.get('integrated_family_lot') is True and created['initial_budget'] == 20000,
                'setup did not use the explicit normal-budget scenario')
        checked(sorted(m['age'] for m in created['members']) == [9,27] and
                sorted(m['body_type'] for m in created['members']) == ['adult','child'],
                'actual private CAS files do not describe an adult and a child')
        checked(0 <= created['purchase_price'] < 20000, 'selected lot is not affordable')
        report['created_family'] = created
        publish()
        private_files = {str(p.relative_to(profile)):digest(p) for p in
                         (profile/'data/opendollhouse/Characters').glob('*.iff')}
        checked(len(private_files) == 2, 'CAS did not publish exactly two private character files')
        if args.setup_only:
            report.update(status='setup_ready', passed=True, setup_only=True,
                evidence_scope='real_CAS_creation_only_runtime_and_transactions_not_executed',
                gui_verified=False, private_character_count=len(private_files))
        elif args.second_move:
            checked(not resumed.get('second_move_completed'), 'second move was already completed; do not perform it again')
            phase=(resumed.get('last_checkpoint') or {}).get('logical_stage')
            checked((resumed.get('passed') is True and resumed.get('same_file_after_return') is True) or
                    phase in ('second-property-query','second-move-in','second-return') or resumed.get('second_move'),
                    'second move requires a completed first chain or its own guarded continuation')
            checked(set(('move-in','buy','edit')).issubset(report['progress']),
                    'second move cannot replace an incomplete first family/lot chain')
            _,current_save,current=previous('edit')
            checked(current_save.stem==digest(current_save), 'first completed checkpoint is not immutable')
            # Decoder diagnostics may grow between builds. Re-read the exact
            # content-addressed source with this same decoder before comparing
            # runtime projections; historical JSON shape is not archive state.
            current=run('second-original-source-inspect',[runner,'--inspect-session',current_save])
            destination=created['second_house']
            history=json.loads(json.dumps(resumed.get('second_move',{})))
            report['second_move']=history
            history.update(source=current,source_relative_path=str(current_save.relative_to(profile)),
                destination=destination,first_chain_replayed=False,original_first_chain_preserved=True)
            # Retain the previous actual observation, explicitly labelled as a
            # copied receipt rather than another execution. This gives failures
            # after eviction an authenticated historical archive anchor.
            anchor='second-continuation-anchor'
            shutil.copyfile(source_stdout,output/(anchor+'-stdout.json'))
            anchor_relative=str(resume_saved.relative_to(resume_profile))
            anchor_record={'stage':anchor,'command':resume_step['command'],'exit':0,
                'binary_provenance_path':str(person),
                'stdout_sha256':digest(output/(anchor+'-stdout.json')),
                'reused_observation':{'source_receipt_sha256':resume_result_hash,
                    'source_stage':resume_step['stage'],'archive_relative_path':anchor_relative,
                    'archive_sha256':digest(profile/anchor_relative),
                    'person_sha256':(resume_step.get('reused_observation') or {}).get('person_sha256',resumed['binaries'][source_person])}}
            report['steps'].append(anchor_record)
            report['last_checkpoint']={'logical_stage':phase,'source_stage':anchor,
                'relative_path':anchor_relative,'sha256':digest(profile/anchor_relative),
                'tick':resume_document['ticks'],'blocking_dialog':resume_notice}
            publish()
            state=run('second-family-state',[runner,'--inspect-family-state',game,
                str(created['family_chunk_id']),str(destination)])
            checked(state['managed'] and state['destination_price']==created['second_purchase_price'] and
                    state['destination_house']==destination and
                    sorted(state['member_guids'])==sorted(m['guid'] for m in current['members']),
                    'current family/destination differs from its protected original identities/SIMI')
            if state['house']==current['house']:
                checked(not history.get('unhoused'), 'family unexpectedly returned to the old occupancy')
                head=run('second-source-head',[runner,'--inspect-session-head',game,
                    str(current['house']),str(created['family_chunk_id'])])
                checked(head.get('authenticated_journal_head') is True and
                        Path(head['archive_path']).resolve()==current_save.resolve() and
                        state['budget']==current['budget'] and head.get('blocking_dialog') is None,
                        'second move source is not its current unblocked authenticated home head')
                observed_hash=anchor_record['reused_observation']['person_sha256']
                if phase=='second-property-query' and observed_hash==digest(person):
                    query_doc=resume_document
                    query_current=run('second-property-observation-inspect',[runner,'--inspect-session',current_save])
                    checked(resume_saved.stem==current_save.stem and query_current==current,
                            'recorded property query differs from the actual unchanged checkpoint')
                    history['query_reused_from_receipt']=resume_result_hash
                else:
                    query_doc,query_save,query_current=product('second-property-query',
                        ['--load-session',str(current_save)],0,logical_stage='second-property-query')
                    checked(query_current==current and digest(query_save)==digest(current_save),
                            'property query unexpectedly changed the complete gameplay checkpoint')
                valuation=query_doc.get('runtime_property_valuation')
                checked(valuation is not None and valuation.get('status')=='resolved' and
                        valuation.get('tick')==query_doc['ticks'],
                        'fresh real property valuation must resolve before eviction')
                refreshed=run('second-property-head',[runner,'--inspect-session-head',game,
                    str(current['house']),str(created['family_chunk_id'])])
                state=run('second-quoted-family',[runner,'--inspect-family-state',game,
                    str(created['family_chunk_id']),str(destination)])
                checked(refreshed.get('authenticated_journal_head') is True and
                        Path(refreshed['archive_path']).resolve()==current_save.resolve() and
                        state['budget']==current['budget'] and state['value_in_arch']==valuation['property_value'],
                        'fresh quote is not the cache on this same authenticated occupancy/head')
                quote=state['move_out_quote_budget']
                checked(isinstance(quote,int) and quote==current['budget']+valuation['property_value'] and
                        quote>=state['destination_price'],
                        'independent property plus cash must fund the destination before eviction')
                history.update(runtime_property_valuation=valuation,source_family_state=state,
                    quote_before_effects=quote,source_receipt_sha256=resume_result_hash)
                publish()
                moved=run('second-move-out',common+['--house',str(current['house']),
                    '--resident-id',str(created['neighbor_id']),'--headless-move-out-family',
                    str(created['family_chunk_id']),'--headless-ticks','0'])
                checked(moved=={'status':'moved_out','family_chunk_id':created['family_chunk_id']},
                        'real move-out command did not complete')
                history['move_out_receipt']=moved;publish()
                state=run('second-unhoused-family',[runner,'--inspect-family-state',game,
                    str(created['family_chunk_id']),str(destination)])
            else:
                checked(history.get('move_out_receipt')=={'status':'moved_out','family_chunk_id':created['family_chunk_id']} and
                        history.get('source_family_state') and history.get('runtime_property_valuation',{}).get('status')=='resolved',
                        'post-eviction continuation lacks its actual pre-effect quote and move-out receipt')
                quote=history['quote_before_effects']
                checked(quote==current['budget']+history['runtime_property_valuation']['property_value'] and
                        quote>=state['destination_price'], 'historical source quote/affordability is inconsistent')
            selection=['--house',str(destination),'--resident-id',str(created['neighbor_id'])]
            resumed_return=None
            if state['house']==0:
                checked(state['budget']==quote and state['value_in_arch']==0 and
                        state['occupancy_epoch']==current['occupancy_epoch'],
                        'unhoused family differs from the recorded source refund/occupancy')
                history['unhoused']=state;publish()
                arrival_options=['--headless-move-in-family',str(created['family_chunk_id'])]
                if args.recover_saved_code:arrival_options.append('--recover-saved-code')
                arrived_doc,arrived_save,arrived=product('second-move-in',selection,0,arrival_options,
                    logical_stage='second-move-in')
            else:
                checked(state['house']==destination and state['budget']==quote-state['destination_price'] and
                        state['occupancy_epoch']==current['occupancy_epoch']+1 and
                        phase in ('second-move-in','second-return'),
                        'post-effect family is not the exact already-paid destination occupancy')
                resumed_path=profile/resume_saved.relative_to(resume_profile)
                arrived=run('second-destination-inspect',[runner,'--inspect-session',resumed_path])
                head=run('second-destination-head',[runner,'--inspect-session-head',game,
                    str(destination),str(created['family_chunk_id'])])
                checked(head.get('authenticated_journal_head') is True and
                        Path(head['archive_path']).resolve()==resumed_path.resolve() and
                        arrived.get('blocking_dialog')==resume_notice==head.get('blocking_dialog'),
                        'destination observation/modal differs from its exact current serialized head')
                if resume_notice:
                    answer=0 if args.observed_response_code is None else args.observed_response_code
                    arrived_doc,arrived_save,arrived=product('second-arrival-notice',
                        ['--load-session',str(resumed_path)],1,['--headless-dialog-response',str(answer),''],
                        logical_stage='second-move-in',expected_answers=1)
                else:
                    arrived_doc,arrived_save=resume_document,resumed_path
                    if phase=='second-return':resumed_return=(arrived_doc,arrived_save,arrived)
            checked(arrived['house']==destination and roster(arrived)==roster(current) and
                    arrived['family_chunk_id']==current['family_chunk_id'] and
                    arrived['family_number']==current['family_number'] and
                    arrived['occupancy_epoch']==current['occupancy_epoch']+1,
                    'second move did not bind the same family into the next destination occupancy')
            checked(arrived['budget']==quote-state['destination_price'],
                    'second move did not debit exactly the protected destination purchase price')
            for before in current['members']:
                after=next(m for m in arrived['members'] if m['guid']==before['guid'])
                checked(all(after['motives'][i]==before['motives'][i] for i in (5,6,7,8,9,14,15)),
                        'second move lost portable needs for GUID '+str(before['guid']))
            remember('second-move-in',(arrived_doc,arrived_save,arrived))
            returned_doc,returned_save,returned=resumed_return or product('second-return',selection,0,
                logical_stage='second-return')
            checked(returned_doc.get('session_loaded') is True and returned==arrived and
                    digest(returned_save)==digest(arrived_save),
                    'new-process second-house selection did not preserve the entire arrival checkpoint')
            checked({name:digest(profile/name) for name in private_files}==private_files,
                    'second move modified immutable private character resources')
            history.update(arrived=arrived,returned=returned,quote_before_effects=quote,first_chain_replayed=False)
            report.update(status='passed',passed=True,second_move_completed=True,
                final_checkpoint={'path':str(returned_save),'sha256':digest(returned_save)},
                same_file_after_return=True,gui_verified=False,
                limits=['CLI consumers and authenticated complete profiles; no GUI acceptance',
                        'Personal transfer checks identity and portable needs; old-lot filtering/persistence remains a separate gate'])
        else:
            selection = ['--house',str(created['house']),'--resident-id',str(created['neighbor_id'])]
            initial_options = ['--headless-move-in-family',str(created['family_chunk_id'])]
            if args.recover_saved_code:
                initial_options.append('--recover-saved-code')
            resumed_stage = None
            continued = None
            if args.resume_run is not None:
                old_path=Path(resume_document['saved_session_path']).resolve()
                resumed_path=profile/old_path.relative_to(resume_profile)
                observed=run('resume-checkpoint-inspect',[runner,'--inspect-session',resumed_path])
                head=run('resume-authenticated-head',[runner,'--inspect-session-head',game,
                    str(observed['house']),str(created['family_chunk_id'])])
                checked(head.get('authenticated_journal_head') is True and
                        Path(head['archive_path']).resolve()==resumed_path.resolve() and
                        digest(resumed_path)==old_path.stem,
                        'resume checkpoint is not the current authenticated copied profile head')
                checked(observed.get('blocking_dialog')==resume_notice and head.get('blocking_dialog')==resume_notice,
                        'serialized current modal does not exactly match ticket/routine/pc/caller/type/title/message/choices')
                resumed_stage=(resumed.get('last_checkpoint') or {}).get('logical_stage',resume_step['stage'])
                checked(resumed_stage in ('move-in','buy','edit','return'),'unsupported continuation stage')
                answer_code=0 if args.observed_response_code is None else args.observed_response_code
                answer=['--headless-dialog-response',str(answer_code),''] if resume_notice is not None else []
                remaining=max(0,report['move_in_goal_tick']-int(resume_document['ticks'])) if resumed_stage=='move-in' else 0
                if answer:remaining=max(1,remaining)
                report['authorized_observed_notice']=resume_notice
                report['authorized_observed_response']={'code':answer_code,'text':''} if resume_notice is not None else None
                continued=product(resumed_stage+'-resume',['--load-session',str(resumed_path)],remaining,answer,
                    logical_stage=resumed_stage,expected_answers=1 if answer else 0)
            if resumed_stage=='move-in':
                initial_doc,initial_save,initial=continued
            elif previous('move-in') is not None:
                initial_doc,initial_save,initial=previous('move-in')
            else:
                initial_doc, initial_save, initial = product('move-in', selection, report['move_in_goal_tick'], initial_options)
            checked(initial['tick']>=report['move_in_goal_tick'],'move-in did not complete its original bounded horizon')
            checked(initial['budget'] == 20000-created['purchase_price'], 'move-in money was not charged exactly once')
            checked(initial['family_chunk_id'] == created['family_chunk_id'] and
                    initial['family_number'] == created['family_number'] and
                    initial['house'] == created['house'] and initial['occupancy_epoch'] is not None,
                    'move-in did not publish its actual family/occupancy lease')
            checked(sorted(m['guid'] for m in initial['members']) ==
                    sorted(m['guid'] for m in created['members']) and
                    sorted(m['age'] for m in initial['members']) == [9,27],
                    'saved World lost the created adult/child identities or ages')
            checked(initial_doc.get('body_initialized') is True and
                    initial_doc.get('body_state',{}).get('avatar_type') == 'adult' and
                    initial_doc.get('body_state',{}).get('body') and
                    initial_doc.get('body_state',{}).get('head'),
                    'selected adult lacks its actual initialized BodySTR descriptor')
            # World.avatar_appearance is a live outfit override, not the
            # resource's default BodySTR. Its absence does not erase the
            # immutable CAS character resource used to recreate that default.
            report['appearance_evidence'] = {
                'primary_runtime_descriptor':initial_doc['body_state'],
                'private_CAS_types':[m['body_type'] for m in created['members']],
                'per_member_default_resources_preserved':True,
                'all_residents_visually_verified':False}
            if args.revise_pending_purchase:
                checked(resumed_stage=='move-in' and not initial['purchase_groups'] and
                        initial['budget']==20000-created['purchase_price'],
                        'revised placement requires the real uncharged pre-purchase family checkpoint')
            remember('move-in',(initial_doc,initial_save,initial))
            owner = ownership(initial)
            purchase = list(args.purchase)
            old_groups = {g['group_id'] for g in initial['purchase_groups']}
            if resumed_stage=='buy':
                bought_doc,bought_save,bought=continued
            elif previous('buy') is not None:
                bought_doc,bought_save,bought=previous('buy')
            else:
                bought_doc, bought_save, bought = product('buy', ['--load-session',str(initial_save)], 0,
                                                         ['--buy-object']+list(map(str,purchase)))
            checked(ownership(bought) == owner, 'purchase changed family, occupancy or resident appearance')
            new = [g for g in bought['purchase_groups'] if g['group_id'] not in old_groups]
            checked(len(new) == 1 and new[0]['requested_guid'] == purchase[0] and new[0]['members'],
                    'purchase did not publish one actual requested furniture group')
            group = new[0]
            checked(isinstance(group['price'],int) and group['price'] > 0 and
                    bought['budget'] == initial['budget']-group['price'],
                    'purchase price and real family debit disagree')
            checked(any(m['position'] == purchase[1:] for m in group['members']),
                    'purchased furniture did not retain the requested placement')
            remember('buy',(bought_doc,bought_save,bought))
            material, x, y, level = args.floor
            if resumed_stage=='edit':
                edited_doc,edited_save,edited=continued
            elif previous('edit') is not None:
                edited_doc,edited_save,edited=previous('edit')
            else:
                edited_doc, edited_save, edited = product('edit', ['--load-session',str(bought_save)], 0,
                    ['--edit-floor',str(material),str(x),str(y),str(level)])
            checked(ownership(edited) == owner and edited['purchase_groups'] == bought['purchase_groups'],
                    'floor edit changed residents or the purchased furniture')
            architecture = edited.get('architecture')
            checked(architecture is not None and 0 <= x < architecture['width'] and
                    0 <= y < architecture['height'] and level in (1,2),
                    'floor edit did not publish its actual architecture grid')
            checked(architecture['floors'][level-1][y*architecture['width']+x] == material,
                    'saved floor cell differs from the selected material')
            checked(isinstance(edited['budget'],int) and 0 <= edited['budget'] < bought['budget'],
                    'floor edit did not debit a positive cost from the real remaining budget')
            remember('edit',(edited_doc,edited_save,edited))
            # A new process enters by house/resident selection, not the saved file.
            # This exercises the current neighborhood occupancy and journal head.
            returned_doc, returned_save, returned = product('return', selection, 0)
            checked(returned_doc.get('session_loaded') is True, 'house selection did not load its current journal checkpoint')
            checked(returned == edited and digest(returned_save) == digest(edited_save),
                    'new-process return did not preserve the whole final checkpoint')
            checked({name:digest(profile/name) for name in private_files} == private_files,
                    'runtime modified the private source character IFFs')
            checked(all(digest(Path(path)) == sha for path,sha in archives.items()),
                    'an earlier immutable checkpoint changed')
            report.update(status='passed', passed=True, created_family=created,
                initial=initial, purchased_group=group, edited=edited, returned=returned,
                money={'after_move_in':initial['budget'],'after_purchase':bought['budget'],
                       'after_floor_edit':edited['budget'],'after_return':returned['budget']},
                final_checkpoint={'path':str(returned_save),'sha256':digest(returned_save)},
                same_file_after_return=True, gui_verified=False,
                limits=['CAS setup invokes real screen services through its native runner; the screen itself is not driven',
                        'Purchase, floor edit and return use product CLI with real journal publication/reload',
                        'This case does not accept all content, all family profiles or the first playable milestone'])
    except Exception as error:
        report['status'] = 'failed'
        report['errors'].append(str(error))
    finally:
        try:
            current_inventory = sorted(str(p.relative_to(game)) for p in game.rglob('*') if p.is_file())
            report['commercial_inventory_added'] = sorted(set(current_inventory)-set(source_inventory))
            report['commercial_inventory_removed'] = sorted(set(source_inventory)-set(current_inventory))
            report['commercial_files_changed'] = [name for name,sha in protected.items()
                if not (game/name).is_file() or digest(game/name) != sha]
            report['source_guards'] = {'commercial_unchanged':not(report['commercial_inventory_added'] or
                report['commercial_inventory_removed'] or report['commercial_files_changed']),
                'binaries_unchanged':all(digest(Path(path)) == sha for path,sha in binaries.items())}
            if resume_profile is not None:
                report['source_guards']['setup_profile_unchanged'] = inventory(resume_profile) == resume_inventory
                report['source_guards']['setup_receipt_unchanged'] = digest(resume_receipt.resolve()) == resume_result_hash
            report['profile_files'] = profile_tree()
            report['profile_files_created'] = sorted(report['profile_files'])
            if not all(report['source_guards'].values()):
                report.update(passed=False,status='failed')
                report['errors'].append('source immutability guard failed')
        except Exception as error:
            report.update(passed=False,status='failed')
            report['errors'].append('source guard verification failed: '+str(error))
        publish()
    print(json.dumps({'status':report['status'],'result':str(output/'result.json'),
                      'profile':str(profile),'errors':report['errors']}))
    return 0 if report['passed'] else 1



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path, help='built odh-cas-movein-e2e executable')
    parser.add_argument('--person', required=True, type=Path, help='built odh-person executable')
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--integrated-family-lot', action='store_true')
    parser.add_argument('--resume-run', type=Path, help='continue a guarded full-profile integrated result without replaying completed transactions')
    parser.add_argument('--second-move', action='store_true', help='continue a completed integrated family/lot receipt through real eviction, second-house purchase and new-process return')
    parser.add_argument('--revise-pending-purchase', action='store_true', help='explicitly revise only an uncommitted furniture placement from an authenticated pre-purchase resume receipt')
    parser.add_argument('--respond-observed-notice', type=Path, help='respond once to the exact serialized modal observed in this previous product stdout')
    parser.add_argument('--observed-response-code', type=int, choices=(0,1), help='explicit observed button: informational OK0; yes/no Yes0 or No1 (required for type1)')
    parser.add_argument('--resume-setup', type=Path, help='integrated mode: copy a verified CAS-only result/profile without creating another family')
    parser.add_argument('--setup-only', action='store_true', help='integrated mode: create the real adult/child family without launching gameplay')
    parser.add_argument('--output', type=Path, help='new durable evidence/profile directory for integrated mode')
    parser.add_argument('--purchase', nargs=5, type=int, metavar=('GUID','X','Y','LEVEL','DIRECTION'))
    parser.add_argument('--floor', nargs=4, type=int, metavar=('MATERIAL','X','Y','LEVEL'))
    parser.add_argument('--recover-saved-code', action='store_true')
    args = parser.parse_args()
    game = args.game_root.resolve(strict=True)
    runner, person = args.runner.resolve(strict=True), args.person.resolve(strict=True)
    if args.integrated_family_lot:
        if args.output is None:
            parser.error('--integrated-family-lot requires --output')
        return integrated_family_lot(args, game, runner, person)
    if args.output is not None or args.purchase is not None or args.floor is not None or args.recover_saved_code or args.setup_only or args.resume_setup is not None or args.resume_run is not None or args.respond_observed_notice is not None or args.observed_response_code is not None or args.revise_pending_purchase or args.second_move:
        parser.error('integrated options require --integrated-family-lot')
    inputs = source_paths(game)
    before = {p: digest(p) for p in inputs}
    with tempfile.TemporaryDirectory(prefix='odh-cas-movein-e2e-') as temp:
        profile = Path(temp)
        env = os.environ.copy()
        env['HOME'] = str(profile / 'home')
        env['XDG_CONFIG_HOME'] = str(profile / 'config')
        env['XDG_STATE_HOME'] = str(profile / 'state')
        env['XDG_DATA_HOME'] = str(profile / 'data')
        env['ODH_STATE_DIR'] = str(profile / 'state/opendollhouse')
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        result = subprocess.run([str(runner), str(game), str(profile)], env=env,
                                capture_output=True, text=True, timeout=args.timeout)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or f'integration runner exited {result.returncode}')
        report = json.loads(result.stdout)
        plan = subprocess.run([str(person), '--game-root', str(game), '--mode', '1',
                               '--house', str(report['house']), '--resident-id', str(report['neighbor_id']),
                               '--plan-only'], env=env, capture_output=True, text=True,
                              timeout=args.timeout)
        if plan.returncode:
            raise RuntimeError(plan.stderr.strip() or f'odh-person --plan-only exited {plan.returncode}')
        plan_report = json.loads(plan.stdout)
        if plan_report.get('status') != 'ready' or plan_report.get('resident_guid') != report['guid']:
            raise RuntimeError(f'launcher did not return a successful resource plan: {plan.stdout[:1000]}')
        selection = [str(person), '--game-root', str(game), '--mode', '1',
                     '--house', str(report['house']), '--resident-id', str(report['neighbor_id'])]

        def run_session(arguments, expected_ticks, expected_resume):
            session_result = subprocess.run(arguments, env=env, capture_output=True, text=True,
                                             timeout=args.timeout)
            if session_result.returncode:
                raise RuntimeError(session_result.stderr.strip() or
                                   f'game session exited {session_result.returncode}')
            session = json.loads(session_result.stdout)
            if session.get('host_failed') or session.get('ticks') != expected_ticks:
                raise RuntimeError(f'bounded game session did not reach tick {expected_ticks}: '
                                   f'{session_result.stdout[:1200]}')
            if session.get('session_loaded') is not expected_resume:
                raise RuntimeError(f'game session resume state was unexpected: {session_result.stdout[:1200]}')
            if not session.get('tick_budget_exhausted'):
                raise RuntimeError('bounded game session did not stop at its requested tick budget')
            household = session.get('household_main', {})
            if household.get('scheduler_entities') != report['member_count']:
                raise RuntimeError(f'full CAS household did not enter the runtime: {session_result.stdout[:1600]}')
            additional = household.get('additional_residents', [])
            if (len(additional) != report['member_count'] - 1 or
                    any(member.get('status') != 'waiting' or member.get('diagnostic') or
                        member.get('position') is None for member in additional)):
                raise RuntimeError(f'additional CAS resident did not reach its idle Main: {session_result.stdout[:2000]}')
            return session

        first_session = run_session(selection + ['--headless-ticks', '16', '--save-session-on-exit'],
                                    16, False)
        archive = Path(first_session['saved_session_path']).resolve(strict=True)
        try:
            archive.relative_to(profile / 'data/opendollhouse/saves')
        except ValueError as error:
            raise RuntimeError('session archive escaped the temporary player profile') from error
        resumed_session = run_session([str(person), '--game-root', str(game), '--load-session', str(archive),
                                       '--headless-ticks', '16', '--save-session-on-exit'], 32, True)
        resumed_bytes = archive.read_bytes()
        direct_session = run_session(selection + ['--headless-ticks', '32', '--save-session-on-exit'],
                                     32, False)
        direct_archive = Path(direct_session['saved_session_path']).resolve(strict=True)
        if direct_archive != archive or direct_archive.read_bytes() != resumed_bytes:
            raise RuntimeError('fresh-process save/resume differs from uninterrupted 32-tick execution')
        changed = [str(p) for p, sha in before.items() if digest(p) != sha]
        if changed:
            raise RuntimeError('installed game data changed: ' + ', '.join(changed[:8]))
        private = profile / 'data/opendollhouse/Characters'
        overlay_files = list((profile / 'config/opendollhouse/player-state').glob('*.odhp'))
        if (not private.is_dir() or len(list(private.glob('*.iff'))) != report['member_count'] or
                len(overlay_files) != 1):
            raise RuntimeError('expected the complete private CAS household and one player overlay in temporary profile')
        print(json.dumps({'status': 'passed', 'mode': report['mode'], 'house': report['house'],
                          'purchase_price': report['purchase_price'], 'household_members': report['member_count'],
                          'launcher_plan': 'passed',
                          'launcher_resource_files': len(plan_report.get('resource_identity', {}).get('files', [])),
                          'runtime_entry': 'passed', 'runtime_ticks': resumed_session['ticks'],
                          'additional_resident_statuses': [member['status'] for member in
                              resumed_session['household_main']['additional_residents']],
                          'fresh_process_resume': 'passed', 'resume_matches_continuous': True,
                          'commercial_source_files_sha256_checked': len(before),
                          'neighborhood_sha256': before[game / 'UserData/Neighborhood.iff'],
                          'game_data_unchanged': True,
                          'private_character_count': len(list(private.glob('*.iff'))),
                          'player_overlay_count': 1}, separators=(',', ':')))


if __name__ == '__main__':
    raise SystemExit(main())
