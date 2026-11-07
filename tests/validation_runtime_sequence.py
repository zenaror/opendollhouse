"""Real subprocess fixtures only; never launch the game or build the product."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[1] / "tools" / "validate_runtime_sequence.py"
FAKE = r'''
import json,os,sys,time
from pathlib import Path
argv=sys.argv[1:]
def value(flag):return argv[argv.index(flag)+1]
source=Path(value('--load-session'));state=json.loads(source.read_text())
game=Path(value('--game-root'));base=state['tick'];count=int(value('--headless-ticks'))
mode=state['mode'];stage=state.get('stage',0)
marker=None
if state.get('require_profile'):
 marker_path=Path(os.environ['XDG_CONFIG_HOME'])/'occupancy.odhp'
 marker=json.loads(marker_path.read_text())
 assert marker=={'fixture':'occupied','stage':stage}, 'whole profile was lost or stale'
 marker_path.write_text(json.dumps(dict(marker,stage=stage+1)))
 if mode=='profile_mutation':
  original=Path(state['original_profile'])/'config'/'occupancy.odhp'
  original.write_text('mutated template')
with (game.parent/'trace.jsonl').open('a') as out:
 out.write(json.dumps({'base':base,'count':count,'argv':argv,'pid':os.getpid(),
 'profile_marker':marker,'display':os.environ.get('DISPLAY'),'wayland':os.environ.get('WAYLAND_DISPLAY'),
 'xdg':[os.environ[k] for k in ('XDG_DATA_HOME','XDG_CONFIG_HOME','XDG_STATE_HOME','XDG_CACHE_HOME')]})+'\n')
if stage==1:
 if mode=='failure':print('relevant simulated application failure',file=sys.stderr);sys.exit(9)
 if mode=='timeout':time.sleep(3)
 if mode=='missing_report':sys.exit(0)
 if mode=='invalid_report':print('{broken');sys.exit(0)
 if mode=='protected_mutation':(game/'protected.dat').write_text('changed')
 if mode=='input_mutation':source.write_text('changed input')
 if mode=='producer_profile_mutation':
  previous=Path(os.environ['XDG_CONFIG_HOME']).parents[2]/'segment-0000'/'profile'/'config'/'occupancy.odhp'
  previous.write_text('mutated previous profile')
end=base+count
document={'host_failed':False,'instruction_budget_exhausted':False,'scheduler_budget_yields':0,'status':'waiting',
 'runtime_policy':'gameplay_per_tick_per_entity','runtime_budget_scope':'per_tick',
 'resumed_tick':base,'ticks':end,'blocking_dialog':None,'blocking_dialog_responses':0,
 'headless_dialog_responses_requested':0,'headless_dialog_responses_consumed':0,
 'object_main_fault_count':0,'registry_first_spatial_invalidation':None,
 'simulation':{'skipped_updates':0,'clock':{'ticks':end}},'diagnostics_truncated':False}
if stage==1:
 if mode=='dialog':document['blocking_dialog']={'message':'human decision'}
 if mode=='fault':document['object_main_fault_count']=1
 if mode=='slice_budget':document['scheduler_budget_yields']=1
 if mode=='wrong_work_policy':document['runtime_budget_scope']='total_probe'
 if mode=='missing_slice_budget':del document['scheduler_budget_yields']
 if mode=='host_fault':document['host_failed']=True
 if mode=='skipped':document['simulation']['skipped_updates']=1
 if mode=='clock':document['simulation']['clock']['ticks']+=1
 if mode=='wrong_base':document['resumed_tick']+=1;document['ticks']+=1;document['simulation']['clock']['ticks']+=1
 if mode=='missing_counter':del document['object_main_fault_count']
 if mode=='bool_counter':document['object_main_fault_count']=False
 if mode=='answered':document['headless_dialog_responses_consumed']=1
 if mode=='spatial':document['registry_first_spatial_invalidation']={'id':1}
 if mode=='truncated':document['diagnostics_truncated']=True
folder=Path(os.environ['XDG_DATA_HOME']);folder.mkdir(parents=True,exist_ok=True)
path=folder/('saved-%s.odhs'%end if state.get('require_profile') else 'saved.odhs')
path.write_text(json.dumps(dict(state,tick=end,stage=stage+1),sort_keys=True))
document['saved_session_path']=str(path)
print(json.dumps(document))
'''


class RuntimeSequenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="odh-sequence-fixture-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.game = self.base / "game"
        self.game.mkdir()
        self.protected = self.game / "protected.dat"
        self.protected.write_text("original fixture")
        self.manifest = self.base / "protected.json"
        self.manifest.write_text(json.dumps({"files": {"protected.dat": self.sha(self.protected)}}))
        self.checkpoint = self.base / "checkpoint.odhs"
        self.fake = self.base / "fake app.py"
        self.fake.write_text(FAKE)
        self.output = self.base / "output"

    @staticmethod
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def run_sequence(self, mode="success", options=(), app_args=(), profile=None):
        state = {"tick": 100, "mode": mode}
        if profile is not None:
            template = self.base / "template"
            (template / "config").mkdir(parents=True)
            state.update(require_profile=True, original_profile=str(template))
        self.checkpoint.write_text(json.dumps(state))
        if profile is not None:
            marker = template / "config" / "occupancy.odhp"
            marker.write_text(json.dumps({"fixture": "occupied", "stage": 0}))
            saved = template / "data" / "initial.odhs"
            saved.parent.mkdir()
            saved.write_bytes(self.checkpoint.read_bytes())
            if profile == "missing":
                marker.unlink()
            if profile == "stale":
                saved.write_text(json.dumps(dict(state, tick=99)))
            if profile == "symlink":
                (template / "outside").symlink_to(self.protected)
            selected_template = template
            if profile == "root_symlink":
                selected_template = self.base / "template-link"
                selected_template.symlink_to(template, target_is_directory=True)
            options = ("--profile-template", str(selected_template)) + tuple(options)
            self.template_before = {str(p.relative_to(template)): self.sha(p) for p in template.rglob("*") if p.is_file()}
        self.initial_hash = self.sha(self.checkpoint)
        command = [sys.executable, str(RUNNER), "--binary", sys.executable,
                   "--launcher-arg", str(self.fake), "--game-root", str(self.game),
                   "--checkpoint", str(self.checkpoint), "--protected-manifest", str(self.manifest),
                   "--output", str(self.output), "--start-tick", "100", "--total-ticks", "8",
                   "--segment-ticks", "3", "--max-segments", "3", "--timeout-seconds", "2",
                   "--total-timeout-seconds", "8"] + list(options)
        if app_args:
            command += ["--app-args"] + list(app_args)
        env = dict(os.environ, DISPLAY="must be removed", WAYLAND_DISPLAY="must be removed")
        run = subprocess.run(command, capture_output=True, text=True, env=env, timeout=12)
        path = self.output / "result.json"
        return run, json.loads(path.read_text()) if path.exists() else None

    def trace(self):
        path = self.base / "trace.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def assert_stopped_after_one_good(self, run, summary):
        self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
        self.assertFalse(summary["sequence_completed"])
        self.assertFalse(summary["final_acceptance"])
        self.assertEqual(summary["completed_ticks"], 3)
        self.assertEqual(len(self.trace()), 2)  # No retry or third launch.
        checkpoint = summary["last_valid_checkpoint"]
        self.assertEqual(checkpoint["tick"], 103)
        self.assertEqual(checkpoint["origin"], "segment-0000")
        self.assertEqual(self.sha(Path(checkpoint["path"])), checkpoint["sha256"])
        self.assertTrue(summary["last_checkpoint_integrity_verified"])
        self.assertEqual(self.sha(self.checkpoint), self.initial_hash)
        for reference in summary["segments"]:
            record = json.loads(Path(reference["result"]).read_text())
            self.assertTrue(record["pid_absent"])
            for name in ("stdout.json", "stderr.log"):
                self.assertTrue(Path(reference["result"]).with_name(name).exists())
        return json.loads(Path(summary["segments"][-1]["result"]).read_text())

    def test_success_chains_actual_saved_inputs_with_bounded_final_segment(self):
        run, summary = self.run_sequence(app_args=("--mode", "1", "--language", "14", "--no-audio"))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertTrue(summary["sequence_completed"])
        self.assertEqual(summary["completed_ticks"], 8)
        self.assertEqual(summary["last_valid_checkpoint"]["tick"], 108)
        self.assertEqual([(x['base'], x['count']) for x in self.trace()], [(100, 3), (103, 3), (106, 2)])
        for item in self.trace():
            self.assertNotIn("--instruction-budget", item['argv'])
            self.assertNotIn("--headless-dialog-ok", item['argv'])
            self.assertNotIn("--headless-dialog-response", item['argv'])
            self.assertIsNone(item['display'])
            self.assertIsNone(item['wayland'])
            self.assertEqual(len(set(item['xdg'])), 4)
        self.assertEqual(len({x['xdg'][0] for x in self.trace()}), 3)
        self.assertFalse(summary["reopening_verified"])
        self.assertFalse(summary["control_equivalence_verified"])
        self.assertEqual(summary["independent_review"], "not_performed")
        self.assertFalse(summary["final_acceptance"])
        self.assertEqual(len(run.stdout.splitlines()), 4)
        self.assertLess(len(run.stdout), 2500)

    def test_whole_profile_chains_occupancy_and_updates_without_reusing_template(self):
        run, summary = self.run_sequence(profile="complete")
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertTrue(summary["sequence_completed"])
        self.assertTrue(summary["profile_template_unchanged"])
        self.assertTrue(summary["last_profile_integrity_verified"])
        self.assertEqual([item['profile_marker']['stage'] for item in self.trace()], [0, 1, 2])
        self.assertEqual(summary['profile_template_after'], self.template_before)
        records = [json.loads(Path(item['result']).read_text()) for item in summary['segments']]
        for index, record in enumerate(records):
            self.assertTrue(record['proofs']['source_profile_unchanged'])
            self.assertEqual(record['source_profile_before'], record['source_profile_after'])
            if index:
                self.assertEqual(record['source_profile'], records[index-1]['xdg_profile'])
                self.assertEqual(record['source_profile_before'], records[index-1]['profile_after'])
        final_marker = Path(summary['last_valid_profile']['path']) / 'config' / 'occupancy.odhp'
        self.assertEqual(json.loads(final_marker.read_text())['stage'], 3)
        self.assertFalse(summary['reopening_verified'])

    def test_missing_stale_or_symlink_profile_is_refused_before_launch(self):
        for scenario in ('missing', 'stale', 'symlink', 'root_symlink'):
            with self.subTest(scenario=scenario):
                # Separate test instances avoid reusing or deleting a user's profile.
                other = RuntimeSequenceTests()
                other.setUp()
                try:
                    run, summary = other.run_sequence(profile=scenario)
                    self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
                    self.assertIsNone(summary)
                    self.assertEqual(other.trace(), [])
                finally:
                    other.doCleanups()

    def test_previous_producer_profile_mutation_is_not_promoted(self):
        run, summary = self.run_sequence(mode='producer_profile_mutation', profile='complete')
        self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
        self.assertFalse(summary['sequence_completed'])
        self.assertTrue(summary['profile_template_unchanged'])
        self.assertFalse(summary['last_profile_integrity_verified'])
        self.assertEqual(len(self.trace()), 2)
        self.assertEqual(summary['completed_ticks'], 3)
        self.assertEqual(summary['last_valid_checkpoint']['origin'], 'segment-0000')
        failed = json.loads(Path(summary['segments'][-1]['result']).read_text())
        self.assertFalse(failed['proofs']['source_profile_unchanged'])

    def test_source_template_mutation_stops_without_promoting_or_retrying(self):
        run, summary = self.run_sequence(mode='profile_mutation', profile='complete')
        self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
        self.assertFalse(summary['sequence_completed'])
        self.assertFalse(summary['profile_template_unchanged'])
        self.assertEqual(len(self.trace()), 1)
        self.assertEqual(summary['completed_ticks'], 0)
        self.assertEqual(summary['last_valid_checkpoint']['origin'], 'supplied_input')
        self.assertTrue(summary['last_checkpoint_integrity_verified'])

    def test_intermediate_error_preserves_checkpoint_and_relevant_stderr(self):
        run, summary = self.run_sequence("failure")
        record = self.assert_stopped_after_one_good(run, summary)
        self.assertEqual(record['exit'], 9)
        self.assertIn("relevant simulated application failure", run.stdout)

    def test_pending_dialog_never_answered_and_blocks_next_segment(self):
        run, summary = self.run_sequence("dialog")
        self.assert_stopped_after_one_good(run, summary)
        self.assertEqual(summary["status"], "human_decision_required")
        for item in self.trace():
            self.assertFalse(any(x.startswith('--headless-dialog') for x in item['argv']))

    def test_timeout_reaps_process_without_retry(self):
        run, summary = self.run_sequence("timeout", options=("--timeout-seconds", "0.25"))
        record = self.assert_stopped_after_one_good(run, summary)
        self.assertIn("process timeout", record['errors'])

    def test_overall_timeout_bounds_active_stage(self):
        run, summary = self.run_sequence("timeout", options=("--total-timeout-seconds", "0.5"))
        self.assert_stopped_after_one_good(run, summary)
        self.assertLess(summary['elapsed_seconds'], 2)

    def test_missing_or_invalid_report_is_not_application_success(self):
        for mode in ("missing_report", "invalid_report"):
            with self.subTest(mode=mode):
                self.output = self.base / mode
                # Each subcase gets an independent trace, not a replay.
                if (self.base / "trace.jsonl").exists():
                    (self.base / "trace.jsonl").unlink()
                run, summary = self.run_sequence(mode)
                record = self.assert_stopped_after_one_good(run, summary)
                self.assertEqual(record['exit'], 0)
                self.assertTrue(record['errors'])

    def test_fail_closed_for_fault_clock_counters_and_protection(self):
        for mode in ("fault", "host_fault", "slice_budget", "missing_slice_budget", "wrong_work_policy", "skipped", "clock", "wrong_base", "missing_counter",
                     "bool_counter", "answered", "spatial", "input_mutation", "protected_mutation"):
            with self.subTest(mode=mode):
                self.output = self.base / mode
                self.protected.write_text("original fixture")
                if (self.base / "trace.jsonl").exists():
                    (self.base / "trace.jsonl").unlink()
                run, summary = self.run_sequence(mode)
                self.assert_stopped_after_one_good(run, summary)

    def test_truncated_diagnostics_retained_without_claiming_action_coverage(self):
        run, summary = self.run_sequence("truncated")
        self.assertEqual(run.returncode, 0, run.stderr)
        record = json.loads(Path(summary["segments"][1]["result"]).read_text())
        self.assertTrue(record["diagnostics_truncated"])
        self.assertEqual(summary["evidence_scope"], "sequential_continuity_only")

    def test_limits_and_automatic_answer_injection_rejected_before_launch(self):
        cases = [(("--max-segments", "2"), ()), (("--segment-ticks", "4097"), ()),
                 (("--total-timeout-seconds", "nan"), ()),
                 ((), ("--headless-dialog-ok",)), ((), ("--headless-dialog-response", "0", "")),
                 ((), ("--headless-resident-interaction-row", "105", "182", "0", "1")),
                 (("--launcher-arg=--headless-dialog-ok",), ())]
        for options, app_args in cases:
            with self.subTest(options=options, app_args=app_args):
                run, summary = self.run_sequence(options=options, app_args=app_args)
                self.assertEqual(run.returncode, 2)
                self.assertIsNone(summary)
                self.assertEqual(self.trace(), [])

    def test_wrong_manifest_rejected_before_launch(self):
        self.manifest.write_text(json.dumps({"files": {"protected.dat": "0" * 64}}))
        run, summary = self.run_sequence()
        self.assertEqual(run.returncode, 2)
        self.assertIsNone(summary)
        self.assertEqual(self.trace(), [])

    def test_output_inside_checkout_and_existing_directory_rejected(self):
        self.output = RUNNER.parent
        run, summary = self.run_sequence()
        self.assertEqual(run.returncode, 2)
        self.assertIsNone(summary)
        self.assertEqual(self.trace(), [])
        self.output = self.base / "existing"
        self.output.mkdir()
        (self.output / "keep.txt").write_text("preserve")
        run, summary = self.run_sequence()
        self.assertEqual(run.returncode, 2)
        self.assertIsNone(summary)
        self.assertEqual((self.output / "keep.txt").read_text(), "preserve")
        self.assertEqual(self.trace(), [])


if __name__ == "__main__":
    unittest.main()
