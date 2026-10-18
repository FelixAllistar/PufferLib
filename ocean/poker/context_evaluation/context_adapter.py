"""Network-free native adapters and independent terminal verification.

The legacy policy executes the exact control library. The continuation library
is used only to validate its public action grammar; no candidate policy or CFR
function is invoked on that path. Revealed holdings are confined to callbacks
which the auditor invokes after every policy context has closed.
"""
from __future__ import annotations

import hashlib
import json
import re

from context_audit import (integer, require, visible_record, terminal_visible,
                           public_snapshot_identity, check_decision_snapshot, token)

U64_MAX = (1 << 64) - 1
TOKENS = re.compile(r"b[0-9]+|[kcf/]")


class LegacySession:
    """Receipt-checked transport around the unchanged stateless control bot."""
    def __init__(self, native, parser_native, evaluation, transport, owner, seat,
                 policy=7, samples=0):
        require(type(owner) is str and 0 < len(owner.encode()) <= 512, "invalid legacy owner")
        require(native.players == 2 and parser_native.players == 2, "legacy adapter requires HU")
        require(policy == 7 and samples == native.samples, "legacy policy configuration differs")
        self.native, self.parser = native, parser_native
        self.evaluation, self.transport = evaluation, transport
        transport.bind_library(parser_native.lib)
        self.owner = hashlib.sha256(owner.encode()).hexdigest()
        self.seat = integer(seat, 0, 1, "legacy seat")
        self.policy, self.samples, self.context = policy, samples, self
        self.generation, self.closed, self.freed, self.failed = 0, True, False, False
        self.started, self.terminal, self.events, self.board, self.own = False, False, (), (), None
        self.state, self.pending, self.requests = None, None, 0

    def _fail(self):
        self.failed = True

    def begin(self, generation):
        try:
            integer(generation, 1, U64_MAX, "legacy generation")
            require(not self.freed and self.closed and generation > self.generation,
                    "legacy begin must follow close with a newer generation")
            self.generation, self.closed, self.failed = generation, False, False
            self.started, self.terminal, self.events, self.board, self.own = False, False, (), (), None
            self.state, self.pending, self.requests = None, None, 0
        except Exception:
            self._fail()
            raise

    def ingest(self, generation, response):
        try:
            # Validate the captured envelope before touching the response body.
            integer(generation, 1, U64_MAX, "legacy response generation")
            require(not self.freed and not self.failed and generation == self.generation
                    and (not self.closed or self.terminal), "legacy response envelope rejected")
            require(type(response) is dict and type(response.get("terminal")) is bool,
                    "legacy response needs an explicit terminal boolean")
            public = visible_record(response, terminal=response["terminal"])
            require(public["client_pos"] == self.seat, "legacy response changed seat")
            own = tuple(sorted(self.transport.cards(public["hole_cards"], "own holding", (2,))))
            board = self.transport.cards(public["board"], "visible board", (0, 3, 4, 5))
            require(not set(own).intersection(board), "legacy card collision")
            require(self.own is None or self.own == own, "legacy own holding changed")
            require(board[:len(self.board)] == self.board, "legacy board prefix changed")
            require(len(public["action"]) <= 65535, "legacy history too long")
            replay, _, state, events = self.transport._rebuild(
                self.parser, self.seat, own, board, public["action"])
            require(len(events) >= len(self.events) and events[:len(self.events)] == self.events,
                    "legacy ordered public prefix changed")
            terminal = state.actor == 255
            require(terminal == public["terminal"] and (terminal or state.actor == self.seat),
                    "legacy terminal marker or actor differs")
            if self.pending is not None:
                require(self.native.rng.value == self.pending["after"], "legacy pending RNG changed")
            if self.terminal:
                require(terminal and events == self.events and board == self.board
                        and self.native.rng.value == self.terminal_rng,
                        "legacy closed terminal receipt changed")
                return {"terminal": True, "closed": True, "new_events": 0,
                        "events": len(events), "audit": self.audit()}
            new_events = events[len(self.events):]
            pending = self.pending
            for event in new_events:
                before = self.transport.Public.from_buffer_copy(event.before)
                if before.actor == self.seat:
                    require(pending is not None and pending["action"] == event.action
                            and pending["public"] == event.before,
                            "legacy own action lacks its exact pending receipt")
                    pending = None
            # Preserve the original response's own-card order in the original
            # library. The parser's arbitrary hidden fillers need not match it.
            prior_rng = self.native.rng.value
            self.evaluation.replay(self.native, {key: public[key] for key in
                ("client_pos", "hole_cards", "board", "action")})
            require(self.native.rng.value == prior_rng, "legacy public replay changed RNG")
            if not terminal:
                require(public_snapshot_identity(self.native.snapshot(), self.seat)
                        == public_snapshot_identity(replay.snapshot(), self.seat),
                        "legacy and parser public states differ")
            self.pending = pending
            self.state, self.events, self.board, self.own = bytes(state), events, board, own
            self.started, self.terminal = True, terminal
            if terminal:
                require(self.pending is None, "legacy terminal has an unacknowledged own action")
                self.closed, self.terminal_rng = True, self.native.rng.value
            return {"terminal": terminal, "closed": self.closed, "new_events": len(new_events),
                    "events": len(events), "audit": self.audit()}
        except Exception:
            self._fail()
            raise

    def decide(self, request_id):
        try:
            integer(request_id, 1, U64_MAX, "legacy request ID")
            require(not self.freed and not self.failed and not self.closed and self.started
                    and not self.terminal, "legacy decision outside live hand")
            require(self.native.snapshot()[0][0] == self.seat, "legacy decision for another actor")
            if self.pending is not None:
                require(request_id == self.pending["request"] and self.state == self.pending["public"]
                        and self.native.rng.value == self.pending["after"],
                        "legacy duplicate decision receipt differs")
                return self.pending["action"]
            require(request_id == self.requests + 1, "legacy request sequence differs")
            before_snapshot, before_rng = self.native.snapshot(), self.native.rng.value
            action = self.native.bot(self.policy)
            integer(action, -1, 20000, "legacy native action")
            require(self.native.snapshot() == before_snapshot, "legacy policy mutated its game")
            self.requests = request_id
            self.pending = {"request": request_id, "public": self.state, "action": action,
                            "before": before_rng, "after": self.native.rng.value}
            return action
        except Exception:
            self._fail()
            raise

    def audit(self):
        prefix = hashlib.sha256()
        for event in self.events:
            prefix.update(event.before)
            prefix.update(event.action.to_bytes(4, "little", signed=True))
            prefix.update(event.after)
        result = {"protocol": "original_control_receipts_v1", "generation": self.generation,
                  "owner": self.owner, "seat": self.seat, "events": len(self.events),
                  "requests": self.requests, "phase": 5 if self.failed else 4 if self.closed else 1,
                  "public_hash": hashlib.sha256(self.state or b"").hexdigest(),
                  "prefix_hash": prefix.hexdigest(), "pending": self.pending is not None}
        if self.pending is not None:
            result.update({"pending_" + key: value for key, value in self.pending.items() if key != "public"})
        result["whole_hash"] = hashlib.sha256(json.dumps(result, sort_keys=True).encode()).hexdigest()
        return result

    def abandon(self):
        self.closed = True

    def close(self):
        self.closed, self.freed = True, True


def factories(evaluation, transport, candidate_library, control_library, samples=0):
    def candidate(seed, owner, seat):
        native = evaluation.Native(players=2, seed=seed, library=candidate_library, samples=samples)
        return transport.Session(transport.Context(native, owner, seat, policy=7, samples=samples))

    def control(seed, owner, seat):
        native = evaluation.Native(players=2, seed=seed, library=control_library, samples=samples)
        parser = evaluation.Native(players=2, seed=1, library=candidate_library, samples=0)
        return LegacySession(native, parser, evaluation, transport, owner, seat, policy=7, samples=samples)
    return {"control": control, "candidate": candidate}


def independent_payout(own, other, board, history, hero):
    """Replay HU contributions without native money state; rank with eval7."""
    import eval7
    tokens = TOKENS.findall(history)
    require("".join(tokens) == history, "independent payout malformed history")
    total, bets, high, actor, folder = [100, 50], [100, 50], 100, 1, None
    for part in tokens:
        if part == "/":
            bets, high, actor = [0, 0], 0, 0
            continue
        require(folder is None, "independent payout action after fold")
        if part == "f":
            folder = actor
        else:
            target = int(part[1:]) if part.startswith("b") else high
            pay = min(target - bets[actor], 20000 - total[actor])
            require(0 <= pay <= 20000 - total[actor], "independent payout invalid contribution")
            bets[actor] += pay
            total[actor] += pay
            if part.startswith("b"):
                high = target
        actor = 1 - actor
    if folder is not None:
        gain = -total[hero] if folder == hero else total[1 - hero]
    else:
        require(len(board) == 5, "independent showdown needs all five board cards")
        ranks = [eval7.evaluate([eval7.Card(card) for card in holding + board])
                 for holding in (own, other)]
        gain = ((ranks[0] > ranks[1]) - (ranks[0] < ranks[1])) * min(total)
    return gain, total


class TruthCallbacks:
    def __init__(self, evaluation, transport, candidate_library, control_library):
        self.evaluation, self.transport = evaluation, transport
        self.parser = evaluation.Native(players=2, seed=1, library=candidate_library)
        transport.bind_library(self.parser.lib)
        self.settlement = evaluation.Native(players=2, seed=1, library=control_library)

    def revealed(self, hand):
        hero = integer(hand.get("position"), 0, 1, "truth position")
        public = terminal_visible(hand)
        own = self.transport.cards(public["hole_cards"], "truth own cards", (2,))
        board = self.transport.cards(public["board"], "truth board", (0, 3, 4, 5))
        aliases = [hand[key] for key in ("slumbot_hole_cards", "bot_hole_cards") if key in hand]
        require(bool(aliases), "missing opponent revelation")
        others = [self.transport.cards(value, "opponent revelation", (2,)) for value in aliases]
        require(all(set(value) == set(others[0]) for value in others), "conflicting opponent aliases")
        require(len(set(own + board + others[0])) == len(own) + len(board) + 2, "truth card collision")
        return hero, public, aliases[0]

    def verify(self, hand):
        hero, public, other = self.revealed(hand)
        _, _, terminal, events = self.transport._rebuild(self.parser, hero,
            tuple(sorted(self.transport.cards(public["hole_cards"], "own", (2,)))),
            self.transport.cards(public["board"], "board", (0, 3, 4, 5)), public["action"])
        require(terminal.actor == 255, "truth history is not terminal")
        self.evaluation.replay(self.settlement, {**public, "action": "", "slumbot_hole_cards": other})
        decisions = hand.get("decisions")
        require(type(decisions) is list, "truth decision trace missing")
        prior_rng = integer(hand.get("policy_seed_at_hand_start"), 1, U64_MAX, "truth hand seed")
        index = 0
        for event_index, event in enumerate(events):
            state, rows, _ = self.settlement.snapshot()
            before = self.transport.Public.from_buffer_copy(event.before)
            require(state[0] == before.actor and state[1] == before.street, "truth actor/street mismatch")
            if state[0] == hero:
                require(index < len(decisions), "missing actual decision in terminal trace")
                decision = decisions[index]
                visible = visible_record(decision)
                require(visible["client_pos"] == hero and set(visible["hole_cards"]) == set(public["hole_cards"])
                        and visible["board"] == public["board"][:(0, 3, 4, 5)[state[1]]],
                        "truth decision cards or seat changed")
                _, _, _, prefix = self.transport._rebuild(self.parser, hero,
                    tuple(sorted(self.transport.cards(visible["hole_cards"], "own", (2,)))),
                    self.transport.cards(visible["board"], "board", (0, 3, 4, 5)), visible["action"])
                require(prefix == events[:event_index], "decision is not the normalized terminal prefix")
                check_decision_snapshot(decision, self.settlement)
                require(decision["native_action"] == event.action
                        and decision["sent"] == token(event.action, state, rows), "terminal actual action differs")
                require(decision["policy_rng_before"] == prior_rng, "truth within-hand RNG chain differs")
                prior_rng = integer(decision["policy_rng_after"], 1, U64_MAX, "truth post-decision RNG")
                index += 1
            self.settlement.step(event.action)
        state, rows, _ = self.settlement.snapshot()
        require(state[0] == 255 and index == len(decisions), "truth terminal decision coverage differs")
        require(hand.get("policy_rng") == prior_rng, "truth terminal RNG chain differs")
        require(hand.get("native_terminal_verified") is True, "live terminal verification missing")
        payout, totals = independent_payout(public["hole_cards"], other, public["board"], public["action"], hero)
        require(payout == hand["winnings_chips"] == rows[hero][0] - 20000,
                "server, original native and independent eval7 payouts differ")
        require(totals == [row[2] for row in rows], "independent contribution totals differ")
        return index

    def baseline(self, hand, decision, actions, snapshot):
        hero, _, other = self.revealed(hand)
        visible = visible_record(decision)
        require(decision["street"] == 3 and len(visible["board"]) == 5,
                "truth baseline is river-only")
        self.evaluation.replay(self.settlement, {**visible, "slumbot_hole_cards": other})
        require(public_snapshot_identity(self.settlement.snapshot(), hero)
                == public_snapshot_identity(snapshot, hero), "truth baseline changed public state")
        _, _, _, prefix = self.transport._rebuild(self.parser, hero,
            tuple(sorted(self.transport.cards(visible["hole_cards"], "own", (2,)))),
            self.transport.cards(visible["board"], "board", (5,)), visible["action"])
        parts = []
        for event in prefix:
            before = self.transport.Public.from_buffer_copy(event.before)
            after = self.transport.Public.from_buffer_copy(event.after)
            parts.append("f" if event.action < 0 else f"b{event.action}" if event.action else
                         "c" if before.high > getattr(before, f"bet{before.actor}") else "k")
            if after.actor != 255 and after.street != before.street:
                parts.append("/")
        history, steps = "".join(parts), 0
        while self.settlement.snapshot()[0][0] != 255:
            state, rows, _ = self.settlement.snapshot()
            require(state[1] == 3 and steps < 2, "river passive baseline failed to close")
            history += token(0, state, rows)
            self.settlement.step(0)
            steps += 1
        payout, totals = independent_payout(visible["hole_cards"], other, visible["board"], history, hero)
        _, rows, _ = self.settlement.snapshot()
        require(payout == rows[hero][0] - 20000 and totals == [row[2] for row in rows],
                "independent passive baseline payout differs")
        return payout
