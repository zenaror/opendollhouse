"""Synthetic subprocess tests for the iteration-only runtime smoke runner."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[1] / "tools" / "validate_runtime_smoke.py"
FAKE = r'''
import argparse,json,os,sys,time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--game-root',type=Path);p.add_argument('--load-session',type=Path)
p.add_argument('--headless-ticks',type=int);p.add_argument('--instruction-budget')
p.add_argument('--save-session-on-exit',action='store_true');p.add_argument('--headless-dialog-ok',action='store_true')
a,extra=p.parse_known_args()
state=json.loads(a.load_session.read_text());mode=state.get('mode','success');ticks=a.headless_ticks
trace=a.game_root.parent/'trace.jsonl'
with trace.open('a') as stream:stream.write(json.dumps({'ticks':ticks,'extra':extra,'pid':os.getpid(),'display':os.environ.get('DISPLAY'),'profile':os.environ['XDG_STATE_HOME']})+'\n')
if mode=='timeout':time.sleep(5)
if mode=='nonzero':sys.exit(9)
if mode=='invalid_json':print('invalid');sys.exit(0)
base=state['tick'];end=base+ticks
if mode=='horizon':end+=1
saved=dict(state,tick=end)
if mode=='changed_reopen' and ticks==0:saved['changed']=True
if mode=='changed_continuation' and base>100 and ticks>0:saved['changed']=True
if mode=='cancel_sibling' and ticks==3:time.sleep(2)
if mode=='input_mutation':a.load_session.write_text('modified input')
if mode=='protected_mutation':(a.game_root/'protected.dat').write_text('modified original')
if mode=='checkpoint_mutation':(a.game_root.parent/'checkpoint.odhs').unlink()
folder=Path(os.environ['XDG_STATE_HOME']);folder.mkdir(parents=True,exist_ok=True)
path=folder/'saved.odhs'
if mode!='missing_save':path.write_text(json.dumps(saved,sort_keys=True))
if mode=='multiple_saves':(folder/'second.odhs').write_text('extra')
if state.get('full_profile_zero'):
 assert (Path(os.environ['XDG_CONFIG_HOME'])/'occupancy.odhp').read_text()=='occupied fixture'
 path=a.load_session
 # Remove only the fake's own just-created artifact, never a fixture input.
 if (folder/'saved.odhs').exists():(folder/'saved.odhs').unlink()
 if mode=='wrong_zero_path':path=folder/'older.odhs'
 if mode=='zero_hash_mutation':path.write_text('changed copied checkpoint')

document={'host_failed':mode=='host_failed' or (mode=='cancel_sibling' and ticks==5),'instruction_budget_exhausted':mode=='budget',
 'scheduler_budget_yields':1 if mode=='slice_budget' else 0,
 'runtime_policy':'finite_probe_total_budget' if mode=='wrong_work_policy' else 'gameplay_per_tick_per_entity','runtime_budget_scope':'per_tick',
 'resumed_tick':base,'ticks':end,'saved_session_path':str(path),'branch':{'direct':0 if mode=='wrong_branch' else 1},'extra':extra}
if mode=='missing_slice_budget':del document['scheduler_budget_yields']
if mode=='missing_zero_path':del document['saved_session_path']
print(json.dumps(document))
'''


class RuntimeSmokeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="odh-runtime-smoke-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.game = self.base / "game"
        self.game.mkdir()
        (self.game / "protected.dat").write_text("immutable original")
        self.original = (self.game / "protected.dat").read_bytes()
        self.checkpoint = self.base / "checkpoint.odhs"
        self.fake = self.base / "fake app.py"
        self.fake.write_text(FAKE)
        self.manifest = self.base / "protected.json"
        self.protected = {"protected.dat": hashlib.sha256(self.original).hexdigest()}
        self.manifest.write_text(json.dumps({"protected_before": self.protected}))
        self.output = self.base / "result"

    def run_smoke(self, mode="success", extra=(), options=()):
        self.checkpoint.write_text(json.dumps({"tick":100,"mode":mode}))
        self.input_before = self.checkpoint.read_bytes()
        command = [sys.executable, str(RUNNER), "--binary", sys.executable,
                   "--launcher-arg", str(self.fake), "--game-root", str(self.game),
                   "--checkpoint", str(self.checkpoint), "--capture-ticks", "2", "--total-ticks", "5",
                   "--protected-manifest", str(self.manifest), "--output", str(self.output)]
        command += list(options)
        if extra:
            command += ["--input-args"] + list(extra)
        env = os.environ.copy()
        env["DISPLAY"] = "must be removed"
        env["WAYLAND_DISPLAY"] = "must be removed"
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=15)
        report = json.loads((self.output / "result.json").read_text()) if self.output.exists() else None
        return result, report

    def trace(self):
        path = self.base / "trace.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def test_success_actual_equivalence_and_fresh_profiles(self):
        result, report = self.run_smoke(extra=["--request", "actor137"],
            options=["--require-integer", "/branch/direct", "1"])
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertTrue(report["dev_smoke_pass"])
        self.assertFalse(report["final_acceptance"])
        self.assertEqual(report["purpose"], "iteration_only")
        self.assertEqual(report["blocked_phases"], [])
        self.assertEqual(set(report["comparisons"]), {"zero_tick", "continuation"})
        for pair in report["comparisons"].values():
            self.assertTrue(pair["actual_bytes_equal"])
            self.assertEqual(Path(pair["left"]).read_bytes(), Path(pair["right"]).read_bytes())
        trace = self.trace()
        self.assertEqual([r["ticks"] for r in trace[:2]], [2, 0])
        self.assertEqual(sorted(r["ticks"] for r in trace[2:]), [3, 5])
        self.assertEqual(len({r["profile"] for r in trace}), 4)
        self.assertTrue(all(r["display"] is None for r in trace))
        self.assertEqual({r["ticks"] for r in trace if r["extra"]}, {2, 5})
        self.assertEqual(self.checkpoint.read_bytes(), self.input_before)
        self.assertEqual((self.game / "protected.dat").read_bytes(), self.original)
        for process in report["processes"].values():
            self.assertTrue(all(process["proofs"].values()))
            self.assertNotIn("--instruction-budget", process["command"])
            directory = Path(process["xdg_profile"]).parent
            self.assertTrue((directory / "stdout.json").is_file())
            self.assertTrue((directory / "stderr.log").is_file())

    def test_full_profile_zero_tick_canonical_save_matrix(self):
        import importlib.util
        from types import SimpleNamespace
        spec = importlib.util.spec_from_file_location('full_profile_smoke', RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for mode, ticks, expected_base, succeeds in (
                ('canonical_zero', 0, 100, True),
                ('wrong_zero_path', 0, 100, False),
                ('missing_zero_path', 0, 100, False),
                ('zero_hash_mutation', 0, 100, False),
                ('canonical_zero', 1, 100, False),
                ('canonical_zero', 0, None, False),
                ('canonical_zero', 0, 99, False)):
            with self.subTest(mode=mode, ticks=ticks, expected_base=expected_base):
                root = self.base / (mode + '-' + str(ticks) + '-' + str(expected_base))
                root.mkdir()
                template = root / 'template'
                (template/'config').mkdir(parents=True)
                (template/'state').mkdir()
                (template/'config'/'occupancy.odhp').write_text('occupied fixture')
                source = template/'state'/'canonical.odhs'
                source.write_text(json.dumps({'tick':100,'mode':mode,'full_profile_zero':True},sort_keys=True))
                (template/'state'/'older.odhs').write_text('unrelated older save')
                before = module.profile_files(template)
                output = root / 'output'
                output.mkdir()
                args = SimpleNamespace(binary=Path(sys.executable), launcher_arg=[str(self.fake)],
                    game_root=self.game, checkpoint=source, protected_manifest=self.manifest,
                    output=output, require_integer=[], timeout_seconds=2)
                smoke = module.Smoke(args)
                record = smoke.run('reopen',source,ticks,[],[],expected_base=expected_base,
                    expected_source_hash=module.digest(source),automatic_dialog_ok=False,
                    profile_source=template,expected_profile_hashes=before)
                self.assertEqual(record['ok'],succeeds,str(record))
                self.assertTrue(record['pid_absent'])
                self.assertEqual(module.profile_files(template),before)
                if succeeds:
                    self.assertTrue(record['canonical_zero_tick_input_reused'])
                    self.assertEqual(record['save_sha256'],module.digest(source))
                    self.assertEqual(Path(record['save']),Path(record['xdg_profile'])/'state'/'canonical.odhs')
                    self.assertEqual(record['profile_after'],before)
                else:
                    self.assertTrue(record['errors'])

    def test_multiple_home_transactions_require_native_current_head(self):
        import importlib.util
        from types import SimpleNamespace
        spec = importlib.util.spec_from_file_location('journal_head_smoke', RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        inspector = self.base / 'inspector.py'
        inspector.write_text('''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
p=Path(os.environ['XDG_STATE_HOME'])
mode=(p/'inspection-mode').read_text()
assert sys.argv[1]=='--inspect-session-head' and sys.argv[-2:]==['5','6']
result={'authenticated_journal_head':True,'archive_path':str((p/'final.odhs').resolve()),
        'house':5,'family_chunk_id':6,'tick':101}
if mode=='unauthenticated':result['authenticated_journal_head']=False
if mode=='other_head':result['archive_path']=str((p/'adoption.odhs').resolve())
if mode=='wrong_family':result['family_chunk_id']=7
if mode=='wrong_house':result['house']=8
if mode=='wrong_tick':result['tick']=102
if mode=='mutation':(p/'side-effect').write_text('unexpected')
print(json.dumps(result))
''')
        inspector.chmod(0o700)
        for mode in ('valid', 'unauthenticated', 'other_head', 'wrong_family',
                     'wrong_house', 'wrong_tick', 'mutation', 'no_inspector',
                     'non_home', 'boolean_mode', 'reported_old', 'reported_outside'):
            with self.subTest(mode=mode):
                directory = self.base / ('journal-' + mode)
                profile = directory / 'profile'
                state = profile / 'state'
                state.mkdir(parents=True)
                (state/'inspection-mode').write_text(mode)
                initial, adoption, final = [state/name for name in ('input.odhs', 'adoption.odhs', 'final.odhs')]
                initial.write_text('unchanged copied original')
                adoption.write_text('initial managed transaction')
                final.write_text('completed purchase transaction')
                before = module.profile_files(profile)
                args = SimpleNamespace(binary=Path(sys.executable),
                    session_inspector=None if mode=='no_inspector' else inspector,
                    game_root=self.game, checkpoint=initial,
                    protected_manifest=self.manifest, output=directory,
                    require_integer=[])
                smoke = module.Smoke(args)
                document = {'ticks':101, 'saved_session_path':str(final),
                    'integrated_session_context':{'neighborhood_mode':1, 'house_number':5, 'family_chunk_id':6}}
                if mode=='non_home':document['integrated_session_context']['neighborhood_mode']=2
                if mode=='boolean_mode':document['integrated_session_context']['neighborhood_mode']=True
                if mode=='reported_old':document['saved_session_path']=str(initial)
                if mode=='reported_outside':document['saved_session_path']=str(self.game/'protected.dat')
                env = dict(os.environ, XDG_STATE_HOME=str(state),
                    XDG_CONFIG_HOME=str(profile/'config'), XDG_DATA_HOME=str(profile/'data'),
                    XDG_CACHE_HOME=str(profile/'cache'))
                if mode=='valid':
                    self.assertEqual(smoke.produced_home_head(document,profile,[adoption,final],env),final)
                else:
                    with self.assertRaises(ValueError):
                        smoke.produced_home_head(document,profile,[adoption,final],env)
                if mode!='mutation':
                    self.assertEqual(module.profile_files(profile),before)
                self.assertEqual(initial.read_text(),'unchanged copied original')

    def test_blocked_boundary_preserves_failure_and_authenticates_only_native_safe_output(self):
        """Real isolated fixture subprocesses exercise publication and final guards together."""
        import importlib.util
        from types import SimpleNamespace
        spec = importlib.util.spec_from_file_location('boundary_smoke', RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        app = self.base / 'boundary-app.py'
        app.write_text(r'''
import argparse,json,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--game-root',type=Path);p.add_argument('--load-session',type=Path);p.add_argument('--headless-ticks',type=int)
a,_=p.parse_known_args();state=json.loads(a.load_session.read_text());mode=state['mode']
folder=Path(os.environ['XDG_STATE_HOME']);folder.mkdir(parents=True,exist_ok=True)
end=100+a.headless_ticks if mode=='complete' else 101
saved=folder/'final.odhs';saved.write_text(json.dumps({'tick':end,'mode':mode}))
if mode.startswith('multi'):(folder/'adoption.odhs').write_text(json.dumps({'tick':100,'mode':mode}))
(folder/'head').write_text('final.odhs')
if mode=='other_head':(folder/'head').write_text('input.odhs')
if mode=='multi_other':(folder/'head').write_text('adoption.odhs')
if mode=='protected_mutation':(a.game_root/'protected.dat').write_text('changed original')
if mode=='input_mutation':a.load_session.write_text('changed copied input')
if mode=='source_mutation':Path(state['original_source']).write_text('changed source')
if mode=='source_profile_mutation':Path(state['original_source']).with_name('new-file').write_text('changed source profile')
if mode=='nonzero':raise SystemExit(7)
d={'host_failed':False,'instruction_budget_exhausted':False,'scheduler_budget_yields':0,
   'runtime_policy':'gameplay_per_tick_per_entity','runtime_budget_scope':'per_tick',
   'resumed_tick':100,'ticks':end,'saved_session_path':str(saved),
   'blocking_dialog':None if mode=='complete' else {'ticket':32,'message':'observed fixture'},
   'integrated_session_context':{'neighborhood_mode':1,'house_number':5,'family_chunk_id':6}}
if mode=='missing_counter':del d['scheduler_budget_yields']
if mode=='outside':d['saved_session_path']=str(a.game_root/'protected.dat')
print(json.dumps(d))
''')
        inspector = self.base / 'boundary-inspector.py'
        inspector.write_text(r'''#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
folder=Path(os.environ['XDG_STATE_HOME']);saved=folder/(folder/'head').read_text()
state=json.loads(saved.read_text());mode=state['mode']
if mode=='decode_failure':raise SystemExit(3)
if mode=='invalid_authority':print('invalid');raise SystemExit(0)
if mode=='inspector_mutation':Path(__file__).write_text('mutated inspector')
result={'authenticated_journal_head':True,'archive_path':str(saved.resolve()),
        'house':5,'family_chunk_id':6,'tick':state['tick']}
if mode=='wrong_tick':result['tick']+=1
if mode=='wrong_identity':result['family_chunk_id']=7
if mode=='profile_mutation':(folder/'inspection-side-effect').write_text('changed profile')
print(json.dumps(result))
''')
        inspector_source = inspector.read_text()
        for mode in ('blocked','validator_failure','complete','multi_valid','multi_other',
                     'other_head','outside','wrong_tick','wrong_identity','decode_failure',
                     'invalid_authority','protected_mutation','input_mutation','source_mutation',
                     'source_profile_mutation','profile_mutation','inspector_mutation',
                     'missing_counter','no_inspector','nonzero'):
            with self.subTest(mode=mode):
                (self.game/'protected.dat').write_bytes(self.original)
                inspector.write_text(inspector_source);inspector.chmod(0o700)
                root=self.base/('boundary-'+mode);template=root/'template'
                (template/'state').mkdir(parents=True);(template/'config').mkdir()
                (template/'config'/'occupancy.odhp').write_text('occupied fixture')
                source=template/'state'/'input.odhs'
                source.write_text(json.dumps({'tick':100,'mode':mode,'original_source':str(source)}))
                before=module.profile_files(template);output=root/'out';output.mkdir()
                args=SimpleNamespace(binary=Path(sys.executable),launcher_arg=[str(app)],
                    session_inspector=None if mode=='no_inspector' else inspector,
                    game_root=self.game,checkpoint=source,protected_manifest=self.manifest,
                    output=output,require_integer=[],timeout_seconds=3)
                smoke=module.Smoke(args)
                def validator(document,record):
                    if mode=='validator_failure':raise ValueError('observed dialog requires explicit decision')
                record=smoke.run('continue',source,10,[],[],expected_base=100,
                    expected_source_hash=module.digest(source),automatic_dialog_ok=False,
                    report_validator=validator,profile_source=template,expected_profile_hashes=before)
                authenticated=mode in ('blocked','validator_failure','complete','multi_valid')
                self.assertEqual(record['checkpoint'] is not None,authenticated,str(record))
                self.assertEqual(record['ok'],mode=='complete',str(record))
                self.assertTrue(record['pid_absent'])
                if mode=='validator_failure':self.assertEqual(record['errors'],['observed dialog requires explicit decision'])
                if mode in ('blocked','multi_valid'):self.assertEqual(record['errors'],['application horizon mismatch'])
                if authenticated:
                    cp=record['checkpoint'];self.assertEqual(cp['tick'],110 if mode=='complete' else 101)
                    self.assertEqual(cp['sha256'],module.digest(Path(cp['path'])))
                    self.assertEqual(cp['profile_after'],module.profile_files(Path(record['xdg_profile'])))
                    self.assertTrue(cp['native_authority']['authenticated_journal_head'])
                    self.assertTrue(all(record['proofs'].values()))
                else:self.assertIsNone(record['checkpoint'])
                if mode=='nonzero':self.assertEqual(record['exit'],7)

    def test_host_failure_exit_zero_blocks_every_later_phase(self):
        result, report = self.run_smoke("host_failed")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(report["processes"]["capture"]["exit"], 0)
        self.assertEqual(len(self.trace()), 1)
        self.assertEqual(report["blocked_phases"], ["reopen", "control", "continue"])

    def test_wrong_branch_blocks_reference(self):
        result, report = self.run_smoke("wrong_branch", options=["--require-integer", "/branch/direct", "1"])
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(self.trace()), 1)
        self.assertIn("required integer branch", str(report))

    def test_actual_modified_reopen_stops_before_reference(self):
        result, report = self.run_smoke("changed_reopen")
        self.assertEqual(result.returncode, 1)
        self.assertEqual([r["ticks"] for r in self.trace()], [2, 0])
        self.assertFalse(report["comparisons"]["zero_tick"]["actual_bytes_equal"])
        self.assertEqual(report["blocked_phases"], ["control", "continue"])

    def test_actual_final_continuation_mismatch(self):
        result, report = self.run_smoke("changed_continuation")
        self.assertEqual(result.returncode, 1)
        self.assertTrue(report["comparisons"]["zero_tick"]["actual_bytes_equal"])
        self.assertFalse(report["comparisons"]["continuation"]["actual_bytes_equal"])
        self.assertEqual(len(self.trace()), 4)

    def test_control_failure_cancels_live_sibling(self):
        result, report = self.run_smoke("cancel_sibling")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["processes"]["control"]["ok"])
        self.assertTrue(report["processes"]["continue"]["pid_absent"])
        self.assertNotIn("continuation", report["comparisons"])
        self.assertIn("final_comparison", report["blocked_phases"])
        self.assertLess(report["elapsed_seconds"], 1.5)

    def test_copied_input_mutation_detected_and_checkpoint_preserved(self):
        result, report = self.run_smoke("input_mutation")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["processes"]["capture"]["proofs"]["input_unchanged"])
        self.assertEqual(self.checkpoint.read_bytes(), self.input_before)
        self.assertEqual(len(self.trace()), 1)

    def test_protected_mutation_detected(self):
        result, report = self.run_smoke("protected_mutation")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["processes"]["capture"]["proofs"]["protected_unchanged"])
        self.assertEqual(len(self.trace()), 1)

    def test_deleted_checkpoint_failure_keeps_diagnostic(self):
        result, report = self.run_smoke("checkpoint_mutation")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["processes"]["capture"]["proofs"]["original_checkpoint_unchanged"])
        self.assertTrue(report["processes"]["capture"]["errors"])

    def test_nonzero_budget_json_horizon_and_save_cardinality_fail_first(self):
        for mode in ("nonzero", "budget", "slice_budget", "missing_slice_budget", "wrong_work_policy", "invalid_json", "horizon", "missing_save", "multiple_saves"):
            with self.subTest(mode=mode):
                self.output = self.base / mode
                before = len(self.trace())
                result, report = self.run_smoke(mode)
                self.assertEqual(result.returncode, 1)
                self.assertFalse(report["dev_smoke_pass"])
                self.assertEqual(len(self.trace()), before + 1)
                self.assertEqual(set(report["processes"]), {"capture"})

    def test_timeout_reaps_child(self):
        result, report = self.run_smoke("timeout", options=["--timeout-seconds", "0.15"])
        self.assertEqual(result.returncode, 1)
        self.assertIn("process timeout", str(report))
        self.assertTrue(report["processes"]["capture"]["pid_absent"])
        self.assertEqual(len(self.trace()), 1)

    def test_no_shell_interpolation_and_single_job(self):
        sentinel = self.base / "SHELL_WAS_EXECUTED"
        payload = "$(touch " + str(sentinel) + ");echo unsafe"
        result, report = self.run_smoke(extra=[payload], options=["--jobs", "1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(sentinel.exists())
        self.assertEqual(self.trace()[0]["extra"], [payload])
        self.assertTrue(report["dev_smoke_pass"])

    def test_managed_override_rejected_without_process(self):
        result, report = self.run_smoke(extra=["--headless-ticks=99"])
        self.assertEqual(result.returncode, 2)
        self.assertIsNone(report)
        self.assertEqual(self.trace(), [])

    def test_output_inside_checkout_rejected_without_writes(self):
        self.output = RUNNER.parents[1] / ("rejected-smoke-" + self.base.name)
        self.assertFalse(self.output.exists())
        result, report = self.run_smoke()
        self.assertEqual(result.returncode, 2)
        self.assertIn("output must be outside the source checkout", result.stderr)
        self.assertIsNone(report)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.trace(), [])

    def test_manifest_forms_and_no_overwrite(self):
        self.manifest.write_text(json.dumps({"files": self.protected}))
        result, _ = self.run_smoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        count = len(self.trace())
        result, _ = self.run_smoke()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(len(self.trace()), count)



# Shared profile-authority tests use only synthetic decoder and application fixtures.
import importlib.util
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_runtime_smoke as v
import validate_downtown_trip as downtown

INSPECTOR='''#!PYTHON
import json,sys
from pathlib import Path
p=Path(sys.argv[2]);d=json.loads(p.read_text());mode=d.get('mode','ok')
if mode=='corrupt':sys.exit(2)
if mode=='mutate-checkpoint':p.write_text('changed')
if mode=='mutate-profile':(p.parent/'new-file').write_text('changed')
if mode=='mutate-inspector':Path(__file__).write_text('changed')
r={'archive_path':str(p.resolve()),'occupancy_epoch':d.get('lease')}
if mode=='wrong-path':r['archive_path']=str(p.parent/'other.odhs')
if mode=='missing-key':del r['occupancy_epoch']
if mode=='invalid-json':print('bad');sys.exit(0)
print(json.dumps(r))
'''
APP='''#!PYTHON
import json,sys
from pathlib import Path
p=Path(sys.argv[sys.argv.index('--load-session')+1]);d=json.loads(p.read_text());tick=d['tick']
print(json.dumps({'host_failed':False,'instruction_budget_exhausted':False,'scheduler_budget_yields':0,
'runtime_policy':'gameplay_per_tick_per_entity','runtime_budget_scope':'per_tick','resumed_tick':tick,
'ticks':tick,'saved_session_path':str(p.resolve())}))
'''

class AuthorityTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.profile=self.root/'profile';self.profile.mkdir()
        self.save=self.profile/'actual.odhs';self.save.write_text(json.dumps({'tick':0}))
        self.inspector=self.root/'inspector';self.executable(self.inspector,INSPECTOR)
    def executable(self,path,body):
        path.write_text(body.replace('PYTHON',sys.executable));path.chmod(0o700)
    def check(self,inspector=True):
        return v.profile_checkpoint(self.profile,v.profile_files(self.profile),v.digest(self.save),self.inspector if inspector else None)
    def test_unmanaged_home_and_trip_allow_explicit_decoder(self):
        for trip in [False,True]:
            with self.subTest(trip=trip):
                self.save.write_text(json.dumps({'tick':0,'trip':trip}))
                self.assertEqual(self.check(),self.save)
    def test_default_missing_overlay_remains_rejected(self):
        with self.assertRaisesRegex(ValueError,'occupancy'):self.check(False)
    def test_managed_missing_overlay_and_incomplete_proof_rejected(self):
        for change in [{'lease':1},{'lease':0},{'mode':'missing-key'},{'mode':'wrong-path'},
                       {'mode':'corrupt'},{'mode':'invalid-json'}]:
            with self.subTest(change=change):
                self.save.write_text(json.dumps({'tick':0,**change}))
                with self.assertRaises(ValueError):self.check()
    def test_duplicate_checkpoint_rejected(self):
        (self.profile/'duplicate.odhs').write_bytes(self.save.read_bytes())
        with self.assertRaisesRegex(ValueError,'unique checkpoint'):self.check()
    def test_inventory_mismatch_before_inspection_rejected(self):
        inventory=v.profile_files(self.profile);self.save.write_text('changed')
        with self.assertRaises(ValueError):v.profile_checkpoint(self.profile,inventory,inventory['actual.odhs'],self.inspector)
    def test_inspector_checkpoint_profile_mutation_rejected(self):
        for mode in ['mutate-checkpoint','mutate-profile','mutate-inspector']:
            with self.subTest(mode=mode):
                self.executable(self.inspector,INSPECTOR)
                self.save.write_text(json.dumps({'tick':0,'mode':mode}))
                with self.assertRaisesRegex(ValueError,'changed'):self.check()
                if (self.profile/'new-file').exists():(self.profile/'new-file').unlink()
    def test_existing_managed_overlay_retains_default_path(self):
        self.save.write_text(json.dumps({'lease':1}));(self.profile/'occupancy.odhp').write_text('synthetic overlay')
        self.assertEqual(self.check(False),self.save)
    def test_smoke_validates_input_and_output_with_shared_decoder(self):
        game=self.root/'game';game.mkdir();(game/'protected.dat').write_text('immutable')
        protected={'protected.dat':v.digest(game/'protected.dat')};manifest=self.root/'protected.json';manifest.write_text(json.dumps(protected))
        app=self.root/'app';self.executable(app,APP);out=self.root/'output';out.mkdir()
        args=SimpleNamespace(binary=app,session_inspector=self.inspector,launcher_arg=[],game_root=game,
            checkpoint=self.save,protected_manifest=manifest,output=out,require_integer=[],timeout_seconds=3)
        smoke=v.Smoke(args);r=smoke.run('reopen',self.save,0,[],[],0,expected_source_hash=v.digest(self.save),
            automatic_dialog_ok=False,profile_source=self.profile,expected_profile_hashes=v.profile_files(self.profile))
        self.assertTrue(r['ok'],str(r));self.assertEqual(r['save_sha256'],v.digest(self.save));self.assertTrue(all(r['proofs'].values()))
        self.assertEqual(r['session_inspector_sha256'],v.digest(self.inspector))
    def test_downtown_preflight_uses_shared_explicit_decoder(self):
        from unittest.mock import patch
        import contextlib,io
        manifest=self.root/'manifest.json';manifest.write_text(json.dumps({'checkpoint':str(self.save),'profile':str(self.profile),'sha256':v.digest(self.save)}))
        recipe=self.root/'recipe.json';recipe.write_text(json.dumps({'steps':[{'id':'continue','kind':'continue','ticks':1}]}))
        game=self.root/'game';game.mkdir()
        argv=['driver','--manifest',str(manifest),'--recipe',str(recipe),'--binary',str(self.root/'not-executed'),
              '--binary-sha256','a'*64,'--game-root',str(game),'--protected-manifest',str(self.root/'not-read'),
              '--output',str(self.root/'output'),'--session-inspector',str(self.inspector)]
        with patch.object(sys,'argv',argv),contextlib.redirect_stdout(io.StringIO()):self.assertEqual(downtown.main(),0)
        self.save.write_text(json.dumps({'lease':1}))
        manifest.write_text(json.dumps({'checkpoint':str(self.save),'profile':str(self.profile),'sha256':v.digest(self.save)}))
        with patch.object(sys,'argv',argv),self.assertRaisesRegex(ValueError,'unmanaged'):downtown.main()


if __name__ == "__main__":
    unittest.main()
