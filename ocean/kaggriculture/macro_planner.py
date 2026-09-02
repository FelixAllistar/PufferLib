#!/usr/bin/env python3
"""Bounded greedy/MPC evaluation for Kaggriculture macro candidates.

The planner is an offline reference implementation of C2.  It is deliberately
not called from the primitive PPO loop: a command-line run restores verified
native states, ranks a feasible candidate set with a fitted scorer, and then
optionally exact-rolls the top ``K`` candidates for a short horizon.  This
makes the two useful baselines auditable before any macro-policy integration:

``greedy``
    choose ``argmax(model(s, candidate))``;

``mpc``
    rank with the model, exact-roll the top K candidates in the native core,
    and choose the one with the best realized cash from the common starting
    state.  The continuation provider is reactive when ``--opponent-mode
    rule`` is selected.

The output is a TSV, one row per evaluated state/player, plus a JSON summary.
No training configuration, reward, checkpoint, or live evaluator is changed.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
from typing import Any

from build_replay_state_bank import BANK_FORMAT_VERSION, BANK_HEADER, BANK_MAGIC
from counterfactual_dataset import (
    _default_config,
    _initial_pair,
    _players,
    _read_manifest,
    _read_record,
    _restore,
    branch_payload,
    _load_trace,
)
from macro_actions import candidate_actions, merge_macro_action
from macro_value_model import MacroValueModel, choose_best
from replay_native import clone_action, c_snapshot, load_core
from state_sampling import SELECTIONS, select_rows


DEFAULT_LIB = pathlib.Path(__file__).with_name("build") / "libkaggriculture.so"
OUTPUT_FIELDS = (
    "state_id", "record_index", "episode_id", "turn", "player", "opponent_mode",
    "horizon", "search", "candidate_count", "top_k", "baseline_final_money",
    "greedy_index", "greedy_candidate", "greedy_prediction", "greedy_final_money",
    "greedy_delta_money", "selected_index", "selected_candidate", "selected_prediction",
    "selected_final_money", "selected_delta_money", "selected_exact_rank",
    "exact_candidates_json",
)


def _read_bank_header(bank: pathlib.Path) -> tuple[int, int, int]:
    with bank.open("rb") as stream:
        header = stream.read(BANK_HEADER.size)
    if len(header) != BANK_HEADER.size:
        raise ValueError(f"truncated state-bank header: {bank}")
    magic, bank_version, state_version, state_size, record_count, _reserved = BANK_HEADER.unpack(header)
    if magic != BANK_MAGIC or bank_version != BANK_FORMAT_VERSION:
        raise ValueError(f"unsupported state-bank format: {bank}")
    return state_version, state_size, record_count


def _horizon(value: str) -> int | None:
    if value == "terminal":
        return None
    try:
        parsed = int(value)
    except ValueError as error:
        raise ValueError("--horizon must be a positive integer or terminal") from error
    if parsed < 1:
        raise ValueError("--horizon must be positive")
    return parsed


def _provider(args: argparse.Namespace):
    if args.opponent_mode == "rule":
        from opponent_providers import RuleProvider

        return RuleProvider(
            episode_steps=args.episode_steps,
            liquidation_turns=args.rule_liquidation_turns,
        )
    if args.opponent_mode == "learned":
        from opponent_providers import learned_provider

        return learned_provider(
            league=args.league, policy_models=args.policy_model,
            deterministic=not args.policy_stochastic, seed=args.state_seed,
            backend=args.policy_backend, device=args.policy_device,
        )
    return None


def _episode_in_validation(episode_id: str, fraction: float, seed: int) -> bool:
    """Use the same stable episode hash as ``fit_macro_value.py``."""

    digest = hashlib.sha256(f"{seed}:{episode_id}".encode("utf-8")).digest()
    value = int.from_bytes(digest[:8], "little") / float(2**64)
    return value < fraction


def evaluate(args: argparse.Namespace) -> dict[str, Any]:
    bank = pathlib.Path(args.bank)
    manifest = pathlib.Path(args.manifest or f"{bank}.manifest.tsv")
    # Read the complete manifest before applying an episode split.  Limiting
    # the first N rows would bias a validation run toward whichever episodes
    # happen to occur at the front of the bank (and can select no validation
    # rows at all).  ``--limit-states`` is applied below after the split.
    rows, manifest_count = _read_manifest(manifest, 0)
    state_version, state_size, record_count = _read_bank_header(bank)
    if record_count != manifest_count:
        raise ValueError(f"manifest/bank record count mismatch for {bank}")
    if args.top_k < 1:
        raise ValueError("--top-k must be positive")
    if args.search == "greedy" and args.top_k != 1:
        # Keeping this explicit prevents a misleading report where the user
        # thought top-K was exact-evaluated but only greedy selection occurred.
        args.top_k = 1
    horizon = _horizon(args.horizon)
    trace = _load_trace(pathlib.Path(args.trace) if args.trace else None)
    if args.opponent_mode == "trace" and not trace:
        raise ValueError("--opponent-mode trace requires --trace")
    provider = _provider(args)
    model = MacroValueModel.load(args.model)
    lib = load_core(pathlib.Path(args.lib))
    actual_size = int(lib.kg_state_serialized_size())
    actual_version = int(lib.kg_state_serialization_version())
    if (actual_size, actual_version) != (state_size, state_version):
        raise ValueError(
            f"native ABI mismatch: bank version/size={state_version}/{state_size}, "
            f"library={actual_version}/{actual_size}"
        )
    config = _default_config(lib)
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    summary: dict[str, Any] = {
        "format": "kaggriculture_macro_planner_v1",
        "bank": str(bank), "manifest": str(manifest), "model": str(args.model),
        "model_backend": model.backend, "model_target": model.target,
        "opponent_mode": args.opponent_mode, "horizon": args.horizon,
        "search": args.search, "top_k": args.top_k,
        "episode_split": args.episode_split,
        "validation_fraction": args.validation_fraction,
        "split_seed": args.split_seed,
        "state_selection": args.state_selection,
        "state_seed": args.state_seed,
        "league": args.league,
        "policy_models": args.policy_model,
        "policy_stochastic": args.policy_stochastic,
        "policy_backend": args.policy_backend,
        "policy_device": args.policy_device,
        "episode_steps": args.episode_steps, "turns_per_day": args.turns_per_day,
        "states": 0, "players": 0, "selected": {}, "mean_baseline_money": 0.0,
        "mean_greedy_delta_money": 0.0, "mean_selected_delta_money": 0.0,
    }
    # Apply the episode filter before sampling.  This makes a validation run a
    # genuine held-out subset even when ``--limit-states`` is small.
    if args.episode_split != "all":
        filtered_rows = []
        for row in rows:
            in_validation = _episode_in_validation(
                str(row.get("episode_id", "")), args.validation_fraction, args.split_seed,
            )
            if args.episode_split == "validation" and in_validation:
                filtered_rows.append(row)
            elif args.episode_split == "train" and not in_validation:
                filtered_rows.append(row)
        rows = filtered_rows
    rows = select_rows(
        rows, args.limit_states, strategy=args.state_selection,
        seed=args.state_seed, episode_steps=args.episode_steps,
    )
    baseline_total = greedy_delta_total = selected_delta_total = 0.0
    with bank.open("rb") as bank_stream, output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=OUTPUT_FIELDS, delimiter="\t")
        writer.writeheader()
        for row in rows:
            payload = _read_record(bank_stream, row, state_size)
            restored = _restore(lib, payload, config)
            try:
                snapshot = c_snapshot(lib, restored)
                if provider is not None and hasattr(provider, "begin_episode"):
                    provider.begin_episode(str(row["episode_id"]), int(row["turn"]))
                initial_pair = _initial_pair(
                    row, args.opponent_mode, trace, snapshot=snapshot, provider=provider,
                    lib=lib, native_state=restored,
                )
            finally:
                lib.kg_destroy(restored)
            step = int(snapshot.get("step", row.get("turn", 0)))
            for player in _players(row, args.players):
                candidates = candidate_actions(
                    snapshot, player, max_candidates=args.max_candidates,
                    include_strategic=not args.direct_only,
                    episode_steps=args.episode_steps,
                    shed_capacity=args.shed_capacity,
                )
                if not candidates:
                    continue
                predictions = model.predict_candidates(
                    snapshot, player, candidates, episode_steps=args.episode_steps,
                    turns_per_day=args.turns_per_day, shed_capacity=args.shed_capacity,
                )
                greedy_index, greedy_candidate, greedy_prediction = choose_best(candidates, predictions)
                baseline = branch_payload(
                    lib, payload, config, state_size, step, player, initial_pair,
                    None, str(row["episode_id"]), args.opponent_mode, trace, provider, horizon,
                )
                baseline_money = float(baseline["final_money"])
                exact_indices = [greedy_index]
                if args.search == "mpc":
                    exact_indices = sorted(
                        range(len(candidates)),
                        key=lambda index: (float(predictions[index]), -index),
                        reverse=True,
                    )[: min(args.top_k, len(candidates))]
                exact: dict[int, dict[str, Any]] = {}
                for index in exact_indices:
                    candidate = candidates[index]
                    pair = [clone_action(initial_pair[0]), clone_action(initial_pair[1])]
                    if isinstance(pair[player], dict):
                        pair[player] = merge_macro_action(
                            candidate.payload(), pair[player],
                        )
                    else:
                        pair[player] = candidate.payload()
                    exact[index] = branch_payload(
                        lib, payload, config, state_size, step, player, pair,
                        candidate.action_sequence(), str(row["episode_id"]),
                        args.opponent_mode, trace, provider, horizon,
                    )
                greedy_result = exact[greedy_index]
                if args.search == "greedy":
                    selected_index = greedy_index
                else:
                    selected_index = max(
                        exact_indices,
                        key=lambda index: (
                            float(exact[index]["final_money"]),
                            float(predictions[index]),
                            -index,
                        ),
                    )
                selected_result = exact[selected_index]
                selected = candidates[selected_index]
                selected_rank = exact_indices.index(selected_index) + 1
                exact_json = [
                    {
                        "index": index,
                        "candidate": candidates[index].action_id,
                        "prediction": float(predictions[index]),
                        "final_money": float(exact[index]["final_money"]),
                        "delta_money": float(exact[index]["final_money"] - baseline_money),
                    }
                    for index in exact_indices
                ]
                greedy_delta = float(greedy_result["final_money"] - baseline_money)
                selected_delta = float(selected_result["final_money"] - baseline_money)
                writer.writerow({
                    "state_id": f"{row['record_index']}:{player}",
                    "record_index": row["record_index"], "episode_id": row["episode_id"],
                    "turn": row["turn"], "player": player,
                    "opponent_mode": args.opponent_mode, "horizon": args.horizon,
                    "search": args.search, "candidate_count": len(candidates),
                    "top_k": len(exact_indices), "baseline_final_money": baseline_money,
                    "greedy_index": greedy_index, "greedy_candidate": greedy_candidate.action_id,
                    "greedy_prediction": greedy_prediction,
                    "greedy_final_money": float(greedy_result["final_money"]),
                    "greedy_delta_money": greedy_delta,
                    "selected_index": selected_index, "selected_candidate": selected.action_id,
                    "selected_prediction": float(predictions[selected_index]),
                    "selected_final_money": float(selected_result["final_money"]),
                    "selected_delta_money": selected_delta,
                    "selected_exact_rank": selected_rank,
                    "exact_candidates_json": json.dumps(exact_json, separators=(",", ":")),
                })
                summary["players"] += 1
                baseline_total += baseline_money
                greedy_delta_total += greedy_delta
                selected_delta_total += selected_delta
                summary["selected"][selected.action_id] = summary["selected"].get(selected.action_id, 0) + 1
            summary["states"] += 1
    if summary["players"] == 0:
        raise ValueError(
            "episode split selected no rows; adjust --validation-fraction/--split-seed"
        )
    count = int(summary["players"])
    summary["mean_baseline_money"] = baseline_total / count
    summary["mean_greedy_delta_money"] = greedy_delta_total / count
    summary["mean_selected_delta_money"] = selected_delta_total / count
    pathlib.Path(f"{output}.summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bank", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--model", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--lib", default=str(DEFAULT_LIB))
    parser.add_argument("--limit-states", type=int, default=0)
    parser.add_argument("--max-candidates", type=int, default=0)
    parser.add_argument("--direct-only", action="store_true")
    parser.add_argument("--players", choices=("both", "0", "1"), default="both")
    parser.add_argument("--horizon", default="24")
    parser.add_argument("--search", choices=("greedy", "mpc"), default="mpc")
    parser.add_argument("--top-k", type=int, default=4)
    parser.add_argument(
        "--episode-split", choices=("all", "train", "validation"), default="all",
        help="filter complete episodes using the fitter's stable hash",
    )
    parser.add_argument("--validation-fraction", type=float, default=0.2)
    parser.add_argument("--split-seed", type=int, default=707)
    parser.add_argument(
        "--state-selection", choices=SELECTIONS, default="stratified",
        help="how to choose a limited subset after episode filtering",
    )
    parser.add_argument("--state-seed", type=int, default=707)
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
    args = parser.parse_args(argv)
    if args.limit_states < 0 or args.max_candidates < 0 or args.top_k < 1:
        parser.error("limits must be nonnegative and top-k positive")
    if not 0.0 < args.validation_fraction < 1.0:
        parser.error("validation-fraction must be between 0 and 1")
    if args.rule_liquidation_turns < 0:
        parser.error("rule-liquidation-turns must be nonnegative")
    return args


def main(argv: list[str] | None = None) -> int:
    print(json.dumps(evaluate(parse_args(argv)), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
