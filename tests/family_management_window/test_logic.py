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

if __name__=='__main__':unittest.main(verbosity=2)
