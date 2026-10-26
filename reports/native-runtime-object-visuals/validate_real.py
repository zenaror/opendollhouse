"""Opt-in real-resource native food artwork/save/restore gate, metadata and PNG only.

All archives and original-resource-derived frames remain inside owned temporary
profiles. Only an owned Xvfb display runs for the actual GUI visibility check.
No commercial executable or original save writer runs.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import select
import time
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('food_checkpoint_helpers',root/'reports/native-family-checkpoint/validate_real.py')
    helpers = importlib.util.module_from_spec(spec); spec.loader.exec_module(helpers)
    spec = importlib.util.spec_from_file_location('food_world_helpers',root/'reports/native-occupancy-reconstruction/validate_real.py')
    world_helpers = importlib.util.module_from_spec(spec);spec.loader.exec_module(world_helpers)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path(__file__).parent/'attempt-1')
    args = parser.parse_args()
    out = args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binary=root/'build/linux-checkpoint/odh-person'
    game=(root/'../referencias/The Sims').resolve()
    prior=json.loads((root/'reports/native-residential-switch/real-1/result.json').read_text())
    protected=set(prior['protected_before'])
    for folder in ('UserData/Houses','UserData/Characters'):
        protected.update(str(p.relative_to(game)) for p in (game/folder).rglob('*') if p.is_file())
    for name in ('UIGraphics/UIGraphics.far','GameData/UIText.iff'):
        if (game/name).is_file():protected.add(name)
    report=dict(evidence_type='native Linux headless real-resource runtime food rendering and session continuation',
        limits=['Actual native Simantics/CreateObject and SavedLotRenderer, not original-game or physical display equivalence.',
                'Artwork ownership pixels and real renderer pick validate visibility/selectability; no mouse input is claimed.',
                'All commercial resources are read-only; full archives/PPMs exist only in owned temporary profiles.'],
        binary_sha256=helpers.sha(binary),protected_before={p:helpers.sha(game/p) for p in sorted(protected)},
        runs=[],cases={},passed=False)
    report['base_revision']=subprocess.run(['git','rev-parse','HEAD'],cwd=root,capture_output=True,text=True,check=True).stdout.strip()
    profile_paths=[]
    try:
        with tempfile.TemporaryDirectory(prefix='odh-runtime-food-visuals-') as temporary:
            temporary=Path(temporary)
            def environment(label):
                profile=temporary/label;profile_paths.append(str(profile))
                env=os.environ.copy();env.pop('DISPLAY',None);env.pop('WAYLAND_DISPLAY',None)
                for key,suffix in (('HOME','home'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),
                    ('XDG_STATE_HOME','state'),('XDG_CACHE_HOME','cache'),('ODH_STATE_DIR','logs')):
                    env[key]=str(profile/suffix)
                return env
            env=environment('split');control_env=environment('control')
            def run(label,options,environment,capture_tick=None):
                command=[str(binary),'--game-root',str(game)]+options
                ppm=temporary/(label+'.ppm')
                if capture_tick is not None:
                    command+=['--headless-status-frame',str(ppm),'--headless-status-tick',str(capture_tick)]
                item=dict(label=label,command=command,binary_before=helpers.sha(binary));report['runs'].append(item)
                assert item['binary_before']==report['binary_sha256'],'binary changed before '+label
                child=subprocess.Popen(command,cwd=root,env=environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                item['pid']=child.pid
                try:
                    stdout,stderr=child.communicate(timeout=180)
                except subprocess.TimeoutExpired:
                    item['timed_out']=True;child.terminate()
                    try:stdout,stderr=child.communicate(timeout=10)
                    except subprocess.TimeoutExpired:child.kill();stdout,stderr=child.communicate()
                finally:
                    item['exit']=child.returncode
                    item['pid_absent']=not Path('/proc',str(child.pid)).exists()
                    item['binary_after']=helpers.sha(binary)
                item['stdout_sha256']=hashlib.sha256(stdout.encode()).hexdigest()
                item['stderr_sha256']=hashlib.sha256(stderr.encode()).hexdigest()
                item['stderr_tail']=stderr[-2500:]
                assert item['binary_after']==report['binary_sha256'],'binary changed during '+label
                assert child.returncode==0,(label,child.returncode,stderr[-2500:])
                data=json.loads(stdout)
                item.update(tick=data['ticks'],resumed_tick=data['resumed_tick'],session_loaded=data['session_loaded'],
                    host_failed=data['host_failed'],created_objects=data['create_object_instance'],render=data.get('render'))
                assert not data['host_failed'],(label,data.get('host_diagnostic'))
                path=Path(data['saved_session_path']);assert path.is_relative_to(temporary) and path.is_file()
                item['save_sha256']=helpers.sha(path);item['sections']=helpers.section_report(path)
                if capture_tick is not None:
                    assert ppm.is_file(),label+' did not capture its actual live frame'
                    from PIL import Image
                    with Image.open(ppm) as image:
                        item['frame_size']=list(image.size);image.save(out/(label+'.png'))
                    item['png_sha256']=helpers.sha(out/(label+'.png'))
                    ppm.unlink()
                return data,path
            def visible_food(data,require_visible=False):
                objects=data['create_object_instance']['runtime_objects']
                food=[o for o in objects if o['id']==180 and o['guid']==4076987253]
                assert len(food)==1,objects
                food=food[0]
                if require_visible:assert food['rendered_pixels']>0 and food['render_pick_matches'] is True,food
                return food
            def completed_actor(data,actor):
                # CTRL roster v1 contains current controller codec v5 values.
                # Unlike interaction_runtime (new-input diagnostics), these are
                # authoritative persisted continuations after a fresh restore.
                r=world_helpers.Reader(data)
                assert r.read('H')==1
                count=r.read('H');assert 1<=count<=64
                payload=None
                for _ in range(count):
                    current,size=r.read('hI');assert size<=len(r.data)-r.position
                    body=r.data[r.position:r.position+size];r.skip(size)
                    if current==actor:
                        assert payload is None;payload=body
                assert r.position==len(data) and payload is not None
                r=world_helpers.Reader(payload);assert r.read('H')==5
                if r.read('B'):r.skip(18)  # Sleep actor ID/generation/VM run.
                if r.read('B'):r.skip(32)  # Optional Sleep job.
                if r.read('B'):r.skip(8)
                r.read('Q');r.skip(2);r.text()  # clock/interrupted/notify/diagnostic.
                current=r.read('h');r.skip(8)  # queue capacity.
                generation,run,sequence,clock,idle=r.read('QQQQQ')
                assert current==actor and generation>0 and run>0
                if r.read('B'):r.read('Q')
                if r.read('B'):r.read('Q')
                actions=r.read('I');assert actions==0,'consumed actor queue still contains actions'
                active=r.read('B');assert active==0,'consumed actor queue remains active'
                suspended=r.read('I');assert suspended==0,'consumed actor has suspended actions'
                wait=r.read('B');assert wait==0,'consumed actor still awaits an interaction'
                assert r.read('B')==1,'consumed actor has no persisted completion'
                uid,ticket,tick,result=r.read('QQQB')
                assert uid>0 and ticket>0 and result==1 and tick<=clock
                interrupted=r.read('B');r.text();profile=r.read('B');next_uid=r.read('H');r.read('B')
                assert not interrupted and r.position==len(payload)
                return dict(actor=actor,generation=generation,run=run,sequence=sequence,clock=clock,
                    queue_size=actions,queue_active=False,suspended=suspended,waiting=False,
                    completion=dict(uid=uid,ticket=ticket,tick=tick,result=True),profile=profile,next_queue_uid=next_uid)
            def gui_visibility(path,expected_archive):
                gui_env=env.copy()
                xvfb=subprocess.Popen(['Xvfb','-displayfd','1','-screen','0','800x600x24','-nolisten','tcp'],
                    stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
                report['xvfb']=dict(pid=xvfb.pid)
                try:
                    ready,_,_=select.select([xvfb.stdout],[],[],10);assert ready,'Xvfb handshake timeout'
                    display=xvfb.stdout.readline().strip();xvfb.stdout.close();assert display.isdigit()
                    gui_env['DISPLAY']=':'+display;report['xvfb']['display']=gui_env['DISPLAY']
                    report['actions']=[]
                    def xdo(*arguments,check=True):
                        return subprocess.run(['xdotool',*map(str,arguments)],env=gui_env,
                            capture_output=True,text=True,timeout=5,check=check)
                    log=Path(gui_env['ODH_STATE_DIR'])/'opendollhouse/logs/odh-person.log'
                    for angle in range(4):
                        label='held-food-gui-angle'+str(angle)
                        command=[str(binary),'--game-root',str(game),'--load-session',str(path),'--no-audio']
                        item=dict(label=label,command=command,binary_before=helpers.sha(binary));report['runs'].append(item)
                        assert item['binary_before']==report['binary_sha256']
                        stdout_path=temporary/(label+'.stdout');stderr_path=temporary/(label+'.stderr')
                        with stdout_path.open('w') as stdout,stderr_path.open('w') as stderr:
                            child=subprocess.Popen(command,cwd=root,env=gui_env,stdout=stdout,stderr=stderr)
                            item['pid']=child.pid
                            try:
                                def wait(description,predicate):
                                    deadline=time.monotonic()+35
                                    while time.monotonic()<deadline:
                                        if child.poll() is not None:raise AssertionError(label+' exited waiting '+description)
                                        value=predicate()
                                        if value:return value
                                        time.sleep(.05)
                                    raise AssertionError(label+' timeout '+description)
                                def find_window():
                                    result=xdo('search','--onlyvisible','--pid',child.pid,'--name','OpenDollhouse',check=False)
                                    return result.stdout.splitlines()[-1] if result.returncode==0 and result.stdout.strip() else None
                                window=wait('window',find_window)
                                def logs():return log.read_text(errors='replace') if log.is_file() else ''
                                wait('live frame',lambda:'lot frame presented tick=' in logs())
                                def key(value):
                                    xdo('windowfocus','--sync',window);xdo('key','--clearmodifiers',value)
                                    report['actions'].append(dict(label=label,key=value))
                                marker=len(logs());key('p')
                                wait('pause',lambda:'runtime session paused tick=' in logs()[marker:])
                                for _ in range(angle):key('Right')
                                time.sleep(.4)
                                raw=temporary/(label+'.xwd');png=out/(label+'.png')
                                subprocess.run(['xwd','-display',gui_env['DISPLAY'],'-id',window,'-silent','-out',str(raw)],check=True,timeout=10)
                                subprocess.run(['convert',str(raw),str(png)],check=True,timeout=15)
                                item['png_sha256']=helpers.sha(png);raw.unlink()
                                key('Escape');child.wait(timeout=45)
                            finally:
                                if child.poll() is None:
                                    child.terminate()
                                    try:child.wait(timeout=10)
                                    except subprocess.TimeoutExpired:child.kill();child.wait()
                                item.update(exit=child.returncode,pid_absent=not Path('/proc',str(child.pid)).exists(),binary_after=helpers.sha(binary))
                        stdout=stdout_path.read_text(errors='replace');stderr=stderr_path.read_text(errors='replace')
                        item.update(stdout_sha256=hashlib.sha256(stdout.encode()).hexdigest(),stderr_sha256=hashlib.sha256(stderr.encode()).hexdigest(),stderr_tail=stderr[-2500:])
                        assert item['exit']==0,(label,item['exit'],stderr[-2500:])
                        assert item['binary_after']==report['binary_sha256']
                        records=[json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
                        assert len(records)==1
                        data=records[0]
                        item.update(tick=data['ticks'],resumed_tick=data['resumed_tick'],session_loaded=data['session_loaded'],
                            host_failed=data['host_failed'],created_objects=data['create_object_instance'],render=data.get('render'))
                        assert data['session_loaded'] and data['resumed_tick']==512 and not data['host_failed']
                        assert path.read_bytes()==expected_archive,'unsaved GUI changed checkpoint'
                        food=visible_food(data)
                        if food['rendered_pixels']>0 and food['render_pick_matches'] is True:
                            report['cases']['actual_gui_visible']=dict(angle=angle,food=food,loaded_tick=512,current_tick=data['ticks'],
                                screenshot=label+'.png',checkpoint_unchanged=True)
                            return
                    raise AssertionError('None of four actual GUI camera angles showed/picked held food')
                finally:
                    xvfb.terminate()
                    try:xvfb.wait(timeout=10)
                    except subprocess.TimeoutExpired:xvfb.kill();xvfb.wait()
                    report['xvfb'].update(exit=xvfb.returncode,pid_absent=not Path('/proc',str(xvfb.pid)).exists())
            base=['--mode','1','--language','14','--house','7','--resident-id','47',
                  '--headless-resident-interaction','105','112','1']
            first,path=run('cut512',base+['--headless-ticks','512','--save-session-on-exit'],env,512)
            report['cases']['cut512_headless_view']=visible_food(first)
            original=path.read_bytes();original_sections=helpers.sections(path)
            assert b'GEXP' in original_sections,'created object has no archive provenance'
            zero,zero_path=run('zero-reopen',['--load-session',str(path),'--headless-ticks','0','--save-session-on-exit'],env)
            assert zero['session_loaded'] and zero['resumed_tick']==512 and zero['ticks']==512
            assert zero_path.read_bytes()==original,'zero-tick reopen changed full archive'
            report['cases']['zero_reopen']=dict(whole_archive_equal=True,all_sections_equal=True,tick=zero['ticks'])
            gui_visibility(zero_path,original)
            resumed,resumed_path=run('resumed513',['--load-session',str(zero_path),'--headless-ticks','1','--save-session-on-exit'],env,1)
            report['cases']['resumed513_headless_view']=visible_food(resumed)
            resumed_sections=helpers.sections(resumed_path)
            control,control_path=run('continuous513',base+['--headless-ticks','513','--save-session-on-exit'],control_env,513)
            report['cases']['continuous513_headless_view']=visible_food(control)
            same={name.decode():resumed_sections.get(name)==helpers.sections(control_path).get(name)
                  for name in set(resumed_sections)|set(helpers.sections(control_path))}
            fields={name:resumed.get(name)==control.get(name) for name in ('actor_state','global','rng_seed_final','ticks')}
            report['cases']['split_vs_continuous']=dict(sections_equal=same,state_fields_equal=fields,
                frame_png_equal=helpers.sha(out/'resumed513.png')==helpers.sha(out/'continuous513.png'))
            assert all(same.values()) and all(fields.values()),'resumed513 differs from continuous513'
            assert report['cases']['split_vs_continuous']['frame_png_equal'],'resumed artwork differs from continuous frame'
            final,final_path=run('consumed4096',['--load-session',str(resumed_path),'--headless-ticks','3583','--save-session-on-exit'],env,3583)
            objects=final['create_object_instance']['runtime_objects']
            assert not objects,'temporary food remains after consumption'
            final_sections=helpers.sections(final_path)
            completion=completed_actor(final_sections[b'CTRL'][1],105)
            entities=world_helpers.world_entities(final_sections[b'WRLD'][1])
            assert 180 not in entities,'food180 remains in the authoritative saved World'
            assert final['create_object_instance']['remove_object_instance']['last_diagnostic']=='removed object 180'
            report['cases']['consumed4096']=dict(tick=final['ticks'],runtime_objects=objects,
                actor_controller=completion,food_absent_from_runtime_registry=True,food_absent_from_saved_world=True,
                removal_diagnostic=final['create_object_instance']['remove_object_instance']['last_diagnostic'])
            assert final['ticks']==4096
            report['passed']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error)
    finally:
        report['binary_after']=helpers.sha(binary)
        report['protected_after']={p:helpers.sha(game/p) for p in sorted(protected)}
        report['protected_unchanged']=report['protected_before']==report['protected_after']
        report['all_pids_absent']=all(x.get('pid_absent',False) for x in report['runs']) and report.get('xvfb',{}).get('pid_absent',True)
        report['all_profiles_removed']=all(not Path(p).exists() for p in profile_paths)
        report['passed']=report['passed'] and report['protected_unchanged'] and report['all_pids_absent'] and report['all_profiles_removed'] and report['binary_after']==report['binary_sha256']
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=report['passed'],error=report.get('error'),output=str(out/'result.json'))))
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
