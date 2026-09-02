#!/usr/bin/env python3
"""Evaluate a mode-2 clone on whole-episode held-out rows.

This is an offline head/fidelity gate, not a substitute for a simulator
rollout.  The manifest is used to hold out complete replay trajectories, and
the default split holds out the latest source day, avoiding neighboring-turn
leakage.  The evaluator runs the recurrent policy from a fresh state per
episode, applies the recorded mode-2 mask, and reports intent/quantity/target
accuracy, legal-label coverage, opening (`<=60`) fidelity, the turn-180 window,
and macro/quantity/target diversity descriptors.
It also records the ten most common hard-label opening signatures (the first
eight opening decisions by default) and how often predictions land in that
expert top-ten set.

The old primitive clone artifacts cannot be evaluated as mode-2 checkpoints:
their first-head command IDs have different semantics.  Use the importer on
the old cutoff to train a matching macro-compatible baseline, then invoke this
tool with the same held-out source day and manifest.
"""

from __future__ import annotations

import argparse
import collections
import csv
import importlib.util
import json
import os
import pathlib
import struct
import sys
from typing import Any

import numpy as np


HEADER = struct.Struct("<8I")
MAGIC = 0x4B414742
VERSION = 2


def _load_submission():
    path = pathlib.Path(__file__).with_name("submission") / "main.py"
    spec = importlib.util.spec_from_file_location("kaggriculture_eval_submission", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load submission codec from {path}")
    module = importlib.util.module_from_spec(spec)
    # Loading the codec is enough for NativeMinGRU; avoid accidentally
    # loading the bundled competition checkpoint just to inspect its classes.
    previous = os.environ.get("PUFFERLIB_MODEL_PATH")
    os.environ["PUFFERLIB_MODEL_PATH"] = str(path.with_name(
        "__evaluate_without_model__.bin"
    ))
    try:
        spec.loader.exec_module(module)
    finally:
        if previous is None:
            os.environ.pop("PUFFERLIB_MODEL_PATH", None)
        else:
            os.environ["PUFFERLIB_MODEL_PATH"] = previous
    return module


def _read_header(path: pathlib.Path) -> tuple[int, ...]:
    with path.open("rb") as stream:
        values = HEADER.unpack(stream.read(HEADER.size))
    if values[0] != MAGIC or values[1] != VERSION:
        raise ValueError(f"unsupported BC dataset: {path}")
    magic, version, count, row_obs, row_expert, row_mask, games, steps = values
    if count != games * steps or row_obs != 1280 or row_expert != 47 or row_mask != 133:
        raise ValueError(f"invalid mode-2 BC dimensions: {values}")
    return values


def _source_day(value: str) -> str:
    import re
    match = re.search(r"20\d\d-\d\d-\d\d", value)
    return match.group(0) if match else "unknown"


def _load_manifest(path: pathlib.Path, games: int) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != games:
        raise ValueError(f"manifest rows {len(rows)} != dataset games {games}")
    return rows


def _holdout_games(
    rows: list[dict[str, str]], *, day: str | None, fraction: float
) -> tuple[list[int], str]:
    by_day = collections.defaultdict(list)
    for index, row in enumerate(rows):
        by_day[_source_day(row.get("source", ""))].append(index)
    days = sorted(by_day)
    if day:
        selected_days = {day}
    elif len(days) > 1:
        # Later-day evaluation is the strongest anti-leakage default.
        selected_days = {days[-1]}
    else:
        count = max(1, int(round(len(rows) * max(0.01, fraction))))
        selected = sorted(range(len(rows)),
                          key=lambda index: (rows[index].get("episode_id", ""), index))[-count:]
        return selected, "episode-hash-tail"
    selected = [index for index, row in enumerate(rows)
                if _source_day(row.get("source", "")) in selected_days]
    if not selected:
        raise ValueError(f"no manifest rows match holdout day(s): {sorted(selected_days)}")
    return selected, ",".join(sorted(selected_days))


def _metric(counter: tuple[int, int]) -> float | None:
    numerator, denominator = counter
    return numerator / denominator if denominator else None


def evaluate(
    dataset: pathlib.Path, model_path: pathlib.Path, manifest_path: pathlib.Path,
    *, holdout_day: str | None = None, holdout_fraction: float = 0.2,
    opening_steps: int = 61, turn180_start: int = 168, turn180_end: int = 192,
    opening_signature_steps: int = 8,
) -> dict[str, Any]:
    codec = _load_submission()
    magic, version, count, row_obs, row_expert, row_mask, games, steps = _read_header(dataset)
    rows = _load_manifest(manifest_path, games)
    selected_games, holdout = _holdout_games(
        rows, day=holdout_day, fraction=holdout_fraction
    )
    selected = set(selected_games)
    obs_offset = HEADER.size
    expert_offset = obs_offset + count * row_obs
    mask_offset = expert_offset + count * row_expert * 4
    observations = np.memmap(
        dataset, dtype=np.uint8, mode="r", offset=obs_offset,
        shape=(count, row_obs), order="C",
    )
    experts = np.memmap(
        dataset, dtype="<f4", mode="r", offset=expert_offset,
        shape=(count, row_expert), order="C",
    )
    masks = np.memmap(
        dataset, dtype=np.uint8, mode="r", offset=mask_offset,
        shape=(count, row_mask), order="C",
    )
    model = codec.NativeMinGRU(str(model_path))
    metric_names = ("macro", "quantity", "target")
    totals = {name: [0, 0] for name in metric_names}
    openings = {name: [0, 0] for name in metric_names}
    around180 = {name: [0, 0] for name in metric_names}
    legal_labels = [0, 0]
    top3 = [0, 0]
    macro_counts: collections.Counter[str] = collections.Counter()
    quantity_counts: collections.Counter[str] = collections.Counter()
    target_counts: collections.Counter[str] = collections.Counter()
    source_counts: collections.Counter[str] = collections.Counter()
    expert_opening_signatures: collections.Counter[str] = collections.Counter()
    predicted_opening_signatures: collections.Counter[str] = collections.Counter()
    heldout_rows = 0
    skipped_rows = 0
    for game in selected_games:
        model.reset()
        source = rows[game].get("source", "")
        source_counts[_source_day(source)] += 1
        expert_signature: list[tuple[int, int, int]] = []
        predicted_signature: list[tuple[int, int, int]] = []
        for turn in range(steps):
            index = game * steps + turn
            expert = np.asarray(experts[index], dtype=np.float32)
            # Even an ambiguous/filtered expert row is a real observation in
            # the recurrent trajectory.  Advance the hidden state before
            # skipping its metrics; otherwise every later prediction would
            # see a history with those turns silently removed, unlike a live
            # mode-2 rollout.
            logits = model.forward(np.asarray(observations[index]))
            if expert[0] < 0:
                skipped_rows += 1
                continue
            heldout_rows += 1
            mask = np.unpackbits(masks[index], bitorder="little")[:row_mask * 8]
            predictions = []
            for head in range(3):
                start, end = codec.HEAD_OFFSETS[head], codec.HEAD_OFFSETS[head + 1]
                legal = np.flatnonzero(mask[start:end])
                if legal.size == 0:
                    predictions.append(-1)
                    continue
                values = logits[start:end][legal]
                order = np.argsort(values)[::-1]
                predictions.append(int(legal[order[0]]))
                if head == 0 and int(expert[head]) in legal:
                    top3[1] += 1
                    top3[0] += int(int(expert[head]) in legal[order[:3]])
            if all(prediction >= 0 for prediction in predictions):
                legal_labels[1] += 1
                legal_labels[0] += int(all(
                    bool(mask[codec.HEAD_OFFSETS[h] + int(expert[h])])
                    for h in range(3)
                ))
            for name, head in zip(metric_names, range(3)):
                totals[name][1] += 1
                totals[name][0] += int(predictions[head] == int(expert[head]))
                target_counter = openings if turn < opening_steps else (
                    around180 if turn180_start <= turn <= turn180_end else None
                )
                if target_counter is not None:
                    target_counter[name][1] += 1
                    target_counter[name][0] += int(predictions[head] == int(expert[head]))
            if turn < opening_steps and len(expert_signature) < opening_signature_steps:
                expert_signature.append(tuple(int(value) for value in expert[:3]))
                predicted_signature.append(tuple(int(value) for value in predictions[:3]))
            macro_counts[str(int(expert[0]))] += 1
            quantity_counts[str(int(expert[1]))] += 1
            target_counts[str(int(expert[2]))] += 1

        if len(expert_signature) == opening_signature_steps:
            def encode_signature(values: list[tuple[int, int, int]]) -> str:
                return ";".join(
                    ",".join(str(value) for value in triple) for triple in values
                )
            expert_opening_signatures[encode_signature(expert_signature)] += 1
            predicted_opening_signatures[encode_signature(predicted_signature)] += 1

    expert_top10 = expert_opening_signatures.most_common(10)
    predicted_top10 = predicted_opening_signatures.most_common(10)
    expert_top10_keys = {signature for signature, _count in expert_top10}
    total_signature_episodes = sum(predicted_opening_signatures.values())
    predicted_in_expert_top10 = sum(
        count for signature, count in predicted_opening_signatures.items()
        if signature in expert_top10_keys
    )
    exact_signature_matches = sum(
        count for signature, count in predicted_opening_signatures.items()
        if signature in expert_opening_signatures
    )

    return {
        "dataset": str(dataset), "model": str(model_path),
        "manifest": str(manifest_path), "holdout": holdout,
        "heldout_games": len(selected_games), "heldout_rows": heldout_rows,
        "skipped_ambiguous_or_padding_rows": skipped_rows,
        "source_days": dict(sorted(source_counts.items())),
        "accuracy": {name: _metric(tuple(value)) for name, value in totals.items()},
        "opening_accuracy": {name: _metric(tuple(value)) for name, value in openings.items()},
        "turn180_accuracy": {name: _metric(tuple(value)) for name, value in around180.items()},
        "macro_top3": _metric(tuple(top3)),
        "legal_label_coverage": _metric(tuple(legal_labels)),
        "diversity": {
            "macro_ids": dict(macro_counts.most_common()),
            "quantity_bins": dict(quantity_counts.most_common()),
            "target_bins": dict(target_counts.most_common()),
        },
        "opening_signatures": {
            "steps": opening_signature_steps,
            "episodes_with_signature": sum(expert_opening_signatures.values()),
            "expert_top10": [
                {"signature": signature, "count": count}
                for signature, count in expert_top10
            ],
            "predicted_top10": [
                {"signature": signature, "count": count}
                for signature, count in predicted_top10
            ],
            "predicted_in_expert_top10": predicted_in_expert_top10,
            "predicted_in_expert_top10_rate": (
                predicted_in_expert_top10 / total_signature_episodes
                if total_signature_episodes else None
            ),
            "exact_signature_matches": exact_signature_matches,
            "exact_signature_match_rate": (
                exact_signature_matches / total_signature_episodes
                if total_signature_episodes else None
            ),
        },
        "mode2_abi": {"observation_bytes": 1280, "heads": 47, "mask_bits": 1058},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=pathlib.Path)
    parser.add_argument("model", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--holdout-day")
    parser.add_argument("--holdout-fraction", type=float, default=0.2)
    parser.add_argument("--opening-steps", type=int, default=61)
    parser.add_argument("--turn180-start", type=int, default=168)
    parser.add_argument("--turn180-end", type=int, default=192)
    parser.add_argument("--opening-signature-steps", type=int, default=8)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.opening_signature_steps < 1:
        parser.error("--opening-signature-steps must be positive")
    manifest = args.manifest or args.dataset.with_suffix(args.dataset.suffix + ".players.tsv")
    result = evaluate(
        args.dataset, args.model, manifest, holdout_day=args.holdout_day,
        holdout_fraction=args.holdout_fraction, opening_steps=args.opening_steps,
        turn180_start=args.turn180_start, turn180_end=args.turn180_end,
        opening_signature_steps=args.opening_signature_steps,
    )
    payload = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload + "\n", encoding="utf-8")
    print(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
