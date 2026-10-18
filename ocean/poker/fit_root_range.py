#!/usr/bin/env python3
"""One fixed scalar root-range fit and its frozen nine predictive gates."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np

from export_ranges import PLAN_SHA, PAIRS, check_plan, sha, validate_choice, write_json

HERE = Path(__file__).resolve().parent
LAMBDA = 2.


def load_data(path):
    manifest = json.loads((path / "manifest.json").read_text())
    if manifest["plan_sha256"] != PLAN_SHA:
        raise ValueError("Dataset comes from a different plan")
    if sha(path / "rows.json") != manifest["rows_sha256"] or sha(path / "distributions.npz") != manifest["distributions_sha256"]:
        raise ValueError("Dataset content hashes differ")
    if manifest["exporter_sha256"] != sha(HERE / "export_ranges.py"):
        raise ValueError("Dataset exporter differs from the frozen source")
    rows = json.loads((path / "rows.json").read_text())
    with np.load(path / "distributions.npz", allow_pickle=False) as value:
        weights, targets = value["weights"], value["targets"]
        shares, ranks = value["shares"], value["ranks"]
    n = len(rows)
    if n < 1 or weights.shape != (n, PAIRS) or shares.shape != weights.shape or ranks.shape != weights.shape or targets.shape != (n,):
        raise ValueError("Invalid distribution dimensions")
    if weights.dtype != np.float64 or not np.isfinite(weights).all() or not (weights > 0).all():
        raise ValueError("Weights must be positive finite float64")
    if not np.issubdtype(targets.dtype, np.integer) or not ((targets >= 0) & (targets < PAIRS)).all():
        raise ValueError("Invalid revealed target indices")
    if not np.isin(shares, [0., .5, 1.]).all() or not ((ranks >> 24) <= 8).all():
        raise ValueError("Invalid deterministic showdown labels")
    identities = [(r["job"], r["identity"]) for r in rows]
    if identities != sorted(identities) or len(set(identities)) != n:
        raise ValueError("Rows must be canonical and unique by whole hand")
    if any(r["partition"] != manifest["partition"] or r["arm"] not in ("control", "candidate") for r in rows):
        raise ValueError("Dataset partition or arm is invalid")
    if manifest["coverage"]["eligible_states"] != n:
        raise ValueError("Cohort manifest and distributions disagree")
    return manifest, rows, weights, targets.astype(np.int64), shares


class Objective:
    def __init__(self, weights, targets):
        self.logw = np.log(weights)
        self.logw -= self.logw.max(axis=1, keepdims=True)
        self.target = self.logw[np.arange(len(targets)), targets]

    def value(self, alpha):
        if not 0 <= alpha <= 1 or not math.isfinite(alpha):
            raise ValueError("alpha must lie in[0,1]")
        mass = np.exp(alpha * self.logw)
        totals = mass.sum(axis=1, dtype=np.float64)
        probability = mass / totals[:, None]
        loss = np.log(totals) - alpha * self.target
        meanlog = (probability * self.logw).sum(axis=1, dtype=np.float64)
        objective = math.fsum(map(float, loss)) + (LAMBDA / 2) * (alpha - 1) ** 2
        derivative = math.fsum(map(float, meanlog - self.target)) + LAMBDA * (alpha - 1)
        return objective, derivative

    def fit(self):
        lower, upper = self.value(0.), self.value(1.)
        lo, hi, steps = 0., 1., 0
        if lower[1] >= 0:
            alpha, kind = 0., "lower_endpoint"
        elif upper[1] <= 0:
            alpha, kind = 1., "upper_endpoint"
        else:
            kind = "interior"
            for _ in range(60):
                mid = (lo + hi) / 2
                if self.value(mid)[1] <= 0:
                    lo = mid
                else:
                    hi = mid
                steps += 1
            alpha = (lo + hi) / 2
        chosen = self.value(alpha)
        tolerance = 1e-10 * max(1., abs(lower[0]), abs(upper[0]))
        if chosen[0] > min(lower[0], upper[0]) + tolerance:
            raise ValueError("Selected objective is worse than an endpoint")
        if kind == "interior" and not (self.value(lo)[1] <= 0 and self.value(hi)[1] >= 0):
            raise ValueError("Invalid convex derivative bracket")
        return alpha, {"optimizer": "fixed_endpoint_checks_then_60_derivative_bisections",
            "solution": kind, "updates": steps, "final_bracket": [lo, hi],
            "objective_at_zero": lower[0], "derivative_at_zero": lower[1],
            "objective_at_one": upper[0], "derivative_at_one": upper[1],
            "selected_objective": chosen[0], "selected_derivative": chosen[1],
            "endpoint_objective_tolerance": tolerance}


def predictions(weights, targets, shares, alpha):
    if alpha == 0:
        probabilities = np.full_like(weights, 1. / PAIRS)
        loss = np.full(len(weights), math.log(PAIRS), dtype=np.float64)
        predicted_share = shares.mean(axis=1, dtype=np.float64)
    else:
        logw = np.log(weights)
        logw -= logw.max(axis=1, keepdims=True)
        mass = np.exp(alpha * logw)
        totals = mass.sum(axis=1, dtype=np.float64)
        probabilities = mass / totals[:, None]
        loss = np.log(totals) - alpha * logw[np.arange(len(targets)), targets]
        predicted_share = (probabilities * shares).sum(axis=1, dtype=np.float64)
    actual = shares[np.arange(len(targets)), targets]
    return {"pair_nll": loss, "share_mse": (predicted_share - actual) ** 2,
        "observed_minus_predicted_share": actual - predicted_share,
        "predicted_share": predicted_share, "actual_share": actual,
        "entropy": -(probabilities * np.log(probabilities)).sum(axis=1, dtype=np.float64),
        "effective_pair_support": 1 / (probabilities ** 2).sum(axis=1, dtype=np.float64)}


def interval(values):
    values = list(map(float, values))
    if len(values) < 2 or not all(math.isfinite(x) for x in values):
        return {"n": len(values), "mean": None, "se": None, "ci95": None, "eligible": False}
    mean = math.fsum(values) / len(values)
    se = math.sqrt(math.fsum((x - mean) ** 2 for x in values) / (len(values) * (len(values) - 1)))
    return {"n": len(values), "mean": mean, "se": se,
            "ci95": [mean - 1.96 * se, mean + 1.96 * se], "eligible": True}


def nine_gates(groups):
    gates = {}
    for comparator in ("original", "uniform"):
        score = groups["all"]["deltas"][f"fitted_minus_{comparator}_pair_nll"]
        gates[f"pooled_nll_vs_{comparator}_upper95_negative"] = score["eligible"] and score["ci95"][1] < 0
    score = groups["all"]["deltas"]["fitted_minus_original_share_mse"]
    gates["pooled_share_mse_vs_original_mean_negative"] = score["eligible"] and score["mean"] < 0
    for arm in ("control", "candidate"):
        for name in ("fitted_minus_original_pair_nll", "fitted_minus_uniform_pair_nll", "fitted_minus_original_share_mse"):
            score = groups[arm]["deltas"][name]
            gates[f"{arm}_{name}_mean_nonpositive"] = score["eligible"] and score["mean"] <= 0
    assert len(gates) == 9
    return gates


def score_data(rows, weights, targets, shares, alpha):
    cache = {a: predictions(weights, targets, shares, a) for a in {0., 1., alpha}}
    models = {"fitted": cache[alpha], "original": cache[1.], "uniform": cache[0.]}
    selectors = {"all": list(range(len(rows))), "control": [], "candidate": [], "facing_bet": [], "free_action": []}
    for i, row in enumerate(rows):
        selectors[row["arm"]].append(i)
        selectors["facing_bet" if row["call_chips"] > 0 else "free_action"].append(i)
    groups = {}
    for group, selected in selectors.items():
        summary = {"hands": len(selected), "models": {}, "deltas": {}}
        for name, pred in models.items():
            summary["models"][name] = {metric: interval(values[selected]) for metric, values in pred.items()}
        for comparator in ("original", "uniform"):
            for metric in ("pair_nll", "share_mse"):
                delta = models["fitted"][metric] - models[comparator][metric]
                summary["deltas"][f"fitted_minus_{comparator}_{metric}"] = interval(delta[selected])
        groups[group] = summary
    gates = nine_gates(groups)
    return {"groups": groups, "nine_gates": gates, "all_nine_passed": all(gates.values()),
        "interval_semantics": "Paired model losses on one first eligible HERO river state per whole hand; approximate normal95% intervals.",
        "playing_strength_claim": False, "winnings_used": False}


def check_freeze(path):
    freeze = json.loads(path.read_text())
    if freeze.get("plan_sha256") != PLAN_SHA:
        raise ValueError("Source freeze is for another plan")
    for name, digest in freeze["files"].items():
        if sha(HERE / name) != digest:
            raise ValueError(f"Frozen source changed: {name}")
    if "fit_root_range.py" not in freeze["files"] or "export_ranges.py" not in freeze["files"]:
        raise ValueError("Fitter and exporter must be frozen together")
    return freeze


def run_fit(args):
    plan = check_plan(args.plan)
    freeze = check_freeze(args.freeze)
    manifest, rows, weights, targets, shares = load_data(args.data)
    if manifest["partition"] != "train" or len(rows) != plan["training"]["expected_eligible_training_states"]:
        raise ValueError("Fit is restricted to the fixed original TRAIN cohort")
    if manifest["native_library_sha256"] != freeze["files"]["native/build/root_range_distribution.so"]:
        raise ValueError("TRAIN exporter library differs from the source freeze")
    if args.output.exists() or args.report.exists():
        raise ValueError("Refusing to replace an existing model or training report")
    alpha, optimization = Objective(weights, targets).fit()
    model = {"schema": 1, "version": "poker_root_range_shrinkage_v1", "alpha": alpha,
        "alpha_roundtrip_decimal": format(alpha, ".17g"), "bounds": [0, 1], "lambda": LAMBDA,
        "plan_sha256": PLAN_SHA, "training_partition": "train", "training_states": len(rows),
        "training_data_manifest_sha256": sha(args.data / "manifest.json"),
        "source_freeze_sha256": sha(args.freeze), "fitter_sha256": sha(Path(__file__)),
        "exporter_sha256": sha(HERE / "export_ranges.py"), "optimization": optimization,
        "runtime": plan["runtime"], "response_reference_and_coefficients_unchanged": True,
        "heldout_labels_used_for_fit": False}
    write_json(args.output, model)
    report = score_data(rows, weights, targets, shares, alpha)
    report.update({"partition": "train", "acceptance_applicable": False,
        "model_sha256": sha(args.output), "plan_sha256": PLAN_SHA,
        "data_manifest_sha256": sha(args.data / "manifest.json"), "optimization": optimization})
    write_json(args.report, report)
    print(json.dumps({"alpha": alpha, "model_sha256": sha(args.output), "training_states": len(rows), "optimization": optimization}, indent=2))


def run_score(args):
    check_plan(args.plan)
    model = json.loads(args.model.read_text())
    if model["plan_sha256"] != PLAN_SHA or model["fitter_sha256"] != sha(Path(__file__)) or model["exporter_sha256"] != sha(HERE / "export_ranges.py"):
        raise ValueError("Held-out scoring sources/model differ from the TRAIN freeze")
    if args.partition == "test":
        if args.choice is None:
            raise ValueError("TEST requires its later exact-choice freeze")
        validate_choice(args.choice, args.model, Path(__file__))
    manifest, rows, weights, targets, shares = load_data(args.data)
    if manifest["partition"] != args.partition or manifest["model_sha256"] != sha(args.model):
        raise ValueError("Held-out dataset belongs to a different partition/model")
    report = score_data(rows, weights, targets, shares, model["alpha"])
    report.update({"partition": args.partition, "acceptance_applicable": True,
        "plan_sha256": PLAN_SHA, "model_sha256": sha(args.model),
        "scorer_sha256": sha(Path(__file__)), "data_manifest_sha256": sha(args.data / "manifest.json"),
        "coverage": manifest["coverage"], "choice_sha256": sha(args.choice) if args.choice else None})
    write_json(args.output, report)
    print(json.dumps({"partition": args.partition, "all_nine_passed": report["all_nine_passed"],
        "nine_gates": report["nine_gates"], "pooled_differences": report["groups"]["all"]["deltas"],
        "report_sha256": sha(args.output)}, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    modes = p.add_subparsers(dest="mode", required=True)
    fit = modes.add_parser("fit")
    fit.add_argument("--data", type=Path, required=True)
    fit.add_argument("--plan", type=Path, required=True)
    fit.add_argument("--freeze", type=Path, required=True)
    fit.add_argument("--output", type=Path, required=True)
    fit.add_argument("--report", type=Path, required=True)
    score = modes.add_parser("score")
    score.add_argument("--partition", choices=("val", "test"), required=True)
    score.add_argument("--data", type=Path, required=True)
    score.add_argument("--plan", type=Path, required=True)
    score.add_argument("--model", type=Path, required=True)
    score.add_argument("--choice", type=Path)
    score.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    (run_fit if args.mode == "fit" else run_score)(args)


if __name__ == "__main__":
    main()
