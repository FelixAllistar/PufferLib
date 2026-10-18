"""Fixed, offline HTTP injection test using actual frozen native policies.

Eight hands per arm use the first four predeclared decks, both orientations,
without selecting paths or outcomes. A separate two-hand job injects one
uncertain action response and must stop with its original reservation intact.
There are no real network calls and no playing-strength assertion.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import context_eval as runner
import context_audit as audit
import context_adapter as adapter
import context_series as series


class FakeResponse:
    status_code = 200
    def __init__(self, value):
        self.content = json.dumps(value, allow_nan=False).encode()


class FakeHttp:
    def __init__(self, evaluation, transport, candidate, control, decks, log, fail_action=False):
        self.headers = {}
        self.evaluation, self.transport = evaluation, transport
        self.truth = evaluation.Native(players=2, seed=1, library=control)
        self.opponent = evaluation.Native(players=2, seed=1, library=control)
        self.parser = evaluation.Native(players=2, seed=1, library=candidate)
        transport.bind_library(self.parser.lib)
        self.decks, self.log, self.fail_action = decks, Path(log), fail_action
        self.new_calls, self.action_calls, self.closed = 0, 0, False
        self.history, self.hand = "", -1

    def advance(self, action):
        before = self.truth.snapshot()
        self.history += runner.action_token(action, before[0], before[1])
        self.truth.step(action)
        after = self.truth.snapshot()[0]
        if after[0] != 255 and after[1] != before[0][1]:
            self.history += "/"

    def visible(self):
        state, rows, board = self.truth.snapshot()
        count = (0, 3, 4, 5)[state[1]]
        return {"client_pos": self.hero, "hole_cards": [self.evaluation.text_card(c) for c in rows[self.hero][5:]],
                "board": [self.evaluation.text_card(c) for c in board[:count]], "action": self.history}

    def respond(self):
        while self.truth.snapshot()[0][0] not in (255, self.hero):
            state, rows, board = self.truth.snapshot()
            other = 1 - self.hero
            public = {"client_pos": other, "hole_cards": [self.evaluation.text_card(c) for c in rows[other][5:]],
                      "board": [self.evaluation.text_card(c) for c in board[:(0, 3, 4, 5)[state[1]]]],
                      "action": self.history}
            self.evaluation.replay(self.opponent, public)
            self.advance(self.opponent.bot(7))
        value = {**self.visible(), "token": "synthetic_token_never_sent_to_a_network"}
        if self.truth.snapshot()[0][0] == 255:
            rows = self.truth.snapshot()[1]
            value.update(winnings=rows[self.hero][0] - 20000, baseline_winnings=rows[self.hero][0] - 20000,
                         slumbot_hole_cards=[self.evaluation.text_card(c) for c in rows[1-self.hero][5:]])
        return FakeResponse(value)

    def post(self, url, json, timeout):
        audit.require(not self.closed and timeout == (10, 45), "fake HTTP contract differs")
        if url == runner.ENDPOINT + "new_hand":
            self.hand += 1
            self.new_calls += 1
            audit.require(self.hand < 8, "fake server received a replacement or extra hand")
            records = [runner.decode(line) for line in self.log.read_bytes().splitlines()]
            reserved = [row["hand_reserved"] for row in records if "hand_reserved" in row]
            audit.require(len(reserved) == self.new_calls and reserved[-1]["game_hand_attempt"] == self.hand,
                          "HTTP hand request preceded its durable reservation")
            self.hero = self.hand % 2
            self.truth.reset([20000, 20000], self.decks[self.hand // 2], button=1)
            self.opponent.rng.value = 202610187501 + 1009 * self.hand
            self.history = ""
            return self.respond()
        audit.require(url == runner.ENDPOINT + "act" and type(json) is dict
                      and set(json) == {"token", "incr"}, "unexpected fake HTTP operation")
        self.action_calls += 1
        audit.require(self.truth.snapshot()[0][0] == self.hero, "fake action at another actor")
        part = json["incr"]
        action = int(part[1:]) if part.startswith("b") else -1 if part == "f" else 0
        public = self.visible()
        _, _, state, _ = self.transport._rebuild(self.parser, self.hero,
            tuple(sorted(self.transport.cards(public["hole_cards"], "own", (2,)))),
            self.transport.cards(public["board"], "board", (0, 3, 4, 5)), self.history)
        self.transport._validate_action(state, part[0] if part.startswith("b") else part, action)
        self.advance(action)
        if self.fail_action:
            # The wager has been applied but its response is unavailable. The
            # caller may neither retry it nor replace the unfinished hand.
            raise TimeoutError("synthetic uncertainty after action application")
        return self.respond()

    def close(self):
        self.closed = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--expected-plan-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    plan = series.load_plan(args.plan, args.expected_plan_sha256)
    audit.require(plan["mode"] == "synthetic", "this proof requires a synthetic plan")
    args.output.mkdir(parents=True, exist_ok=False)
    evaluation = runner.load_module("pipeline_original", Path(plan["poker_source"]) / "eval.py")
    transport = runner.load_module("pipeline_transport", Path(plan["context_source"]) / "continuation_transport.py")
    candidate, control = (plan["libraries"][arm]["path"] for arm in ("candidate", "control"))
    decks = plan["proof_decks"]
    audit.require(len(decks) == 4 and all(len(deck) == 9 and len(set(deck)) == 9 for deck in decks),
                  "fixed pipeline deck count differs")
    receipts, callback_counts = [], []
    for job in plan["jobs"]:
        audit.require(job["hands"] == 8 and job["games"] == 1, "fixed pipeline workload differs")
        log = args.output / (job["id"] + ".jsonl")
        fake = FakeHttp(evaluation, transport, candidate, control, decks, log)
        code = runner.run(series.arguments(plan, job, args.expected_plan_sha256, log), http_factory=lambda: fake)
        audit.require(code == 0 and fake.closed and fake.new_calls == 8, "actual-native fake HTTP job failed")
        receipts.append({"id": job["id"], "complete": True, "interrupted": False,
                         "log": str(log.resolve()), "log_sha256": runner.sha256(log)})
        callback_counts.append({"arm": job["arm"], "new_hand_calls": fake.new_calls,
                                "action_calls": fake.action_calls})
    series.save_new(args.output / "series_receipt.json", {"complete": True,
        "plan_sha256": args.expected_plan_sha256, "attempts": receipts})
    analysis = series.analyze(args.plan, args.expected_plan_sha256, args.output, args.output / "analysis.json")
    audit.require(not analysis["relative_improvement_supported"] and not analysis["absolute_slumbot_win_supported"],
                  "synthetic proof must not emit a strength claim")

    job = {"id": "uncertain_action", "arm": "candidate", "hands": 2, "games": 1,
           "seed": 202610187599, "run_id": plan["run_id"]}
    log = args.output / "uncertain_action.jsonl"
    fake = FakeHttp(evaluation, transport, candidate, control, decks, log, fail_action=True)
    code = runner.run(series.arguments(plan, job, args.expected_plan_sha256, log), http_factory=lambda: fake)
    audit.require(code == 2 and fake.closed and fake.action_calls == 1, "uncertain action was retried or accepted")
    records = [runner.decode(line) for line in log.read_bytes().splitlines()]
    interruptions = [row["interrupted"] for row in records if "interrupted" in row]
    reservations = [row["hand_reserved"] for row in records if "hand_reserved" in row]
    audit.require(len(interruptions) == 1 and interruptions[0]["replacement_started"] is False
                  and len(reservations) == fake.new_calls and records[-1]["summary"]["complete"] is False,
                  "uncertain action lost its reservation or started replacement work")
    report = {"schema": "actual-native-context-pipeline-proof-v1", "status": "passed",
              "scope": "16 fixed synthetic native hands plus one uncertain-response job; no live requests or strength claim",
              "plan_sha256": args.expected_plan_sha256, "analysis_sha256": runner.sha256(args.output / "analysis.json"),
              "actual_native_decisions_checked": analysis["actual_native_decisions_checked"],
              "callbacks": callback_counts, "uncertain_action_calls": fake.action_calls,
              "uncertain_new_hand_calls": fake.new_calls}
    series.save_new(args.output / "proof.json", report)
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
