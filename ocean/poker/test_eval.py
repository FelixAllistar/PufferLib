"""Offline checks for independent API games, contact headers and recovery."""
import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import requests
import eval as runner


class Response:
    def __init__(self, data, status=200, headers=None):
        self.data, self.status_code, self.headers = data, status, headers or {}

    def json(self):
        return self.data

    def raise_for_status(self):
        if self.status_code >= 400:
            raise requests.HTTPError(response=self)


class API:
    def __init__(self, fault=None):
        self.lock = threading.Lock()
        self.sessions, self.calls = [], []
        self.active = self.maximum = self.closed = 0
        self.fault, self.failed = fault, False

    def session(self):
        api = self

        class Session:
            def __init__(self):
                self.headers = {}
                self.token, self.pending, self.owner = None, False, None
                with api.lock:
                    self.index = len(api.sessions)
                    api.sessions.append(self)

            def post(self, url, json, timeout):
                owner = threading.get_ident()
                assert self.owner in (None, owner)
                self.owner = owner
                with api.lock:
                    api.active += 1
                    api.maximum = max(api.maximum, api.active)
                    api.calls.append((self.index, url, dict(json)))
                try:
                    time.sleep(0.01)
                    assert "https://github.com/example" in self.headers["User-Agent"]
                    if url.endswith("/new_hand"):
                        if api.fault == "429" and not api.failed:
                            api.failed = True
                            return Response({}, 429, {"Retry-After": "1"})
                        assert json.get("token") in (None, self.token)
                        self.token = f"private-{self.index}-{len(api.calls)}"
                        self.pending = True
                        return Response({"token": self.token, "client_pos": 1,
                            "hole_cards": ["Ac", "Kd"], "board": [], "action": ""})
                    assert url.endswith("/act") and self.pending
                    assert json["token"] == self.token
                    assert json["incr"] == "b250"
                    self.pending = False
                    if api.fault == "400":
                        return Response({"error_msg": f"Rejected action for {self.token}"}, 400)
                    if api.fault == "timeout" and not api.failed:
                        api.failed = True
                        raise requests.ReadTimeout()
                    return Response({"token": self.token, "client_pos": 1,
                        "winnings": 100, "action": "b250f"})
                finally:
                    with api.lock:
                        api.active -= 1

            def close(self):
                with api.lock:
                    api.closed += 1

        return Session()


class EvaluationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.args = argparse.Namespace(seed=73,
            library=str(runner.ROOT / "build/poker/libpoker2.so"), style=1,
            output=str(Path(self.directory.name) / "hands.jsonl"), hands=12,
            request_delay=0, resume=False, games=4,
            contact="https://github.com/example", progress=None)

    def run_api(self, api):
        with patch("requests.Session", side_effect=api.session), redirect_stdout(io.StringIO()):
            runner.slumbot(self.args)
        records = [json.loads(s) for s in Path(self.args.output).read_text().splitlines()]
        self.assertEqual(api.closed, len(api.sessions))
        self.assertNotIn("private-", Path(self.args.output).read_text())
        return records

    def test_concurrent_games_and_resume(self):
        api = API()
        records = self.run_api(api)
        self.assertGreater(api.maximum, 1)
        self.assertLessEqual(api.maximum, self.args.games)
        hands = [r for r in records if "winnings_chips" in r]
        self.assertEqual([r["hand"] for r in hands], list(range(12)))
        self.assertEqual(set(r["game"] for r in hands), set(range(4)))
        self.assertTrue(records[-1]["summary"]["complete"])
        self.assertEqual(records[-1]["summary"]["net_bb"], 12)
        self.args.resume, self.args.hands = True, 20
        records = self.run_api(API())
        hands = [r for r in records if "winnings_chips" in r]
        self.assertEqual([r["hand"] for r in hands], list(range(20)))
        self.assertTrue(records[-1]["summary"]["complete"])
        progress = Path(self.args.output).with_suffix(".progress.txt").read_text()
        self.assertIn("hands=20/20", progress)

    def test_ambiguous_act_is_not_replayed(self):
        self.args.games, self.args.hands = 1, 2
        api = API("timeout")
        records = self.run_api(api)
        self.assertEqual(sum(url.endswith("/act") for _, url, _ in api.calls), 3)
        self.assertEqual(sum(url.endswith("/new_hand") for _, url, _ in api.calls), 3)
        summary = records[-1]["summary"]
        self.assertTrue(summary["target_reached"])
        self.assertFalse(summary["complete"])
        self.assertEqual(summary["unresolved_hands"], 1)
        self.assertEqual(len(summary["bb100_bounds_for_unresolved"]), 2)
        interrupted = next(r["interrupted"] for r in records if "interrupted" in r)
        self.assertEqual(interrupted["hole_cards"], ["Ac", "Kd"])
        self.assertEqual(interrupted["decisions"][-1]["sent"], "b250")

    def test_trace_replays_decisions_without_future_cards(self):
        self.args.games, self.args.hands = 1, 1
        common = {"token": "private-trace", "client_pos": 1, "hole_cards": ["As", "Ad"]}
        responses = [
            Response({**common, "action": "", "board": []}),
            Response({**common, "action": "b250c/b500", "board": ["Kh", "7c", "2d"]}),
            Response({**common, "action": "b250c/b500f", "board": ["Kh", "7c", "2d"],
                "slumbot_hole_cards": ["Ks", "Qc"], "winnings": -250}),
        ]
        with patch("requests.Session") as factory, redirect_stdout(io.StringIO()):
            factory.return_value.headers = {}
            factory.return_value.post.side_effect = responses
            runner.slumbot(self.args)
        contents = Path(self.args.output).read_text()
        self.assertNotIn("private-", contents)
        records = [json.loads(line) for line in contents.splitlines()]
        hand = next(r for r in records if "winnings_chips" in r)
        self.assertEqual(hand["slumbot_hole_cards"], ["Ks", "Qc"])
        self.assertEqual([d["board"] for d in hand["decisions"]], [[], ["Kh", "7c", "2d"]])
        native = runner.Native(library=self.args.library)
        for decision in hand["decisions"]:
            self.assertNotIn("slumbot_hole_cards", decision)
            runner.replay(native, decision)
            native.rng.value = decision["policy_rng_before"]
            self.assertEqual(native.bot(1), decision["native_action"])
            self.assertEqual(native.rng.value, decision["policy_rng_after"])
        self.assertEqual([d["sent"] for d in hand["decisions"]], ["b250", "f"])
        self.assertEqual(hand["decisions"][1]["call_chips"], 500)

    def test_rate_limit_retry_after(self):
        self.args.games, self.args.hands = 1, 1
        records = self.run_api(API("429"))
        self.assertEqual([r["backoff"]["seconds"] for r in records if "backoff" in r], [1])
        self.assertTrue(records[-1]["summary"]["complete"])

    def test_bad_request_records_reason_without_token(self):
        self.args.games, self.args.hands = 1, 1
        api = API("400")
        with patch("requests.Session", side_effect=api.session), redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit):
                runner.slumbot(self.args)
        text = Path(self.args.output).read_text()
        records = [json.loads(s) for s in text.splitlines()]
        error = next(r["interrupted"] for r in records if "interrupted" in r)
        self.assertEqual(error["message"], "Rejected action for <token>")
        self.assertEqual(error["sent"], "b250")
        self.assertNotIn("private-", text)
        self.assertFalse(records[-1]["summary"]["target_reached"])


if __name__ == "__main__":
    unittest.main()
