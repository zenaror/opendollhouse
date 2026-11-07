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


if __name__ == "__main__":
    unittest.main()
