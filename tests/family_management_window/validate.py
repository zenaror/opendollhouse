#!/usr/bin/env python3
"""Opt-in actual family UI on a private Xvfb; never starts from an artificial family.

Runs only when explicitly invoked by the coordinator. This is a graphical native
implementation gate, not an original-game appearance or full-day acceptance.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import struct
import zlib
import time
from types import SimpleNamespace


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def need(condition, message):
    if not condition:
        raise ValueError(message)


def sections(path):
    """Read only the bounded ODHS container envelope; typed decoding stays in C++."""
    path = Path(path)
    need(path.stat().st_size <= 256 * 1024 * 1024, 'session container exceeds inspection limit')
    data = path.read_bytes()
    need(len(data) >= 28 and data[:8] == b'ODHSES\r\n', 'invalid session container header')
    need(zlib.crc32(data[:24]) == struct.unpack_from('<I', data, 24)[0], 'session header CRC mismatch')
    need(struct.unpack_from('<Q', data, 16)[0] == len(data) - 28, 'session container extent mismatch')
    count = struct.unpack_from('<I', data, 12)[0]
    need(count <= (len(data) - 28) // 20, 'session section count exceeds available headers')
    result, offset = {}, 28
    for _ in range(count):
        need(offset + 20 <= len(data), 'truncated session section header')
        name, flags, length, crc = struct.unpack_from('<4sIQI', data, offset)
        offset += 20
        need(length <= len(data) - offset, 'session section exceeds container extent')
        body = data[offset:offset + length]
        offset += length
        need(zlib.crc32(body) == crc, 'session section CRC mismatch')
        need(name not in result, 'duplicate session section')
        result[name] = (flags, body)
    need(offset == len(data), 'trailing bytes after session sections')
    return result


def rgb_region(frame, box, width=800, height=600):
    """Observe an exact private-window RGB rectangle, without interpreting assets."""
    need(len(frame) == width * height * 3, 'window RGB extent differs from actual geometry')
    x, y, w, h = box
    need(w > 0 and h > 0 and x >= 0 and y >= 0 and x + w <= width and y + h <= height,
         'observation rectangle outside actual window')
    return b''.join(frame[((y + row) * width + x) * 3:((y + row) * width + x + w) * 3] for row in range(h))


def personality_led_counts(frame):
    """Read the admitted 800x600 CAS LED strip, not a guessed click counter.

    These two exact RGB values were observed in the failed real capture. Other
    assets/colors or an obscuring modal are unsupported observations, not zero.
    The layout's six-pixel step is the production paint_meter/step_cas_trait one.
    """
    need(len(frame) == 800 * 600 * 3, 'window RGB extent differs from actual geometry')
    counts = []
    for row in range(5):
        states = []
        for column in range(10):
            offset = ((143 + row * 32) * 800 + 230 + column * 6) * 3
            color = tuple(frame[offset:offset + 3])
            if color == (0, 255, 255):states.append(True)
            elif color == (41, 41, 90):states.append(False)
            else:return None
        count = sum(states)
        if states != [True] * count + [False] * (10 - count):return None
        counts.append(count)
    return counts


def picker_selected_row(frame):
    """Read only the admitted row strip's normal/hover/selected palette.

    Samples come from the failed actual picker capture and the source's 68px
    strip layout. A white hover is NOT a selected cyan row. Unknown frames and
    zero/multiple selected rows do not authorize any management action.
    """
    need(len(frame) == 800 * 600 * 3, 'window RGB extent differs from actual geometry')
    selected = ((0,241,244),(0,240,243),(0,241,244))
    normal = ((117,121,149),)*3
    hover = ((241,241,244),(240,240,243),(241,241,244))
    active = []
    for row in range(6):
        samples = []
        for x in (44,45,46):
            offset=((106+row*68)*800+x)*3
            samples.append(tuple(frame[offset:offset+3]))
        samples=tuple(samples)
        if samples == selected:active.append(row)
        elif samples not in (normal,hover):return None
    return active[0] if len(active)==1 else None


def isolated_output(output, roots):
    output = Path(output).resolve()
    need(not output.exists(), 'output must be new')
    def inside(path, parent):
        try:
            path.relative_to(parent)
            return True
        except ValueError:
            return False
    for root in roots:
        root = Path(root).resolve()
        need(not inside(output, root) and not inside(root, output), 'output overlaps a protected input/root: '+str(root))
    return output


def stable_family(before, after, family):
    old = next(x for x in before['families'] if x['id'] == family)
    new = next(x for x in after['families'] if x['id'] == family)
    need(old == new, 'protected occupied family changed during unrelated management')
    return new


def compare_reopen(before_sections, after_sections, before, after, admitted_nids, deleted=False):
    need(set(before_sections) == set(after_sections), 'reopen section set changed')
    changed = [name for name in before_sections if before_sections[name] != after_sections[name]]
    need(set(changed) <= {b'RELA'}, 'reopen changed sections outside relationship admission: '+repr(changed))
    key = lambda row: (row['matrix'], row['id'], row['generation'])
    old = {key(row): row for row in before['relationship_sources']}
    new = {key(row): row for row in after['relationship_sources']}
    need(len(old) == len(before['relationship_sources']) and len(new) == len(after['relationship_sources']), 'duplicate relation source')
    need(all(new.get(k) == row for k, row in old.items()), 'pre-existing relationship source was removed or changed')
    extra = {k: row for k, row in new.items() if k not in old}
    expected = set() if deleted else {(0, nid, 0) for nid in admitted_nids}
    need(set(extra) == expected, 'reopen did not admit exactly the newly authenticated neighbor sources')
    need(all(row['pairs'] == 0 for row in extra.values()), 'zero-tick admission invented relationship values')
    if deleted:
        need(not changed, 'after deletion the original checkpoint must reopen byte-identically')
    return dict(changed_sections=[x.decode('ascii') for x in changed], new_empty_neighbor_sources=sorted(admitted_nids) if not deleted else [])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for flag in ('checkout', 'binary', 'reader', 'game-root', 'source-receipt', 'protected-manifest', 'output'):
        p.add_argument('--'+flag, required=True, type=Path)
    p.add_argument('--family', type=int, default=7)
    p.add_argument('--house', type=int, default=1)
    p.add_argument('--cash', type=int, default=9497)
    p.add_argument('--checkpoint-sha256', default='b7409c4d9e61dacab7eb12ff19d1ae254ae36f0cb5fdd361a5ea54b5ffc49533')
    p.add_argument('--timeout-seconds', type=float, default=120)
    args = p.parse_args()
    args.checkout = args.checkout.resolve()
    for name in ('binary', 'reader', 'game_root', 'source_receipt', 'protected_manifest'):
        setattr(args, name, getattr(args, name).resolve())
    smoke_mod = module('family_management_smoke', args.checkout/'tools/validate_runtime_smoke.py')
    sha, inventory = smoke_mod.digest, smoke_mod.profile_files
    producer = json.loads(args.source_receipt.read_text())
    need(producer.get('sequence_completed') and producer.get('last_profile_integrity_verified'), 'source sequence is not complete/integrity verified')
    checkpoint = Path(producer['last_valid_checkpoint']['path']).resolve()
    source_profile = Path(producer['last_valid_profile']['path']).resolve()
    expected_files = producer['last_valid_profile']['files']
    need(sha(checkpoint) == args.checkpoint_sha256, 'source checkpoint is not the explicitly accepted input')
    need(inventory(source_profile) == expected_files, 'source producer inventory changed')
    need(smoke_mod.profile_checkpoint(source_profile, expected_files, args.checkpoint_sha256) == checkpoint, 'receipt checkpoint/profile are not the same context')
    protected = smoke_mod.protected_files(args.protected_manifest, args.game_root)
    original_before = {name: sha(args.game_root/name) for name in protected}
    need(original_before == protected, 'protected source manifest differs before execution')
    args.output = isolated_output(args.output, [args.game_root, source_profile, checkpoint.parent, args.checkout, args.source_receipt.parent])
    args.output.mkdir(parents=True)
    profile = args.output/'profile'
    shutil.copytree(source_profile, profile, symlinks=True)
    need(inventory(profile) == expected_files, 'complete profile copy differs')
    copied_checkpoint = profile/checkpoint.relative_to(source_profile)
    env = os.environ.copy()
    for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'ODH_STATE_DIR'):
        env.pop(name, None)
    for name, child in (('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_STATE_HOME','state'),('XDG_CACHE_HOME','cache')):
        env[name] = str(profile/child)
    report = dict(schema='family_management_window/v1', passed=False, evidence='actual native X11 UI plus separate cold process probes',
                  limits=['Private Xvfb/XTest, not physical display or original appearance comparison.',
                          'Draft editing only; no post-creation Sim editing is claimed.',
                          'No gameplay ticks/day coverage in the management window.'],
                  source_receipt_sha256=sha(args.source_receipt), checkpoint_sha256=args.checkpoint_sha256,
                  binary_sha256=sha(args.binary), reader_sha256=sha(args.reader), actions=[], runs=[], probes=[])
    process = xvfb = window = None
    opened = []

    def save():
        (args.output/'result.json').write_text(json.dumps(report, indent=2)+'\n')

    def inspect(*options, environment=None):
        run = subprocess.run([str(args.reader), *map(str, options)], cwd=args.checkout, env=environment or env,
                             capture_output=True, text=True, timeout=args.timeout_seconds)
        need(run.returncode == 0, 'read-only inspector failed: '+run.stderr[-1600:])
        return json.loads(run.stdout)

    def state(layout=False):
        return inspect('--inspect-family-management', args.game_root, *(['--map-layout'] if layout else []))

    def important():
        return {name: digest for name, digest in inventory(profile).items()
                if Path(name).suffix.lower() in ('.odhp','.odhs','.iff')}

    def xdo(*tokens, check=True):
        run = subprocess.run(['xdotool', *map(str, tokens)], env=env, capture_output=True, text=True, timeout=8)
        if check:
            need(run.returncode == 0, 'private xdotool failed: '+run.stderr)
        return run.stdout.strip()

    def title():
        return xdo('getwindowname', window, check=False) if window else ''

    def wait(label, predicate, timeout=None):
        deadline = time.monotonic()+(timeout or args.timeout_seconds)
        while time.monotonic() < deadline:
            if predicate():
                report['actions'].append(dict(stage=label, title=title()))
                save()
                return
            need(process is None or process.poll() is None, 'window process exited during '+label)
            time.sleep(.12)
        raise TimeoutError(label+'; title='+title())

    def key(value):
        xdo('key','--window',window,'--clearmodifiers',value)
        time.sleep(.16)

    def click(x,y):
        need(0 <= x < 800 and 0 <= y < 600, 'click outside actual 800x600 layout')
        xdo('windowfocus','--sync',window)
        xdo('mousemove','--window',window,x,y)
        xdo('mousedown','1')
        time.sleep(.06)
        xdo('mouseup','1')
        time.sleep(.16)

    def text(value):
        need(value.isascii(), 'gate text must be ASCII to avoid synthesizing input method state')
        xdo('type','--window',window,'--clearmodifiers','--delay','35','--',value)
        time.sleep(.16)

    def observed_rgb():
        raw = subprocess.run(['xwd','-display',env['DISPLAY'],'-id',window,'-silent'],
                             capture_output=True,timeout=12,check=True)
        converted = subprocess.run(['convert','xwd:-','-depth','8','rgb:-'],input=raw.stdout,
                                   capture_output=True,timeout=15,check=True)
        need(len(converted.stdout)==800*600*3, 'private window RGB capture extent differs')
        return converted.stdout

    def observed_region(box):
        return rgb_region(observed_rgb(),box)

    def await_visual_action(label, box, action):
        before = observed_region(box)
        action()
        after = None
        def changed():
            nonlocal after
            after = observed_region(box)
            return after != before
        wait(label,changed,timeout=15)
        report['actions'].append(dict(visual_ack=label,rectangle=list(box),
            before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=hashlib.sha256(after).hexdigest()))

    def await_personality(label, expected, action=None):
        before = personality_led_counts(observed_rgb())
        if action is not None:
            need(before is not None, 'cannot actuate a personality bar before its actual frame is readable')
            action()
        actual = None
        frame_hash = None
        def matches():
            nonlocal actual,frame_hash
            frame = observed_rgb()
            actual = personality_led_counts(frame)
            frame_hash = hashlib.sha256(frame).hexdigest()
            return actual == expected
        wait(label,matches,timeout=20)
        report['actions'].append(dict(personality_ack=label,before=before,after=actual,
                                      expected=expected.copy(),frame_sha256=frame_hash))

    def erase_visible_name(value):
        # WindowInput.text_backspace is a boolean per poll; never enqueue a
        # burst and assume every Backspace was consumed by the UI.
        for index in range(len(value)):
            await_visual_action('name-backspace-'+str(index),(275,52,255,25),lambda:key('BackSpace'))

    def screenshot(label):
        xwd = subprocess.run(['xwd','-display',env['DISPLAY'],'-id',window,'-silent'], capture_output=True, timeout=12, check=True)
        path = args.output/(label+'.png')
        subprocess.run(['convert','xwd:-',str(path)], input=xwd.stdout, capture_output=True, timeout=15, check=True)
        report['actions'].append(dict(screenshot=path.name, sha256=sha(path), title=title()))

    def launch(label):
        nonlocal process, window
        need(process is None, 'previous window process still exists')
        out = (args.output/(label+'-stdout.txt')).open('w')
        err = (args.output/(label+'-stderr.txt')).open('w')
        opened.extend([out,err])
        command = [str(args.binary),'--game-root',str(args.game_root),'--mode','1','--language','14','--no-audio']
        process = subprocess.Popen(command, cwd=args.checkout, env=env, stdout=out, stderr=err)
        record = dict(stage=label,command=command,pid=process.pid)
        report['runs'].append(record)
        window = None
        def found():
            nonlocal window
            values=xdo('search','--onlyvisible','--pid',process.pid,'--name','OpenDollhouse',check=False).splitlines()
            if values:
                window=values[-1]
                return True
            return False
        wait(label+'-map-window',found)
        # The actual source sets the map title before polling. Keep it as an
        # observed state, rather than hardcoding a translated title.
        wait(label+'-actual-map',lambda:title()==baseline['ui_titles']['map'])
        geometry=xdo('getwindowgeometry','--shell',window)
        need('WIDTH=800' in geometry and 'HEIGHT=600' in geometry, 'actual UI geometry changed; no scaled coordinate guesses')
        return title()

    def close_map():
        nonlocal process,window
        key('Escape')
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            raise RuntimeError('map Escape did not close the actual application')
        report['runs'][-1].update(exit=process.returncode,pid_absent=not Path('/proc',str(process.pid)).exists())
        need(process.returncode == 0, 'management window did not exit cleanly')
        process=None
        window=None
        for handle in opened:
            handle.flush()

    def reopen_probe(label, admitted_nids, deleted=False):
        need(process is None, 'probe requires stopped management window')
        before_files=inventory(profile)
        probe_root=args.output/(label+'-probe')
        probe_root.mkdir()
        settings=SimpleNamespace(binary=args.binary,game_root=args.game_root,checkpoint=checkpoint,
            protected_manifest=args.protected_manifest,output=probe_root,require_integer=[],launcher_arg=['--mode','1','--language','14','--no-audio'],
            timeout_seconds=args.timeout_seconds)
        smoke=smoke_mod.Smoke(settings)
        def valid(document, record):
            need(document.get('blocking_dialog') is None, 'unexpected blocking dialog during zero-tick reopen')
        record=smoke.run('reopen',copied_checkpoint,0,[],[],expected_base=baseline_session['tick'],
                         expected_source_hash=args.checkpoint_sha256,automatic_dialog_ok=False,report_validator=valid,
                         profile_source=profile,expected_profile_hashes=before_files)
        need(record['ok'], 'cold reopen failed: '+repr(record['errors']))
        after=inspect('--inspect-session',record['save'])
        comparison=compare_reopen(baseline_sections,sections(Path(record['save'])),baseline_session,after,admitted_nids,deleted)
        if deleted:
            need(record['save_sha256'] == args.checkpoint_sha256, 'final cold input changed after auxiliary archive')
        report['probes'].append(dict(stage=label,process=record,comparison=comparison))
        need(inventory(profile) == before_files, 'probe modified its management source profile')
        save()

    def selected_picker_row(label, expected):
        actual = None
        frame_hash = None
        def selected():
            nonlocal actual,frame_hash
            frame=observed_rgb()
            actual=picker_selected_row(frame)
            frame_hash=hashlib.sha256(frame).hexdigest()
            return actual==expected
        wait(label,selected,timeout=25)
        report['actions'].append(dict(picker_selected_row=actual,expected=expected,
                                      stage=label,frame_sha256=frame_hash))

    def park_pointer():
        # The source paints a held/hovered row using another strip state. Read
        # selection only with the pointer outside rows and buttons, after release.
        xdo('mousemove','--window',window,780,510)

    def select_aux(aux_id, layout):
        click(layout['vacant_click']['x'],layout['vacant_click']['y'])
        wait('vacant-lot-picker',lambda:title()==baseline['ui_titles']['picker'])
        park_pointer()
        # Title is assigned before expensive portrait rendering. A title alone
        # does not prove that its initial input loop/frame is ready.
        selected_picker_row('picker-initial-real-selection',0)
        current=state()
        index=current['unhoused_order'].index(aux_id)
        for page in range(index//6):
            def next_page():
                click(726,504)
                park_pointer()
            await_visual_action('picker-page-'+str(page+1),(41,76,690,408),next_page)
            selected_picker_row('picker-new-page-selection',0)
        click(240,76+(index%6)*68+30)
        park_pointer()
        selected_picker_row('auxiliary-row-selected',index%6)
        return title()

    try:
        baseline=state(True)
        baseline_session=inspect('--inspect-session-head',args.game_root,args.house,args.family)
        need(baseline_session['authenticated_journal_head'], 'source head not authenticated')
        need(Path(baseline_session['archive_path']).resolve()==copied_checkpoint.resolve(), 'source head differs from accepted copied input')
        protected_family=next(x for x in baseline['families'] if x['id']==args.family)
        need((protected_family['house'],protected_family['budget'],protected_family['head'])==(args.house,args.cash,args.checkpoint_sha256), 'protected family state differs')
        baseline_sections=sections(copied_checkpoint)
        baseline_important=important()
        report['baseline']=baseline
        # Private X server; no inherited desktop display or physical input.
        xvfb=subprocess.Popen(['Xvfb','-displayfd','1','-screen','0','800x600x24','-nolisten','tcp'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        ready,_,_=select.select([xvfb.stdout],[],[],10)
        need(bool(ready), 'private Xvfb handshake timed out')
        number=xvfb.stdout.readline().strip()
        need(number.isdigit(), 'private Xvfb did not provide its display number')
        xvfb.stdout.close()
        env['DISPLAY']=':'+number
        report['xvfb']=dict(pid=xvfb.pid,display=env['DISPLAY'])
        map_title=launch('create-draft')
        key('c');wait('family-list',lambda:title()==baseline['ui_titles']['picker'])
        picker_title=title();key('c');wait('cas-family',lambda:title()==baseline['ui_titles']['cas_family'])
        family_title=title()
        click(350,62);text('ODH UI Management')
        def new_member(name,child=False):
            click(660,150);wait('cas-member-editor',lambda:title()==baseline['ui_titles']['cas_character'])
            await_personality('fresh-member-zero-points',[0]*5)
            click(350,64);text(name)
            if child:
                await_visual_action('child-selected',(406,101,150,101),lambda:click(450,155))
            expected=[0]*5
            for row in range(5):
                for point in range(5):
                    expected[row]=point+1
                    await_personality('trait-'+str(row)+'-point-'+str(point+1),expected,
                                      lambda row=row:click(284,143+row*32))
            click(200,560);wait('cas-member-accepted',lambda:title()==family_title)
        new_member('Ava Original')
        click(145,160);click(655,355);wait('edit-draft-member',lambda:title()==baseline['ui_titles']['cas_character'])
        await_personality('existing-draft-points',[5]*5)
        click(350,64)
        erase_visible_name('Ava Original')
        text('Ava Edited');click(200,560);wait('draft-edit-accepted',lambda:title()==family_title)
        click(145,160);click(655,355);wait('edit-draft-cancel',lambda:title()==baseline['ui_titles']['cas_character'])
        await_personality('existing-draft-points-before-cancel',[5]*5)
        click(350,64)
        erase_visible_name('Ava Edited')
        text('Ava Discarded');key('Escape');wait('draft-edit-cancelled',lambda:title()==family_title)
        new_member('Ben Child',True)
        screenshot('draft-adult-child')
        click(595,560);key('Return')  # Actual CAS question defaults to No.
        need(title()==family_title, 'cancel-family No unexpectedly left the draft')
        need(important()==baseline_important, 'draft/edit cancellations published player data')
        click(200,560);screenshot('finish-family-question');key('Tab');key('Return')
        wait('family-created',lambda:state()['creation_count']==baseline['creation_count']+1)
        created=state()
        aux_candidates=[x for x in created['families'] if x['id'] not in {f['id'] for f in baseline['families']}]
        need(len(aux_candidates)==1, 'actual UI did not create exactly one auxiliary family')
        aux=aux_candidates[0];aux_id=aux['id'];admitted_nids=[x['nid'] for x in aux['members']]
        need(aux['house']==0 and aux['budget']==20000 and aux['name']=='ODH UI Management', 'auxiliary family is not a normal unhoused creation')
        need(sorted((m['age'],m['given_name'],m['catalog_strings_id']) for m in aux['members'])==[(9,'Ben Child',2000),(27,'Ava Edited',2000)], 'actual character reader did not confirm accepted adult/child and cancelled edit')
        stable_family(baseline,created,args.family)
        report['auxiliary_created']=aux
        screenshot('created-family-in-list')
        key('Escape');wait('back-to-map',lambda:title()==map_title);close_map()
        reopen_probe('after-create',admitted_nids)
        map_title=launch('rename')
        picker_title=select_aux(aux_id,baseline)
        before_cancel=important()
        key('r');wait('rename-dialog',lambda:title()!=picker_title);text('ODH Discard Rename');key('Escape')
        wait('rename-cancelled',lambda:title()==picker_title)
        need(important()==before_cancel, 'cancelled rename changed durable family data')
        key('r');wait('rename-dialog-accepted',lambda:title()!=picker_title);text('ODH UI Renamed');key('Return')
        wait('rename-persisted',lambda:next(f for f in state()['families'] if f['id']==aux_id)['name']=='ODH UI Renamed')
        renamed=state();stable_family(baseline,renamed,args.family)
        renamed_aux=next(f for f in renamed['families'] if f['id']==aux_id)
        need(renamed_aux['members']==aux['members'] and renamed_aux['guids']==aux['guids'], 'rename changed immutable character identity/content')
        screenshot('renamed-family');key('Escape');wait('rename-back-map',lambda:title()==map_title);close_map()
        reopen_probe('after-rename',admitted_nids)
        map_title=launch('archive')
        select_aux(aux_id,baseline)
        before_cancel=important();picker_title=title()
        click(458,560);wait('archive-question',lambda:title()!=picker_title);screenshot('archive-cancel-question');key('Escape')
        wait('archive-cancel-map',lambda:title()==map_title)
        need(important()==before_cancel, 'cancelled archive changed durable family data')
        select_aux(aux_id,baseline);picker_title=title();click(458,560);wait('archive-confirm',lambda:title()!=picker_title)
        key('Return')  # Management confirmation explicitly maps Enter to Yes.
        wait('archive-persisted',lambda:all(f['id']!=aux_id for f in state()['families']))
        archived=state();stable_family(baseline,archived,args.family)
        need(archived['archive_count']==baseline['archive_count']+1 and archived['creation_count']==created['creation_count'], 'archive did not retain its real creation history')
        # Logical archive retains private characters; it cannot delete arbitrary files.
        final_important=important()
        for name,digest in before_cancel.items():
            if Path(name).suffix.lower() in ('.iff','.odhs'):
                need(final_important.get(name)==digest, 'archive changed character/session bytes')
        wait('archive-back-map',lambda:title()==map_title);close_map()
        reopen_probe('after-delete',admitted_nids,deleted=True)
        map_title=launch('reopen-management')
        key('c');wait('reopened-family-list',lambda:title()!=map_title)
        need(aux_id not in state()['unhoused_order'], 'archived family reappeared after reopening management')
        screenshot('reopened-list-without-auxiliary');key('Escape');wait('final-map',lambda:title()==map_title);close_map()
        report['final']=state()
        stable_family(baseline,report['final'],args.family)
        need(report['final']['families']==baseline['families'], 'management changed a pre-existing effective family')
        need(report['final']['assignment_count']==baseline['assignment_count'], 'management unexpectedly purchased/moved a family')
        for name,digest in baseline_important.items():
            if Path(name).suffix.lower() in ('.iff','.odhs'):
                need(important().get(name)==digest, 'management changed a pre-existing character/save')
        report['passed']=True
    except Exception as error:
        report['error']=str(error)
        if window and process and process.poll() is None:
            try:screenshot('failure-current-window')
            except Exception as capture_error:report['capture_error']=str(capture_error)
    finally:
        for child,label in ((process,'window_cleanup'),(xvfb,'xvfb_cleanup')):
            if child is not None:
                if child.poll() is None:
                    child.terminate()
                    try:child.wait(timeout=8)
                    except subprocess.TimeoutExpired:child.kill();child.wait(timeout=8)
                report[label]=dict(pid=child.pid,exit=child.returncode,pid_absent=not Path('/proc',str(child.pid)).exists())
        for handle in opened:handle.close()
        report['guards']=dict(source_profile_unchanged=inventory(source_profile)==expected_files,
            source_checkpoint_unchanged=sha(checkpoint)==args.checkpoint_sha256,
            source_receipt_unchanged=sha(args.source_receipt)==report['source_receipt_sha256'],
            binary_unchanged=sha(args.binary)==report['binary_sha256'],reader_unchanged=sha(args.reader)==report['reader_sha256'],
            commercial_sources_unchanged={name:sha(args.game_root/name) for name in protected}==original_before)
        report['passed']=report['passed'] and all(report['guards'].values())
        save()
    print(json.dumps(dict(passed=report['passed'],result=str(args.output/'result.json'),error=report.get('error'))))
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
