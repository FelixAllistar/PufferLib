#!/usr/bin/env python3
"""Offline whole-hand native replay and common-prefix river comparison.

The actual policy is always executed through the final recorded decision.
Independent common-prefix shadows stop at their first external action or
post-decision RNG difference. Internal context differences do not define D.
This module is network-free and never silently drops an interrupted slot.
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


def audit_hand(hand, job, identities, session_factories, settlement_verify, baseline):
    """Audit one complete actual hand with injectable, independently owned sessions.

    Factories accept ``(seed, owner, seat)`` and return an unstarted Session.
    Production uses exact frozen native libraries. Explicit test shims may be
    injected by the separately labeled synthetic construction corpus only.
    ``settlement_verify(hand)`` independently checks all actual action prefixes,
    revealed-card payoff and recorded RNG chain after every context is closed.
    ``baseline(hand, decision, actions, snapshot)`` returns a check/call river
    payoff in chips using a separate revealed-card engine, never a policy engine.
    """
    require(job["arm"] in ARMS and set(session_factories) == set(ARMS), "invalid arms/factories")
    for arm in ARMS:
        require(identities[arm]["policy_id"] == 7, "unsupported policy style")
    hero = integer(hand.get("position"), 0, 1, "terminal position")
    seed = integer(hand.get("policy_seed_at_hand_start"), 1, (1 << 64) - 1, "hand seed")
    generation = integer(hand.get("context_generation", hand.get("game_hand_attempt", -1) + 1),
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
           "position": hero, "actual_net_bb": winnings / 100,
           "already_adjusted_net_bb": adjusted / 100, "diverged": False,
           "actual_native_decisions_checked": 0, "actual_native_decisions_after_D": 0,
           "prefix_decision_pairs_checked": 0, "control_variate_kind": "no_divergence_common_return",
           "control_variate_bb": winnings / 100, "residual_bb": 0.0,
           "terminal_return_verified": False}
    divergence = None
    try:
        actual.context.begin(generation)
        # This pass is deliberately independent of the comparison shadows. It
        # executes all actual decisions even when the later pass finds an early D.
        for index, decision in enumerate(decisions):
            require(decision.get("client_pos") == hero, "actual decision changed seat")
            require(decision.get("context_generation", generation) == generation,
                    "actual decision changed generation")
            request_id = decision.get("context_request_id", index + 1)
            integer(request_id, 1, (1 << 64) - 1, "request ID")
            require(request_id == index + 1, "actual decision request sequence changed")
            visible = visible_record(decision)
            status = actual.ingest(generation, visible)
            require(status.get("terminal") is False, "actual decision unexpectedly terminal")
            snapshot = check_decision_snapshot(decision, actual.native)
            before_rng = actual.native.rng.value
            require(before_rng == decision.get("policy_rng_before"), "actual RNG before differs")
            if "context_before" in decision:
                require(actual.context.audit() == decision["context_before"], "actual context-before receipt differs")
            action = actual.decide(request_id)
            require(actual.native.snapshot() == snapshot, "actual policy changed engine state")
            require(action == decision.get("native_action") and token(action, snapshot[0], snapshot[1]) == decision.get("sent"),
                    "actual native/sent action differs")
            require(actual.native.rng.value == decision.get("policy_rng_after"), "actual RNG after differs")
            if "context_after" in decision:
                require(actual.context.audit() == decision["context_after"], "actual context-after receipt differs")
            row["actual_native_decisions_checked"] += 1
        status = actual.ingest(generation, terminal_visible(hand))
        require(status.get("terminal") is True and status.get("closed") is True,
                "actual terminal failed to close policy")
        require(actual.native.rng.value == hand.get("policy_rng"), "actual terminal RNG differs")
        if "context_terminal_audit" in hand:
            require(actual.context.audit() == hand["context_terminal_audit"], "actual terminal context receipt differs")
        actual.context.close()
        sessions.remove(actual)

        shadows = {}
        for arm in ARMS:
            shadow = session_factories[arm](seed, base_owner + "/" + arm + "_shadow", hero)
            shadow.context.begin(generation)
            shadows[arm] = shadow
            sessions.append(shadow)
        for index, decision in enumerate(decisions):
            visible = visible_record(decision)
            actions, after, snapshots = {}, {}, {}
            for arm in ARMS:
                shadow = shadows[arm]
                status = shadow.ingest(generation, visible)
                require(status.get("terminal") is False, "common-prefix decision unexpectedly terminal")
                snapshots[arm] = check_decision_snapshot(decision, shadow.native)
                require(shadow.native.rng.value == decision["policy_rng_before"],
                        "common-prefix RNG changed before D")
                actions[arm] = shadow.decide(index + 1)
                after[arm] = shadow.native.rng.value
                require(shadow.native.snapshot() == snapshots[arm], "shadow policy changed engine state")
            require(public_snapshot_identity(snapshots["control"], hero)
                    == public_snapshot_identity(snapshots["candidate"], hero),
                    "common-prefix public engine states or own holdings differ")
            row["prefix_decision_pairs_checked"] += 1
            require(actions[job["arm"]] == decision["native_action"] and after[job["arm"]] == decision["policy_rng_after"],
                    "actual arm and independent prefix shadow differ")
            if actions["control"] == actions["candidate"] and after["control"] == after["candidate"]:
                continue
            require(decision["street"] == 3 and len(decision["board"]) == 5,
                    "new context comparison supports river-only first divergence")
            divergence = (decision, actions, snapshots["control"])
            row.update(diverged=True, first_decision_index=index, first_street=decision["street"],
                       first_actions=actions, first_rng_after=after,
                       action_divergence=actions["control"] != actions["candidate"],
                       rng_divergence=after["control"] != after["candidate"],
                       control_variate_kind="river_all_check_call",
                       actual_native_decisions_after_D=len(decisions) - index - 1)
            break
        # A no-D terminal must also acknowledge the final pending action. At D,
        # the counterfactual shadow ends here and must not ingest actual futures.
        if divergence is None:
            for shadow in shadows.values():
                status = shadow.ingest(generation, terminal_visible(hand))
                require(status.get("closed") is True, "no-D shadow failed terminal close")
    finally:
        for session in sessions:
            close_session(session)
    # Both functions below may use revealed holdings. Every policy context is
    # already closed and freed, including all common-prefix shadows.
    checked = settlement_verify(hand)
    require(type(checked) is int and checked == len(decisions), "terminal trace/decision count differs")
    row["terminal_trace_decisions_checked"] = checked
    row["terminal_return_verified"] = True
    if divergence is not None:
        decision, actions, snapshot = divergence
        chips = baseline(hand, decision, actions, snapshot)
        integer(chips, -20000, 20000, "common river baseline")
        row.update(control_variate_bb=chips / 100, residual_bb=(winnings - chips) / 100)
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


def require_river_action_only(rows):
    """The retained policy promises original external RNG consumption.

    Detect an RNG-only D in audit_hand, then reject this source-specific gate.
    It must not be erased or silently treated as a no-divergence hand.
    """
    for row in rows:
        if row["diverged"]:
            require(row.get("first_street") == 3 and row.get("action_divergence") is True
                    and row.get("rng_divergence") is False,
                    "first divergence violates the river action-only policy contract")


def summarize(rows, hands_per_arm):
    require(len(rows) == 2 * hands_per_arm, "not all planned hands present")
    require(len({(row["job"], row["hand"]) for row in rows}) == len(rows), "duplicate hand identities")
    require(all(row["terminal_return_verified"] and row["actual_native_decisions_checked"]
                == row["terminal_trace_decisions_checked"] for row in rows), "incomplete actual native replay")
    require_river_action_only(rows)
    arms = {}
    for arm in ARMS:
        selected = [row for row in rows if row["arm"] == arm]
        require(len(selected) == hands_per_arm, "arm denominator differs from plan")
        arms[arm] = {name: estimate([row[field] for row in selected]) for name, field in
                     (("raw", "actual_net_bb"), ("already_adjusted", "already_adjusted_net_bb"),
                      ("first_divergence", "residual_bb"))}
    differences = {}
    for metric in arms["control"]:
        c, t = arms["control"][metric], arms["candidate"][metric]
        gain, se = t["bb100"] - c["bb100"], math.hypot(t["se_bb100"], c["se_bb100"])
        differences[metric] = {"gain_bb100": gain, "se_bb100": se,
                               "ci95_bb100": [gain - 1.96 * se, gain + 1.96 * se]}
    return {"arms": arms, "differences": differences,
            "method": "all-hand independent-arm approximate normal 95% intervals",
            "actual_native_decisions_checked": sum(row["actual_native_decisions_checked"] for row in rows),
            "actual_native_decisions_after_D": sum(row["actual_native_decisions_after_D"] for row in rows)}
