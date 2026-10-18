#!/usr/bin/env python3
"""Read-only audit of PufferLib schema-2 Slumbot logs and local block CSVs.

No network calls, policy changes, or opponent requests occur in this module.
Returns are big blinds (100 chips in the recorded Slumbot interface).
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
import hashlib
import heapq
import json
import math
from pathlib import Path
import re
import statistics
import sys
from typing import Any

STREETS = ("preflop", "flop", "turn", "river")
IMMUTABLE = ("style", "equity_samples", "library_sha256", "opponent", "starting_bb",
             "policy_rng_mode", "hand_seed_derivation", "hand_reservation_scheme")
TOKEN = re.compile(r"b\d+|[kcf/]")
ACTION = {"f": "fold", "k": "check", "c": "call"}


def fingerprint(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def estimate(values: list[float]) -> dict[str, Any]:
    """Two-sided normal 95% CI of 100 times mean BB per independent unit."""
    n = len(values)
    mean = statistics.fmean(values) if n else None
    sd = statistics.stdev(values) if n > 1 else None
    se = 100 * sd / math.sqrt(n) if sd is not None else None
    half = 1.96 * se if se is not None else None
    rate = 100 * mean if mean is not None else None
    return {
        "hands": n, "net_bb": math.fsum(values), "bb100": rate,
        "sd_bb_per_hand": sd, "se_bb100": se,
        "ci95_half_width_bb100": half,
        "ci95_bb100": [rate - half, rate + half] if half is not None else None,
    }


def difference(candidate: dict, control: dict) -> dict:
    if candidate["se_bb100"] is None or control["se_bb100"] is None:
        raise ValueError("At least two independent observations are needed in each arm")
    gain = candidate["bb100"] - control["bb100"]
    se = math.hypot(candidate["se_bb100"], control["se_bb100"])
    half = 1.96 * se
    return {"gain_bb100": gain, "se_bb100": se,
            "ci95_half_width_bb100": half, "ci95_bb100": [gain - half, gain + half],
            "method": "independent hand returns; approximate normal 95% CI"}


def move_name(decision: dict) -> str:
    sent = decision.get("sent", "")
    return "raise" if sent.startswith("b") else ACTION.get(sent, "unknown")


def terminal_type(record: dict) -> str:
    history = record.get("action", "")
    decisions = record.get("decisions", [])
    if history.endswith("f"):
        if decisions and decisions[-1].get("sent") == "f" and \
                decisions[-1].get("action", "") + "f" == history:
            return "hero_fold"
        return "opponent_fold"
    return "showdown_revealed" if record.get("slumbot_hole_cards") or record.get("bot_hole_cards") else "no_reveal_terminal"


def pot_type(record: dict) -> str:
    preflop = record.get("action", "").split("/")[0]
    raises = len(re.findall(r"b\d+", preflop))
    return ("unraised_or_limped", "single_raised", "three_bet", "four_bet_plus")[min(raises, 3)]


def pot_odds_bucket(decision: dict) -> str:
    call = decision.get("call_chips", 0)
    pot = decision.get("pot_chips", 0)
    if call <= 0:
        return "free_action"
    odds = call / (pot + call) if pot + call else 0
    for limit, label in ((0.20, "under_20pct"), (0.30, "20_to_30pct"),
                         (0.40, "30_to_40pct")):
        if odds < limit:
            return label
    return "40pct_plus"


@dataclass
class Decisions:
    moves: Counter = field(default_factory=Counter)
    faced: int = 0
    folds_facing: int = 0
    calls_facing: int = 0
    raises_facing: int = 0
    check_opportunities: int = 0
    bets_when_checked_to: int = 0
    call_chips_sum: int = 0
    pot_chips_sum: int = 0

    def add(self, decision: dict) -> None:
        name = move_name(decision)
        self.moves[name] += 1
        call = decision.get("call_chips", 0)
        self.call_chips_sum += call
        self.pot_chips_sum += decision.get("pot_chips", 0)
        if call > 0:
            self.faced += 1
            self.folds_facing += name == "fold"
            self.calls_facing += name == "call"
            self.raises_facing += name == "raise"
        else:
            self.check_opportunities += 1
            self.bets_when_checked_to += name == "raise"

    def output(self) -> dict:
        n = sum(self.moves.values())
        return {"decisions": n, "actions": dict(sorted(self.moves.items())),
                "faced_bet": self.faced, "folds_facing_bet": self.folds_facing,
                "fold_pct_facing_bet": 100 * self.folds_facing / self.faced if self.faced else None,
                "call_pct_facing_bet": 100 * self.calls_facing / self.faced if self.faced else None,
                "raise_pct_facing_bet": 100 * self.raises_facing / self.faced if self.faced else None,
                "check_opportunities": self.check_opportunities,
                "bet_pct_when_check_available": 100 * self.bets_when_checked_to / self.check_opportunities
                if self.check_opportunities else None,
                "mean_call_bb": self.call_chips_sum / (100 * n) if n else None,
                "mean_pot_bb": self.pot_chips_sum / (100 * n) if n else None}


def _partition(grouped: dict[str, list[float]], all_hands: int) -> dict:
    result = {}
    for name, values in sorted(grouped.items()):
        result[name] = {**estimate(values),
                       "contribution_bb100_of_all_hands": 100 * math.fsum(values) / all_hands
                       if all_hands else None}
    return result


def parse_log(path: Path, prefix_hands: int | None = None,
              details: bool = True, top_hands: int = 0,
              baseline_mode: str = "recorded_only") -> dict:
    if prefix_hands is not None and prefix_hands <= 0:
        raise ValueError("prefix_hands must be positive")
    metadata: dict = {}
    latest_metadata: dict = {}
    summaries: list[tuple[int, dict]] = []
    unknown_prior_segments: list[dict] = []
    interrupted: list[dict] = []
    parse_errors: list[dict] = []
    integrity_errors: list[str] = []
    records_by_type: Counter = Counter()
    values: list[float] = []
    all_values: list[float] = []
    baseline_values: list[float] = []
    raw_with_baseline: list[float] = []
    position_values = defaultdict(list)
    terminal_street_values = defaultdict(list)
    terminal_type_values = defaultdict(list)
    last_hero_action_values = defaultdict(list)
    preflop_pot_values = defaultdict(list)
    game_values = defaultdict(list)
    decision_groups = defaultdict(Decisions)
    diagnostic_errors = Counter()
    losses: list = []
    wins: list = []
    last_event_line = last_summary_line = selected_last_line = 0
    first_interrupted_after = None
    last_line = 0
    total_decisions = 0
    hand_ids: set[int] = set()
    reserved_ids: set[int] = set()
    pending_reservations: set[int] = set()
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for line_no, raw in enumerate(handle, 1):
            last_line = line_no
            digest.update(raw)
            if not raw.strip():
                continue
            try:
                record = json.loads(raw)
                if not isinstance(record, dict):
                    raise ValueError("record must be a JSON object")
            except (json.JSONDecodeError, UnicodeDecodeError, ValueError) as exc:
                parse_errors.append({"line": line_no, "completed_before": len(all_values),
                                     "error": str(exc)[:200],
                                     "line_ended_in_newline": raw.endswith(b"\n")})
                last_event_line = line_no
                continue
            if "metadata" in record or "resumed" in record:
                key = "metadata" if "metadata" in record else "resumed"
                if key == "resumed" and last_summary_line <= last_event_line:
                    unknown_prior_segments.append({"resumed_line": line_no,
                                                   "completed_before": len(all_values)})
                new = record[key]
                records_by_type[key] += 1
                if not metadata:
                    metadata = new
                else:
                    for name in IMMUTABLE:
                        if metadata.get(name, 0 if name == "equity_samples" else None) != \
                                new.get(name, 0 if name == "equity_samples" else None):
                            integrity_errors.append(f"line {line_no}: immutable {name} changed")
                latest_metadata = new
                last_event_line = line_no
            elif "winnings_chips" in record:
                records_by_type["completed"] += 1
                last_event_line = line_no
                if metadata.get("policy_rng_mode") == "independent_hand_seeds_v1":
                    index = record.get("hand_reservation_index")
                    if index not in pending_reservations:
                        integrity_errors.append(f"line {line_no}: terminal hand lacks a unique pending reservation")
                    else:
                        pending_reservations.remove(index)
                if "hand" in record:
                    ident = record["hand"]
                    if ident in hand_ids:
                        integrity_errors.append(f"line {line_no}: duplicate hand id {ident}")
                    if ident != len(all_values):
                        integrity_errors.append(f"line {line_no}: hand id {ident}, expected {len(all_values)}")
                    hand_ids.add(ident)
                net = record["winnings_chips"] / 100
                if not math.isfinite(net):
                    integrity_errors.append(f"line {line_no}: nonfinite winnings")
                    continue
                all_values.append(net)
                if prefix_hands is not None and len(values) >= prefix_hands:
                    continue
                values.append(net)
                if "baseline_winnings_chips" in record:
                    baseline = record["baseline_winnings_chips"]
                    if isinstance(baseline, (int, float)) and math.isfinite(baseline):
                        baseline_values.append(baseline / 100)
                        raw_with_baseline.append(net)
                    else:
                        integrity_errors.append(f"line {line_no}: invalid baseline_winnings_chips")
                selected_last_line = line_no
                position_values[str(record.get("position", "unknown"))].append(net)
                game_values[str(record.get("game", "unknown"))].append(net)
                terminal_street_values[STREETS[min(record.get("action", "").count("/"), 3)]].append(net)
                terminal_type_values[terminal_type(record)].append(net)
                preflop_pot_values[pot_type(record)].append(net)
                decisions = record.get("decisions", [])
                last_hero_action_values[move_name(decisions[-1]) if decisions else "no_hero_decision"].append(net)
                total_decisions += len(decisions)
                if details:
                    for decision in decisions:
                        street = STREETS[min(max(decision.get("street", 0), 0), 3)]
                        pos = str(decision.get("client_pos", record.get("position", "unknown")))
                        for key in (f"street/{street}", f"position/{pos}", f"position_street/{pos}/{street}",
                                    f"street_pot_odds/{street}/{pot_odds_bucket(decision)}"):
                            decision_groups[key].add(decision)
                        for problem in trace_problems(decision):
                            diagnostic_errors[problem] += 1
                if top_hands:
                    compact = {k: record.get(k) for k in ("hand", "game", "position", "winnings_chips",
                               "hole_cards", "board", "action", "slumbot_hole_cards", "decisions")}
                    heapq.heappush(losses, (-net, line_no, compact))
                    heapq.heappush(wins, (net, line_no, compact))
                    if len(losses) > top_hands:
                        heapq.heappop(losses)
                    if len(wins) > top_hands:
                        heapq.heappop(wins)
            elif "summary" in record:
                records_by_type["summary"] += 1
                summaries.append((line_no, record["summary"]))
                last_summary_line = line_no
            elif "interrupted" in record:
                records_by_type["interrupted"] += 1
                last_event_line = line_no
                if first_interrupted_after is None:
                    first_interrupted_after = len(all_values)
                event = record["interrupted"]
                if metadata.get("policy_rng_mode") == "independent_hand_seeds_v1":
                    index = event.get("hand_reservation_index")
                    if index not in pending_reservations:
                        integrity_errors.append(f"line {line_no}: interrupted hand lacks a pending reservation")
                    else:
                        pending_reservations.remove(index)
                interrupted.append({"line": line_no, "completed_before": len(all_values),
                                    "game": event.get("game"), "stage": event.get("stage"),
                                    "type": event.get("type"), "status": event.get("status"),
                                    "decision_count": len(event.get("decisions", []))})
            elif "hand_reserved" in record:
                records_by_type["hand_reserved"] += 1
                last_event_line = line_no
                reservation = record["hand_reserved"]
                index = reservation.get("hand_reservation_index") if isinstance(reservation, dict) else None
                if metadata.get("log_schema") != 4 or metadata.get("policy_rng_mode") != "independent_hand_seeds_v1":
                    integrity_errors.append(f"line {line_no}: hand reservation without schema-4 seed mode")
                elif type(index) is not int or index < 0 or index in reserved_ids:
                    integrity_errors.append(f"line {line_no}: invalid or duplicate hand reservation")
                else:
                    reserved_ids.add(index)
                    pending_reservations.add(index)
            elif "backoff" in record:
                records_by_type["backoff"] += 1
                last_event_line = line_no
            else:
                records_by_type["unknown"] += 1
                integrity_errors.append(f"line {line_no}: unknown record type")
                last_event_line = line_no
    final_summary = summaries[-1][1] if summaries else None
    if not metadata:
        integrity_errors.append("missing initial metadata")
    summary_terminal = final_summary is not None and last_summary_line > last_event_line
    pending_unknown = not summary_terminal or bool(unknown_prior_segments) or bool(pending_reservations)
    if parse_errors:
        pending_unknown = True
    if summary_terminal:
        checks = (("hands", len(all_values)), ("unresolved_hands", len(interrupted)))
        for key, actual in checks:
            if final_summary.get(key) != actual:
                integrity_errors.append(f"final summary {key}={final_summary.get(key)}, parsed={actual}")
        if "net_bb" in final_summary and not math.isclose(final_summary["net_bb"], math.fsum(all_values),
                                                          abs_tol=1e-6, rel_tol=1e-10):
            integrity_errors.append("final summary net_bb disagrees with completed records")
        if final_summary.get("active_games", 0):
            pending_unknown = True
    selected_full = prefix_hands is None or prefix_hands >= len(all_values)
    cutoff = last_line if selected_full else selected_last_line
    selected_interruptions = [x for x in interrupted if x["line"] <= cutoff]
    selected_parse_errors = [x for x in parse_errors if x["line"] <= cutoff]
    requested_reached = prefix_hands is None or len(values) == prefix_hands
    # A finalized early stop with zero unresolved hands is clean but incomplete.
    # A manually selected prefix can be usable only under a separately recorded,
    # outcome-independent window rule; this audit does not confer that status.
    selected_pending_unknown = pending_unknown and selected_full
    clean_window = (not selected_interruptions and not selected_parse_errors and
                    not selected_pending_unknown and not integrity_errors and requested_reached)
    n = len(values)
    unmatched = len(selected_interruptions)
    cap = float(metadata.get("starting_bb", 200))
    bounds = None
    if unmatched and not selected_pending_unknown:
        bounds = [100 * (math.fsum(values) + direction * cap * unmatched) / (n + unmatched)
                  for direction in (-1, 1)]
    source_state = ("unknown_pending_or_truncated" if pending_unknown else
                    "finalized_with_unresolved" if interrupted else
                    "target_completed_clean" if final_summary.get("target_reached") else
                    "graceful_stop_clean_partial")
    output = {
        "source": str(path.resolve()), "sha256": digest.hexdigest(), "metadata": metadata,
        "latest_metadata": latest_metadata,
        "window": {"mode": "all_completed" if prefix_hands is None else "first_completed_prefix",
                   "prefix_hands": prefix_hands, "requested_prefix_reached": requested_reached,
                   "last_selected_line": selected_last_line,
                   "requires_outcome_independent_prefix_plan": prefix_hands is not None,
                   "clean": clean_window, "unresolved_attempts": unmatched,
                   "pending_outcomes_unknown": selected_pending_unknown,
                   "missing_outcome_extreme_mean_bounds_bb100": bounds},
        "estimate": estimate(values), "decisions": total_decisions,
        "audit": {"source_state": source_state, "source_completed_hands": len(all_values),
                  "source_unresolved_attempts": len(interrupted), "source_pending_outcomes_unknown": pending_unknown,
                  "first_interrupted_after_completed_hands": first_interrupted_after,
                  "parse_errors": parse_errors, "integrity_errors": integrity_errors,
                  "unknown_prior_segments": unknown_prior_segments,
                  "trace_problem_counts": dict(diagnostic_errors), "record_counts": dict(records_by_type),
                  "interrupted_events": interrupted, "final_summary": final_summary,
                  "final_summary_is_terminal": summary_terminal},
        "by_position": _partition(position_values, n),
        "by_terminal_street": _partition(terminal_street_values, n),
        "by_terminal_type": _partition(terminal_type_values, n),
        "by_last_hero_action": _partition(last_hero_action_values, n),
        "by_preflop_pot_type": _partition(preflop_pot_values, n),
        "by_game": _partition(game_values, n),
        "decision_stats": {key: group.output() for key, group in sorted(decision_groups.items())},
        "notes": ["Normal hand-return intervals assume independent hands and a frozen policy/opponent.",
                  "Counts and payoff partitions are descriptive; conditional outcomes do not estimate action value.",
                  "Missing-outcome bounds bound the sample mean, not the population rate or a confidence interval.",
                  "A clean prefix needs a separately fixed outcome-independent selection rule.",
                  "Position 0 is big blind and position 1 small blind in this HU interface."]}
    if top_hands:
        output["largest_losses"] = [x[2] for x in sorted(losses, reverse=True)]
        output["largest_wins"] = [x[2] for x in sorted(wins, reverse=True)]
    output["server_baseline"] = baseline_summary(values, raw_with_baseline, baseline_values, baseline_mode)
    recorded_semantics = metadata.get("baseline_winnings_semantics")
    output["server_baseline"]["recorded_semantics"] = recorded_semantics
    if baseline_values and baseline_mode == "already_adjusted" and \
            recorded_semantics != "already_adjusted_hero_minus_slumbot_selfplay":
        raise ValueError("already_adjusted requires metadata baseline_winnings_semantics="
                         "already_adjusted_hero_minus_slumbot_selfplay")
    if baseline_values and baseline_mode == "raw_reference" and \
            recorded_semantics == "already_adjusted_hero_minus_slumbot_selfplay":
        raise ValueError("Recorded server field is already adjusted; refusing to subtract it again")
    return output


def baseline_summary(all_raw: list[float], paired_raw: list[float], recorded: list[float], mode: str) -> dict:
    if mode not in ("recorded_only", "raw_reference", "already_adjusted"):
        raise ValueError(f"Unsupported baseline mode {mode}")
    n = len(recorded)
    result = {"mode": mode, "coverage_hands": n, "coverage_fraction": n / len(all_raw) if all_raw else None,
              "complete_coverage": n == len(all_raw) and bool(n),
              "recorded_baseline_estimate": estimate(recorded),
              "raw_on_same_hands": estimate(paired_raw),
              "raw_recorded_correlation": None,
              "server_relative_estimate": None,
              "interpretation": "Recorded field only; no assumption about whether it is already adjusted."}
    if n > 1 and statistics.pstdev(recorded) and statistics.pstdev(paired_raw):
        result["raw_recorded_correlation"] = statistics.correlation(paired_raw, recorded)
    if mode != "recorded_only":
        adjusted = [raw - ref for raw, ref in zip(paired_raw, recorded)] if mode == "raw_reference" else recorded
        stats = estimate(adjusted)
        result["server_relative_estimate"] = stats
        result["interpretation"] = (
            "raw_reference explicitly selected: hero winnings minus recorded reference winnings, fixed coefficient 1. "
            "Interpret as the same absolute win rate only if the server reference is verified to have mean zero under this evaluation."
            if mode == "raw_reference" else
            "already_adjusted explicitly selected: use recorded baseline field directly, without subtracting it again. "
            "Its estimand follows the verified server semantics.")
        raw_se, adjusted_se = result["raw_on_same_hands"]["se_bb100"], stats["se_bb100"]
        result["variance_ratio_raw_over_adjusted"] = (raw_se / adjusted_se) ** 2 \
            if raw_se is not None and adjusted_se else None
        result["no_fit_on_evaluation_data"] = True
    return result


def trace_problems(decision: dict) -> list[str]:
    """Only mechanical trace checks; abstract-action membership is not legality."""
    problems = []
    name = move_name(decision)
    call = decision.get("call_chips", 0)
    native = decision.get("native_action")
    if name == "fold" and call == 0:
        problems.append("fold_when_check_available")
    if name == "check" and call > 0:
        problems.append("check_while_facing_call")
    if native is not None:
        expected = "f" if native < 0 else f"b{native}" if native else "c" if call > 0 else "k"
        if decision.get("sent") != expected:
            problems.append("sent_native_action_mismatch")
    if name == "raise" and native is not None:
        maximum = decision.get("stack_chips", 0) + decision.get("street_commitment_chips", 0)
        minimum = decision.get("high_bet_chips", 0) + decision.get("full_raise_increment_chips", 100)
        if native > maximum:
            problems.append("raise_above_available_stack")
        if native < minimum and native != maximum:
            problems.append("raise_below_minimum_without_allin")
    return problems


def compare_logs(control_path: Path, candidate_path: Path, prefix_hands: int | None = None,
                 phase: str = "development", minimum_gain: float = 0,
                 baseline_mode: str = "recorded_only", metric: str = "raw",
                 selection: dict | None = None) -> dict:
    control = parse_log(control_path, prefix_hands, details=False, baseline_mode=baseline_mode)
    candidate = parse_log(candidate_path, prefix_hands, details=False, baseline_mode=baseline_mode)
    blockers = []
    for label, result in (("control", control), ("candidate", candidate)):
        if not result["window"]["clean"]:
            blockers.append(f"{label} window is not clean")
        if result["audit"]["integrity_errors"]:
            blockers.append(f"{label} integrity errors")
    for key in ("opponent", "starting_bb"):
        if control["metadata"].get(key) != candidate["metadata"].get(key):
            blockers.append(f"different {key}")
    if control["sha256"] == candidate["sha256"]:
        blockers.append("identical logs are not independent arms")
    raw_diff = difference(candidate["estimate"], control["estimate"])
    adjusted_diff = None
    if baseline_mode != "recorded_only" and all(result["server_baseline"]["coverage_hands"] > 1
                                                 for result in (control, candidate)):
        adjusted_diff = difference(candidate["server_baseline"]["server_relative_estimate"],
                                   control["server_baseline"]["server_relative_estimate"])
        adjusted_diff["method"] = "independent hand-level server-relative returns; fixed adjustment; approximate normal 95% CI"
    if metric == "server_relative":
        if adjusted_diff is None:
            raise ValueError("server_relative metric requires explicit baseline semantics and two paired records per arm")
        for label, result in (("control", control), ("candidate", candidate)):
            if not result["server_baseline"]["complete_coverage"]:
                blockers.append(f"{label} lacks full baseline coverage")
    if selection is not None:
        for label, result in (("control", control), ("candidate", candidate)):
            if result["metadata"].get("library_sha256") != selection.get(f"{label}_library_sha256"):
                blockers.append(f"{label} library does not match frozen selection")
            if result["metadata"].get("seed") != selection.get(f"confirmation_{label}_seed"):
                blockers.append(f"{label} seed does not match frozen selection")
            if result["estimate"]["hands"] != selection.get("confirmation_hands_per_arm"):
                blockers.append(f"{label} count does not match frozen selection")
            if result["metadata"].get("equity_samples", 0) != selection.get(f"{label}_equity_samples", 0):
                blockers.append(f"{label} sample override does not match frozen selection")
            if result["metadata"].get("style") != selection.get(f"{label}_style", 7):
                blockers.append(f"{label} policy ID does not match frozen selection")
        if prefix_hands is not None and selection.get("window", {}).get("mode") != "first_completed_prefix":
            blockers.append("prefix window was not frozen in selection")
        if minimum_gain != selection.get("minimum_gain_bb100", 0):
            blockers.append("minimum gain differs from frozen selection")
        if selection.get("primary_metric", "raw") != metric:
            blockers.append("primary metric differs from frozen selection")
        if selection.get("baseline_mode", "recorded_only") != baseline_mode:
            blockers.append("baseline semantics differ from frozen selection")
    if phase == "confirmation" and selection is None:
        blockers.append("no frozen selection manifest supplied; confirmatory provenance unverified")
    diff = adjusted_diff if metric == "server_relative" else raw_diff
    low, high = diff["ci95_bb100"]
    decision = "not_eligible" if blockers else "improvement_supported" if low > minimum_gain else \
        "target_gain_ruled_out_at_this_precision" if high < minimum_gain else "unresolved"
    return {"phase": phase, "control": control, "candidate": candidate,
            "difference": diff, "raw_difference": raw_diff, "server_relative_difference": adjusted_diff,
            "primary_metric": metric, "minimum_gain_bb100": minimum_gain,
            "assessment": decision, "eligibility_blockers": blockers,
            "interpretation": "Development comparisons are exploratory and cannot confirm a selected winner."
            if phase == "development" else
            "Confirmation requires policies, sample size, seeds, selection rule and window rule frozen before these outcomes.",
            "pooling": "This command never pools development and confirmation hands."}


def compare_blocks(control_path: Path, candidate_path: Path) -> dict:
    import csv
    def read(path):
        with path.open(newline="") as handle:
            records = list(csv.DictReader(handle))
        ids = [row["block"] for row in records]
        if len(ids) != len(set(ids)):
            raise ValueError(f"duplicate block ids in {path}")
        return {row["block"]: float(row["mean_bb"]) for row in records}
    a, b = read(control_path), read(candidate_path)
    if a.keys() != b.keys():
        raise ValueError("Paired local comparisons require exactly matching block IDs")
    diffs = [b[key] - a[key] for key in a]
    stats = estimate(diffs)
    stats["independent_blocks"] = stats.pop("hands")
    stats["gain_bb100"] = stats.pop("bb100")
    stats.pop("net_bb")
    return {"control_sha256": fingerprint(control_path), "candidate_sha256": fingerprint(candidate_path),
            "difference": stats, "all_blocks_identical": all(value == 0 for value in diffs),
            "method": "paired independent duplicate-deal block means; session means if history enabled",
            "required_assumptions": "Matching IDs alone do not prove pairing: freeze same seed, deal order, opponent field and session settings."}


def development_diagnostic(path: Path, top_hands: int = 12) -> dict:
    result = parse_log(path, details=True, top_hands=top_hands)
    pockets = defaultdict(lambda: {"decisions": 0, "actions": Counter(), "faced_bet": 0, "folds_facing": 0})
    risky_folds = []
    # Read visible decision cards only for action buckets; terminal revelations
    # remain in loss examples and are never fed back into a policy decision.
    with path.open() as handle:
        for raw in handle:
            try:
                record = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if "winnings_chips" not in record:
                continue
            for i, decision in enumerate(record.get("decisions", [])):
                hole = decision.get("hole_cards", [])
                if len(hole) != 2:
                    continue
                ranks = "23456789TJQKA"
                ordered = sorted((ranks.index(card[0]) for card in hole), reverse=True)
                label = "".join(ranks[r] for r in ordered)
                if ordered[0] != ordered[1]:
                    label += "s" if hole[0][1] == hole[1][1] else "o"
                if decision.get("street") == 0:
                    key = f"{decision.get('client_pos')}/{label}"
                    bucket = pockets[key]
                    bucket["decisions"] += 1
                    bucket["actions"][move_name(decision)] += 1
                    bucket["faced_bet"] += decision.get("call_chips", 0) > 0
                    bucket["folds_facing"] += move_name(decision) == "fold" and decision.get("call_chips", 0) > 0
                if move_name(decision) == "fold" and decision.get("call_chips", 0) > 0:
                    # This list prioritizes expensive decisions for review. A large
                    # pot or low pot odds does not establish a strategic mistake.
                    call = decision["call_chips"]
                    pot = decision.get("pot_chips", 0)
                    item = {"hand": record.get("hand"), "decision_index": i,
                            "hand_net_bb": record["winnings_chips"] / 100,
                            "call_pot_odds": call / (pot + call),
                            "pot_bb": pot / 100, "decision": decision}
                    heapq.heappush(risky_folds, (pot, record.get("hand", 0), i, item))
                    if len(risky_folds) > top_hands:
                        heapq.heappop(risky_folds)
    result["phase"] = "development_diagnostic"
    result["preflop_holding_actions"] = {key: {**value, "actions": dict(value["actions"])}
                                            for key, value in sorted(pockets.items())}
    result["largest_pot_folds_for_review"] = [item[3] for item in sorted(risky_folds, reverse=True)]
    result["diagnostic_warning"] = (
        "Exploratory development data only. Largest losses and conditional fold rates are diagnostic prompts, "
        "not causal action values. Do not infer overfolding from an aggregate fold rate. Pot odds are "
        "call/(current pot+call); showdown equity alone omits future betting and range conditioning.")
    return result


def selection_record(args) -> dict:
    result = {"schema": 1, "phase": "selection", "candidate_name": args.name,
              "candidate_library": str(args.candidate_library.resolve()),
              "candidate_library_sha256": fingerprint(args.candidate_library),
              "control_library": str(args.control_library.resolve()),
              "control_library_sha256": fingerprint(args.control_library),
              "control_style": args.control_style, "candidate_style": args.candidate_style,
              "control_equity_samples": args.control_samples,
              "candidate_equity_samples": args.candidate_samples,
              "reason": args.reason, "confirmation_hands_per_arm": args.hands,
              "confirmation_control_seed": args.control_seed,
              "confirmation_candidate_seed": args.candidate_seed,
              "primary_endpoint": "candidate minus frozen incumbent BB/100 on fresh Slumbot hands",
              "minimum_gain_bb100": args.minimum_gain,
              "primary_metric": args.metric, "baseline_mode": args.baseline_mode,
              "window": {"mode": "all_completed", "target_hands_per_arm": args.hands},
              "window_rule": "All completed hands to the fixed target; no interrupted outcomes. If interrupted, preserve the complete log and mark the result incomplete. Any rerun uses a new seed fixed before outcomes.",
              "strategy_tuning_after_selection": False,
              "development_logs": [{"path": str(path.resolve()), "sha256": fingerprint(path)}
                                    for path in args.development_log]}
    from datetime import datetime, timezone
    result["created_at_utc"] = datetime.now(timezone.utc).isoformat()
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    one = sub.add_parser("summarize", help="Score completed hands, audit stops, and report action counts")
    one.add_argument("log", type=Path)
    one.add_argument("--prefix-hands", type=int)
    one.add_argument("--compact", action="store_true", help="omit decision bucket calculation")
    pair = sub.add_parser("compare", help="Independent live comparison; never pools logs")
    pair.add_argument("--control", type=Path, required=True)
    pair.add_argument("--candidate", type=Path, required=True)
    pair.add_argument("--prefix-hands", type=int)
    pair.add_argument("--phase", choices=("development", "confirmation"), default="development")
    pair.add_argument("--minimum-gain", type=float, default=0)
    pair.add_argument("--selection", type=Path, help="Frozen manifest; needed for confirmatory eligibility")
    pair.add_argument("--metric", choices=("raw", "server_relative"), default="raw")
    local = sub.add_parser("compare-blocks", help="Pair local CSV duplicate-deal or session block means")
    local.add_argument("--control", type=Path, required=True)
    local.add_argument("--candidate", type=Path, required=True)
    diag = sub.add_parser("diagnose", help="Exploratory state/action diagnostics; development logs ONLY")
    diag.add_argument("--development-log", type=Path, required=True)
    diag.add_argument("--top-hands", type=int, default=12)
    freeze = sub.add_parser("freeze-selection", help="Create a candidate/control manifest before confirmation")
    freeze.add_argument("--name", required=True)
    freeze.add_argument("--candidate-library", type=Path, required=True)
    freeze.add_argument("--control-library", type=Path, required=True)
    freeze.add_argument("--reason", required=True)
    freeze.add_argument("--hands", type=int, required=True)
    freeze.add_argument("--control-seed", type=int, required=True)
    freeze.add_argument("--candidate-seed", type=int, required=True)
    freeze.add_argument("--minimum-gain", type=float, default=0)
    freeze.add_argument("--control-style", type=int, default=7)
    freeze.add_argument("--candidate-style", type=int, default=7)
    freeze.add_argument("--control-samples", type=int, choices=(0, 32, 64, 128), default=0)
    freeze.add_argument("--candidate-samples", type=int, choices=(0, 32, 64, 128), default=0)
    freeze.add_argument("--metric", choices=("raw", "server_relative"), default="raw")
    freeze.add_argument("--development-log", type=Path, action="append", default=[])
    for command in (one, pair, local, diag, freeze):
        command.add_argument("--output", type=Path, help="Create JSON output; refuses to overwrite an existing file")
    for command in (one, pair, freeze):
        command.add_argument("--baseline-mode", choices=("recorded_only", "raw_reference", "already_adjusted"),
                             default="recorded_only", help="Set only after verifying server semantics")
    args = parser.parse_args(argv)
    try:
        if args.command == "summarize":
            result = parse_log(args.log, args.prefix_hands, not args.compact, baseline_mode=args.baseline_mode)
        elif args.command == "compare":
            selection = json.loads(args.selection.read_text()) if args.selection else None
            result = compare_logs(args.control, args.candidate, args.prefix_hands, args.phase, args.minimum_gain,
                                  args.baseline_mode, args.metric, selection)
        elif args.command == "compare-blocks":
            result = compare_blocks(args.control, args.candidate)
        elif args.command == "diagnose":
            result = development_diagnostic(args.development_log, args.top_hands)
        else:
            if args.hands < 2 or args.control_seed == args.candidate_seed:
                raise ValueError("Use at least two hands and distinct fresh control/candidate seeds")
            if args.metric == "server_relative" and args.baseline_mode == "recorded_only":
                raise ValueError("Select explicit verified baseline semantics for a server-relative primary metric")
            result = selection_record(args)
        content = json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + "\n"
        if args.output:
            with args.output.open("x") as handle:
                handle.write(content)
            print(args.output.resolve())
        else:
            sys.stdout.write(content)
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(f"audit error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
