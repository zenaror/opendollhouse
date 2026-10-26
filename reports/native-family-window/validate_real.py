"""Real X11 family-management gate on a private Xvfb display.

CAS adults, initial funds, and original-source eviction are explicit test-driver
setup. Every subsequent purchase, save, management return and eviction uses real
xdotool window input. Metadata/screenshots only: no saves/assets copied to reports.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import tempfile
import time


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def main():
    checkout = Path(__file__).resolve().parents[2]
    helpers = module('window_checkpoint_helpers', checkout/'reports/native-family-checkpoint/validate_real.py')
    occupancy = module('window_occupancy_helpers', checkout/'reports/native-occupancy-reconstruction/validate_real.py')
    transfer = module('window_transfer_helpers', checkout/'reports/native-family-transfer/validate_real.py')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path(__file__).parent/'attempt-1')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    binary = checkout/'build/linux-checkpoint/odh-person'
    driver = checkout/'build/linux-checkpoint/odh-cas-movein-e2e'
    game = (checkout/'../referencias/The Sims').resolve()
    report = dict(evidence_type='native Linux real-data graphical Xvfb keyboard/mouse family management',
        limits=['CAS creation/funds/source-family eviction are explicit test-driver setup, not graphical CAS proof.',
                'Isolated Xvfb rendering and real XTest input, not physical desktop/hardware or original-game equivalence.',
                'Screenshots and metadata only; no commercial assets or full session archives retained.'],
        binary_sha256=helpers.sha(binary),helper_sha256=helpers.sha(driver),runs=[],actions=[],cases={},passed=False)
    prior = json.loads((checkout/'reports/native-residential-switch/real-1/result.json').read_text())
    protected = set(prior['protected_before'])
    for folder in ('UserData/Houses','UserData/Characters'):
        protected.update(str(p.relative_to(game)) for p in (game/folder).rglob('*') if p.is_file())
    for extra in ('UIGraphics/UIGraphics.far','GameData/UIText.iff'):
        if (game/extra).is_file(): protected.add(extra)
    report['base_revision']=subprocess.run(['git','rev-parse','HEAD'],cwd=checkout,capture_output=True,text=True,check=True).stdout.strip()
    report['source_state_note']='Base revision plus uncommitted graphical management changes; executable hashes are the executed identity.'
    report['protected_before']={name:helpers.sha(game/name) for name in sorted(protected)}
    xvfb = process = None
    profile = None
    stdout_handle = stderr_handle = None
    try:
        with tempfile.TemporaryDirectory(prefix='odh-family-window-') as temporary:
            profile = Path(temporary)/'profile'
            env = os.environ.copy()
            env.pop('DISPLAY',None);env.pop('WAYLAND_DISPLAY',None)
            for key,suffix in (('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_STATE_HOME','state'),
                               ('XDG_CACHE_HOME','cache'),('ODH_STATE_DIR','logs')):
                env[key]=str(profile/suffix)
            setup_command=[str(driver),str(game),str(profile),'--create-only-occupied-house','7']
            setup_child=subprocess.Popen(setup_command,env=env,cwd=checkout,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                setup_stdout,setup_stderr=setup_child.communicate(timeout=90)
            except subprocess.TimeoutExpired:
                setup_child.kill();setup_child.communicate();raise
            setup=subprocess.CompletedProcess(setup_command,setup_child.returncode,setup_stdout,setup_stderr)
            report['setup']=dict(command=setup_command,exit=setup.returncode,stdout_sha256=hashlib.sha256(setup.stdout.encode()).hexdigest(),
                                 stderr_sha256=hashlib.sha256(setup.stderr.encode()).hexdigest(),pid=setup_child.pid,
                                 pid_absent=not Path('/proc',str(setup_child.pid)).exists(),helper_after=helpers.sha(driver))
            assert report['setup']['helper_after']==report['helper_sha256']
            assert setup.returncode==0,setup.stderr
            created=json.loads(setup.stdout)
            report['created']={key:created[key] for key in ('family_chunk_id','family_number','initial_budget','house','second_house',
                'purchase_price','second_purchase_price','members','source_vacated_family_id','source_expired_guids')}
            assert created['house']==7 and created['second_house']==9 and created['create_only']
            family,initial=created['family_chunk_id'],created['initial_budget']
            identity=helpers.sha(game/'UserData/Neighborhood.iff')
            overlay=profile/'config/opendollhouse/player-state'/('neighborhood-'+identity+'.odhp')
            private=profile/'data/opendollhouse/Characters'
            private_before={p.name:helpers.sha(p) for p in private.glob('*.iff')}
            report['private_before']=private_before
            assert len(private_before)==2
            log=profile/'logs/opendollhouse/logs/odh-person.log'
            # Source coordinates are exact English STR# -3 CSV rows.
            raw=(game/'UserData/LotLocations.iff').read_bytes();position=64;locations={}
            while position<len(raw):
                size=struct.unpack_from('>I',raw,position+4)[0]
                assert size>=76 and position+size<=len(raw)
                if raw[position:position+4]==b'STR#' and struct.unpack_from('>H',raw,position+8)[0]==1:
                    body=raw[position+76:position+size];fmt,count=struct.unpack_from('<hH',body);assert fmt==-3
                    offset=4
                    for _ in range(count):
                        language=body[offset];offset+=1
                        end=body.index(0,offset);text=body[offset:end];offset=end+1
                        end=body.index(0,offset);offset=end+1
                        if language==1:
                            house,x,y=map(int,text.split(b','));locations[house]=(x,y)
                position+=size
            assert 7 in locations and 9 in locations
            report['map_coordinates']={str(house):locations[house] for house in (7,9)}
            xvfb=subprocess.Popen(['Xvfb','-displayfd','1','-screen','0','800x600x24','-nolisten','tcp'],
                                  stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
            ready,_,_=select.select([xvfb.stdout],[],[],10);assert ready,'Xvfb display handshake timed out'
            number=xvfb.stdout.readline().strip();xvfb.stdout.close();assert number.isdigit()
            env['DISPLAY']=':'+number;report['xvfb']=dict(pid=xvfb.pid,display=env['DISPLAY'])
            window=None;current=None;log_start=0

            def xdo(*arguments,check=True):
                result=subprocess.run(['xdotool',*map(str,arguments)],env=env,capture_output=True,text=True,timeout=5)
                if check: assert result.returncode==0,(arguments,result.stderr)
                return result

            def title():
                if window is None:return ''
                result=xdo('getwindowname',window,check=False)
                return result.stdout.strip() if result.returncode==0 else ''

            def logs():
                if not log.exists():return ''
                return log.read_text(errors='replace')[log_start:]

            def wait(label,predicate,timeout=90):
                deadline=time.monotonic()+timeout
                while time.monotonic()<deadline:
                    if predicate():
                        report['actions'].append(dict(stage=label,title=title()));print(json.dumps({'stage':label}),flush=True);return
                    if process is not None and process.poll() is not None:
                        raise RuntimeError('game exited while waiting for '+label)
                    time.sleep(.1)
                raise RuntimeError('timed out waiting for '+label+'; title='+title()+'; log_tail='+logs()[-1500:])

            def wait_title(label,text):wait(label,lambda:text in title())

            def key(value):
                xdo('windowfocus','--sync',window);xdo('key','--clearmodifiers',value)
                report['actions'].append(dict(key=value,title=title()))

            def click(x,y):
                assert 0<=x<800 and 0<=y<600
                xdo('windowfocus','--sync',window);xdo('mousemove','--window',window,x,y,'click','1')
                report['actions'].append(dict(click=[x,y],title=title()))

            def capture(label):
                useful={'first-house7-family-selected','first-house7-purchase-confirm','first-house7-save',
                    'first-management-resume-map','evict-house7-evict-confirm','second-house9-save',
                    'return-house7-save','reopened-house7','failure-current-window'}
                if label not in useful:return
                raw_frame=Path(temporary)/'frame.xwd';png=out/(label+'.png')
                subprocess.run(['xwd','-display',env['DISPLAY'],'-id',str(window),'-silent','-out',str(raw_frame)],check=True,timeout=10)
                subprocess.run(['convert',str(raw_frame),str(png)],check=True,timeout=15)
                report['actions'].append(dict(screenshot=png.name,sha256=helpers.sha(png),title=title()))

            def launch(label,arguments=()):
                nonlocal process,window,current,stdout_handle,stderr_handle,log_start
                # LinuxSessionLog rotates the file at each new process. Within
                # one process visit offsets still delimit the appended stages.
                log_start=0
                stdout_path=Path(temporary)/(label+'.stdout');stderr_path=Path(temporary)/(label+'.stderr')
                stdout_handle=stdout_path.open('w');stderr_handle=stderr_path.open('w')
                command=[str(binary),'--game-root',str(game),'--mode','1','--language','14','--no-audio',*arguments]
                current=dict(label=label,command=command,binary_before=helpers.sha(binary),stdout=stdout_path,stderr=stderr_path)
                process=subprocess.Popen(command,cwd=checkout,env=env,stdout=stdout_handle,stderr=stderr_handle)
                current['pid']=process.pid;report['runs'].append(current);window=None
                def find_window():
                    nonlocal window
                    result=xdo('search','--onlyvisible','--pid',process.pid,'--name','OpenDollhouse',check=False)
                    if result.returncode==0 and result.stdout.strip():window=result.stdout.splitlines()[-1];return True
                    return False
                wait(label+'-window',find_window)
                xdo('windowfocus','--sync',window)

            def finish():
                nonlocal process,stdout_handle,stderr_handle
                process.wait(timeout=45);stdout_handle.close();stderr_handle.close()
                stdout_handle=stderr_handle=None
                stdout=current.pop('stdout').read_text(errors='replace');stderr=current.pop('stderr').read_text(errors='replace')
                records=[json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
                current.update(exit=process.returncode,pid_absent=not Path('/proc',str(process.pid)).exists(),binary_after=helpers.sha(binary),
                    stdout_sha256=hashlib.sha256(stdout.encode()).hexdigest(),stderr_sha256=hashlib.sha256(stderr.encode()).hexdigest(),
                    records=[{key:item.get(key) for key in ('ticks','resumed_tick','session_loaded','host_failed','occupancy_retired_ids','family_transfer_applied')}
                             for item in records])
                assert current['binary_before']==current['binary_after']==report['binary_sha256']
                assert process.returncode==0,(process.returncode,stderr[-2500:])
                assert records and all(not item.get('host_failed',False) for item in records),records
                process=None
                return records

            def state():return occupancy.overlay_state(overlay,family,initial)

            projection_baseline=None
            def validate_projection(path,label,house):
                nonlocal projection_baseline
                parts=helpers.sections(path)
                projection=transfer.member_projection(parts)
                assert set(projection)=={member['guid'] for member in created['members']}
                for member in created['members']:
                    assert projection[member['guid']]['identity']=={key:value for key,value in member.items() if key!='guid'}
                people=transfer.world_people(parts[b'WRLD'][1])
                assert not (set(created['source_expired_guids']) & set(people)), 'retired source avatars leaked into GUI save'
                stable={guid:{key:value for key,value in person.items() if key in ('identity','person')} for guid,person in projection.items()}
                if projection_baseline is None:projection_baseline=stable
                else:assert stable==projection_baseline,'portable persistent personal/resource profile changed'
                for person in projection.values():
                    assert all(-100<=need<=100 for need in person['needs'])
                    assert all(0<=fraction<=1000 for fraction in person['fractions'])
                report['cases'][label+'-profile']=dict(
                    complete_roster_and_resources_match=True,persistent_person_projection_equal=True,
                    persistent_projection_sha256=hashlib.sha256(json.dumps(stable,sort_keys=True).encode()).hexdigest(),
                    needs_and_fractions_sha256=hashlib.sha256(json.dumps({guid:(person['needs'],person['fractions']) for guid,person in projection.items()},sort_keys=True).encode()).hexdigest(),
                    motive_policy='Needs and decay fractions may advance while the visible unpaused simulation runs.')

            def save(label,house,epoch,budget):
                marker=len(logs());key('s')
                wait(label+'-written',lambda:'experimental session save written tick=' in logs()[marker:])
                selected=state();assert (selected['house'],selected['epoch'],selected['budget'])==(house,epoch,budget),selected
                path=overlay.parent/'sessions'/identity/(selected['head']+'.odhs')
                assert helpers.sha(path)==selected['head']
                parts=helpers.sections(path)
                assert struct.unpack_from('<HHQ',parts[b'FAML'][1],0)==(1,family,epoch)
                assert struct.unpack_from('<H',parts[b'HOST'][1],11)[0]==house
                validate_projection(path,label,house)
                report['cases'][label]=dict(house=house,epoch=epoch,budget=budget,checkpoint_sha256=selected['head'])
                capture(label)
                return path

            def pause_lot(label):
                wait(label+'-lot-frame',lambda:'lot frame presented tick=' in logs())
                marker=len(logs());key('p');wait(label+'-paused',lambda:'runtime session paused tick=' in logs()[marker:])

            def purchase(label,house,price,budget):
                wait_title(label+'-full-map','C: Create family');capture(label+'-map')
                click(*locations[house]);wait_title(label+'-lot-panel',' | Enter: ');capture(label+'-lot-panel');key('Return')
                wait_title(label+'-family-picker','choose family for House '+str(house))
                key('shift+Tab');capture(label+'-family-selected');key('Return')
                wait_title(label+'-purchase-confirm','confirm family move-in | House '+str(house))
                assert 'budget after '+str(budget) in title(),title()
                capture(label+'-purchase-confirm');key('Return')
                wait_title(label+'-resident','resident '+str(created['members'][0]['neighbor_id']))
                key('Return');pause_lot(label)
                assert state()['house']==house

            def management(label):
                key('m');wait_title(label+'-resume-map','select a lot; current lot')
                capture(label+'-resume-map');key('e')
                wait_title(label+'-full-management','C: Create family');capture(label+'-full-management')

            def evict(label,house,budget):
                click(*locations[house]);wait_title(label+'-lot-panel',' | Enter: ');key('Return')
                wait_title(label+'-occupied-choice','House '+str(house)+' | Enter: enter | E: move out')
                capture(label+'-occupied-choice');key('e')
                wait_title(label+'-evict-confirm','confirm move-out | House '+str(house));capture(label+'-evict-confirm');key('Return')
                wait(label+'-unhoused',lambda:state()['house']==0)
                assert state()['budget']==budget
                wait_title(label+'-map','C: Create family');capture(label+'-unhoused-map')
                report['cases'][label]=dict(house_released=True,budget_unchanged=True)

            try:
                launch('graphical-lifecycle')
                first_budget=initial-created['purchase_price']
                purchase('first-house7',7,created['purchase_price'],first_budget)
                first_path=save('first-house7-save',7,1,first_budget);retained=[(first_path,helpers.sha(first_path))]
                management('first-management');evict('evict-house7',7,first_budget)
                second_budget=first_budget-created['second_purchase_price']
                # Clear per-runtime log view so a prior lot frame cannot satisfy a new visit.
                log_start=len(log.read_text(errors='replace'))
                purchase('second-house9',9,created['second_purchase_price'],second_budget)
                second_path=save('second-house9-save',9,2,second_budget);retained.append((second_path,helpers.sha(second_path)))
                management('second-management');evict('evict-house9',9,second_budget)
                log_start=len(log.read_text(errors='replace'))
                final_budget=second_budget-created['purchase_price']
                purchase('return-house7',7,created['purchase_price'],final_budget)
                final_path=save('return-house7-save',7,3,final_budget);retained.append((final_path,helpers.sha(final_path)))
                final_overlay=overlay.read_bytes();final_digest=helpers.sha(final_path)
                key('Escape');records=finish()
                assert len(records)==3 and all(not item['session_loaded'] for item in records)
                assert [item['family_transfer_applied'] for item in records]==[False,True,True]
                assert [item['occupancy_retired_ids'] for item in records]==[[105,137],[],[105,137]]
                assert all(path.exists() and helpers.sha(path)==digest for path,digest in retained)
                report['cases']['same_process_management']=dict(runtime_visits=3,management_returns=2,graphical_evictions=2,
                    transfer_applied=[item['family_transfer_applied'] for item in records],
                    retired_ids=[item['occupancy_retired_ids'] for item in records])
                saved_tick=struct.unpack_from('<Q',helpers.sections(final_path)[b'HOST'][1],27)[0]
                launch('graphical-fresh-process-reopen',['--house','7','--resident-id',str(created['members'][0]['neighbor_id'])])
                pause_lot('reopened-house7');capture('reopened-house7')
                key('Escape');reopened=finish()
                assert len(reopened)==1 and reopened[0]['session_loaded'] and reopened[0]['resumed_tick']==saved_tick
                assert overlay.read_bytes()==final_overlay and helpers.sha(final_path)==final_digest
                report['cases']['graphical_reopen']=dict(session_loaded=True,resumed_tick=saved_tick,persisted_head_unchanged=True)
                report['private_after']={p.name:helpers.sha(p) for p in private.glob('*.iff')}
                assert report['private_after']==private_before
                report['passed']=True
            except BaseException:
                if window is not None and process is not None and process.poll() is None:
                    try:capture('failure-current-window')
                    except Exception:pass
                report['private_after']={p.name:helpers.sha(p) for p in private.glob('*.iff')}
                report['last_log_excerpt']=logs()[-2500:]
                raise
            finally:
                if process is not None:
                    if process.poll() is None:
                        process.terminate()
                        try:process.wait(timeout=10)
                        except subprocess.TimeoutExpired:process.kill();process.wait()
                    if stdout_handle is not None:stdout_handle.close()
                    if stderr_handle is not None:stderr_handle.close()
                    if current is not None and 'stdout' in current:
                        stdout=current.pop('stdout').read_text(errors='replace');stderr=current.pop('stderr').read_text(errors='replace')
                        current.update(exit=process.returncode,pid_absent=not Path('/proc',str(process.pid)).exists(),binary_after=helpers.sha(binary),
                                       stdout_sha256=hashlib.sha256(stdout.encode()).hexdigest(),stderr_sha256=hashlib.sha256(stderr.encode()).hexdigest(),
                                       stderr_excerpt=stderr[-2500:])
                if xvfb is not None:
                    xvfb.terminate()
                    try:xvfb.wait(timeout=5)
                    except subprocess.TimeoutExpired:xvfb.kill();xvfb.wait()
                    report['xvfb'].update(exit=xvfb.returncode,pid_absent=not Path('/proc',str(xvfb.pid)).exists())
        report['profile_removed']=not profile.exists()
    except BaseException as error:
        report['failure']=str(error)
        report['passed']=False
        if profile is not None:report['profile_removed']=not profile.exists()
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        if xvfb is not None and xvfb.poll() is None:
            xvfb.terminate()
            try:xvfb.wait(timeout=5)
            except subprocess.TimeoutExpired:xvfb.kill();xvfb.wait()
            report['xvfb'].update(exit=xvfb.returncode,pid_absent=not Path('/proc',str(xvfb.pid)).exists())
        report['binary_sha256_after']=helpers.sha(binary)
        report['binary_unchanged']=report['binary_sha256']==report['binary_sha256_after']
        report['protected_after']={name:helpers.sha(game/name) for name in sorted(protected)}
        report['protected_unchanged']=report['protected_before']==report['protected_after']
        if not report['binary_unchanged'] or not report['protected_unchanged']:report['passed']=False
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({'passed':report['passed'],'report':str(out/'result.json'),'failure':report.get('failure')}),flush=True)
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
