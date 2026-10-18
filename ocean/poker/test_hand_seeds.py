"""Offline regression checks for fixed hand reservations and actual policy RNG.

Uses the real native policy and an in-memory API; no network requests occur.
"""
import argparse
from contextlib import redirect_stdout
import ctypes
import hashlib
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

import eval as runner
from test_eval import API, Response


class OrderedAPI:
    """Complete game blocks in a chosen order, independent of thread startup."""

    def __init__(self, order, context, terminal_without_decision=False):
        self.order = order
        self.context = context
        self.terminal_without_decision = terminal_without_decision
        self.condition = threading.Condition()
        self.closed = set()
        self.sessions = []

    def session(self):
        api = self

        class Session:
            def __init__(self):
                self.headers = {}
                self.game = None
                self.token = None
                self.started = 0
                api.sessions.append(self)

            def post(self, url, json, timeout):
                self.game = api.context.game
                with api.condition:
                    ready = api.condition.wait_for(
                        lambda: all(game in api.closed
                                    for game in api.order[:api.order.index(self.game)]),
                        timeout=5)
                    if not ready:
                        raise AssertionError("Test API game ordering deadlocked")
                if url.endswith("/new_hand"):
                    self.started += 1
                    self.token = f"test-private-{self.game}-{self.started}"
                    common = {"token": self.token, "client_pos": 1,
                              "hole_cards": ["As", "Ah"], "board": []}
                    if api.terminal_without_decision:
                        return Response({**common, "client_pos": 0, "action": "f",
                                         "bot_hole_cards": ["Kc", "Qd"],
                                         "winnings": 50, "baseline_winnings": 0})
                    return Response({**common, "action": ""})
                assert url.endswith("/act") and json["token"] == self.token
                assert json["incr"] == "b250"
                return Response({"token": self.token, "client_pos": 1,
                                 "action": "b250f", "board": [],
                                 "bot_hole_cards": ["Kc", "Qd"],
                                 "winnings": 100, "baseline_winnings": 0})

            def close(self):
                with api.condition:
                    api.closed.add(self.game)
                    api.condition.notify_all()

        return Session()


class IndependentHandSeedTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.args = argparse.Namespace(
            seed=202610189007, style=1, equity_samples=0,
            library=str(runner.ROOT / "build/poker/libpoker2.so"),
            output=str(Path(self.directory.name) / "hands.jsonl"), hands=6,
            games=3, request_delay=0, resume=False, progress=None,
            contact="https://github.com/example", independent_hand_seeds=True)

    def load(self):
        return [json.loads(line) for line in Path(self.args.output).read_text().splitlines()]

    def ordered_run(self, name, order, extra_policy_draws=0, no_decisions=False):
        self.args.output = str(Path(self.directory.name) / f"{name}.jsonl")
        context = threading.local()
        api = OrderedAPI(order, context, no_decisions)
        original_native = runner.Native
        master_seed = self.args.seed
        consumed = []
        consumed_lock = threading.Lock()

        class InstrumentedNative(original_native):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                if threading.current_thread() is not threading.main_thread():
                    context.game = (kwargs["seed"] - master_seed) // 1000003

            def bot(self, style):
                before = self.rng.value
                action = super().bot(style)
                # Deliberately vary an earlier hand's consumption while keeping
                # its action unchanged. Later hand starts must remain identical.
                for _ in range(extra_policy_draws):
                    super().bot(style)
                with consumed_lock:
                    consumed.append((context.game, before, self.rng.value))
                return action

        with patch.object(runner, "Native", InstrumentedNative), \
                patch("requests.Session", side_effect=api.session), \
                redirect_stdout(io.StringIO()):
            runner.slumbot(self.args)
        self.assertEqual(api.closed, set(order))
        records = self.load()
        self.assertTrue(records[-1]["summary"]["complete"])
        self.assertNotIn("test-private-", Path(self.args.output).read_text())
        return records, consumed

    def assert_reservations(self, records):
        metadata = records[0]["metadata"]
        self.assertEqual(metadata["log_schema"], 4)
        self.assertEqual(metadata["policy_rng_mode"], runner.HAND_SEED_MODE)
        self.assertEqual(metadata["hand_seed_derivation"], runner.HAND_SEED_DERIVATION)
        self.assertEqual(metadata["hand_reservation_scheme"], runner.HAND_RESERVATION_SCHEME)
        quotas = metadata["hand_reservation_quotas"]
        reserved = {}
        hands = []
        for record in records:
            if "hand_reserved" in record:
                event = record["hand_reserved"]
                index = event["hand_reservation_index"]
                self.assertNotIn(index, reserved)
                expected = int.from_bytes(hashlib.sha256(
                    f"poker-policy-hand-seed-v1:{self.args.seed}:{index}".encode("ascii")
                ).digest()[:8], "big") or 1
                self.assertEqual(event["policy_seed_at_hand_start"], expected)
                self.assertEqual(index, sum(quotas[:event["game"]]) + event["game_hand_attempt"])
                reserved[index] = event
            if "winnings_chips" in record:
                index = record["hand_reservation_index"]
                self.assertIn(index, reserved, "Reservation must precede terminal logging")
                for key, value in reserved[index].items():
                    self.assertEqual(record[key], value)
                if record["decisions"]:
                    self.assertEqual(record["decisions"][0]["policy_rng_before"],
                                     record["policy_seed_at_hand_start"])
                else:
                    self.assertEqual(record["policy_rng"], record["policy_seed_at_hand_start"])
                hands.append(record)
        self.assertEqual(set(reserved), set(range(self.args.hands)))
        self.assertEqual({h["hand_reservation_index"] for h in hands}, set(reserved))
        return hands

    def test_completion_order_and_prior_draws_do_not_change_hand_seeds(self):
        forward, first_draws = self.ordered_run("forward", [0, 1, 2])
        reverse, second_draws = self.ordered_run("reverse", [2, 1, 0], extra_policy_draws=4)
        a, b = self.assert_reservations(forward), self.assert_reservations(reverse)
        self.assertEqual([h["game"] for h in a], [0, 0, 1, 1, 2, 2])
        self.assertEqual([h["game"] for h in b], [2, 2, 1, 1, 0, 0])
        signature = lambda rows: sorted((h["game"], h["game_hand_attempt"],
            h["hand_reservation_index"], h["policy_seed_at_hand_start"]) for h in rows)
        self.assertEqual(signature(a), signature(b))
        self.assertEqual(sorted((g, before) for g, before, _ in first_draws),
                         sorted((g, before) for g, before, _ in second_draws))
        self.assertNotEqual(sorted(first_draws), sorted(second_draws))
        self.assertEqual(sorted(first_draws), sorted((h["game"],
            h["decisions"][0]["policy_rng_before"], h["decisions"][0]["policy_rng_after"]) for h in a))

    def test_terminal_without_hero_decision_keeps_reservation(self):
        records, consumed = self.ordered_run("no_decision", [1, 2, 0], no_decisions=True)
        hands = self.assert_reservations(records)
        self.assertEqual(consumed, [])
        self.assertTrue(all(h["decisions"] == [] for h in hands))
        self.assertTrue(all(h["native_terminal_verified"] for h in hands))

    def test_failed_attempt_is_retained_and_consumes_its_seed(self):
        self.args.games, self.args.hands = 1, 2
        api = API("timeout")
        with patch("requests.Session", side_effect=api.session), redirect_stdout(io.StringIO()):
            runner.slumbot(self.args)
        records = self.load()
        reserved = [r["hand_reserved"] for r in records if "hand_reserved" in r]
        self.assertEqual([r["hand_reservation_index"] for r in reserved], [0, 1, 2])
        interrupted = next(r["interrupted"] for r in records if "interrupted" in r)
        self.assertEqual(interrupted["hand_reservation_index"], 0)
        self.assertEqual(interrupted["policy_seed_at_hand_start"], reserved[0]["policy_seed_at_hand_start"])
        hands = [r for r in records if "winnings_chips" in r]
        self.assertEqual([h["hand_reservation_index"] for h in hands], [1, 2])
        for hand in hands:
            self.assertEqual(hand["decisions"][0]["policy_rng_before"],
                             hand["policy_seed_at_hand_start"])
        self.assertFalse(records[-1]["summary"]["complete"])
        self.assertEqual(records[-1]["summary"]["unresolved_hands"], 1)

    def test_resume_is_rejected_before_native_or_http(self):
        self.args.resume = True
        with patch.object(runner, "Native") as native, patch("requests.Session") as session:
            with self.assertRaisesRegex(ValueError, "does not support --resume"):
                runner.slumbot(self.args)
            native.assert_not_called()
            session.assert_not_called()

    def test_seeded_log_cannot_resume_by_disabling_flag(self):
        self.ordered_run("seeded", [0, 1, 2])
        self.args.resume = True
        self.args.independent_hand_seeds = False
        with patch("requests.Session") as session:
            with self.assertRaisesRegex(ValueError, "created with independent hand seeds"):
                runner.slumbot(self.args)
            session.assert_not_called()

    def test_default_mode_retains_carried_rng_and_schema(self):
        self.args.independent_hand_seeds = False
        records, consumed = self.ordered_run("legacy", [0, 1, 2])
        metadata = records[0]["metadata"]
        self.assertEqual(metadata["log_schema"], 3)
        self.assertNotIn("policy_rng_mode", metadata)
        self.assertFalse(any("hand_reserved" in r for r in records))
        for game in range(self.args.games):
            transitions = [(before, after) for g, before, after in consumed if g == game]
            self.assertEqual(transitions[0][0], self.args.seed + game * 1000003)
            self.assertEqual(transitions[1][0], transitions[0][1])

    def test_reservation_partition_and_overflow_for_uneven_quotas(self):
        for hands, games in ((1, 5), (5, 4), (12, 4), (13, 4), (17, 3)):
            quotas = runner.game_quotas(hands, games)
            self.assertEqual(sum(quotas), hands)
            planned = [runner.hand_reservation_index(game, attempt, quotas)
                       for game, quota in enumerate(quotas) for attempt in range(quota)]
            self.assertEqual(sorted(planned), list(range(hands)))
            overflow = [runner.hand_reservation_index(game, quotas[game] + attempt, quotas)
                        for game in range(games) for attempt in range(3)]
            self.assertEqual(len(set(overflow)), len(overflow))
            self.assertTrue(all(index >= hands for index in overflow))


if __name__ == "__main__":
    unittest.main()
