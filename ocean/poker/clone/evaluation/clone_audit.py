#!/usr/bin/env python3
"""Offline complete actual-policy replay and raw all-hand comparison.

Every recorded decision is reexecuted with the frozen model and exact RNG.
Only after the policy context is closed does a separate revealed-card engine
verify all prefixes and payout. No river control variate or shadow replay is
used for this candidate, which can change earlier-street actions and RNG.
"""
from __future__ import annotations

import math
import statistics

ARMS = ("control", "candidate")
PUBLIC_FIELDS = ("client_pos", "hole_cards", "board", "action")
PRIVATE_FIELDS = frozenset(("slumbot_hole_cards", "bot_hole_cards", "opponent_hole_cards"))


class AuditError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise AuditError(message)


def integer(value, low, high, name):
    require(type(value) is int and low <= value <= high, "invalid " + name)
    return value


def visible_record(record, *, terminal=False):
    require(type(record) is dict and not PRIVATE_FIELDS.intersection(record),
            "private data entered a decision record")
    require(all(key in record for key in PUBLIC_FIELDS), "incomplete visible record")
    require(type(record["hole_cards"]) is list and type(record["board"]) is list,
            "visible card fields must be arrays")
    integer(record["client_pos"], 0, 1, "client position")
    require(type(record["action"]) is str, "history must be text")
    return {"client_pos": record["client_pos"], "hole_cards": list(record["hole_cards"]),
            "board": list(record["board"]), "action": record["action"], "terminal": terminal}


def terminal_visible(hand):
    # Construct an explicit allowlist before handing anything to a policy
    # context. Terminal opponent holdings and returns never cross this call.
    return visible_record({"client_pos": hand["position"], "hole_cards": hand["hole_cards"],
                           "board": hand["board"], "action": hand["action"]}, terminal=True)


def token(action, state, rows):
    integer(action, -1, 20000, "action")
    return "f" if action < 0 else f"b{action}" if action else (
        "c" if state[3] > rows[state[0]][3] else "k")


def check_decision_snapshot(decision, native):
    before = native.snapshot()
    state, rows, _ = before
    hero = integer(decision["client_pos"], 0, 1, "client position")
    require(state[0] == hero, "recorded decision does not belong to native actor")
    row = rows[hero]
    expected = {"street": state[1], "pot_chips": state[2],
                "call_chips": min(state[3] - row[3], row[0]), "stack_chips": row[0],
                "street_commitment_chips": row[3], "high_bet_chips": state[3],
                "full_raise_increment_chips": state[4], "legal_abstract_amounts": native.actions()}
    for key, value in expected.items():
        require(decision.get(key) == value, "native decision state differs: " + key)
    return before


def public_snapshot_identity(snapshot, hero):
    state, rows, board = snapshot
    count = (0, 3, 4, 5)[state[1]]
    # Dummy opponent cards, hidden runout fillers and hole-card ordering may
    # differ between independently reconstructed engines. Match the complete
    # common public state and the actual own holding, not those artifacts.
    return (tuple(state), tuple(tuple(row[:5]) for row in rows),
            tuple(sorted(rows[hero][5:7])), tuple(board[:count]))


def close_session(session):
    try:
        session.abandon()
    finally:
        session.context.close()


def audit_hand(hand, job, identities, session_factories, settlement_verify):
    """Replay every actual decision, then independently verify the terminal.

    Factories accept (seed, owner, seat). Actual policy and context audit
    receipts must match throughout the complete hand. Revealed opponent cards
    enter only settlement_verify after all policy contexts have closed.
    """
    require(job["arm"] in ARMS and set(session_factories) == set(ARMS), "invalid arms/factories")
    for arm in ARMS:
        require(identities[arm]["policy_id"] == 7, "unsupported policy style")
    hero = integer(hand.get("position"), 0, 1, "terminal position")
    seed = integer(hand.get("policy_seed_at_hand_start"), 1, (1 << 64) - 1, "hand seed")
    generation = integer(hand.get("context_generation"),
                         1, (1 << 64) - 1, "generation")
    decisions = hand.get("decisions")
    require(type(decisions) is list, "missing actual decision list")
    winnings = integer(hand.get("winnings_chips"), -20000, 20000, "actual winnings")
    adjusted = hand.get("baseline_winnings_chips")
    require(type(adjusted) in (int, float) and math.isfinite(adjusted), "invalid adjusted outcome")
    base_owner = job.get("owner_base",
                        f"{job['run_id']}/{job['id']}/worker{hand['game']}/lane0/seat{hero}")
    actual = session_factories[job["arm"]](seed, base_owner + "/actual", hero)
    sessions = [actual]
    row = {"job": job["id"], "arm": job["arm"], "hand": hand["hand"], "game": hand["game"],
           "hand_reservation_index": integer(hand.get("hand_reservation_index"), 0, job["hands"] - 1, "reservation"),
           "position": hero, "actual_net_bb": winnings / 100,
           "already_adjusted_net_bb": adjusted / 100,
           "actual_native_decisions_checked": 0, "terminal_return_verified": False}
    try:
        actual.context.begin(generation)
        # This is the complete actual action/RNG trace, including all streets
        # and original-policy fallback decisions.
        for index, decision in enumerate(decisions):
            require(decision.get("client_pos") == hero, "actual decision changed seat")
            require(decision.get("context_generation") == generation,
                    "actual decision changed generation")
            request_id = decision.get("context_request_id")
            integer(request_id, 1, (1 << 64) - 1, "request ID")
            require(request_id == index + 1, "actual decision request sequence changed")
            visible = visible_record(decision)
            status = actual.ingest(generation, visible)
            require(status.get("terminal") is False, "actual decision unexpectedly terminal")
            snapshot = check_decision_snapshot(decision, actual.native)
            before_rng = actual.native.rng.value
            require(before_rng == decision.get("policy_rng_before"), "actual RNG before differs")
            require(type(decision.get("context_before")) is dict
                    and actual.context.audit() == decision["context_before"], "actual context-before receipt differs")
            action = actual.decide(request_id)
            require(actual.native.snapshot() == snapshot, "actual policy changed engine state")
            require(action == decision.get("native_action") and token(action, snapshot[0], snapshot[1]) == decision.get("sent"),
                    "actual native/sent action differs")
            require(actual.native.rng.value == decision.get("policy_rng_after"), "actual RNG after differs")
            require(type(decision.get("context_after")) is dict
                    and actual.context.audit() == decision["context_after"], "actual context-after receipt differs")
            row["actual_native_decisions_checked"] += 1
        status = actual.ingest(generation, terminal_visible(hand))
        require(status.get("terminal") is True and status.get("closed") is True,
                "actual terminal failed to close policy")
        require(actual.native.rng.value == hand.get("policy_rng"), "actual terminal RNG differs")
        require(type(hand.get("context_terminal_audit")) is dict
                and actual.context.audit() == hand["context_terminal_audit"], "actual terminal context receipt differs")
        actual.context.close()
        sessions.remove(actual)

    finally:
        for session in sessions:
            close_session(session)
    # Revealed holdings are used only after the actual policy context has
    # closed and been freed. Both native and independent integer/eval7 payout
    # checks are performed by this callback.
    checked = settlement_verify(hand)
    require(type(checked) is int and checked == len(decisions), "terminal trace/decision count differs")
    row["terminal_trace_decisions_checked"] = checked
    row["terminal_return_verified"] = True
    return row


def require_complete_slots(expected, attempts):
    """No filtering: a single interruption invalidates the full planned set."""
    require(type(expected) is list and len(expected) == len(set(expected)), "invalid planned slots")
    require(type(attempts) is list and len(attempts) == len(expected), "incomplete planned slots")
    ids = []
    for attempt in attempts:
        require(type(attempt) is dict and attempt.get("complete") is True
                and attempt.get("interrupted") is not True, "dirty or interrupted planned slot")
        ids.append(attempt.get("id"))
    require(ids == expected, "replaced, reordered or duplicated planned slots")


def estimate(values):
    require(len(values) >= 2 and all(math.isfinite(v) for v in values), "invalid observations")
    rate = 100 * statistics.fmean(values)
    se = 100 * statistics.stdev(values) / math.sqrt(len(values))
    return {"hands": len(values), "bb100": rate, "se_bb100": se,
            "ci95_bb100": [rate - 1.96 * se, rate + 1.96 * se]}


def summarize(rows, hands_per_arm):
    require(len(rows) == 2 * hands_per_arm, "not all planned hands present")
    require(len({(row["job"], row["hand"]) for row in rows}) == len(rows), "duplicate hand identities")
    require(len({(row["job"], row["hand_reservation_index"]) for row in rows}) == len(rows),
            "duplicate fixed reservations")
    require(all(row["terminal_return_verified"] and row["actual_native_decisions_checked"]
                == row["terminal_trace_decisions_checked"] for row in rows), "incomplete actual native replay")
    arms = {}
    for arm in ARMS:
        selected = [row for row in rows if row["arm"] == arm]
        require(len(selected) == hands_per_arm, "arm denominator differs from plan")
        arms[arm] = {name: estimate([row[field] for row in selected]) for name, field in
                     (("raw", "actual_net_bb"), ("already_adjusted", "already_adjusted_net_bb"))}
    differences = {}
    for metric in arms["control"]:
        c, t = arms["control"][metric], arms["candidate"][metric]
        gain, se = t["bb100"] - c["bb100"], math.hypot(t["se_bb100"], c["se_bb100"])
        differences[metric] = {"gain_bb100": gain, "se_bb100": se,
                               "ci95_bb100": [gain - 1.96 * se, gain + 1.96 * se]}
    return {"arms": arms, "differences": differences,
            "method": "all-hand independent-arm approximate normal 95% intervals",
            "actual_native_decisions_checked": sum(row["actual_native_decisions_checked"] for row in rows)}
