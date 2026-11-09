#!/usr/bin/env python3
"""Synthetic control/gate checks only; does not open X11 or run game binaries."""
import copy
import importlib.util
from pathlib import Path
import unittest
import tempfile
import struct
import zlib

spec=importlib.util.spec_from_file_location('private_management_driver',Path(__file__).with_name('validate.py'))
driver=importlib.util.module_from_spec(spec);spec.loader.exec_module(driver)

class Gates(unittest.TestCase):
    def setUp(self):
        self.old={'relationship_sources':[dict(matrix=0,id=3,generation=0,pairs=1,sha256='old-neighbor'),
                                          dict(matrix=1,id=12,generation=9,pairs=0,sha256='old-object')]}
        self.new=copy.deepcopy(self.old)
        self.new['relationship_sources'] += [dict(matrix=0,id=n,generation=0,pairs=0,sha256='new'+str(n)) for n in (5,6)]
        self.sections={b'HOST':(0,b'host'),b'RELA':(0,b'old')}
        self.changed={**self.sections,b'RELA':(0,b'new')}
    def test_additive_authenticated_sources_only(self):
        result=driver.compare_reopen(self.sections,self.changed,self.old,self.new,[5,6])
        self.assertEqual(result['new_empty_neighbor_sources'],[5,6])
    def test_archive_exact_restoration(self):
        driver.compare_reopen(self.sections,self.sections,self.old,self.old,[5,6],True)
    def test_negative_matrix(self):
        cases=[]
        missing=copy.deepcopy(self.new);missing['relationship_sources'].pop(0);cases.append(('missing_old',self.changed,missing,[5,6],False))
        changed=copy.deepcopy(self.new);changed['relationship_sources'][0]['sha256']='changed';cases.append(('old_values',self.changed,changed,[5,6],False))
        stale=copy.deepcopy(self.new);stale['relationship_sources'][-1]['generation']=1;cases.append(('generation',self.changed,stale,[5,6],False))
        invented=copy.deepcopy(self.new);invented['relationship_sources'][-1]['pairs']=1;cases.append(('invented_pairs',self.changed,invented,[5,6],False))
        wrong=copy.deepcopy(self.new);wrong['relationship_sources'][-1]['id']=7;cases.append(('wrong_neighbor',self.changed,wrong,[5,6],False))
        duplicate=copy.deepcopy(self.new);duplicate['relationship_sources'].append(duplicate['relationship_sources'][0]);cases.append(('duplicate',self.changed,duplicate,[5,6],False))
        cases.append(('missing_admission',self.sections,self.old,[5,6],False))
        cases.append(('extra_after_archive',self.changed,self.new,[5,6],True))
        cases.append(('noncanonical_after_archive',self.changed,self.old,[5,6],True))
        cases.append(('world_changed',{**self.changed,b'WRLD':(0,b'x')},self.new,[5,6],False))
        cases.append(('host_changed',{**self.changed,b'HOST':(0,b'different')},self.new,[5,6],False))
        for name,sections,new,ids,deleted in cases:
            with self.subTest(name=name),self.assertRaises(ValueError):
                driver.compare_reopen(self.sections,sections,self.old,new,ids,deleted)
    def test_picker_source_keyboard_steps_and_reconstruction(self):
        self.assertEqual(driver.picker_forward_steps([4,3,2,8],8),[(1,1),(2,2),(3,3)])
        self.assertEqual(driver.picker_forward_steps([4,3,2,8],4),[])
        self.assertEqual(driver.picker_forward_steps(list(range(10)),8),[(i,i%6) for i in range(1,9)])
        # Calling again means a new source picker initialized at0, not an
        # invented carry-over of the old selected family after a rename modal.
        self.assertEqual(driver.picker_forward_steps([4,3,2,8],8),[(1,1),(2,2),(3,3)])
        for order,target in (([],8),([4,4],4),([4,3],8)):
            with self.subTest(order=order),self.assertRaises(ValueError):driver.picker_forward_steps(order,target)

    def test_generic_render_hover_barrier(self):
        state={'value':b'normal'};moves=[];acks=[]
        def move(point):
            moves.append(point)
            state['value']=b'hover' if point==(1,1) else b'normal'
        def wait(label,predicate):
            self.assertTrue(predicate(),label);acks.append(label)
        normal,hover=driver.hover_roundtrip(lambda:state['value'],move,wait,(1,1),(2,2),'screen')
        self.assertEqual((normal,hover),(b'normal',b'hover'))
        self.assertEqual(moves,[(1,1),(2,2),(1,1),(2,2)])
        self.assertEqual(len(acks),4)
        with self.assertRaises(AssertionError):
            driver.hover_roundtrip(lambda:b'stale',lambda point:None,wait,(1,1),(2,2),'stale-title')
    def test_header_is_presented_not_just_title(self):
        frame=bytearray(800*600*3)
        import hashlib
        expected=hashlib.sha256(frame[:800*40*3]).hexdigest()
        self.assertTrue(driver.header_matches(frame,expected))
        frame[0]=1
        self.assertFalse(driver.header_matches(frame,expected))
        with self.assertRaises(ValueError):driver.header_matches(frame[:-1],expected)
    def test_after_create_resume_only_proven_profile(self):
        aux=dict(id=8,house=0,budget=20000,name='ODH UI Management',members=[
            dict(age=27,given_name='Ava Edited',catalog_strings_id=2000,nid=5),
            dict(age=9,given_name='Ben Child',catalog_strings_id=2000,nid=6)])
        files={'config/authority.odhp':'a','data/character.iff':'b','state/opendollhouse/logs/odh-person.log':'old'}
        guards={k:True for k in ('source_profile_unchanged','source_checkpoint_unchanged','source_receipt_unchanged','binary_unchanged','reader_unchanged','commercial_sources_unchanged')}
        prior=dict(passed=False,error='vacant-lot-picker; title=map',guards=guards,
            source_receipt_sha256='receipt',checkpoint_sha256='b740',auxiliary_created=aux,
            window_cleanup={'pid_absent':True},xvfb_cleanup={'pid_absent':True},
            probes=[dict(stage='after-create',process=dict(ok=True,proofs={'exact':True},source_profile_before=files,source_profile_after=files),
                comparison={'changed_sections':['RELA'],'new_empty_neighbor_sources':[5,6]})])
        current={**files,'state/opendollhouse/logs/odh-person.log':'new'}
        driver.validate_after_create_resume(prior,current,'receipt','b740')
        variants=[]
        wrong=copy.deepcopy(prior);wrong['error']='rename-persisted';variants.append(('later-side-effects',wrong,current))
        wrong=copy.deepcopy(prior);wrong['guards'].pop('reader_unchanged');variants.append(('missing-guard',wrong,current))
        wrong=copy.deepcopy(prior);wrong['guards']['binary_unchanged']=False;variants.append(('bad-guard',wrong,current))
        wrong=copy.deepcopy(prior);wrong['probes'][0]['process']['ok']=False;variants.append(('failed-probe',wrong,current))
        wrong=copy.deepcopy(prior);wrong['auxiliary_created']['members'][1]['age']=27;variants.append(('wrong-age',wrong,current))
        wrong=copy.deepcopy(prior);wrong['probes'][0]['comparison']['changed_sections'].append('META');variants.append(('changed-meta',wrong,current))
        variants.append(('changed-overlay',prior,{**current,'config/authority.odhp':'changed'}))
        variants.append(('new-blob',prior,{**current,'data/newblob.bin':'new'}))
        variants.append(('missing-character',prior,{k:v for k,v in current.items() if k!='data/character.iff'}))
        for label,receipt,inventory in variants:
            with self.subTest(case=label),self.assertRaises(ValueError):driver.validate_after_create_resume(receipt,inventory,'receipt','b740')
        with self.assertRaises(ValueError):driver.validate_after_create_resume(prior,current,'another-receipt','b740')
        with self.assertRaises(ValueError):driver.validate_after_create_resume(prior,current,'receipt','another-save')

    def test_picker_selected_row_is_not_hover_or_pressed(self):
        frame=bytearray(800*600*3)
        normal=((117,121,149),)*3
        selected=((0,241,244),(0,240,243),(0,241,244))
        hover=((241,241,244),(240,240,243),(241,241,244))
        def row(index,colors):
            for x,color in zip((44,45,46),colors):
                offset=((106+index*68)*800+x)*3
                frame[offset:offset+3]=bytes(color)
        for i in range(6):row(i,normal)
        self.assertIsNone(driver.picker_selected_row(frame))
        row(0,selected);row(3,hover)
        self.assertEqual(driver.picker_selected_row(frame),0) # Actual failed screenshot: not3.
        row(0,normal);row(3,selected)
        self.assertEqual(driver.picker_selected_row(frame),3)
        row(1,selected)
        self.assertIsNone(driver.picker_selected_row(frame))
        row(1,normal);row(2,((0,0,0),)*3)
        self.assertIsNone(driver.picker_selected_row(frame))
        with self.assertRaises(ValueError):driver.picker_selected_row(frame[:-1])

    def test_pixel_observation_and_exact_personality(self):
        frame=bytearray(800*600*3)
        def meter(row,values):
            for column,color in enumerate(values):
                offset=((143+row*32)*800+230+column*6)*3
                frame[offset:offset+3]=bytes(color)
        def set_counts(counts):
            for row,count in enumerate(counts):meter(row,[(0,255,255)]*count+[(41,41,90)]*(10-count))
        set_counts([0]*5)
        self.assertEqual(driver.personality_led_counts(frame),[0]*5)
        set_counts([1,5,10,0,4])
        self.assertEqual(driver.personality_led_counts(frame),[1,5,10,0,4])
        set_counts([5]*5)
        self.assertEqual(driver.personality_led_counts(frame),[5]*5)
        meter(0,[(41,41,90),(0,255,255)]+[(41,41,90)]*8)
        self.assertIsNone(driver.personality_led_counts(frame))
        meter(0,[(255,0,255)]+[(41,41,90)]*9)
        self.assertIsNone(driver.personality_led_counts(frame))
        with self.assertRaises(ValueError):driver.personality_led_counts(frame[:-1])
        small=bytes(range(36))
        self.assertEqual(driver.rgb_region(small,(1,1,2,1),4,3),small[15:21])
        for box in ((-1,0,1,1),(0,0,0,1),(3,2,2,1)):
            with self.subTest(box=box),self.assertRaises(ValueError):driver.rgb_region(small,box,4,3)
        with self.assertRaises(ValueError):driver.rgb_region(small[:-1],(0,0,1,1),4,3)

    def test_container_crc_and_bounds(self):
        def container(rows, extent_delta=0, count=None):
            payload=b''.join(struct.pack('<4sIQI',name,flags,len(body),zlib.crc32(body))+body for name,flags,body in rows)
            header=struct.pack('<8sHHIQ',b'ODHSES\r\n',1,0,len(rows) if count is None else count,len(payload)+extent_delta)
            return header+struct.pack('<I',zlib.crc32(header))+payload
        valid=container([(b'HOST',2,b'one'),(b'RELA',0,b'two')])
        bad_header=bytearray(valid);bad_header[8]^=1
        bad_body=bytearray(valid);bad_body[-1]^=1
        excess_length=bytearray(valid);struct.pack_into('<Q',excess_length,36,2**63)
        cases={'short_header':valid[:20],'header_crc':bad_header,'body_crc':bad_body,
               'declared_extent':container([(b'HOST',0,b'one')],extent_delta=1),
               'duplicate':container([(b'HOST',0,b'one'),(b'HOST',0,b'two')]),
               'truncated_section':container([(b'HOST',0,b'one')])[:-1],
               'section_extent':excess_length,'impossible_count':container([],count=1),
               'unclaimed_payload':container([(b'HOST',0,b'one')],count=0)}
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'sample.odhs';path.write_bytes(valid)
            self.assertEqual(driver.sections(path),{b'HOST':(2,b'one'),b'RELA':(0,b'two')})
            for label,data in cases.items():
                path.write_bytes(data)
                with self.subTest(case=label),self.assertRaises(ValueError):driver.sections(path)

    def test_output_boundary_before_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            source=root/'source';source.mkdir()
            checkout=root/'checkout';checkout.mkdir()
            valid=root/'isolated-output'
            self.assertEqual(driver.isolated_output(valid,[source,checkout]),valid)
            for output in (source,source/'new-run',root,checkout/'reports'/'new-run'):
                with self.subTest(output=output),self.assertRaises(ValueError):
                    driver.isolated_output(output,[source,checkout])
            alias=root/'alias';alias.symlink_to(source,target_is_directory=True)
            with self.assertRaises(ValueError):driver.isolated_output(alias/'new-run',[source,checkout])
            self.assertFalse(valid.exists())
    def test_family_guard(self):
        before={'families':[dict(id=7,budget=9497,house=1,head='b740',members=[3,4])]}
        after=copy.deepcopy(before);after['families'].append(dict(id=8,budget=20000,house=0))
        driver.stable_family(before,after,7)
        for field,value in (('budget',9496),('house',9),('head','changed'),('members',[3,5])):
            candidate=copy.deepcopy(after);candidate['families'][0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):driver.stable_family(before,candidate,7)

spec=importlib.util.spec_from_file_location('family_flow',Path(__file__).with_name('family_lot_flow.py'))
flow=importlib.util.module_from_spec(spec);spec.loader.exec_module(flow)

class CompleteGuiGates(unittest.TestCase):
    def plan(self):
        ref=dict(png='observed.png',sha256='a'*64,box=[0,0,100,40])
        return dict(schema='family_lot_window/v1',source_proof='reviewed real UI',purchase_guid=123,
            purchase_price=10,sale_refund=10,build_cost=1,
            floor=dict(x=1,y=1,level=1,material=1),operations={name:dict(
                ready=copy.deepcopy(ref),complete=copy.deepcopy(ref),
                events=[dict(kind='key',value='Escape',before=copy.deepcopy(ref))]) for name in flow.OPERATIONS})
    def test_finite_ui_only_plan(self):
        plan=self.plan();self.assertIs(flow.validate_plan(plan),plan)
        variants=[]
        wrong=copy.deepcopy(plan);wrong['operations'].pop('enter-second');variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['operations']['purchase']['events'][0]={'kind':'shell','command':'writer'};variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['operations']['purchase']['events'][0]['state']={'cash':20000};variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['purchase_price']=True;variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['operations']['purchase']['events']=[];variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['operations']['purchase']['events'][0]['value']='Control_L+q';variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['operations']['purchase']['complete']['box']=[0,0,50,40];variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['operations']['purchase']['events'][0]=dict(kind='drag',before=copy.deepcopy(plan['operations']['purchase']['ready']),**{'from':[0,0],'to':[800,0]});variants.append(wrong)
        wrong=copy.deepcopy(plan);wrong['operations']['purchase']['ready']['sha256']='not-reviewed';variants.append(wrong)
        for index,value in enumerate(variants):
            with self.subTest(case=index),self.assertRaises(ValueError):flow.validate_plan(value)
    def test_pin_plan_and_reference_before_any_runtime(self):
        import hashlib,json
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);image=root/'frame.png';image.write_bytes(b'reference bytes, parser never opens GUI')
            plan=self.plan()
            for operation in plan['operations'].values():
                for ref in [operation['ready'],operation['complete']]+[e['before'] for e in operation['events']]:
                    ref.update(png=str(image),sha256=hashlib.sha256(image.read_bytes()).hexdigest())
            path=root/'plan.json';path.write_text(json.dumps(plan));digest=hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(flow.load_plan(path,digest),plan)
            with self.assertRaises(ValueError):flow.load_plan(path,'0'*64)
            image.write_bytes(b'changed')
            with self.assertRaises(ValueError):flow.load_plan(path,digest)
    def test_actual_native_gui_reports_and_cold_identity(self):
        import json
        native=dict(session_loaded=True,resumed_tick=17,integrated_session_context=dict(family_chunk_id=8,house_number=2))
        text=json.dumps(native)+'\n'+json.dumps({'map_cancelled':True})
        self.assertEqual(len(flow.gui_reports(text,8,2,True,17)),1)
        for body,family,house,loaded,tick in [(text,9,2,True,17),(text,8,3,True,17),
            (text,8,2,False,17),(text,8,2,True,18),(json.dumps({'passed':True}),8,2,True,17)]:
            with self.subTest(body=body,family=family,house=house,loaded=loaded,tick=tick),self.assertRaises(ValueError):
                flow.gui_reports(body,family,house,loaded,tick)
    def test_saved_floor_bounds_and_whole_member_identity(self):
        saved=dict(architecture=dict(width=2,height=2,floors=[[1,2,3,4],[5,6,7,8]]),members=[dict(guid=1,neighbor_id=7,age=9,gender=1,body='child')])
        self.assertEqual(flow.floor_value(saved,dict(x=1,y=1,level=2)),8)
        self.assertEqual(flow.identity(saved),{1:(7,9,1,'child')})
        with self.assertRaises(ValueError):flow.floor_value(saved,dict(x=2,y=1,level=2))
        saved['members']*=2
        with self.assertRaises(ValueError):flow.identity(saved)

class ChildPreviewReadiness(unittest.TestCase):
    def test_age_button_hover_or_press_never_acknowledges_child_preview(self):
        self.assertFalse(driver.child_preview_ready(b'adult',b'normal',b'adult',b'pressed',5))
    def test_preview_change_alone_does_not_acknowledge_selected_idle_child(self):
        self.assertFalse(driver.child_preview_ready(b'adult',b'normal',b'child',b'normal',5))
    def test_transient_frame_is_not_ready(self):
        self.assertFalse(driver.child_preview_ready(b'adult',b'normal',b'child',b'selected',1))
    def test_changed_preview_and_idle_button_must_stabilize(self):
        self.assertTrue(driver.child_preview_ready(b'adult',b'normal',b'child',b'selected',2))

class ShutdownCollection(unittest.TestCase):
    def report(self):
        return '\n=================================================================\n==123==ERROR: LeakSanitizer: detected memory leaks\n\nDirect leak of 256 byte(s) in 1 object(s) allocated from:\n    #0 0x123 in malloc allocator.cpp:10\n\nIndirect leak of 64 byte(s) in 2 object(s) allocated from:\n    #0 0x456 in calloc allocator.cpp:20\n\nSUMMARY: AddressSanitizer: 320 byte(s) leaked in 3 allocation(s).\n'
    def test_only_complete_exclusive_lsan_report_is_eligible_but_gate_fails(self):
        value=driver.classify_shutdown_leaks(self.report(),1,123)
        self.assertEqual((value['bytes'],value['allocations']),(320,3));self.assertFalse(value['gate_passed'])
    def test_no_crash_ub_exception_or_incomplete_report_is_collected(self):
        valid=self.report()
        cases=[valid.replace('==123==','==124=='),valid.replace('320 byte(s) leaked','321 byte(s) leaked'),
          valid.replace('3 allocation(s)','4 allocation(s)'),valid[:valid.index('SUMMARY:')],
          'runtime error: undefined behavior\n'+valid,'ERROR: AddressSanitizer: heap-use-after-free\n'+valid,
          'terminate called after throwing an instance of runtime_error\n'+valid,valid+'unknown warning\n',
          valid.replace('    #0 0x123 in malloc allocator.cpp:10\n',''),valid+valid,'x'*(16*1024*1024+1)]
        for body in cases:
            with self.subTest(body=body[:100]):self.assertIsNone(driver.classify_shutdown_leaks(body,1,123))
        for code in (0,2,-11,-15):
            with self.subTest(code=code):self.assertIsNone(driver.classify_shutdown_leaks(valid,code,123))

class FinalShutdownGates(unittest.TestCase):
    def report(self,gate=True,failures=None):
        return dict(passed=True,guards=dict(source=True),shutdown_gate_passed=gate,
                    shutdown_failures=failures or [],semantic_matrix_completed=True,family_lot=dict(accepted=True))
    def test_collected_shutdown_failure_never_accepts_family_or_whole_run(self):
        report=self.report(False,[dict(exit=1)])
        self.assertFalse(driver.finalize_outcome(report));self.assertFalse(report['family_lot']['accepted'])
        self.assertTrue(report['family_lot']['semantic_stages_completed'])
    def test_strict_or_crash_failure_also_fails_without_collection_record(self):
        report=self.report(False)
        self.assertFalse(driver.finalize_outcome(report));self.assertFalse(report['family_lot']['accepted'])
        self.assertEqual(report['shutdown_failures'],[])
        self.assertFalse(report['shutdown_gate_passed'])
    def test_clean_and_protected_state_gates_remain_required(self):
        report=self.report();self.assertTrue(driver.finalize_outcome(report));self.assertTrue(report['family_lot']['accepted'])
        report=self.report();report['guards']['source']=False
        self.assertFalse(driver.finalize_outcome(report));self.assertFalse(report['family_lot']['accepted'])

class UnexpectedWindowTermination(unittest.TestCase):
    def test_cleanup_crash_cannot_keep_shutdown_or_nested_acceptance_true(self):
        report=dict(passed=True,guards=dict(source=True),shutdown_gate_passed=True,
                    shutdown_failures=[],semantic_matrix_completed=False,
                    family_lot=dict(accepted=True),window_cleanup=dict(exit=-11))
        self.assertFalse(driver.finalize_outcome(report));self.assertFalse(report['shutdown_gate_passed'])
        self.assertFalse(report['family_lot']['accepted'])

class ExactSchemaMigration(Gates):
    def migration(self):
        old={**self.sections,b'SIMS':(1,b'\x01\x00legacy-clock')}
        new={**self.changed,b'SIMS':(1,b'\x02\x00native-clock'),b'NPER':(1,b'exact-native-personal')}
        proof=dict(schema='family-reopen-migration-v1',verified=True,added_sections=['NPER'],
                   changed_sections=['RELA','SIMS'],nper_sha256=driver.hashlib.sha256(new[b'NPER'][1]).hexdigest(),
                   before_sha256='before',after_sha256='after',fresh_catalog_reconstruction=True,reference_sha256='')
        return old,new,proof
    def test_exact_cpp_receipt_and_rela_rules_are_both_required(self):
        old,new,proof=self.migration()
        driver.bind_migration_proof(proof,'before','after')
        result=driver.compare_reopen(old,new,self.old,self.new,[5,6],migration=proof)
        self.assertEqual(result['changed_sections'],['RELA','SIMS'])
        with self.assertRaises(ValueError):driver.compare_reopen(old,new,self.old,self.new,[5,6])
        wrong=copy.deepcopy(self.new);wrong['relationship_sources'][-1]['pairs']=1
        with self.assertRaises(ValueError):driver.compare_reopen(old,new,self.old,wrong,[5,6],migration=proof)
    def test_no_section_flags_schema_or_personal_receipt_loophole(self):
        old,new,proof=self.migration()
        variants=[({**new,b'NPER':(0,b'exact-native-personal')},proof),
                  ({**new,b'NPER':(1,b'extra-record')},proof),
                  ({**new,b'WRLD':(1,b'changed')},proof),
                  ({**new,b'SIMS':(0,b'\x02\x00native-clock')},proof),
                  ({**new,b'SIMS':(1,b'\x03\x00native-clock')},proof),
                  (new,{**proof,'verified':False}),(new,{**proof,'changed_sections':['SIMS']}),
                  (new,{**proof,'added_sections':['NPER','OTHER']})]
        for actual,receipt in variants:
            with self.subTest(actual=actual,receipt=receipt),self.assertRaises(ValueError):
                driver.compare_reopen(old,actual,self.old,self.new,[5,6],migration=receipt)
    def test_proof_bound_to_actual_archives_and_fresh_then_cached_chain(self):
        _,_,proof=self.migration()
        for before,after,reference in [('wrong','after',None),('before','wrong',None),('before','after','arbitrary')]:
            with self.subTest(before=before,after=after,reference=reference),self.assertRaises(ValueError):
                driver.bind_migration_proof(proof,before,after,reference)
        cached={**proof,'fresh_catalog_reconstruction':False,'reference_sha256':'first-proven'}
        driver.bind_migration_proof(cached,'before','after','first-proven')
        with self.assertRaises(ValueError):driver.bind_migration_proof(cached,'before','after')
    def test_archive_allows_only_canonical_sims_upgrade_not_relations(self):
        old,new,proof=self.migration();new[b'RELA']=old[b'RELA'];proof['changed_sections']=['SIMS']
        driver.compare_reopen(old,new,self.old,self.old,[5,6],True,proof)
        with self.assertRaises(ValueError):driver.compare_reopen(old,new,self.old,self.new,[5,6],True,proof)

if __name__=='__main__':unittest.main(verbosity=2)
