#!/usr/bin/env python3
"""Fit a candidate-value model from ``counterfactual_dataset.py`` output.

The default ridge backend is intentionally small and dependency-light (NumPy
only) so the data contract can be tested locally.  ``--backend lightgbm`` is
available on machines where LightGBM is installed.  Both backends predict the
same target: candidate minus same-state baseline cash.  This script does not
alter PPO rewards or checkpoints.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
from typing import Any


NUMERIC_CANDIDATE_FIELDS = (
    "candidate_kind_code", "candidate_item_code", "candidate_quantity",
    "candidate_executable", "candidate_plan_steps",
)


def _stable_validation_group(episode_id: str, fraction: float, seed: int) -> bool:
    digest = hashlib.sha256(f"{seed}:{episode_id}".encode("utf-8")).digest()
    value = int.from_bytes(digest[:8], "little") / float(2**64)
    return value < fraction


def _read_rows(path: pathlib.Path) -> tuple[list[dict[str, str]], list[str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames is None:
            raise ValueError(f"missing dataset header: {path}")
        rows = list(reader)
        fields = list(reader.fieldnames)
    if not rows:
        raise ValueError(f"dataset is empty: {path}")
    return rows, fields


def feature_names(fields: list[str]) -> list[str]:
    names = [field for field in fields if field.startswith("feature_")]
    names.extend(field for field in NUMERIC_CANDIDATE_FIELDS if field in fields)
    if not names:
        raise ValueError("dataset has no numeric feature columns")
    return names


def _float(row: dict[str, str], field: str) -> float:
    try:
        return float(row[field])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError(f"row is missing numeric field {field!r}") from error


def _effective_rows(rows: list[dict[str, str]], fields: list[str]) -> tuple[list[dict[str, str]], int]:
    """Drop rejected candidates while retaining the explicit HOLD baseline.

    The writer retains rejected candidates for auditability.  Training on an
    ineffective BUY/SELL would teach the scorer that an impossible action is
    a valid alternative.  HOLD is different: it is deliberately marked
    ineffective because it is the unchanged continuation baseline, but its
    zero target is the reference the live overlay must learn.  Keep HOLD rows
    and drop only rejected non-HOLD candidates.  Older C0 fixtures did not
    have this column, so they remain accepted unchanged.
    """

    if "candidate_effective" not in fields:
        return rows, 0
    effective: list[dict[str, str]] = []
    dropped = 0
    for row in rows:
        # HOLD is an exact zero-delta reference, not an infeasible action.
        # Without these rows a regression/tree model extrapolates a fictitious
        # HOLD value, and the live safety comparison can accept harmful
        # interventions merely because the learned baseline is negative.
        if str(row.get("candidate_kind", "")).upper() == "HOLD":
            effective.append(row)
            continue
        try:
            keep = int(float(row.get("candidate_effective", "0"))) != 0
        except (TypeError, ValueError):
            keep = False
        if keep:
            effective.append(row)
        else:
            dropped += 1
    if not effective:
        raise ValueError("dataset contains no effective candidate rows")
    return effective, dropped


def _split_rows(
    rows: list[dict[str, str]], validation_fraction: float, seed: int,
) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    train: list[dict[str, str]] = []
    validation: list[dict[str, str]] = []
    for row in rows:
        if _stable_validation_group(str(row.get("episode_id", "")), validation_fraction, seed):
            validation.append(row)
        else:
            train.append(row)
    if not train or not validation:
        raise ValueError(
            "episode-level split produced an empty partition; adjust --validation-fraction"
        )
    return train, validation


def _matrix(rows: list[dict[str, str]], names: list[str]):
    import numpy as np

    return np.asarray([[_float(row, name) for name in names] for row in rows], dtype=np.float64)


def _target(rows: list[dict[str, str]], name: str):
    import numpy as np

    return np.asarray([_float(row, name) for row in rows], dtype=np.float64)


def _groups(rows: list[dict[str, str]]) -> list[int]:
    """Return contiguous state-group sizes for a LightGBM ranker."""

    groups: list[int] = []
    current = None
    count = 0
    for row in rows:
        key = str(row.get("state_id", row.get("record_index", "")))
        if current is not None and key != current:
            groups.append(count)
            count = 0
        current = key
        count += 1
    if count:
        groups.append(count)
    return groups


def _rank_labels(rows: list[dict[str, str]], target_name: str):
    """Convert cash advantages to within-state relevance labels.

    LightGBM's LambdaMART objective only needs an ordering.  Keeping the raw
    target in the dataset and metadata preserves the economic meaning while
    replacing each state group's labels with deterministic integer relevance.
    """

    import numpy as np

    # LightGBM's default label_gain table has 31 entries (relevance 0..30).
    # Candidate catalogs are intentionally variable-sized and can exceed 31
    # rows, so map each within-state ordering monotonically into that fixed
    # relevance range instead of emitting an invalid label such as 58.
    labels = np.zeros(len(rows), dtype=np.float64)
    start = 0
    for size in _groups(rows):
        values = np.asarray([_float(row, target_name) for row in rows[start:start + size]])
        order = np.argsort(np.argsort(values, kind="stable"), kind="stable")
        if size > 1:
            labels[start:start + size] = np.floor(order * 30 / (size - 1))
        else:
            labels[start:start + size] = 0
        start += size
    return labels


def _rmse(actual, predicted) -> float:
    import numpy as np

    return float(np.sqrt(np.mean((actual - predicted) ** 2)))


def fit_ridge(
    train: list[dict[str, str]], validation: list[dict[str, str]],
    names: list[str], target_name: str, alpha: float,
    output: pathlib.Path, objective: str = "regression",
) -> dict[str, Any]:
    import numpy as np

    x_train = _matrix(train, names)
    x_validation = _matrix(validation, names)
    raw_train = _target(train, target_name)
    raw_validation = _target(validation, target_name)
    y_train = _rank_labels(train, target_name) if objective == "rank" else raw_train
    y_validation = _rank_labels(validation, target_name) if objective == "rank" else raw_validation
    mean = x_train.mean(axis=0)
    scale = x_train.std(axis=0)
    scale[scale < 1e-12] = 1.0
    x_train = (x_train - mean) / scale
    x_validation = (x_validation - mean) / scale
    x_train = np.column_stack((np.ones(len(x_train)), x_train))
    x_validation = np.column_stack((np.ones(len(x_validation)), x_validation))
    regularizer = np.eye(x_train.shape[1], dtype=np.float64) * float(alpha)
    regularizer[0, 0] = 0.0
    normal_matrix = x_train.T @ x_train + regularizer
    try:
        weights = np.linalg.solve(normal_matrix, x_train.T @ y_train)
    except np.linalg.LinAlgError:
        # ``--alpha 0`` is useful for an explicit unregularized diagnostic;
        # least squares keeps that diagnostic usable when the design is rank
        # deficient instead of failing with an opaque linear-algebra error.
        weights = np.linalg.lstsq(normal_matrix, x_train.T @ y_train, rcond=None)[0]
    prediction = x_validation @ weights
    output.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output, weights=weights, mean=mean, scale=scale,
        feature_names=np.asarray(names), target=np.asarray([target_name]),
    )
    return {
        "backend": "ridge",
        "model": str(output),
        "feature_names": names,
        "target": target_name,
        "objective": objective,
        "alpha": alpha,
        "train_rows": len(train),
        "validation_rows": len(validation),
        "validation_rmse": _rmse(raw_validation, prediction) if objective == "regression" else None,
        "validation_target_mean": float(raw_validation.mean()),
        "validation_prediction_mean": float(prediction.mean()),
    }


def fit_lightgbm(
    train: list[dict[str, str]], validation: list[dict[str, str]],
    names: list[str], target_name: str, seed: int, objective: str,
    output: pathlib.Path,
) -> dict[str, Any]:
    try:
        import lightgbm as lgb
    except ImportError as error:
        raise RuntimeError(
            "LightGBM is not installed; use --backend ridge or install it on the "
            "remote training environment"
        ) from error
    x_train = _matrix(train, names)
    x_validation = _matrix(validation, names)
    raw_train = _target(train, target_name)
    raw_validation = _target(validation, target_name)
    y_train = _rank_labels(train, target_name) if objective == "rank" else raw_train
    y_validation = _rank_labels(validation, target_name) if objective == "rank" else raw_validation
    rank_groups_train = _groups(train) if objective == "rank" else None
    rank_groups_validation = _groups(validation) if objective == "rank" else None
    if objective == "rank":
        ranker = lgb.LGBMRanker(
            objective="lambdarank", n_estimators=400, learning_rate=0.05,
            num_leaves=31, subsample=0.9, colsample_bytree=0.9,
            random_state=seed, verbosity=-1,
        )
        ranker.fit(
            x_train, y_train, group=rank_groups_train,
            eval_set=[(x_validation, y_validation)],
            eval_group=[rank_groups_validation],
            callbacks=[lgb.early_stopping(40, verbose=False)],
        )
        model = ranker
    else:
        model = lgb.LGBMRegressor(
            objective="regression", n_estimators=400, learning_rate=0.05,
            num_leaves=31, subsample=0.9, colsample_bytree=0.9,
            random_state=seed, verbosity=-1,
        )
        model.fit(
            x_train, y_train, eval_set=[(x_validation, y_validation)],
            callbacks=[lgb.early_stopping(40, verbose=False)],
        )
    prediction = model.predict(x_validation)
    output.parent.mkdir(parents=True, exist_ok=True)
    model.booster_.save_model(str(output))
    return {
        "backend": "lightgbm",
        "model": str(output),
        "feature_names": names,
        "target": target_name,
        "train_rows": len(train),
        "validation_rows": len(validation),
        "objective": objective,
        "validation_rmse": _rmse(raw_validation, prediction) if objective == "regression" else None,
        "validation_target_mean": float(raw_validation.mean()),
        "validation_prediction_mean": float(prediction.mean()),
        "best_iteration": int(model.best_iteration_ or model.n_estimators),
    }


def fit(args: argparse.Namespace) -> dict[str, Any]:
    rows, fields = _read_rows(pathlib.Path(args.dataset))
    rows, dropped_infeasible = _effective_rows(rows, fields)
    names = feature_names(fields)
    if args.target not in fields:
        raise ValueError(f"target column not found: {args.target}")
    train, validation = _split_rows(rows, args.validation_fraction, args.seed)
    output = pathlib.Path(args.output)
    if args.backend == "ridge" and output.suffix != ".npz":
        output = pathlib.Path(f"{output}.npz")
    if args.backend == "ridge":
        result = fit_ridge(
            train, validation, names, args.target, args.alpha, output, args.objective,
        )
    else:
        result = fit_lightgbm(
            train, validation, names, args.target, args.seed, args.objective, output,
        )
    result.update({
        "format": "kaggriculture_macro_value_model_v1",
        "dataset": str(args.dataset),
        "validation_fraction": args.validation_fraction,
        "seed": args.seed,
        "objective": args.objective,
        "split": "stable episode_id hash",
        "warning": "validation is observational/counterfactual-data validation, not a match result",
        "input_rows": len(rows) + dropped_infeasible,
        "dropped_infeasible_rows": dropped_infeasible,
    })
    metadata_path = pathlib.Path(args.metadata or f"{output}.meta.json")
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    metadata_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return result


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True)
    parser.add_argument("--output", required=True, help=".npz for ridge or LightGBM text model")
    parser.add_argument("--metadata")
    parser.add_argument("--backend", choices=("ridge", "lightgbm"), default="ridge")
    parser.add_argument(
        "--objective", choices=("regression", "rank"), default="regression",
        help="LightGBM objective; rank orders candidates within each state",
    )
    parser.add_argument("--target", default="delta_money")
    parser.add_argument("--alpha", type=float, default=10.0)
    parser.add_argument("--validation-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=707)
    args = parser.parse_args(argv)
    if args.alpha < 0:
        parser.error("--alpha must be nonnegative")
    if not 0.0 < args.validation_fraction < 1.0:
        parser.error("--validation-fraction must be between 0 and 1")
    return args


def main(argv: list[str] | None = None) -> int:
    print(json.dumps(fit(parse_args(argv)), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
