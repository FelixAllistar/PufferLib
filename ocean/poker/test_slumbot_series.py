"""Offline integration checks: the child runner below never uses the network."""
import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

from slumbot_series import analyze, check_job, fingerprint, freeze, load_manifest, run, write_new


FAKE_RUNNER = '''import argparse, hashlib, json
from pathlib import Path
p = argparse.ArgumentParser()
for key in ("library", "style", "equity-samples", "hands", "games", "seed", "output", "contact"):
    p.add_argument("--" + key)
p.add_argument("--independent-hand-seeds", action="store_true")
a = p.parse_args()
n, seed = int(a.hands), int(a.seed)
candidate = Path(a.library).read_text() == "candidate"
dirty = seed == DIRTY_SEED
events = [{"metadata": {
    "style": int(a.style), "equity_samples": int(a.equity_samples),
    "seed": seed, "hands_requested": n, "concurrent_games": int(a.games),
    "library_sha256": hashlib.sha256(Path(a.library).read_bytes()).hexdigest(),
    "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "opponent": "slumbot.com", "starting_bb": 200, "log_schema": 3,
    "baseline_winnings_semantics": "already_adjusted_hero_minus_slumbot_selfplay"}}]
if a.independent_hand_seeds:
    pairs, odd = divmod(n, 2)
    games = int(a.games)
    quotas = [2 * (pairs // games + (game < pairs % games))
              + (odd if game == 0 else 0) for game in range(games)]
    planned = [(game, attempt) for game, quota in enumerate(quotas) for attempt in range(quota)]
    events[0]["metadata"].update(
        log_schema=4, policy_rng_mode="independent_hand_seeds_v1",
        hand_seed_derivation="sha256_poker_policy_hand_seed_v1",
        hand_reservation_scheme="game_blocks_with_overflow_v1",
        hand_reservation_quotas=quotas)
values = []
for i in range(n):
    value = (-2, -1, 1, 2)[i % 4] + candidate
    values.append(value)
    events.append({"hand": i, "position": i % 2, "game": i % int(a.games),
                   "winnings_chips": value * 100,
                   "baseline_winnings_chips": 7 + i,
                   "action": "b200f", "decisions": [],
                   "native_terminal_verified": i % 3 != 0})
    if a.independent_hand_seeds:
        game, attempt = planned[i]
        policy_seed = int.from_bytes(hashlib.sha256(
            f"poker-policy-hand-seed-v1:{seed}:{i}".encode("ascii")).digest()[:8], "big") or 1
        reservation = {"game": game, "game_hand_attempt": attempt,
                       "hand_reservation_index": i, "policy_seed_at_hand_start": policy_seed}
        events[-1].update(reservation, policy_rng=policy_seed)
        events.insert(len(events) - 1, {"hand_reserved": reservation})
if dirty:
    events.append({"interrupted": {"game": 0, "stage": "act", "type": "Timeout", "decisions": []}})
events.append({"summary": {"hands": n, "net_bb": sum(values),
                           "unresolved_hands": int(dirty), "active_games": 0,
                           "target_reached": True, "complete": not dirty}})
Path(a.output).write_text("\\n".join(json.dumps(e) for e in events) + "\\n")
raise SystemExit(9 if seed == BAD_EXIT_SEED else 0)
'''


class SeriesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.control = self.directory / "control.so"
        self.candidate = self.directory / "candidate.so"
        self.control.write_text("control")
        self.candidate.write_text("candidate")
        self.runner = self.directory / "fake_eval.py"
        self.manifest = self.directory / "manifest.json"

    def tearDown(self):
        self.temp.cleanup()

    def prepare(self, dirty_seed=-1, bad_exit_seed=-1, divergence=False):
        self.runner.write_text(FAKE_RUNNER.replace("DIRTY_SEED", str(dirty_seed))
                               .replace("BAD_EXIT_SEED", str(bad_exit_seed)))
        args = argparse.Namespace(control=self.control, candidate=self.candidate,
                                  runner=self.runner, python=Path(sys.executable),
                                  output_dir=self.directory / "jobs", hands_per_job=4,
                                  games=5, start_seed=202610185000,
                                  control_samples=0, candidate_samples=0, contact="offline-test")
        if divergence:
            args.first_divergence_analyzer = self.directory / "first_divergence.py"
            args.first_divergence_analyzer.write_text("# offline fixture; never executed\n")
        manifest = freeze(args)
        write_new(self.manifest, manifest)
        return manifest

    def launch(self):
        with redirect_stdout(io.StringIO()):
            return run(self.manifest)

    def test_complete_serialized_series_and_metrics(self):
        manifest = self.prepare()
        self.assertEqual(self.launch(), 0)
        result = analyze(self.manifest)
        self.assertTrue(result["all_planned_jobs_complete_and_eligible"])
        self.assertEqual(result["execution"]["jobs_started"], [j["id"] for j in manifest["jobs"]])
        self.assertEqual(result["arms"]["candidate"]["raw"]["hands"], 16)
        self.assertEqual(result["primary_raw_difference"]["gain_bb100"], 100)
        self.assertEqual(result["secondary_adjusted_difference"]["gain_bb100"], 0)
        self.assertEqual(result["arms"]["control"]["adjusted"]["bb100"], 8.5)
        self.assertEqual(result["arms"]["candidate"]["position_counts"], {"0": 8, "1": 8})
        self.assertEqual(result["jobs"][0]["terminal_verification"], {"not_verifiable": 2, "verified": 2})
        self.assertEqual(result["jobs"][0]["baseline_coverage_hands"], 4)
        with self.assertRaises(ValueError):
            self.launch()

    def test_dirty_job_stops_later_jobs_preserves_outputs(self):
        manifest = self.prepare(dirty_seed=202610185002)
        self.assertEqual(self.launch(), 2)
        result = analyze(self.manifest)
        self.assertFalse(result["all_planned_jobs_complete_and_eligible"])
        self.assertEqual(result["conclusion"], "incomplete_not_confirmatory")
        self.assertEqual(result["execution"]["jobs_finished"], [j["id"] for j in manifest["jobs"][:3]])
        self.assertTrue(Path(manifest["jobs"][2]["output"]).exists())
        self.assertFalse(Path(manifest["jobs"][3]["output"]).exists())
        self.assertEqual(result["arms"]["candidate"]["raw"]["hands"], 4)

    def test_last_job_failure_cannot_be_claimed_complete(self):
        self.prepare(bad_exit_seed=202610185007)
        self.assertEqual(self.launch(), 2)
        result = analyze(self.manifest)
        self.assertFalse(result["all_planned_jobs_complete_and_eligible"])
        self.assertEqual(result["arms"]["candidate"]["raw"]["hands"], 16)
        self.assertIn("execution reported failure: 07_candidate", result["execution"]["errors"])

    def test_changed_binary_refused_before_first_child(self):
        manifest = self.prepare()
        self.control.write_text("changed")
        with self.assertRaisesRegex(ValueError, "library changed"):
            self.launch()
        self.assertFalse(Path(manifest["jobs"][0]["output"]).exists())

    def test_venv_executable_symlink_is_preserved(self):
        self.runner.write_text(FAKE_RUNNER)
        executable = self.directory / "venv" / "bin" / "python"
        executable.parent.mkdir(parents=True)
        executable.symlink_to(Path(sys.executable).resolve())
        args = argparse.Namespace(control=self.control, candidate=self.candidate,
                                  runner=self.runner, python=executable,
                                  output_dir=self.directory / "jobs", hands_per_job=4,
                                  games=5, start_seed=202610185000,
                                  control_samples=0, candidate_samples=0, contact="offline-test")
        self.assertEqual(freeze(args)["python"], str(executable))

    def test_log_mutation_and_missing_job_remove_confirmation(self):
        manifest = self.prepare()
        self.assertEqual(self.launch(), 0)
        output = Path(manifest["jobs"][0]["output"])
        records = [json.loads(line) for line in output.read_text().splitlines()]
        records[0]["metadata"]["equity_samples"] = 32
        output.write_text("\n".join(json.dumps(e) for e in records) + "\n")
        Path(manifest["jobs"][-1]["output"]).unlink()
        result = analyze(self.manifest)
        self.assertFalse(result["all_planned_jobs_complete_and_eligible"])
        self.assertIn("metadata mismatch: equity_samples", result["jobs"][0]["errors"])
        self.assertIn("missing output", result["jobs"][-1]["errors"])
        self.assertIn("log differs from completed execution: 00_control", result["execution"]["errors"])

    def test_paired_seed_mode_routes_flag_but_requires_separate_primary_analysis(self):
        manifest = self.prepare(divergence=True)
        self.assertEqual([j["seed"] for j in manifest["jobs"]],
                         [202610185000 + i // 2 for i in range(8)])
        self.assertEqual(self.launch(), 0)
        result = analyze(self.manifest)
        self.assertTrue(result["all_planned_jobs_complete_and_eligible"])
        self.assertEqual(result["conclusion"], "first_divergence_analysis_required")
        self.assertEqual(result["declared_primary_metric"], "first_divergence_gain_bb100")
        self.assertEqual(result["raw_difference_role"], "secondary")
        events = [json.loads(line) for line in self.manifest.with_suffix(".execution.jsonl").read_text().splitlines()]
        self.assertTrue(all("--independent-hand-seeds" in event["command"]
                            for event in events if "job_started" in event))

    def test_new_mode_rejects_unpaired_seed_or_quotas(self):
        manifest = self.prepare(divergence=True)
        for field, value in (("seed", 999), ("quota", 6), ("games", 4)):
            changed = json.loads(json.dumps(manifest))
            changed["jobs"][1][field] = value
            self.manifest.write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, "pair|Pair"):
                load_manifest(self.manifest)

    def test_changed_analyzer_refused_before_any_live_child(self):
        manifest = self.prepare(divergence=True)
        Path(manifest["first_divergence"]["analyzer"]).write_text("# changed baseline\n")
        with self.assertRaisesRegex(ValueError, "analyzer source changed"):
            self.launch()
        self.assertFalse(Path(manifest["jobs"][0]["output"]).exists())

    def test_paired_seeds_are_not_silently_allowed_in_old_raw_protocol(self):
        manifest = self.prepare()
        manifest["jobs"][1]["seed"] = manifest["jobs"][0]["seed"]
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "distinct seed"):
            load_manifest(self.manifest)

    def test_seed_integrity_failure_is_detected_by_per_job_gate(self):
        manifest = self.prepare(divergence=True)
        self.assertEqual(self.launch(), 0)
        job = manifest["jobs"][0]
        output = Path(job["output"])
        records = [json.loads(line) for line in output.read_text().splitlines()]
        first = next(row for row in records if "winnings_chips" in row)
        first["policy_rng"] += 1
        output.write_text("\n".join(json.dumps(row) for row in records) + "\n")
        _, errors = check_job(job, manifest)
        self.assertIn("hand seed integrity: terminal RNG differs from final decision", errors)


if __name__ == "__main__":
    unittest.main()
