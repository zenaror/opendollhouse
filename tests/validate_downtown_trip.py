"""Pure driver regressions. No game, build, commercial data or real profile."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('downtown_driver',ROOT/'tools/validate_downtown_trip.py')
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)

class DriverGuards(unittest.TestCase):
    def test_phonebook_cancel_is_the_exact_source_sentinel_only(self):
        prompt={'type':10,'ticket':4813,'routine':8325,'pc':1,'caller':21,
                'choices':[{'value':32,'label':'Cassandra'},{'value':33,'label':'Laura'}]}
        cancel={'expected_prompt':prompt,'code':0,'text':'-1','selected_value':-1}
        v.validate_acknowledgement(prompt,cancel)
        v.validate_acknowledgement(prompt,{k:value for k,value in cancel.items() if k!='selected_value'})
        # Ordinary offered selections retain their original exact-value guard.
        v.validate_acknowledgement(prompt,{**cancel,'text':'33','selected_value':33})
        for changes in ({'code':1},{'code':2},{'text':'32'},{'selected_value':32}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                v.validate_acknowledgement(prompt,{**cancel,**changes})
        for changes in ({'text':'34','selected_value':34},{'text':'32','selected_value':33}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                v.validate_acknowledgement(prompt,{**cancel,**changes})
        for dialog_type in (1,5,6,9):
            other={**prompt,'type':dialog_type}
            with self.subTest(dialog_type=dialog_type),self.assertRaises(ValueError):
                v.validate_acknowledgement(other,{**cancel,'expected_prompt':other})
            with self.subTest(dialog_type=dialog_type,without_selected=True),self.assertRaises(ValueError):
                v.validate_acknowledgement(other,{'expected_prompt':other,'code':0,'text':'-1'})
        with self.assertRaisesRegex(ValueError,'exact pending'):
            v.validate_acknowledgement({**prompt,'ticket':4814},cancel)
        with self.assertRaises(ValueError):
            v.validate_acknowledgement({**prompt,'choices':None},
                {**cancel,'expected_prompt':{**prompt,'choices':None}})

    def test_explicit_menu_variant_is_lossless_and_never_falls_back(self):
        step={'actor':21,'target':10,'row':7}
        self.assertEqual(v.interaction_arguments(step),['--headless-resident-interaction-row','21','10','7','1'])
        variant={'param0':268,'language':14,'name_hex':'4368616d61722054e178692e2e2e'}
        self.assertEqual(v.interaction_arguments({**step,'variant':variant}),
            ['--headless-resident-interaction-variant-hex','21','10','7','268','14',variant['name_hex'],'1'])
        # Exact byte spelling/case is retained; no UTF-8 re-encoding or label
        # translation is introduced between the observed query and native guard.
        self.assertEqual(v.interaction_arguments({**step,'variant':{**variant,'name_hex':'E1FF'}})[-2],'E1FF')
        for param in (-32768,0,32767):
            self.assertEqual(v.interaction_arguments({**step,'variant':{**variant,'param0':param}})[4],str(param))
        self.assertEqual(len(v.interaction_arguments({**step,'variant':{**variant,'name_hex':'61'*4096}})[-2]),8192)
        malformed=[None,{}, {'param0':268,'language':14,'name':'raw'},
            {**variant,'extra':1}]+[{**variant,**change} for change in (
                {'param0':-32769},{'param0':32768},{'param0':True},{'param0':'268'},
                {'language':0},{'language':21},{'language':True},{'language':'14'},
                {'name_hex':''},{'name_hex':'a'},{'name_hex':'gg'},{'name_hex':'61 62'},
                {'name_hex':'00'},{'name_hex':'61'*4097},{'name_hex':b'61'})]
        for variant in malformed:
            with self.subTest(variant=str(variant)[:80]),self.assertRaises(ValueError):
                v.interaction_arguments({**step,'variant':variant})
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'recipe.json'
            path.write_text(json.dumps({'steps':[{'id':'bad-variant','kind':'interaction','ticks':512,**step,'variant':None}]}))
            with self.assertRaises(ValueError):v.read_plan(path)

    def test_launcher_stream_is_complete_and_strict(self):
        self.assertEqual(v.documents('{"ticks":1}\n {"ticks":2}\n'),[{'ticks':1},{'ticks':2}])
        for text in ('','log prefix\n{}','{} trailing','[]','{"x":NaN}'):
            with self.subTest(text=text),self.assertRaises(ValueError):v.documents(text)

    def test_failed_producer_cannot_be_adopted(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'result.json').write_text('{"exit":2}')
            with self.assertRaisesRegex(ValueError,'zero-exit'):v.adopt_smoke(p,{}, {})

    def test_live_state_does_not_hide_unknown_or_duplicate_requests(self):
        row={'actor':1,'actor_generation':2,'run_generation':3,'uid':4,'state':'active','target':5}
        with self.assertRaisesRegex(ValueError,'duplicate'):v.live_requests([row,row])
        with self.assertRaisesRegex(ValueError,'unknown'):v.live_requests([{**row,'state':'unknown'}])
        self.assertEqual(v.live_requests([{**row,'state':'completed_true'},row]),[row])

    def test_only_process_local_motive_status_is_excluded(self):
        a={'skipped_updates':0,'clock':{'ticks':42},'avatars':[{'id':1,'status':'unchanged','needs':[70],'fractions':[123]}]}
        b=json.loads(json.dumps(a));b['avatars'][0]['status']=''
        self.assertEqual(v.persisted_simulation(a),v.persisted_simulation(b))
        b['avatars'][0]['fractions'][0]=124
        self.assertNotEqual(v.persisted_simulation(a),v.persisted_simulation(b))
        b['avatars'][0]['status']='unsupported_input'
        with self.assertRaises(ValueError):v.persisted_simulation(b)

    def test_external_durable_checkpoint_binds_unique_existing_profile_member(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);game=p/'game';game.mkdir();profile=p/'profile';profile.mkdir();external=p/'durable.odhs';external.write_bytes(b'synthetic-checkpoint')
            member=profile/'actual.odhs';member.write_bytes(external.read_bytes());(profile/'occupancy.odhp').write_bytes(b'synthetic-occupancy')
            manifest=p/'manifest.json';manifest.write_text(json.dumps({'checkpoint':str(external),'profile':str(profile),'sha256':v.sha(external)}))
            recipe=p/'recipe.json';recipe.write_text(json.dumps({'steps':[{'id':'depart','role':'departure','kind':'interaction','actor':42,'target':51,'row':2,'ticks':400}]}))
            output=p/'not-created'
            argv=['driver','--manifest',str(manifest),'--recipe',str(recipe),'--binary',str(p/'never-executed'),'--binary-sha256','a'*64,'--game-root',str(game),'--protected-manifest',str(p/'not-read-until-execution'),'--output',str(output)]
            with patch.object(sys,'argv',argv),contextlib.redirect_stdout(io.StringIO()) as captured:
                self.assertEqual(v.main(),0)
            self.assertEqual(json.loads(captured.getvalue())['checkpoint'],str(member));self.assertFalse(output.exists())
            duplicate=profile/'duplicate.odhs';duplicate.write_bytes(external.read_bytes())
            with patch.object(sys,'argv',argv),self.assertRaisesRegex(ValueError,'unique checkpoint'):v.main()

if __name__=='__main__':unittest.main()
