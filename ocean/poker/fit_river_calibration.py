#!/usr/bin/env python3
"""Fit one frozen12-coefficient response model; score separate held-out hands.

No live requests, policy changes, hyperparameter search, or outcome winnings.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path

import numpy as np

NAMES = ("fold", "passive", "aggressive")
EPSILON = 0.001
CLIP = 1e-6
RIDGE = 2.0


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_new(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


def read_rows(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    if not rows or any(row["label"] not in (0, 1, 2) for row in rows):
        raise ValueError("Expected finite labeled response rows")
    for row in rows:
        token, call, label = row["observed_action"], row["call_chips"], row["label"]
        if (label == 0 and call == 0) or (label == 2 and row["proxy_raise_amount"] == 0):
            raise ValueError("A training/scoring label violates the legal action mask")
        if (token == "k" and call != 0) or (token == "c" and call == 0):
            raise ValueError("Check/call label differs from the public opportunity")
    return rows


def features(row, q=None):
    q = row["q_exact"] if q is None else q
    z = 2 * q - 1
    t = 4 * row["price"] - 1
    s = math.log1p(min(row["opponent_stack_chips"], row["hero_stack_chips"])
                  / row["pot_chips"]) / math.log(101)
    s = min(1.0, max(0.0, s))
    return {"fold": [1., z, t], "raise": [1., z, z * z, t, s],
            "bet": [1., z, z * z, s]}


def gate_rows(rows, gate):
    if gate == "fold":
        return [r for r in rows if r["call_chips"] > 0]
    if gate == "raise":
        return [r for r in rows if r["call_chips"] > 0 and r["label"] != 0 and r["proxy_raise_amount"]]
    return [r for r in rows if r["call_chips"] == 0 and r["proxy_raise_amount"]]


def sigmoid(value):
    value = np.asarray(value, dtype=np.float64)
    return np.exp(-np.logaddexp(0, -value))


def objective(beta, x, y):
    eta = x @ beta
    return float(np.logaddexp(0, eta).sum() - y @ eta
                 + RIDGE / 2 * (beta[1:] @ beta[1:])
                 + np.logaddexp(0, beta[0]) - .5 * beta[0])


def fit_gate(rows, gate):
    selected = gate_rows(rows, gate)
    if len(selected) < 20:
        raise ValueError(f"Insufficient TRAIN opportunities for the frozen {gate} gate")
    x = np.asarray([features(r)[gate] for r in selected], dtype=np.float64)
    y = np.asarray([r["label"] == (0 if gate == "fold" else 2) for r in selected], dtype=np.float64)
    beta = np.zeros(x.shape[1])
    constant = (float(y.sum()) + .5) / (len(y) + 1)
    beta[0] = math.log(constant / (1 - constant))
    converged = False
    for iteration in range(100):
        p = sigmoid(x @ beta)
        penalty = np.r_[0., np.repeat(RIDGE, len(beta) - 1)]
        intercept_p = float(sigmoid(beta[0]))
        gradient = x.T @ (p - y) + penalty * beta
        gradient[0] += intercept_p - .5
        hessian = x.T @ ((p * (1 - p))[:, None] * x) + np.diag(penalty)
        hessian[0, 0] += intercept_p * (1 - intercept_p)
        if np.max(np.abs(gradient)) < 1e-9:
            converged = True
            break
        step = np.linalg.solve(hessian, gradient)
        start, scale = objective(beta, x, y), 1.0
        directional = float(gradient @ step)
        while scale > 2 ** -40:
            proposed = beta - scale * step
            if objective(proposed, x, y) <= start - 1e-4 * scale * directional:
                break
            scale /= 2
        delta = scale * step
        beta -= delta
        if np.max(np.abs(delta)) < 1e-10 * (1 + np.max(np.abs(beta))):
            converged = True
            break
    if not converged or not np.isfinite(beta).all():
        raise ValueError(f"Frozen {gate} fit did not converge")
    p = sigmoid(x @ beta)
    gradient = x.T @ (p - y) + np.r_[0., RIDGE * beta[1:]]
    gradient[0] += float(sigmoid(beta[0])) - .5
    return {"features": {"fold": ["1", "z", "t"],
                          "raise": ["1", "z", "z2", "t", "s"],
                          "bet": ["1", "z", "z2", "s"]}[gate],
            "coefficients": [float(x) for x in beta], "training_opportunities": len(y),
            "training_positive_labels": int(y.sum()), "training_constant_probability": constant,
            "objective": objective(beta, x, y), "gradient_max_abs": float(np.max(np.abs(gradient))),
            "iterations": iteration + 1, "converged": converged}


def fit(args):
    plan = json.loads(args.plan.read_text())
    if plan["regularization"]["ridge_lambda_nonintercept"] != RIDGE or plan["candidate_models_planned"] != 1:
        raise ValueError("Fit implementation and frozen model specification differ")
    rows = read_rows(args.data)
    if any(r["split"] != "train" for r in rows):
        raise ValueError("Only TRAIN rows may fit coefficients or constants")
    model = {"schema": 1, "phase": "frozen_train_fit", "model_version": "river_response_calibration_v1",
             "plan_sha256": sha(args.plan), "data_sha256": sha(args.data),
             "split_manifest_sha256": plan["split_manifest_sha256"], "fit_source_sha256": sha(Path(__file__)),
             "training_responses": len(rows), "training_hands": len({(r["job"], r["hand"]) for r in rows}),
             "gates": {gate: fit_gate(rows, gate) for gate in ("fold", "raise", "bet")},
             "runtime_scope": plan["runtime_scope"], "feature_transforms": plan["feature_transforms"],
             "probability_clip": [CLIP, 1 - CLIP], "validation_or_test_used_in_fit": False,
             "train_counts_by_arm": dict(Counter(r["arm"] for r in rows))}
    write_new(args.output, model)
    print(json.dumps(model, indent=2))


def predict(row, model, q=None, constant=False):
    values = features(row, q)
    def probability(gate):
        fit = model["gates"][gate]
        p = fit["training_constant_probability"] if constant else float(sigmoid(
            np.dot(values[gate], fit["coefficients"])))
        return min(1 - CLIP, max(CLIP, p))
    if row["call_chips"] > 0:
        fold = probability("fold")
        raise_ = probability("raise") if row["proxy_raise_amount"] else 0
        return np.asarray([fold, (1 - fold) * (1 - raise_), (1 - fold) * raise_])
    bet = probability("bet") if row["proxy_raise_amount"] else 0
    return np.asarray([0., 1 - bet, bet])


def old_predict(row, q):
    call, eligible = row["call_chips"], row["eligible_chips"]
    fold = float(call > 0 and q * eligible < call)
    aggressive = 0.
    if not fold and row["proxy_raise_amount"]:
        if q > (.78 if call else .58):
            aggressive = .85
        elif not call and q > .25:
            aggressive = .045
    return np.asarray([fold, 1 - fold - aggressive, aggressive])


def loss(probability, label):
    truth = np.eye(3)[label]
    return {"logloss": -math.log((probability[label] + EPSILON) / (1 + 3 * EPSILON)),
            "brier": float(np.sum((probability - truth) ** 2))}


def cluster_mean(values, rows):
    mean = float(np.mean(values))
    clusters = defaultdict(float)
    for value, row in zip(values, rows):
        clusters[(row["job"], row["hand"])] += float(value) - mean
    g, n = len(clusters), len(rows)
    se = math.sqrt(g / (g - 1) * math.fsum(value * value for value in clusters.values())) / n if g > 1 else None
    return {"mean": mean, "clustered_se": se,
            "ci95": [mean - 1.96 * se, mean + 1.96 * se] if se is not None else None}


def score(args):
    model = json.loads(args.model.read_text())
    if model.get("phase") != "frozen_train_fit":
        raise ValueError("A frozen TRAIN model is required")
    rows = read_rows(args.data)
    partitions = {r["split"] for r in rows}
    if len(partitions) != 1 or not partitions.issubset({"validation", "test"}):
        raise ValueError("Score one untouched held-out partition at a time")
    scored = []
    for row in rows:
        new_replicas = [predict(row, model, q) for q in row["mc_q_replicas"]]
        old_replicas = [old_predict(row, q) for q in row["mc_q_replicas"]]
        if not np.allclose(np.mean(old_replicas, axis=0), row["mc_probabilities"], rtol=0, atol=1e-12):
            raise ValueError("Native/Python old-proxy Monte Carlo mismatch")
        probabilities = {"calibrated_exact": predict(row, model),
                         "calibrated_mc": np.mean(new_replicas, axis=0),
                         "old_exact": np.asarray(row["exact_probabilities"]),
                         "old_mc": np.asarray(row["mc_probabilities"]),
                         "context_constant": predict(row, model, constant=True)}
        result = {"row": row, "probabilities": probabilities,
                  "losses": {name: loss(p, row["label"]) for name, p in probabilities.items()},
                  "expected_replica_losses": {"calibrated": {key: float(np.mean([
                      loss(p, row["label"])[key] for p in new_replicas])) for key in ("logloss", "brier")},
                      "old": {key: float(np.mean([loss(p, row["label"])[key] for p in old_replicas]))
                              for key in ("logloss", "brier")}}}
        scored.append(result)
    grouped = {"all": scored}
    for arm in ("control", "candidate"):
        grouped[arm] = [s for s in scored if s["row"]["arm"] == arm]
    for label, condition in (("facing_bet", lambda r: r["call_chips"] > 0),
                             ("after_check", lambda r: r["call_chips"] == 0),
                             ("facing_all_in", lambda r: r["call_chips"] > 0 and not r["proxy_raise_amount"])):
        grouped[label] = [s for s in scored if condition(s["row"])]
    metrics = {}
    for group, selected in grouped.items():
        if not selected:
            metrics[group] = {"responses": 0}
            continue
        selected_rows = [s["row"] for s in selected]
        result = {"responses": len(selected), "hands": len({(r["job"], r["hand"]) for r in selected_rows}),
                  "losses": {}, "paired_differences": {}, "calibration": {}, "expected_replica_losses": {}}
        for name in selected[0]["losses"]:
            result["losses"][name] = {key: float(np.mean([s["losses"][name][key] for s in selected]))
                                      for key in ("logloss", "brier")}
            result["calibration"][name] = {NAMES[k]: {"observed": float(np.mean([r["label"] == k for r in selected_rows])),
                "predicted": float(np.mean([s["probabilities"][name][k] for s in selected])),
                "gap": cluster_mean([(s["row"]["label"] == k) - s["probabilities"][name][k] for s in selected], selected_rows)}
                for k in range(3)}
        for name, reference in (("calibrated_mc", "old_mc"), ("calibrated_exact", "old_exact"),
                                ("calibrated_mc", "context_constant"), ("calibrated_exact", "context_constant")):
            result["paired_differences"][name + "_minus_" + reference] = {key: cluster_mean(
                [s["losses"][name][key] - s["losses"][reference][key] for s in selected], selected_rows)
                for key in ("logloss", "brier")}
        for name in ("calibrated", "old"):
            result["expected_replica_losses"][name] = {key: float(np.mean([
                s["expected_replica_losses"][name][key] for s in selected])) for key in ("logloss", "brier")}
        result["expected_replica_paired_difference"] = {key: cluster_mean([
            s["expected_replica_losses"]["calibrated"][key] - s["expected_replica_losses"]["old"][key]
            for s in selected], selected_rows) for key in ("logloss", "brier")}
        metrics[group] = result
    criteria = {}
    for mode in ("mc_single_draw", "exact"):
        def comparison(group):
            if mode == "mc_single_draw":
                return metrics[group]["expected_replica_paired_difference"]
            return metrics[group]["paired_differences"]["calibrated_exact_minus_old_exact"]
        primary = comparison("all")
        criteria[f"{mode}_logloss_ci_upper_negative"] = primary["logloss"]["ci95"][1] < 0
        criteria[f"{mode}_pooled_brier_decreases"] = primary["brier"]["mean"] < 0
        for arm in ("control", "candidate"):
            for key in ("logloss", "brier"):
                criteria[f"{mode}_{arm}_{key}_does_not_regress"] = comparison(arm)[key]["mean"] <= 0
    answer = {"schema": 1, "partition": next(iter(partitions)), "model_sha256": sha(args.model),
              "data_sha256": sha(args.data), "code_sha256": sha(Path(__file__)),
              "parameters_refit": False, "winnings_used": False, "test_data_used": partitions == {"test"},
              "metrics": metrics, "frozen_validation_criteria": criteria,
              "passes_frozen_validation_gate": all(criteria.values()),
              "primary": "Mean per-replica loss across 16 independent 128-reference draws, with whole-hand clustered paired differences; exact-q deployment has additional identical gates; loss of mixture probability is diagnostic only",
              "score": "Proper3-class uniform-smoothed logloss epsilon0.001, and sum-of-three-classes Brier; paired differences clustered by whole hand",
              "limits": "Predictive improvement in a historical public-state distribution does not establish poker value or calibrate unsupported counterfactual bet sizes/deeper raise branches"}
    write_new(args.output, answer)
    print(json.dumps({"partition": answer["partition"], "criteria": criteria,
                     "all": metrics["all"]["losses"], "differences": metrics["all"]["paired_differences"]}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    fit_parser = sub.add_parser("fit")
    fit_parser.add_argument("--plan", type=Path, required=True)
    fit_parser.add_argument("--data", type=Path, required=True)
    fit_parser.add_argument("--output", type=Path, required=True)
    score_parser = sub.add_parser("score")
    score_parser.add_argument("--model", type=Path, required=True)
    score_parser.add_argument("--data", type=Path, required=True)
    score_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    (fit if args.operation == "fit" else score)(args)


if __name__ == "__main__":
    main()
