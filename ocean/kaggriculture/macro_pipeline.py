#!/usr/bin/env python3
"""Generate multi-horizon macro labels and fit scorers in one reproducible run.

This is a convenience driver around the C1 writer and fitter.  It deliberately
requires an explicit state bank and exposes a state limit; running the full
bank at terminal horizon can be expensive.  Each horizon receives a separate
TSV/model/metadata triplet so short-term and terminal targets are not mixed.
"""

from __future__ import annotations

import argparse
import json
import pathlib
from types import SimpleNamespace
from typing import Any

from counterfactual_dataset import DEFAULT_LIB, generate
from fit_macro_value import fit
from state_sampling import SELECTIONS


def _horizons(value: str) -> list[str]:
    values = [item.strip() for item in value.split(",") if item.strip()]
    if not values:
        raise ValueError("--horizons must contain at least one value")
    for item in values:
        if item != "terminal" and (not item.isdigit() or int(item) < 1):
            raise ValueError(f"invalid horizon {item!r}")
    return values


def run(args: argparse.Namespace) -> dict[str, Any]:
    horizons = _horizons(args.horizons)
    output_dir = pathlib.Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    results: list[dict[str, Any]] = []
    for horizon in horizons:
        stem = f"macro_{args.opponent_mode}_{horizon}"
        dataset = output_dir / f"{stem}.tsv"
        model = output_dir / (f"{stem}.npz" if args.backend == "ridge" else f"{stem}.txt")
        dataset_summary = generate(SimpleNamespace(
            bank=args.bank, manifest=args.manifest, output=str(dataset),
            lib=args.lib or str(DEFAULT_LIB),
            limit_states=args.limit_states, max_candidates=args.max_candidates,
            branch_batch_size=args.branch_batch_size,
            branch_workers=args.branch_workers,
            state_selection=args.state_selection, state_seed=args.state_seed,
            state_shard_index=args.state_shard_index,
            state_shard_count=args.state_shard_count,
            direct_only=args.direct_only, players=args.players, horizon=horizon,
            opponent_mode=args.opponent_mode, trace=args.trace,
            rule_liquidation_turns=args.rule_liquidation_turns,
            episode_steps=args.episode_steps, turns_per_day=args.turns_per_day,
            shed_capacity=args.shed_capacity, league=args.league,
            policy_model=args.policy_model, policy_stochastic=args.policy_stochastic,
            policy_backend=args.policy_backend, policy_device=args.policy_device,
        ))
        fit_summary = fit(SimpleNamespace(
            dataset=str(dataset), output=str(model), metadata=None,
            backend=args.backend, objective=args.objective, target=args.target,
            alpha=args.alpha, validation_fraction=args.validation_fraction, seed=args.seed,
        ))
        results.append({"horizon": horizon, "dataset": dataset_summary, "model": fit_summary})
    summary = {
        "format": "kaggriculture_macro_pipeline_v1",
        "bank": str(args.bank), "output_dir": str(output_dir),
        "opponent_mode": args.opponent_mode, "backend": args.backend,
        "objective": args.objective, "results": results,
        "state_selection": args.state_selection, "state_seed": args.state_seed,
        "state_shard_index": args.state_shard_index,
        "state_shard_count": args.state_shard_count,
        "league": args.league,
        "policy_models": args.policy_model,
        "policy_stochastic": args.policy_stochastic,
        "policy_backend": args.policy_backend,
        "policy_device": args.policy_device,
        "branch_batch_size": args.branch_batch_size,
        "branch_workers": args.branch_workers,
    }
    (output_dir / "pipeline.summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bank", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--horizons", default="24,72,terminal")
    parser.add_argument("--lib")
    parser.add_argument("--limit-states", type=int, default=256)
    parser.add_argument(
        "--state-selection", choices=SELECTIONS, default="stratified",
        help="how to choose a limited subset (default: stratified)",
    )
    parser.add_argument("--state-seed", type=int, default=707)
    parser.add_argument("--state-shard-index", type=int, default=0)
    parser.add_argument("--state-shard-count", type=int, default=1)
    parser.add_argument("--max-candidates", type=int, default=0)
    parser.add_argument("--branch-batch-size", type=int, default=128)
    parser.add_argument(
        "--branch-workers", type=int, default=1,
        help="parallel native branch workers for learned Torch labels (1 is the GPU reference path)",
    )
    parser.add_argument("--direct-only", action="store_true")
    parser.add_argument("--players", choices=("both", "0", "1"), default="both")
    parser.add_argument(
        "--opponent-mode", choices=("pass", "expert_first", "trace", "rule", "learned"), default="rule",
    )
    parser.add_argument("--trace")
    parser.add_argument("--rule-liquidation-turns", type=int, default=48)
    parser.add_argument("--episode-steps", type=int, default=720)
    parser.add_argument("--turns-per-day", type=int, default=24)
    parser.add_argument("--shed-capacity", type=int, default=100)
    parser.add_argument(
        "--policy-model", action="append",
        help="learned opponent checkpoint (repeatable or comma-separated)",
    )
    parser.add_argument("--league", help="league.ini containing enabled learned policies")
    parser.add_argument(
        "--policy-stochastic", action="store_true",
        help="sample learned policy logits instead of taking argmax",
    )
    parser.add_argument("--policy-backend", choices=("numpy", "torch"), default="numpy")
    parser.add_argument("--policy-device", default="cuda")
    parser.add_argument("--backend", choices=("ridge", "lightgbm"), default="ridge")
    parser.add_argument("--objective", choices=("regression", "rank"), default="regression")
    parser.add_argument("--target", default="delta_money")
    parser.add_argument("--alpha", type=float, default=10.0)
    parser.add_argument("--validation-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=707)
    args = parser.parse_args(argv)
    if (
        args.limit_states < 0 or args.max_candidates < 0 or args.branch_batch_size < 1
        or args.branch_workers < 1
        or args.state_shard_count < 1
        or args.state_shard_index < 0
        or args.state_shard_index >= args.state_shard_count
    ):
        parser.error("limits must be nonnegative")
    if args.alpha < 0 or not 0.0 < args.validation_fraction < 1.0:
        parser.error("alpha must be nonnegative and validation fraction in (0,1)")
    return args


def main(argv: list[str] | None = None) -> int:
    print(json.dumps(run(parse_args(argv)), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
