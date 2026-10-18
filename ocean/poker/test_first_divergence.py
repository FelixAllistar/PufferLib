"""Offline estimator, seed/provenance, and native duplicate-deal checks.

Pure Python tests need no network or PufferLib installation. Native fixtures use
POKER_FIRST_DIVERGENCE_SOURCE (directory containing eval.py) and
POKER_FIRST_DIVERGENCE_LIBRARY (any compatible two-player native library).
"""
import argparse
from contextlib import redirect_stdout
import copy
import io
import json
import os
from pathlib import Path
import tempfile
import unittest

import first_divergence as fd


def seeded_fixture():
    job = {"id": "control", "quota": 4, "games": 2, "seed": 8128}
    metadata = {"log_schema": 4, "policy_rng_mode": fd.RNG_MODE,
                "hand_seed_derivation": fd.SEED_DERIVATION,
                "hand_reservation_scheme": fd.RESERVATION_SCHEME,
                "seed": job["seed"], "hand_reservation_quotas": [2, 2]}
    events, hands = [], []
    for index in range(4):
        seed = fd.hand_seed(job["seed"], index)
        data = {"game": index // 2, "game_hand_attempt": index % 2,
                "hand_reservation_index": index, "policy_seed_at_hand_start": seed}
        events.append({"hand_reserved": data.copy()})
        hands.append({**data, "position": index % 2, "winnings_chips": 0,
                      "decisions": [], "policy_rng": seed})
    return job, [{"metadata": metadata}, *events, *reversed(hands), {"summary": {}}]


class SeedAndProvenanceTests(unittest.TestCase):
    def test_completion_order_does_not_change_seed_coverage(self):
        job, records = seeded_fixture()
        signature = fd.seeded_log_signature(job, records)
        self.assertEqual(set(signature), set(range(4)))
        records[5:9] = reversed(records[5:9])
        self.assertEqual(signature, fd.seeded_log_signature(job, records))

    def test_missing_duplicate_or_late_reservation_fails(self):
        job, records = seeded_fixture()
        variants = []
        missing = copy.deepcopy(records); missing.pop(2); variants.append(missing)
        duplicate = copy.deepcopy(records); duplicate.insert(2, duplicate[1]); variants.append(duplicate)
        late = copy.deepcopy(records); late.append(late.pop(1)); variants.append(late)
        for value in variants:
            with self.subTest(value=value), self.assertRaises(fd.AnalysisError):
                fd.seeded_log_signature(job, value)

    def test_wrong_seed_index_quota_or_rng_fails(self):
        job, records = seeded_fixture()
        cases = [(1, "hand_reserved", "policy_seed_at_hand_start", 1),
                 (1, "hand_reserved", "hand_reservation_index", 3),
                 (1, "hand_reserved", "game_hand_attempt", 2),
                 (5, None, "policy_rng", 1),
                 (0, "metadata", "hand_reservation_quotas", [1, 3]),
                 (0, "metadata", "policy_rng_mode", "carried")]
        for index, nested, key, wrong in cases:
            bad = copy.deepcopy(records)
            (bad[index][nested] if nested else bad[index])[key] = wrong
            with self.subTest(key=key), self.assertRaises(fd.AnalysisError):
                fd.seeded_log_signature(job, bad)
        bad = copy.deepcopy(records)
        bad[5]["decisions"] = [{"policy_rng_before": 1}]
        with self.assertRaisesRegex(fd.AnalysisError, "first policy RNG"):
            fd.seeded_log_signature(job, bad)

    def test_missing_terminal_or_duplicate_terminal_fails(self):
        job, records = seeded_fixture()
        for bad in (records[:5] + records[6:], records + [records[5]]):
            with self.assertRaises(fd.AnalysisError):
                fd.seeded_log_signature(job, bad)

    def test_hash_binding_and_ambiguous_json_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "log.jsonl"
            path.write_text('{"metadata":{}}\n{"summary":{}}\n')
            digest = fd.fingerprint(path)
            self.assertEqual(len(fd.read_log(path, digest)), 2)
            path.write_text('{"metadata":{}}\n{"summary":{"complete":true}}\n')
            with self.assertRaisesRegex(fd.AnalysisError, "provenance"):
                fd.read_log(path, digest)
            path.write_text('{"metadata":{},"metadata":{}}\n{"summary":{}}\n')
            with self.assertRaisesRegex(fd.AnalysisError, "Duplicate JSON"):
                fd.read_log(path, fd.fingerprint(path))

    def test_frozen_spec_requires_dependencies_and_whole_pair_coverage(self):
        source_hashes = {name: "a" * 64 for name in fd.SUPPORT_SOURCES}
        manifest = {"schema": 2, "primary_metric": "first_divergence_gain_bb100",
                    "secondary_metrics": ["raw_gain_bb100", "already_adjusted_gain_bb100"],
                    "runner_sha256": "a" * 64,
                    "jobs": [{"id": arm, "arm": arm, "seed": 1, "quota": 4, "games": 1}
                             for arm in fd.ARMS],
                    "first_divergence": {"version": fd.VERSION, "baseline_rule": fd.BASELINE_RULE,
                        "policy_rng_mode": fd.RNG_MODE, "hand_seed_derivation": fd.SEED_DERIVATION,
                        "hand_reservation_scheme": fd.RESERVATION_SCHEME, "analyzer_sha256": "b" * 64,
                        "support_sources_sha256": source_hashes, "paired_jobs": [["control", "candidate"]]}}
        self.assertEqual(fd.confirmatory_spec(manifest, "b" * 64, source_hashes), [["control", "candidate"]])
        for key in ("analyzer_sha256", "support_sources_sha256", "paired_jobs", "baseline_rule"):
            bad = copy.deepcopy(manifest); bad["first_divergence"].pop(key)
            with self.subTest(key=key), self.assertRaises(fd.AnalysisError):
                fd.confirmatory_spec(bad, "b" * 64, source_hashes)
        for key in ("seed", "quota", "games"):
            bad = copy.deepcopy(manifest); bad["jobs"][1][key] += 1
            with self.subTest(key=key), self.assertRaises(fd.AnalysisError):
                fd.confirmatory_spec(bad, "b" * 64, source_hashes)
        with self.assertRaises(fd.AnalysisError):
            fd.confirmatory_spec({**manifest, "schema": 1}, "b" * 64, source_hashes)

    def test_arm_library_or_sample_changes_fail(self):
        jobs = [{"id": f"{arm}_{i}", "arm": arm, "library_sha256": "a" * 64,
                 "library": "same.so", "policy_id": 7, "equity_samples": 0}
                for arm in fd.ARMS for i in range(2)]
        self.assertEqual(set(fd.arm_identities({"jobs": jobs})), set(fd.ARMS))
        for key, value in (("library_sha256", "b" * 64), ("policy_id", 1), ("equity_samples", 32)):
            bad = copy.deepcopy(jobs); bad[1][key] = value
            with self.subTest(key=key), self.assertRaises(fd.AnalysisError):
                fd.arm_identities({"jobs": bad})


class DenominatorTests(unittest.TestCase):
    @staticmethod
    def row(arm, hand, actual, baseline, diverged=True):
        return {"arm": arm, "job": arm, "hand": hand, "actual_net_bb": actual,
                "already_adjusted_net_bb": actual, "control_variate_bb": baseline,
                "residual_bb": actual - baseline if diverged else 0.0, "diverged": diverged,
                "control_variate_kind": "river_all_check_call" if diverged else "no_divergence_common_return",
                "action_divergence": diverged, "rng_divergence": False,
                "prefix_decision_pairs_checked": 1, "terminal_trace_decisions_checked": 2,
                "terminal_return_verified": True}

    def test_one_common_baseline_preserves_duplicate_deal_difference(self):
        # These complete latent-deal outcomes include a common large loss, a BB
        # rescue with later gains, a river tie, and a refunded raise folded to.
        outcomes = [(-200, -200, -200, False), (-1, 9, -1, True),
                    (0, -3, 0, True), (2, 4, 2, True)]
        rows = []
        for hand, (r0, r1, baseline, diverged) in enumerate(outcomes):
            a = self.row("control", hand, r0, baseline, diverged)
            b = self.row("candidate", hand, r1, baseline, diverged)
            self.assertEqual(b["residual_bb"] - a["residual_bb"], r1 - r0)
            rows.extend((a, b))
        counts, arms, differences = fd.summarize(rows, 4)
        self.assertEqual(counts["hands"], 8)
        self.assertEqual(arms["candidate"]["residual"]["hands"], 4)
        self.assertEqual(arms["candidate"]["residual"]["bb100"], 225)
        self.assertEqual(differences["residual"]["gain_bb100"], differences["raw"]["gain_bb100"])
        self.assertEqual(arms["candidate"]["kinds"]["river_all_check_call"]
                         ["contribution_full_hand_denominator"]["bb100"], 225)

    def test_missing_extra_duplicate_or_nonzero_unchanged_rows_fail(self):
        rows = [self.row(arm, i, 0, 0, False) for arm in fd.ARMS for i in range(2)]
        bad_cases = [rows[:-1], rows + [rows[0]], [rows[0], rows[0], *rows[2:]]]
        bad = copy.deepcopy(rows); bad[0]["residual_bb"] = 1; bad_cases.append(bad)
        for bad in bad_cases:
            with self.assertRaises(fd.AnalysisError):
                fd.summarize(bad, 2)


class ScriptPolicy:
    """A deterministic scripted policy using the real engine and external RNG."""
    def __init__(self, native, logic):
        self.native, self.logic, self.calls = native, logic, 0
        self.rng = native.rng
        self.visible = None

    def __getattr__(self, name):
        return getattr(self.native, name)

    def bot(self, style):
        action, draws = self.logic(self.visible, self.native.snapshot())
        for _ in range(draws):
            self.rng.value = (self.rng.value * 6364136223846793005 + 1442695040888963407) % (2**64) or 1
        self.calls += 1
        return action


class PublicRuntime:
    def __init__(self, evaluation):
        self.evaluation = evaluation
        self.policy_payloads = []

    def replay(self, native, visible):
        if isinstance(native, ScriptPolicy):
            if fd.PRIVATE_KEYS.intersection(visible):
                raise AssertionError("Private cards reached a policy replay")
            self.policy_payloads.append(copy.deepcopy(visible))
            native.visible = visible
        return self.evaluation.replay(native, visible)


class NativeCausalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(os.environ.get("POKER_FIRST_DIVERGENCE_SOURCE", Path(__file__).resolve().parent))
        library = os.environ.get("POKER_FIRST_DIVERGENCE_LIBRARY")
        if not library or not (source / "eval.py").is_file():
            raise unittest.SkipTest("Set POKER_FIRST_DIVERGENCE_SOURCE and POKER_FIRST_DIVERGENCE_LIBRARY for native fixtures")
        cls.evaluation = fd.load_module("first_divergence_native_test_eval", source / "eval.py")
        cls.source = source
        cls.library = Path(library)

    def native(self):
        return self.evaluation.Native(players=2, library=self.library)

    def make_hand(self, hero, logic, opponent, board=None, own=None, other=None, seed=73):
        own, other = own or ["As", "Ah"], other or ["Kd", "Qc"]
        board = board or ["2c", "3d", "7h", "9s", "Tc"]
        runtime = PublicRuntime(self.evaluation)
        policy = ScriptPolicy(self.native(), logic) if logic is not None else self.native()
        policy.rng.value = seed
        deck = ([*own, *other] if hero == 0 else [*other, *own]) + board
        policy.reset([20000, 20000], [self.evaluation.card(card) for card in deck])
        history, decisions = "", []
        while policy.snapshot()[0][0] != 255:
            state, players, _ = policy.snapshot()
            street = state[1]
            visible = {"client_pos": hero, "hole_cards": own,
                       "board": board[:(0, 3, 4, 5)[street]], "action": history}
            if state[0] == hero:
                policy.visible = visible
                before = policy.rng.value
                action = policy.bot(7)
                row = players[hero]
                decisions.append({**copy.deepcopy(visible), "street": street,
                    "pot_chips": state[2], "call_chips": min(state[3] - row[3], row[0]),
                    "stack_chips": row[0], "street_commitment_chips": row[3],
                    "high_bet_chips": state[3], "full_raise_increment_chips": state[4],
                    "legal_abstract_amounts": policy.actions(), "native_action": action,
                    "sent": fd.action_token(action, state, players), "policy_rng_before": before,
                    "policy_rng_after": policy.rng.value})
            else:
                action = opponent(visible, (state, players, []))
            history += fd.action_token(action, state, players)
            policy.step(action)
            after = policy.snapshot()[0]
            if after[0] != 255 and after[1] > street:
                history += "/"
            self.assertLess(len(decisions), 20)
        state, players, _ = policy.snapshot()
        net = players[hero][0] - players[hero][1]
        terminal_board = board if not history.endswith("f") else board[:(0, 3, 4, 5)[state[1]]]
        return {"hand": 0, "game": 0, "position": hero, "hole_cards": own,
                "slumbot_hole_cards": other, "board": terminal_board, "action": history,
                "winnings_chips": net, "baseline_winnings_chips": net,
                "native_terminal_verified": True, "decisions": decisions, "policy_rng": policy.rng.value}

    def analyze(self, hand, arm, control, candidate):
        runtime = PublicRuntime(self.evaluation)
        policies = {name: ScriptPolicy(self.native(), logic)
                    for name, logic in (("control", control), ("candidate", candidate))}
        row = fd.analyze_hand(runtime, policies, self.native(),
                              {name: {"policy_id": 7} for name in fd.ARMS},
                              {"arm": arm, "id": arm}, hand)
        return row, policies, runtime

    @staticmethod
    def passive(visible, snapshot):
        return 0, 1

    @staticmethod
    def open_then_passive(visible, snapshot):
        state = snapshot[0]
        return 200 if state[1] == 0 and not visible["action"] else 0

    def test_identical_policies_and_no_hero_decision_are_zero(self):
        hand = self.make_hand(0, self.passive, self.open_then_passive)
        row, policies, _ = self.analyze(hand, "control", self.passive, self.passive)
        self.assertFalse(row["diverged"])
        self.assertEqual(row["residual_bb"], 0)
        self.assertEqual(row["prefix_decision_pairs_checked"], len(hand["decisions"]))
        no_action = self.make_hand(0, self.passive, lambda *_: -1)
        row, policies, _ = self.analyze(no_action, "candidate", self.passive, self.passive)
        self.assertEqual(row["prefix_decision_pairs_checked"], 0)
        self.assertEqual(sum(p.calls for p in policies.values()), 0)

    def test_bb_first_divergence_owns_later_river_change(self):
        def control(view, snap):
            return (-1 if snap[0][1] == 0 else 0), 1
        def candidate(view, snap):
            return (400 if snap[0][1] == 3 and not view["action"].split("/")[-1] else 0), 1
        hand0 = self.make_hand(0, control, self.open_then_passive)
        hand1 = self.make_hand(0, candidate, self.open_then_passive)
        a, _, _ = self.analyze(hand0, "control", control, candidate)
        b, policies, _ = self.analyze(hand1, "candidate", control, candidate)
        self.assertEqual(a["control_variate_bb"], -1)
        self.assertEqual(b["control_variate_bb"], -1)
        self.assertEqual(b["control_variate_kind"], "bb_minraise_fold")
        self.assertEqual(b["prefix_decision_pairs_checked"], 1)
        self.assertTrue(all(p.calls == 1 for p in policies.values()))
        self.assertEqual(b["residual_bb"] - a["residual_bb"],
                         (hand1["winnings_chips"] - hand0["winnings_chips"]) / 100)

    def test_river_duplicate_deal_tie_and_unmatched_raise_refund(self):
        def candidate(view, snap):
            return (333 if snap[0][1] == 3 else 0), 1
        def opponent(view, snap):
            if snap[0][1] == 3 and snap[0][3] > 0:
                return -1
            return self.open_then_passive(view, snap)
        for board in (None, ["Ts", "Js", "Qs", "Ks", "As"]):
            own = ["2c", "3c"] if board else ["As", "Ah"]
            hands = [self.make_hand(0, logic, opponent, board=board, own=own)
                     for logic in (self.passive, candidate)]
            a, _, _ = self.analyze(hands[0], "control", self.passive, candidate)
            b, _, runtime = self.analyze(hands[1], "candidate", self.passive, candidate)
            self.assertEqual(a["control_variate_bb"], b["control_variate_bb"])
            self.assertEqual(b["residual_bb"] - a["residual_bb"],
                             (hands[1]["winnings_chips"] - hands[0]["winnings_chips"]) / 100)
            self.assertEqual(hands[1]["winnings_chips"], 200)  # 333-chip uncalled river bet refunded.
            self.assertTrue(all(set(payload) == set(fd.PUBLIC_KEYS) for payload in runtime.policy_payloads))
            if board:
                self.assertEqual(a["control_variate_bb"], 0)

    def test_rng_only_difference_stops_even_when_actions_match(self):
        def extra_draw(view, snap):
            return 0, (2 if snap[0][1] == 3 else 1)
        hand = self.make_hand(0, self.passive, self.open_then_passive)
        row, _, _ = self.analyze(hand, "control", self.passive, extra_draw)
        self.assertTrue(row["rng_divergence"])
        self.assertFalse(row["action_divergence"])
        self.assertEqual(row["control_variate_kind"], "river_all_check_call")

    def test_missing_extra_private_or_broken_trace_fails_closed(self):
        hand = self.make_hand(0, self.passive, self.open_then_passive)
        cases = []
        missing = copy.deepcopy(hand); missing["decisions"].pop(); cases.append(missing)
        extra = copy.deepcopy(hand); extra["decisions"].append(extra["decisions"][-1]); cases.append(extra)
        private = copy.deepcopy(hand); private["decisions"][0]["bot_hole_cards"] = ["Kd", "Qc"]; cases.append(private)
        rng = copy.deepcopy(hand); rng["decisions"][0]["policy_rng_before"] += 1; cases.append(rng)
        payout = copy.deepcopy(hand); payout["winnings_chips"] += 1; cases.append(payout)
        reveal = copy.deepcopy(hand); reveal.pop("slumbot_hole_cards"); cases.append(reveal)
        aliases = copy.deepcopy(hand); aliases["bot_hole_cards"] = ["4d", "5s"]; cases.append(aliases)
        for bad in cases:
            with self.subTest(bad=bad), self.assertRaises(fd.AnalysisError):
                self.analyze(bad, "control", self.passive, self.passive)

    def test_actual_policy_replay_mismatch_and_unsupported_gate_fail(self):
        hand = self.make_hand(0, self.passive, self.open_then_passive)
        with self.assertRaisesRegex(fd.AnalysisError, "action replay mismatch"):
            self.analyze(hand, "control", lambda *_: (-1, 1), self.passive)
        with self.assertRaisesRegex(fd.AnalysisError, "Unsupported first divergence"):
            self.analyze(hand, "control", self.passive,
                         lambda view, snap: ((400 if snap[0][1] == 1 else 0), 1))

    def test_frozen_archive_end_to_end_and_corrupt_execution_hash(self):
        # Synthetic local records with actual native decisions: no runner or
        # server is called. Both arms deliberately use one identical binary.
        fd.load_module("poker_audit", self.source / "poker_audit.py")
        series = fd.load_module("slumbot_series", self.source / "slumbot_series.py")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "reports"
            freeze_args = argparse.Namespace(
                control=self.library, candidate=self.library, runner=self.source / "eval.py",
                python=Path(os.sys.executable), output_dir=report / "jobs", hands_per_job=2,
                games=1, start_seed=12000, control_samples=0, candidate_samples=0,
                contact="synthetic-offline-test", first_divergence_analyzer=Path(fd.__file__))
            manifest = series.freeze(freeze_args)
            manifest_path = report / "manifest.json"
            series.write_new(manifest_path, manifest)
            execution = [{"started": True, "manifest_sha256": fd.fingerprint(manifest_path)}]
            for job in manifest["jobs"]:
                metadata = {"style": 7, "equity_samples": 0, "seed": job["seed"],
                    "hands_requested": 2, "concurrent_games": 1, "starting_bb": 200,
                    "opponent": "slumbot.com", "library_sha256": job["library_sha256"],
                    "runner_sha256": manifest["runner_sha256"],
                    "baseline_winnings_semantics": series.SEMANTICS, "log_schema": 4,
                    "policy_rng_mode": fd.RNG_MODE, "hand_seed_derivation": fd.SEED_DERIVATION,
                    "hand_reservation_scheme": fd.RESERVATION_SCHEME, "hand_reservation_quotas": [2]}
                records, net = [{"metadata": metadata}], 0
                for index in range(2):
                    seed = fd.hand_seed(job["seed"], index)
                    reservation = {"game": 0, "game_hand_attempt": index,
                                   "hand_reservation_index": index, "policy_seed_at_hand_start": seed}
                    hand = self.make_hand(index, None, self.open_then_passive, seed=seed)
                    hand.update(reservation, hand=index)
                    records.extend(({"hand_reserved": reservation}, hand))
                    net += hand["winnings_chips"] / 100
                records.append({"summary": {"hands": 2, "net_bb": net, "unresolved_hands": 0,
                                             "active_games": 0, "target_reached": True, "complete": True}})
                path = Path(job["output"]); path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("".join(json.dumps(record) + "\n" for record in records))
                execution.extend(({"job_started": job["id"]},
                                  {"job_finished": job["id"], "returncode": 0,
                                   "errors": [], "log_sha256": fd.fingerprint(path)}))
            execution.append({"all_jobs_finished": True})
            execution_path = manifest_path.with_suffix(".execution.jsonl")
            execution_path.write_text("".join(json.dumps(event) + "\n" for event in execution))
            args = argparse.Namespace(manifest=manifest_path, report_root=report,
                mode="confirmatory", output=root / "analysis", poker_source=self.source,
                control_library=None, candidate_library=None, recorded_report_root=None,
                expected_manifest_sha256=fd.fingerprint(manifest_path))
            with redirect_stdout(io.StringIO()):
                result = fd.analyze(args)
            self.assertFalse(result["not_confirmatory"])
            self.assertTrue(result["independent_hand_seed_schedule_verified"])
            self.assertEqual(result["counts"]["hands"], 16)
            self.assertEqual(result["counts"]["no_divergence_hands"], 16)
            self.assertEqual(result["differences"]["residual"]["gain_bb100"], 0)
            execution[2]["log_sha256"] = "0" * 64
            execution_path.write_text("".join(json.dumps(event) + "\n" for event in execution))
            args.output = root / "rejected"
            with self.assertRaisesRegex(fd.AnalysisError, "execution-bound eligibility"):
                fd.analyze(args)
            self.assertFalse((args.output / "summary.json").exists())


if __name__ == "__main__":
    unittest.main()
