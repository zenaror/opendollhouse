#!/usr/bin/env python3
"""Run a bounded Downtown recipe through real player actions and observed dialogs.

Default prints commands only. Every process copies the complete preceding XDG
profile. A newly observed dialog stops the recipe; add an exact acknowledgement
step after inspecting it, then resume the result. No direct lot switch, queue
cancellation, state patch, generic OK or data-file modification is supported.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from validate_runtime_smoke import profile_files, profile_checkpoint, protected_files


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()


def require(ok,message):
    if not ok:raise ValueError(message)


def validate_acknowledgement(prompt,step):
    """Bind a response to its complete observed prompt and actual selector offer.

    PhoneBook's source cancellation is code0/text-1, rather than an offered
    neighbor ID. This exception grants no arbitrary selector values or answers.
    """
    require(prompt is not None and prompt==step['expected_prompt'],'acknowledgement is not the exact pending observed dialog')
    selected=step.get('selected_value')
    phonebook_cancel=prompt.get('type')==10 and step.get('text','')=='-1'
    if selected==-1 or step.get('text','')=='-1':
        require(phonebook_cancel and step.get('code')==0 and
                ('selected_value' not in step or selected==-1),
                'only PhoneBook code0/text-1 may cancel outside offered choices')
        require(isinstance(prompt.get('choices'),list),'PhoneBook cancellation requires actual observed choices')
        return
    if 'selected_value' in step:
        choices=prompt.get('choices')
        require(isinstance(choices,list) and any(c['value']==selected for c in choices),'selector value is not among actual offered choices')
        require(step.get('text')==str(selected),'selector response must carry its actual selected value')


def documents(raw):
    """A real launcher can emit HOME and visited-lot JSON in one invocation."""
    decoder=json.JSONDecoder(parse_constant=lambda x:(_ for _ in ()).throw(ValueError(x)))
    result=[];offset=0
    while raw[offset:].strip():
        offset+=len(raw[offset:])-len(raw[offset:].lstrip())
        value,end=decoder.raw_decode(raw,offset)
        require(isinstance(value,dict),'application report is not an object')
        result.append(value);offset=end
    require(result,'application emitted no JSON');return result


def persisted_simulation(value):
    value=json.loads(json.dumps(value))
    require(value['skipped_updates']==0,'simulation skipped updates')
    for a in value['avatars']:
        require(a['status'] in ('','updated','unchanged'),'unsupported motive status')
        del a['status']
    return value


def live_requests(rows):
    live={'pending','active','cancel_requested'}
    terminal={'completed_true','completed_false','cancelled','dropped','invalidated'}
    require(all(x['state'] in live|terminal for x in rows),'unknown request state')
    result=[x for x in rows if x['state'] in live]
    key=lambda x:tuple(x[k] for k in ('actor','actor_generation','run_generation','uid'))
    require(len({key(x) for x in result})==len(result),'duplicate live request identity')
    return sorted(result,key=key)


def read_plan(path):
    plan=json.loads(path.read_text());seen=set()
    for step in plan['steps']:
        require(re.fullmatch(r'[a-z0-9][a-z0-9-]*',step['id']) and step['id'] not in seen,'invalid/duplicate step id')
        seen.add(step['id']);require(step['kind'] in ('interaction','continue','ack','reopen'),'unsupported step kind')
        require(isinstance(step['ticks'],int) and 0<=step['ticks']<=4096,'step exceeds4096 ticks')
        if step['kind']=='reopen':require(step['ticks']==0,'reopen must not advance time')
        if step['kind']=='interaction':require(step['ticks']>0 and all(isinstance(step[k],int) and step[k]>0 for k in ('actor','target')) and isinstance(step['row'],int) and step['row']>=0,'invalid player request')
        if step['kind']=='ack':require(step['ticks']>0 and isinstance(step['expected_prompt'],dict) and isinstance(step['code'],int) and isinstance(step.get('text',''),str),'ack needs the exact previously observed prompt and response')
    require(0<len(seen)<=24,'recipe exceeds24 steps');return plan


def validate_observation(a):
    require(a.get('host_failed') is False and a.get('object_main_fault_count')==0 and a.get('instruction_budget_exhausted') is False,'runtime fault or incomplete success diagnostics')
    require('registry_first_spatial_invalidation' in a and a['registry_first_spatial_invalidation'] is None,'registry integrity unknown/invalid')
    require(a.get('runtime_policy')=='gameplay_per_tick_per_entity' and a.get('runtime_budget_scope')=='per_tick' and a.get('scheduler_budget_yields')==0,'not a complete gameplay scheduling profile')
    require(a['simulation']['skipped_updates']==0 and a['simulation']['clock']['ticks']==a['ticks'],'motive/clock gate failed')
    require('blocking_dialog' in a,'pending dialog state is unknown')


def adopt_smoke(directory,manifest,plan):
    """Authenticate a coordinator's existing completed Smoke process, never replay it."""
    directory=directory.resolve();path=directory/'result.json';record=json.loads(path.read_text())
    require(record.get('exit')==0,'adoption requires a completed zero-exit producer, not an in-progress/failed process')
    required=('binary_unchanged','protected_unchanged','input_unchanged','original_checkpoint_unchanged','pid_absent','source_profile_unchanged')
    require(all(record.get('proofs',{}).get(k) is True for k in required),'producer integrity proofs are missing/failed')
    require(record.get('input_sha256_before')==manifest['sha256'],'producer did not start from this origin checkpoint')
    departure=[step for step in plan['steps'] if step.get('role')=='departure' and step['kind']=='interaction']
    require(len(departure)==1,'recipe must identify the already executed departure step exactly once')
    authored=departure[0]
    cmd=record['command'];flag='--headless-resident-interaction-row'
    require(cmd.count(flag)==1 and cmd[cmd.index(flag)+1:cmd.index(flag)+5]==[str(authored['actor']),str(authored['target']),str(authored['row']),'1'],'producer is not the single authored departure request')
    require(not any(x in cmd for x in ('--headless-dialog-ok','--headless-dialog-response','--headless-travel-house','--headless-cancel-interaction','--headless-move-entity')),'producer used a forbidden shortcut or unseen answer')
    stream=directory/'stdout.json';docs=documents(stream.read_text());last=docs[-1]
    for a in docs:validate_observation(a)
    require(sum(a['headless_dialog_responses_consumed'] for a in docs)==0,'departure producer answered an unobserved dialog')
    accepted_modal_stop=False
    if not record.get('ok'):
        accepted_modal_stop=bool(last.get('blocking_dialog')) and record.get('errors') in (['application horizon mismatch'],['human decision required: game dialog is waiting; no response supplied'])
        require(accepted_modal_stop,'producer rejection is not an independently validated observed-modal stop')
        require(docs[0]['resumed_tick']==manifest['tick'] and manifest['tick']<docs[0]['ticks']<=manifest['tick']+record['requested_ticks'],'modal stop is outside producer tick bound')
    profile=Path(record.get('xdg_profile',directory/'profile')).resolve();saved=Path(last['saved_session_path']).resolve()
    require(saved.is_relative_to(profile) and saved.is_file(),'producer actual save is outside its complete profile')
    submissions=[x for a in docs for x in a['player_submissions']]
    require(len(submissions)==1 and submissions[0]['accepted'] and submissions[0]['actor']==authored['actor'] and submissions[0]['target']==authored['target'],'producer did not accept the one expected departure request')
    ledger=[{'step':departure[0]['id'],'role':'departure','submission':submissions[0]}]
    matches=[q for a in docs for q in a['player_requests'] if all(q[k]==submissions[0][k] for k in ('actor','actor_generation','run_generation','uid'))]
    if matches:ledger[0]['last_observed_request']=matches[-1]
    return {'integrity_ok':True,'origin_sha256':manifest['sha256'],'latest_profile':str(profile),'latest_save':str(saved),'latest_sha256':sha(saved),'ledger':ledger,'executed_steps':[departure[0]['id']],'latest_observation':last,'observed_lots':[a['integrated_session_context']['house_number'] for a in docs],'adopted_producer':{'result':str(path),'result_sha256':sha(path),'stdout':str(stream),'stdout_sha256':sha(stream),'binary_sha256':record['binary_sha256_after'],'producer_ok':bool(record.get('ok')),'producer_errors':record.get('errors',[]),'accepted_observed_modal_stop':accepted_modal_stop,'acceptance_scope':'observed modal and authenticated real save only; not Smoke horizon success'}}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True,help='durable checkpoint/profile manifest')
    p.add_argument('--recipe',type=Path,required=True)
    p.add_argument('--resume-result',type=Path)
    p.add_argument('--adopt-run',type=Path,help='adopt an existing completed Smoke departure directory; never submit it again')
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--binary-sha256',required=True)
    p.add_argument('--build-profile',choices=('normal','iteration'),default='normal')
    p.add_argument('--game-root',type=Path,required=True)
    p.add_argument('--protected-manifest',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--execute',action='store_true')
    p.add_argument('--timeout',type=int,default=240)
    args=p.parse_args()
    require(re.fullmatch('[a-f0-9]{64}',args.binary_sha256),'explicit full binary SHA256 required')
    require(1<=args.timeout<=300,'process timeout outside1..300')
    args.output=args.output.resolve();args.game_root=args.game_root.resolve();args.binary=args.binary.resolve()
    root=Path(__file__).resolve().parents[1]
    require(not(args.output.is_relative_to(root) or root.is_relative_to(args.output)), 'runtime output/profile must remain outside the source checkout; use a sibling .odh-validation/runs directory')
    require(not(args.output.is_relative_to(args.game_root) or args.game_root.is_relative_to(args.output)), 'runtime output/profile must remain outside the game data tree')
    plan=read_plan(args.recipe);manifest=json.loads(args.manifest.read_text())
    origin=Path(manifest['checkpoint']).resolve()
    source_profile=Path(manifest['profile'])
    require(not source_profile.is_symlink(),'profile root must not be a symlink')
    source_profile=source_profile.resolve();load=origin
    expected=manifest['sha256'];prior=None;ledger=[];completed=[];last=None
    require(not(args.resume_result and args.adopt_run),'choose resume-result or adopt-run, not both')
    if args.adopt_run:prior=adopt_smoke(args.adopt_run,manifest,plan)
    if args.resume_result:prior=json.loads(args.resume_result.read_text())
    if prior:
        require(prior['integrity_ok'],'preceding result lost integrity')
        require(prior['origin_sha256']==manifest['sha256'],'resume belongs to a different trip origin')
        source_profile=Path(prior['latest_profile']);load=Path(prior['latest_save']);expected=prior['latest_sha256']
        ledger=prior['ledger'];completed=prior['executed_steps'];last=prior['latest_observation']
    require(sha(load)==expected and sha(origin)==manifest['sha256'],'source checkpoint differs from its producer')
    inventory=profile_files(source_profile)
    member=profile_checkpoint(source_profile,inventory,expected)
    require(sha(member)==expected,'profile checkpoint differs from authenticated producer hash')
    # The durable checkpoint is allowed outside the complete profile. Bind its
    # unique existing exact-byte member; never overlay a save onto that profile.
    load=member
    require(not(args.output.is_relative_to(source_profile) or source_profile.is_relative_to(args.output)), 'runtime output and producer profile must not overlap')
    require(not args.output.exists(),'new output directory required')
    if not args.execute:
        print(json.dumps({'prepared_only':True,'source_profile':str(source_profile),'checkpoint':str(load),'checkpoint_sha256':expected,'binary':str(args.binary),'binary_sha256':args.binary_sha256,'remaining_steps':[s for s in plan['steps'] if s['id'] not in completed],'limits':'new dialogs stop; no generic responses or direct travel; full acceptance requires marked-companion proof'},indent=2));return 0
    require(sha(args.binary)==args.binary_sha256,'binary differs from approved artifact')
    protected=protected_files(args.protected_manifest,args.game_root)
    original_tree=profile_files(source_profile)
    guards={str(origin):manifest['sha256'],str(args.manifest.resolve()):sha(args.manifest),str(args.recipe.resolve()):sha(args.recipe)}
    if args.resume_result:guards[str(args.resume_result.resolve())]=sha(args.resume_result)
    if prior and prior.get('adopted_producer'):
        producer=prior['adopted_producer']
        guards[producer['result']]=producer['result_sha256'];guards[producer['stdout']]=producer['stdout_sha256']
    root=Path(__file__).resolve().parents[1]
    for f in ('tools/person.cpp','tools/linux_game_entry.cpp','tools/linux_travel_session.cpp','src/ts1_travel.cpp','src/ts1_travel_return.cpp','tools/ts1_travel_catalog.cpp','tools/validate_downtown_trip.py'):
        guards[str(root/f)]=sha(root/f)
    args.output.mkdir(parents=True)
    r={'title':'Home, Downtown activity and return through real scripts','status':'running','trip_accepted':False,'iteration_only':args.build_profile=='iteration','playable_build_acceptance':False,'binary_sha256':args.binary_sha256,'origin_sha256':manifest['sha256'],'original_profile':str(source_profile),'executed_steps':completed,'ledger':ledger,'runs':[],'integrity_ok':False,'latest_profile':str(source_profile),'latest_save':str(load),'latest_sha256':expected,'latest_observation':last,'observed_lots':prior.get('observed_lots',[]) if prior else []}
    if prior and prior.get('adopted_producer'):r['adopted_producer']=prior['adopted_producer']
    initial_profile=source_profile
    def record():(args.output/'result.json').write_text(json.dumps(r,indent=2)+'\n')
    try:
        for step in plan['steps']:
            if step['id'] in completed:continue
            require(len(r['runs'])<12,'per-invocation process limit12 reached')
            require(sha(args.binary)==args.binary_sha256 and all(sha(f)==h for f,h in guards.items()),'binary/source/input changed')
            if last and last.get('blocking_dialog'):require(step['kind']=='ack','pending dialog requires its exact observed acknowledgement step')
            if step['kind']=='ack':
                validate_acknowledgement(last.get('blocking_dialog') if last else None,step)
            staged_ledger=json.loads(json.dumps(ledger))
            before_hash=sha(load);before_profile=profile_files(source_profile)
            directory=args.output/step['id'];directory.mkdir();profile=directory/'profile';shutil.copytree(source_profile,profile,symlinks=True)
            require(profile_files(profile)==before_profile,'full profile copy differs')
            copied=profile/load.relative_to(source_profile);require(sha(copied)==before_hash,'copied input differs')
            env=os.environ.copy()
            for key in ('DISPLAY','WAYLAND_DISPLAY','ODH_STATE_DIR','ODH_TRACE_MOTIVE_CHANGES'):env.pop(key,None)
            for key,suffix in (('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_STATE_HOME','state'),('XDG_CACHE_HOME','cache')):env[key]=str(profile/suffix)
            cmd=[str(args.binary),'--game-root',str(args.game_root),'--load-session',str(copied),'--mode','1','--language',str(plan.get('language',14)),'--no-audio','--headless-ticks',str(step['ticks']),'--save-session-on-exit']
            if step['kind']=='interaction':cmd+=['--headless-resident-interaction-row',str(step['actor']),str(step['target']),str(step['row']),'1']
            if step['kind']=='ack':cmd+=['--headless-dialog-response',str(step['code']),step.get('text','')]
            row={'step':step,'command':cmd,'input':str(load),'input_sha256':before_hash,'profile':str(profile)};r['runs'].append(row);record()
            with (directory/'stdout.json').open('w') as stdout,(directory/'stderr.log').open('w') as stderr:
                proc=subprocess.Popen(cmd,env=env,cwd=root,stdout=stdout,stderr=stderr);row['pid']=proc.pid;record()
                try:row['exit']=proc.wait(timeout=args.timeout)
                finally:
                    if proc.poll() is None:
                        proc.terminate()
                        try:proc.wait(timeout=10)
                        except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=10)
                    row['pid_absent']=not Path('/proc',str(proc.pid)).exists()
            require(profile_files(source_profile)==before_profile and sha(load)==before_hash,'producer profile/input changed')
            raw=(directory/'stdout.json').read_text()
            if not raw.strip():
                row['stderr_tail']=(directory/'stderr.log').read_text(errors='replace')[-2000:]
                raise ValueError('application emitted no JSON; exit='+str(row['exit'])+'; stderr: '+row['stderr_tail'])
            docs=documents(raw)
            row['runtime_count']=len(docs);row['observations']=[]
            for a in docs:
                r['observed_lots'].append(a['integrated_session_context']['house_number'])
                row['observations'].append({'house':a['integrated_session_context']['house_number'],'tick':a['ticks'],'blocking_dialog':a['blocking_dialog'],'saved_session_path':a.get('saved_session_path')})
                validate_observation(a)
            observation=docs[-1];saved=Path(observation['saved_session_path']).resolve()
            require(saved.is_relative_to(profile.resolve()) and saved.is_file(),'actual saved path escaped complete isolated profile')
            row['exit_profile_snapshot']=profile_files(profile);row['save']=str(saved);row['save_sha256']=sha(saved)
            row['forensic_observed_save']={'profile':str(profile),'path':str(saved),'sha256':sha(saved),'observation':observation,'eligible_for_resume':False}
            # A produced file is evidence, not authority, until every stage gate passes.
            record();require(row['exit']==0,'launcher failed; real emitted observations/save retained without promoting trip')
            consumed=sum(a['headless_dialog_responses_consumed'] for a in docs)
            require(consumed==(1 if step['kind']=='ack' else 0),'response consumed outside exact observed step')
            submissions=[s for a in docs for s in a['player_submissions']]
            if step['kind']=='interaction':
                require(len(submissions)==1 and submissions[0]['accepted'] and submissions[0]['actor']==step['actor'] and submissions[0]['target']==step['target'],'normal player request not accepted exactly once')
                staged_ledger.append({'step':step['id'],'role':step.get('role'),'submission':submissions[0]})
            else:require(not submissions,'continuation unexpectedly resubmitted a player request')
            for item in staged_ledger:
                submission=item['submission'];matches=[q for a in docs for q in a['player_requests'] if all(q[k]==submission[k] for k in ('actor','actor_generation','run_generation','uid'))]
                if matches:item['last_observed_request']=matches[-1]
            if step['kind']=='reopen':
                require(sha(saved)==before_hash,'zero-tick reopen changed entire saved file')
                require(last and observation['ticks']==last['ticks'] and observation['actor_state']==last['actor_state'],'reopen clock/actor state changed')
                require(persisted_simulation(observation['simulation'])==persisted_simulation(last['simulation']) and live_requests(observation['player_requests'])==live_requests(last['player_requests']),'reopen saved simulation/live requests changed')
                row['whole_file_and_semantic_reopen_equal']=True
            ledger=staged_ledger;r['ledger']=ledger
            r.update(latest_profile=str(profile),latest_save=str(saved),latest_sha256=sha(saved),latest_observation=observation)
            row['forensic_observed_save']['eligible_for_resume']=True
            completed.append(step['id']);source_profile=profile;load=saved;last=observation
            record()
            if observation['blocking_dialog']:
                r['status']='observed_dialog_requires_exact_next_step';break
        else:r['status']='observed_dialog_requires_exact_next_step' if last and last.get('blocking_dialog') else 'recipe_completed_supporting_evidence'
        r['macro_acceptance']={'downtown_observed':any(house in range(21,31) for house in r['observed_lots']),'activity_completed_true':any(x.get('role')=='activity' and x.get('last_observed_request',{}).get('state')=='completed_true' and x['last_observed_request']['result'] is True for x in ledger),'returned_home_observed':bool(any(house in range(21,31) for house in r['observed_lots']) and last and last['integrated_session_context']['house_number']==plan.get('home_house',5)),'reopen_verified':any(x.get('whole_file_and_semantic_reopen_equal') for x in r['runs']),'marked_companion_and_all_effects_verified':False}
        r['open_acceptance']='Needs official saved TRIP/NINV/personal-progress/party inspection; an unmarked visiting relative is not a marked companion. Macro acceptance remains false.'
    except Exception as error:r['status']='stopped_at_explicit_gate';r['error']=str(error)
    finally:
        r['binary_unchanged']=sha(args.binary)==args.binary_sha256
        r['protected_unchanged']=all(sha(args.game_root/f)==h for f,h in protected.items())
        r['source_and_inputs_unchanged']=all(sha(f)==h for f,h in guards.items())
        r['original_profile_unchanged']=profile_files(initial_profile)==original_tree
        r['integrity_ok']=all(r[k] for k in ('binary_unchanged','protected_unchanged','source_and_inputs_unchanged','original_profile_unchanged'))
        record()
    print(json.dumps({k:r.get(k) for k in ('status','error','trip_accepted','latest_save','latest_sha256','integrity_ok')},indent=2))
    return 0 if r['status']=='recipe_completed_supporting_evidence' and r['integrity_ok'] else 1

if __name__=='__main__':raise SystemExit(main())
