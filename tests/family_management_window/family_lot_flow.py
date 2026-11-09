"""One opt-in family/lot GUI chain. No writers, headless actions or game invocation.
Called by the maintained window driver, which owns isolation/process cleanup.
Coordinates/frame references must be reviewed from the real UI before execution.
"""
import hashlib
import json
from pathlib import Path
import subprocess

OPERATIONS = ('enter-first','purchase','move','rotate','cancel','sell','build','leave-first',
              'enter-cold','leave-cold','enter-second','leave-second',
              'enter-reoccupied','leave-reoccupied','enter-reoccupied-cold','leave-reoccupied-cold')
KEYS = {'Return','Escape','Tab','BackSpace','F1','F2','F3','F5','p','s','m','e','Delete','comma','period'}

def need(value, message):
    if not value:raise ValueError(message)

def integer(value, low, high):return type(value) is int and low <= value <= high

def signature(value):
    need(isinstance(value,dict) and set(value)=={'png','sha256','box'}, 'frame reference requires exact png/SHA/box')
    need(isinstance(value['png'],str) and value['png'].endswith('.png'), 'reference must name an observed PNG')
    need(isinstance(value['sha256'],str) and len(value['sha256'])==64 and all(c in '0123456789abcdef' for c in value['sha256']), 'invalid observed PNG SHA')
    b=value['box'];need(isinstance(b,list) and len(b)==4 and all(type(x) is int for x in b), 'invalid reference rectangle')
    x,y,w,h=b;need(x>=0 and y>=0 and w>0 and h>0 and x+w<=800 and y+h<=600, 'reference rectangle outside800x600')

def validate_plan(plan):
    need(isinstance(plan,dict) and set(plan)=={'schema','source_proof','purchase_guid','purchase_price','sale_refund','build_cost','floor','operations'}, 'unknown or missing GUI plan field')
    need(plan['schema']=='family_lot_window/v1' and isinstance(plan['source_proof'],str) and plan['source_proof'].strip(), 'reviewed UI source provenance required')
    for key,low,high in [('purchase_guid',1,0xffffffff),('purchase_price',1,20000),('sale_refund',0,20000),('build_cost',1,20000)]:
        need(integer(plan[key],low,high), 'invalid '+key)
    f=plan['floor'];need(isinstance(f,dict) and set(f)=={'x','y','level','material'}, 'exact floor observation required')
    need(integer(f['x'],0,63) and integer(f['y'],0,63) and integer(f['level'],1,2) and integer(f['material'],1,65535), 'invalid observed floor')
    need(isinstance(plan['operations'],dict) and set(plan['operations'])==set(OPERATIONS), 'all fixed GUI operations are mandatory')
    for name,op in plan['operations'].items():
        need(isinstance(op,dict) and set(op)=={'ready','events','complete'}, 'unknown operation field')
        signature(op['ready']);signature(op['complete']);need(isinstance(op['events'],list) and 1<=len(op['events'])<=128, 'GUI gesture limit')
        for event in op['events']:
            need(isinstance(event,dict) and event.get('kind') in {'key','click','drag'}, 'only actual UI gestures allowed')
            signature(event.get('before'))
            if event['kind']=='key':need(set(event)=={'kind','value','before'} and event['value'] in KEYS, 'unapproved UI key')
            else:
                fields={'kind','point','before'} if event['kind']=='click' else {'kind','from','to','before'}
                need(set(event)==fields, 'unknown pointer gesture field')
                for field in (['point'] if event['kind']=='click' else ['from','to']):
                    q=event[field];need(isinstance(q,list) and len(q)==2 and integer(q[0],0,799) and integer(q[1],0,599), 'pointer outside actual window')
        need(op['events'][-1]['before']['box']==op['complete']['box'], 'completion must observe the final gesture rectangle')
    return plan

def load_plan(path, expected):
    data=Path(path).read_bytes();need(hashlib.sha256(data).hexdigest()==expected, 'GUI plan changed since review')
    plan=validate_plan(json.loads(data))
    for op in plan['operations'].values():
        for ref in [op['ready'],op['complete']]+[e['before'] for e in op['events']]:
            f=Path(ref['png']);need(f.is_file() and not f.is_symlink() and f.stat().st_size<=16*1024*1024, 'observed reference PNG unavailable')
            need(hashlib.sha256(f.read_bytes()).hexdigest()==ref['sha256'], 'observed reference PNG changed')
    return plan

def gui_reports(text, family, house, loaded=None, resumed_tick=None):
    """Actual stdout from GUI-driven product runs, never fabricated probe output."""
    documents=[];decoder=json.JSONDecoder();remaining=text
    while remaining.strip():
        value,end=decoder.raw_decode(remaining.lstrip());documents.append(value);remaining=remaining.lstrip()[end:]
    runs=[d for d in documents if isinstance(d,dict) and 'session_loaded' in d]
    need(runs, 'GUI process produced no native runtime report')
    for d in runs:
        c=d.get('integrated_session_context') or {}
        need(c.get('family_chunk_id')==family and c.get('house_number')==house, 'GUI entered another family/house')
        need(not d.get('diagnostics_truncated',False), 'runtime diagnostics truncated')
    if loaded is not None:need(runs[0]['session_loaded'] is loaded, 'cold GUI did not restore native session')
    if resumed_tick is not None:need(runs[0].get('resumed_tick')==resumed_tick, 'cold GUI restored another tick')
    return runs

def identity(document):
    members=document['members'];result={m['guid']:(m['neighbor_id'],m['age'],m['gender'],m['body']) for m in members}
    need(len(result)==len(members), 'duplicate household identity')
    return result

def floor_value(document, floor):
    a=document['architecture'];x,y=floor['x'],floor['y']
    need(x<a['width'] and y<a['height'], 'floor coordinate outside admitted house')
    return a['floors'][floor['level']-1][y*a['width']+x]

def run(ctx, plan):
    """Finite chain; the existing driver supplies ONLY UI actions/read-only observers."""
    args=ctx['args'];report=ctx['report'];state=ctx['state'];inspect=ctx['inspect'];wait=ctx['wait'];key=ctx['key'];click=ctx['click'];save=ctx['save']
    baseline=ctx['baseline'];a=ctx['aux'];aid=a['id'];refs={};records=[]
    report['family_lot']=dict(plan_source_proof=plan['source_proof'],stages=records,accepted=False,
        limits=['GUI chain not original appearance or full Build acceptance.','Draft editing only, no saved Sim editing.',
                'No whole game day or first playable milestone acceptance.','Unknown frames/dialogs stop; no generic notice answers.'])
    def family(fid):return next(f for f in state()['families'] if f['id']==fid)
    def head(fid):
        f=family(fid);need(f['house'] and f['head'], 'family has no committed GUI checkpoint')
        d=inspect('--inspect-session-head',args.game_root,f['house'],fid)
        need(d['authenticated_journal_head'] and d['family_chunk_id']==fid and d['house']==f['house'], 'head does not authenticate current occupancy')
        need(ctx['sha'](Path(d['archive_path']))==f['head'], 'head bytes differ from overlay')
        need(not d.get('blocking_dialog'), 'unreviewed native dialog stops GUI chain')
        return d
    def observe(ref):
        path=Path(ref['png']);need(ctx['sha'](path)==ref['sha256'], 'reference changed during GUI')
        cache=(str(path),ref['sha256'])
        if cache not in refs:
            q=subprocess.run(['convert',str(path),'-depth','8','rgb:-'],capture_output=True,timeout=15,check=True)
            need(len(q.stdout)==800*600*3, 'reference PNG must be actual800x600 window')
            refs[cache]=q.stdout
        expected=ctx['rgb_region'](refs[cache],ref['box'])
        wait('observed-gui-frame',lambda:ctx['observed_region'](ref['box'])==expected,timeout=30)
        return expected
    def operation(name):
        op=plan['operations'][name];observe(op['ready'])
        for event in op['events']:
            before=observe(event['before'])
            if event['kind']=='key':key(event['value'])
            elif event['kind']=='click':click(*event['point'])
            else:ctx['drag'](event['from'],event['to'])
        complete=observe(op['complete'])
        need(complete!=before, 'completion reference does not acknowledge the last gesture') # A cancellation must be visibly consumed before checking no writes.
        records.append(dict(operation=name,ui_gestures=len(op['events'])));save()
    def start(label):
        ctx['launch'](label);ctx['ui_ready']('map')
    def leave(label,fid,house,operation_name,loaded=None,tick=None):
        operation(operation_name);wait('actual-management-map',lambda:ctx['title']()==baseline['ui_titles']['map'])
        ctx['close_map']() # Resume-picker Escape returns to lot and FAILS this exit check.
        path=args.output/(label+'-stdout.txt');runs=gui_reports(path.read_text(),fid,house,loaded,tick)
        records.append(dict(gui_process=label,native_runtime_reports=len(runs),family=fid,house=house,
                            loaded=runs[0]['session_loaded'],resumed_tick=runs[0].get('resumed_tick')));save()
        return head(fid)
    def quote(fid,house):
        try:return inspect('--inspect-property-head',args.game_root,house)['vacant_purchase_value']
        except ValueError as error:
            need('house has no committed property head' in str(error), 'property authority failed: '+str(error))
        return inspect('--inspect-family-state',args.game_root,fid,house)['destination_price']
    def buy_house(label,fid,lot,entry,cancel=False):
        start(label);amount=quote(fid,lot['house']);before=family(fid);need(before['house']==0 and before['budget']>=amount, 'normal family cannot afford selected real house')
        def question():
            click(lot['x'],lot['y']);wait('actual-family-picker',lambda:ctx['title']()==baseline['ui_titles']['picker']);ctx['keyboard_select_aux'](fid)
            ctx['open_modal']('actual-house-question',lambda:key('Return'))
        question()
        if cancel:
            important=ctx['important']();key('Escape');wait('purchase-cancelled-map',lambda:ctx['title']()==baseline['ui_titles']['map'])
            need(ctx['important']()==important and family(fid)==before, 'cancelled house purchase published state');question()
        key('Return');wait('actual-house-purchase',lambda:family(fid)['house']==lot['house'])
        operation(entry);wait('actual-house-save',lambda:bool(family(fid)['head']));d=head(fid)
        need(d['budget']==before['budget']-amount, 'house price not charged exactly once')
        return d
    def evict(label,fid,lot,cancel=False):
        before=family(fid);q=inspect('--inspect-family-state',args.game_root,fid)['move_out_quote_budget'];start(label)
        def question():key('e');ctx['open_modal']('actual-eviction-question',lambda:click(lot['x'],lot['y']))
        question()
        if cancel:
            important=ctx['important']();key('Escape');wait('evict-cancel-map',lambda:ctx['title']()==baseline['ui_titles']['map'])
            need(ctx['important']()==important and family(fid)==before, 'cancelled eviction changed data');question()
        key('Return');wait('actual-family-unhoused',lambda:family(fid)['house']==0)
        need(family(fid)['budget']==q, 'eviction refund differs from actual shared quote')
        property=inspect('--inspect-property-head',args.game_root,lot['house']);need(property['authenticated_property_head'] and not property['occupied_by'], 'vacant property not published')
        need(not ({m['guid'] for m in before['members']} & {o['guid'] for o in property['active_objects']}), 'retired family remains active in released lot')
        wait('evict-map',lambda:ctx['title']()==baseline['ui_titles']['map']);ctx['close_map']();return property
    first=state(True)['vacant_click'];initial=buy_house('first-house',aid,first,'enter-first',True)
    need(identity(initial)=={m['guid']:(m['nid'],m['age'],m['gender'],next(x['body'] for x in initial['members'] if x['guid']==m['guid'])) for m in a['members']}, 'created adult/child identity changed at entry')
    before=initial;operation('purchase');wait('purchase-checkpoint',lambda:family(aid)['head']!=ctx['sha'](Path(before['archive_path'])));bought=head(aid)
    groups=[g for g in bought['purchase_groups'] if g['requested_guid']==plan['purchase_guid'] and g['group_id']not in {g['group_id'] for g in before['purchase_groups']}]
    need(len(groups)==1 and groups[0]['price']==plan['purchase_price'] and bought['budget']==before['budget']-plan['purchase_price'], 'actual GUI purchase/group/charge differ')
    group=groups[0]
    for phase in ('move','rotate'):
        prior=head(aid);operation(phase);wait(phase+'-saved',lambda:family(aid)['head']!=ctx['sha'](Path(prior['archive_path'])));after=head(aid)
        old=next(g for g in prior['purchase_groups'] if g['group_id']==group['group_id']);new=next(g for g in after['purchase_groups'] if g['group_id']==group['group_id'])
        need(old['members']!=new['members'] and after['budget']==prior['budget'] and {(x['id'],x['generation'],x['guid']) for x in old['members']}=={(x['id'],x['generation'],x['guid']) for x in new['members']}, 'move/rotation lost group or charged money')
    important=ctx['important']();operation('cancel');need(ctx['important']()==important, 'cancelled buy gesture published data')
    prior=head(aid);operation('sell');wait('sell-saved',lambda:family(aid)['head']!=ctx['sha'](Path(prior['archive_path'])));sold=head(aid)
    need(all(g['group_id']!=group['group_id'] for g in sold['purchase_groups']) and sold['budget']==prior['budget']+plan['sale_refund'], 'sale/refund mismatch')
    operation('build');wait('build-saved',lambda:family(aid)['head']!=ctx['sha'](Path(sold['archive_path'])));built=head(aid)
    need(built['budget']==sold['budget']-plan['build_cost'] and floor_value(built,plan['floor'])==plan['floor']['material'] and built['architecture']['canonical_sha256']!=sold['architecture']['canonical_sha256'], 'actual construction/cost/floor mismatch')
    # Remember next actual vacant lot BEFORE releasing first; never guess another map coordinate.
    second=state(True)['vacant_click'];need(second['house']!=first['house'], 'second destination is not distinct')
    saved=leave('first-house',aid,first['house'],'leave-first')
    start('cold-first');click(first['x'],first['y']);operation('enter-cold')
    cold=leave('cold-first',aid,first['house'],'leave-cold',True,saved['tick'])
    need(identity(cold)==identity(saved) and cold['budget']==saved['budget'] and cold['architecture']==saved['architecture'], 'cold GUI lost family/house state')
    released=evict('first-evict',aid,first,True)
    need(released['architecture']['canonical_sha256']==built['architecture']['canonical_sha256'], 'released house lost construction')
    need(not ({x['guid'] for x in group['members']} & {o['guid'] for o in released['active_objects']}), 'sold group resurrected during release')
    moved=buy_house('second-house',aid,second,'enter-second');need(identity(moved)==identity(saved), 'second house changed character identity')
    moved=leave('second-house',aid,second['house'],'leave-second');need(family(aid)['members']==a['members'], 'second move rewrote private character resources')
    # A second normal family is created with the SAME actual CAS consumer.
    start('create-reoccupant');key('c');wait('family-picker',lambda:ctx['title']()==baseline['ui_titles']['picker']);ctx['ui_ready']('picker');key('c');wait('cas-family',lambda:ctx['title']()==baseline['ui_titles']['cas_family']);ctx['ui_ready']('cas_family')
    old=state();ctx['text']('ODH UI Reoccupy');key('Return');ctx['ui_ready']('cas_family');ctx['new_member']('Cora Resident');ctx['new_member']('Dan Child',True)
    ctx['open_modal']('finish-reoccupant',lambda:click(200,560));key('Tab');key('Return');wait('reoccupant-created',lambda:state()['creation_count']==old['creation_count']+1)
    created=[f for f in state()['families'] if f['id']not in {x['id'] for x in old['families']}];need(len(created)==1, 'CAS did not create exactly one reoccupant');b=created[0];bid=b['id']
    need(b['house']==0 and b['budget']==20000 and sorted((m['age'],m['given_name']) for m in b['members'])==[(9,'Dan Child'),(27,'Cora Resident')], 'reoccupant is not a real normal adult/child family')
    key('Escape');wait('created-map',lambda:ctx['title']()==baseline['ui_titles']['map']);ctx['close_map']()
    vacant=state(True)['vacant_click'];need(vacant['house']==first['house'], 'actual first vacant selector no longer identifies released house')
    newhome=buy_house('reoccupy',bid,vacant,'enter-reoccupied');need(floor_value(newhome,plan['floor'])==plan['floor']['material'], 'reoccupied house reverted to installed IFF')
    need(not ({x['guid'] for x in group['members']} & {x['guid'] for g in newhome['purchase_groups'] for x in g['members']}), 'old sold group reappeared for new family')
    newhome=leave('reoccupy',bid,first['house'],'leave-reoccupied');start('cold-reoccupied');click(vacant['x'],vacant['y']);operation('enter-reoccupied-cold')
    reopened=leave('cold-reoccupied',bid,first['house'],'leave-reoccupied-cold',True,newhome['tick']);need(identity(reopened)==identity(newhome) and reopened['budget']==newhome['budget'] and reopened['architecture']==newhome['architecture'], 'new family cold reopen lost state')
    # Check A still opens from its own checkpoint after B published shared state.
    start('cold-second');click(second['x'],second['y']);operation('enter-second');after=leave('cold-second',aid,second['house'],'leave-second',True,moved['tick']);need(identity(after)==identity(moved) and after['budget']==moved['budget'], 'other family overwrote moved household')
    evict('final-evict',aid,second)
    start('rename-delete');ctx['select_aux'](aid,state(True));before=ctx['important']();ctx['open_modal']('rename-cancel',lambda:key('r'));ctx['rename_text']('Discarded');key('Escape');wait('rename-cancelled',lambda:ctx['title']()==baseline['ui_titles']['picker']);need(ctx['important']()==before, 'rename cancel changed data')
    ctx['keyboard_select_aux'](aid);ctx['open_modal']('rename-accept',lambda:key('r'));ctx['rename_text']('ODH UI Moved');key('Return');wait('renamed',lambda:family(aid)['name']=='ODH UI Moved');ctx['ui_ready']('picker')
    immutable_before_archive=ctx['important']()
    immutable_before_archive=ctx['important']()
    before=ctx['important']();ctx['open_modal']('delete-cancel',lambda:click(458,560),baseline['ui_titles']['archive']);key('Escape');wait('archive-cancelled-map',lambda:ctx['title']()==baseline['ui_titles']['map']);need(ctx['important']()==before, 'archive cancel changed data')
    ctx['select_aux'](aid,state(True));ctx['open_modal']('delete-accept',lambda:click(458,560),baseline['ui_titles']['archive']);key('Return');wait('archived',lambda:all(f['id']!=aid for f in state()['families']));wait('archived-map',lambda:ctx['title']()==baseline['ui_titles']['map']);ctx['close_map']()
    start('reopened-final-list');key('c');wait('actual-final-picker',lambda:ctx['title']()==baseline['ui_titles']['picker']);ctx['ui_ready']('picker');need(aid not in state()['unhoused_order'], 'archived family returned');key('Escape');wait('final-map',lambda:ctx['title']()==baseline['ui_titles']['map']);ctx['close_map']()
    final_files=ctx['important']()
    need(all(final_files.get(name)==digest for name,digest in immutable_before_archive.items() if Path(name).suffix.lower() in ('.iff','.odhs')), 'archive changed character or session bytes')
    final_files=ctx['important']()
    need(all(final_files.get(name)==digest for name,digest in immutable_before_archive.items() if Path(name).suffix.lower() in ('.iff','.odhs')), 'archive changed character or session bytes')
    final=state();need(final['archive_count']==baseline['archive_count']+1 and final['creation_count']==baseline['creation_count']+2, 'GUI history does not contain two real creations and one logical archive')
    need(final['archive_count']==baseline['archive_count']+1 and final['creation_count']==baseline['creation_count']+2, 'GUI history does not contain two real creations and one logical archive')
    need([f for f in final['families'] if f['id']!=bid]==baseline['families'], 'pre-existing families changed')
    need(family(bid)['members']==b['members'] and family(bid)['house']==first['house'], 'reoccupant identity/occupancy changed')
    report['final']=final;report['family_lot'].update(accepted=True,family_a=aid,family_b=bid,first_house=first['house'],second_house=second['house'],final_reoccupied_head=family(bid)['head'],observed_reference_hashes=sorted({sha for _,sha in refs}))
    report['passed']=True;save()
