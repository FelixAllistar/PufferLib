#!/usr/bin/env python3
"""Convert official Kaggriculture replays into the native BC v2 format.

The converter deliberately uses the submission encoder and action mask.  This
keeps imported demonstrations on the exact observation and action ABIs used by
training and submission. Replays are processed one
at a time, so a multi-gigabyte daily archive never needs to be unpacked or held
in memory.

BC v2 stores three contiguous sections after its header: all observations,
then all float32 expert heads, then all packed masks.  Each player trajectory
is one recurrent sequence (normally 720 rows).
"""

from __future__ import annotations

import argparse
import collections
import csv
import glob
import gzip
import importlib.util
import io
import json
import os
import pathlib
import struct
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from typing import Any, Iterable, Iterator

import numpy as np


HEADER = struct.Struct("<8I")
MAGIC = 0x4B414742  # KAGB
VERSION = 2
EXPECTED_STEPS = 720


def _load_codec():
    path = pathlib.Path(__file__).with_name("submission") / "main.py"
    spec = importlib.util.spec_from_file_location("kaggriculture_submission_codec", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import Kaggriculture codec from {path}")
    module = importlib.util.module_from_spec(spec)
    # This tool only needs the codec. A historical bundled model may use an
    # older observation/action ABI, so explicitly suppress model loading.
    previous_model_path = os.environ.get("PUFFERLIB_MODEL_PATH")
    os.environ["PUFFERLIB_MODEL_PATH"] = str(
        path.with_name("__elite_import_without_model__.bin")
    )
    try:
        spec.loader.exec_module(module)
    finally:
        if previous_model_path is None:
            os.environ.pop("PUFFERLIB_MODEL_PATH", None)
        else:
            os.environ["PUFFERLIB_MODEL_PATH"] = previous_model_path
    return module


CODEC = _load_codec()
OBS_SIZE = int(CODEC.OBS_SIZE)
NUM_HEADS = len(CODEC.HEAD_SIZES)
MASK_SIZE = int(CODEC.MASK_SIZE)
MASK_BYTES = (MASK_SIZE + 7) // 8
UNIT_HEADS = int(CODEC.UNIT_HEADS)
DIRECT_HANDS = int(CODEC.DIRECT_HANDS)
OVERFLOW_COHORTS = int(CODEC.OVERFLOW_COHORTS)
MARKET_SLOTS = int(CODEC.MARKET_SLOTS)
MARKET_QUANTITIES = tuple(int(value) for value in CODEC.MARKET_QUANTITIES)


UNIT_SIMPLE = {
    "PASS": 0,
    "NORTH": 1,
    "SOUTH": 2,
    "EAST": 3,
    "WEST": 4,
    "DROP": 17,
    "WATER": 23,
    "HARVEST": 24,
    "FERTILIZE": 25,
    "BUILD_COOP": 26,
    "BUILD_PASTURE": 27,
    "DIG": 28,
    "FEED": 29,
    "COLLECT_FERTILIZER": 30,
    "CARE": 31,
}


@dataclass
class Audit:
    counts: collections.Counter[str] = field(default_factory=collections.Counter)
    skip_reasons: collections.Counter[str] = field(default_factory=collections.Counter)
    module_versions: collections.Counter[str] = field(default_factory=collections.Counter)
    unit_actions: collections.Counter[str] = field(default_factory=collections.Counter)
    market_actions: collections.Counter[str] = field(default_factory=collections.Counter)
    market_quantities: collections.Counter[str] = field(default_factory=collections.Counter)
    final_money: list[float] = field(default_factory=list)

    def merge(self, other: "Audit") -> None:
        self.counts.update(other.counts)
        self.skip_reasons.update(other.skip_reasons)
        self.module_versions.update(other.module_versions)
        self.unit_actions.update(other.unit_actions)
        self.market_actions.update(other.market_actions)
        self.market_quantities.update(other.market_quantities)
        self.final_money.extend(other.final_money)

    def as_dict(self) -> dict[str, Any]:
        money = np.asarray(self.final_money, dtype=np.float64)
        if money.size:
            money_summary = {
                "count": int(money.size),
                "min": float(np.min(money)),
                "p25": float(np.percentile(money, 25)),
                "median": float(np.median(money)),
                "p75": float(np.percentile(money, 75)),
                "max": float(np.max(money)),
                "mean": float(np.mean(money)),
            }
        else:
            money_summary = {"count": 0}
        counts = dict(sorted(self.counts.items()))
        rows = counts.get("rows", 0)
        market_rows = counts.get("market_rows", 0)
        cohort_rows = counts.get("overflow_cohort_rows", 0)
        ratios = {
            "whole_row_head_representable": _ratio(
                counts.get("whole_row_head_representable", 0), rows
            ),
            "market_exact": _ratio(counts.get("market_exact_rows", 0), market_rows),
            "market_usable": _ratio(counts.get("market_usable_rows", 0), market_rows),
            "overflow_cohort_conflict": _ratio(
                counts.get("overflow_cohort_conflicts", 0), cohort_rows
            ),
            "illegal_unit_head": _ratio(
                counts.get("illegal_unit_heads", 0), counts.get("unit_heads_seen", 0)
            ),
        }
        return {
            "format": {
                "magic": "KAGB",
                "version": VERSION,
                "observation_bytes": OBS_SIZE,
                "expert_heads": NUM_HEADS,
                "mask_bits": MASK_SIZE,
                "mask_bytes": MASK_BYTES,
            },
            "counts": counts,
            "ratios": ratios,
            "skip_reasons": dict(sorted(self.skip_reasons.items())),
            "module_versions": dict(sorted(self.module_versions.items())),
            "unit_actions": dict(self.unit_actions.most_common()),
            "market_actions": dict(self.market_actions.most_common()),
            "market_quantities": dict(self.market_quantities.most_common()),
            "final_money": money_summary,
        }


def _ratio(numerator: int, denominator: int) -> float | None:
    return float(numerator) / float(denominator) if denominator else None


def _version_tuple(value: Any) -> tuple[int, ...]:
    parts = []
    for token in str(value).split("."):
        digits = "".join(char for char in token if char.isdigit())
        if not digits:
            break
        parts.append(int(digits))
    return tuple(parts)


def _expand_inputs(values: Iterable[str]) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for value in values:
        matches = glob.glob(value, recursive=True)
        if not matches:
            matches = [value]
        for match in matches:
            path = pathlib.Path(match)
            candidates: Iterable[pathlib.Path]
            if path.is_dir():
                candidates = sorted(
                    child
                    for child in path.rglob("*")
                    if child.is_file()
                    and (child.suffix.lower() in (".json", ".zip", ".gz"))
                )
            else:
                candidates = (path,)
            for candidate in candidates:
                resolved = candidate.resolve()
                if resolved not in seen:
                    seen.add(resolved)
                    paths.append(candidate)
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing replay input(s): " + ", ".join(missing))
    return paths


def _iter_replays(paths: Iterable[pathlib.Path]) -> Iterator[tuple[str, dict[str, Any]]]:
    for path in paths:
        suffix = path.suffix.lower()
        if suffix == ".zip":
            with zipfile.ZipFile(path) as archive:
                for name in sorted(archive.namelist()):
                    if name.lower().endswith(".json") and not name.endswith("/"):
                        with archive.open(name) as raw:
                            with io.TextIOWrapper(raw, encoding="utf-8") as stream:
                                yield f"{path}:{name}", json.load(stream)
        elif suffix == ".gz":
            with gzip.open(path, "rt", encoding="utf-8") as stream:
                yield str(path), json.load(stream)
        elif suffix == ".json":
            with path.open(encoding="utf-8") as stream:
                yield str(path), json.load(stream)
        else:
            raise ValueError(f"unsupported replay input: {path}")


def _unit_action_id(action: Any) -> tuple[int | None, bool]:
    """Return the policy command ID and whether its quantity is exact.

    Quantity is not represented by the 44-way unit head.  The native policy
    executes one PICKUP/PLACE unit, so a demonstrated quantity other than one
    is useful at the command level but is reported as lossy.
    """
    if not isinstance(action, (list, tuple)) or not action:
        return None, False
    op = str(action[0]).upper()
    if op in UNIT_SIMPLE:
        return UNIT_SIMPLE[op], True
    if op == "PICKUP" and len(action) >= 2:
        item = CODEC.ITEM_ID.get(str(action[1]).upper())
        quantity = action[2] if len(action) >= 3 else 1
        return (5 + item, quantity == 1) if item is not None else (None, False)
    if op == "PLANT" and len(action) >= 2:
        crop = CODEC.CROP_ID.get(str(action[1]).upper())
        return (18 + crop, True) if crop is not None else (None, False)
    if op == "PLACE" and len(action) >= 2:
        item = CODEC.ITEM_ID.get(str(action[1]).upper())
        quantity = action[2] if len(action) >= 3 else 1
        return (32 + item, quantity == 1) if item is not None else (None, False)
    return None, False


def _market_command_id(order: Any) -> tuple[int, int | None] | None:
    if not isinstance(order, (list, tuple)) or not order:
        return None
    op = str(order[0]).upper()
    if op == "BUY_SEED" and len(order) >= 2:
        item = CODEC.CROP_ID.get(str(order[1]).upper())
        return (item, _quantity(order)) if item is not None else None
    if op == "BUY_PRODUCT" and len(order) >= 2:
        name = str(order[1]).upper()
        command = {"WHEAT": 5, "FERTILIZER": 6}.get(name)
        return (command, _quantity(order)) if command is not None else None
    if op == "BUY_ANIMAL" and len(order) >= 2:
        item = CODEC.ITEM_ID.get(str(order[1]).upper())
        if item is not None and 9 <= item < 12:
            return 7 + item - 9, _quantity(order)
        return None
    if op == "SELL" and len(order) >= 2:
        item = CODEC.ITEM_ID.get(str(order[1]).upper())
        if item is not None and item < 9:
            return 10 + item, _quantity(order)
        return None
    if op == "HIRE":
        return 19, None
    if op == "BUY_LAND":
        return 20, None
    return None


def _quantity(order: list[Any] | tuple[Any, ...]) -> int | None:
    value = order[2] if len(order) >= 3 else 1
    if isinstance(value, bool):
        return None
    try:
        result = int(value)
    except (TypeError, ValueError, OverflowError):
        return None
    return result if result > 0 else None


def _quantity_id_floor(quantity: int) -> int:
    best = 0
    for index, value in enumerate(MARKET_QUANTITIES):
        if value > quantity:
            break
        best = index
    return best


def _quantity_chunks(quantity: int) -> list[int]:
    chunks: list[int] = []
    remaining = quantity
    for value in reversed(MARKET_QUANTITIES):
        while value <= remaining:
            chunks.append(value)
            remaining -= value
    if remaining:
        raise RuntimeError(f"cannot decompose market quantity {quantity}")
    return chunks


def _encode_market_orders(
    orders: Any, audit: Audit
) -> tuple[list[tuple[int, int | None]], bool] | None:
    """Map an official order list to at most ten policy slots.

    When exact quantity decomposition fits, repeated slots reproduce the
    official order exactly.  Otherwise each official order keeps one slot and
    uses the native floor/cap quantity mapping.  This retains strategic order
    choice while recording that the quantity was lossy.
    """
    audit.counts["market_rows"] += 1
    if not isinstance(orders, list) or len(orders) > MARKET_SLOTS:
        audit.counts["market_invalid_rows"] += 1
        return None
    parsed: list[tuple[int, int | None]] = []
    exact_expanded: list[tuple[int, int | None]] = []
    exact = True
    for order in orders:
        decoded = _market_command_id(order)
        if decoded is None:
            audit.counts["market_unknown_orders"] += 1
            audit.counts["market_invalid_rows"] += 1
            return None
        command, quantity = decoded
        op = str(order[0]).upper()
        audit.market_actions[op] += 1
        parsed.append((command, quantity))
        if quantity is None:
            exact_expanded.append((command, None))
            continue
        audit.market_quantities[str(quantity)] += 1
        chunks = _quantity_chunks(quantity)
        exact_expanded.extend(
            (command, MARKET_QUANTITIES.index(chunk)) for chunk in chunks
        )
    if len(exact_expanded) <= MARKET_SLOTS:
        encoded = exact_expanded
    else:
        exact = False
        audit.counts["market_slot_overflow_rows"] += 1
        encoded = []
        for command, quantity in parsed:
            if quantity is None:
                encoded.append((command, None))
                continue
            quantity_id = _quantity_id_floor(quantity)
            if MARKET_QUANTITIES[quantity_id] != quantity:
                audit.counts["market_lossy_quantities"] += 1
            encoded.append((command, quantity_id))
    audit.counts["market_orders"] += len(parsed)
    audit.counts["market_encoded_slots"] += len(encoded)
    if exact:
        audit.counts["market_exact_rows"] += 1
    else:
        audit.counts["market_lossy_rows"] += 1
    return encoded, exact


def _head_legal(mask: np.ndarray, head: int, action: int) -> bool:
    if action < 0 or action >= CODEC.HEAD_SIZES[head]:
        return False
    return bool(mask[int(CODEC.HEAD_OFFSETS[head]) + action])


def _build_row(observation: dict[str, Any], action: dict[str, Any]) -> tuple[
    np.ndarray, np.ndarray, np.ndarray, Audit
]:
    audit = Audit()
    encoded_obs = np.asarray(CODEC.encode_observation(observation), dtype=np.uint8)
    if encoded_obs.shape != (OBS_SIZE,):
        raise ValueError(f"encoder returned shape {encoded_obs.shape}")
    mask = np.asarray(CODEC.action_mask(observation), dtype=np.bool_)
    if mask.shape != (MASK_SIZE,):
        raise ValueError(f"action mask returned shape {mask.shape}")
    expert = np.full(NUM_HEADS, -1.0, dtype="<f4")

    farm = observation["farms"][int(observation.get("player", 0))]
    observed_hands = len(farm.get("hands", ()))
    if observed_hands > DIRECT_HANDS and OVERFLOW_COHORTS == 0:
        raise ValueError(
            f"observation has {observed_hands} hands but policy capacity is "
            f"{DIRECT_HANDS}"
        )
    demonstrated_hands = action.get("hands", [])
    if not isinstance(demonstrated_hands, list):
        demonstrated_hands = []
    padded_hands = list(demonstrated_hands[:observed_hands])
    padded_hands.extend([["PASS"]] * (observed_hands - len(padded_hands)))

    farmer_action = action.get("farmer", ["PASS"])
    for demonstrated in [farmer_action, *padded_hands]:
        name = (str(demonstrated[0]).upper()
                if isinstance(demonstrated, (list, tuple)) and demonstrated
                else "INVALID")
        audit.unit_actions[name] += 1

    farmer_id, farmer_quantity_exact = _unit_action_id(farmer_action)
    audit.counts["unit_heads_seen"] += 1
    if farmer_id is None or not _head_legal(mask, 0, farmer_id):
        audit.counts["illegal_unit_heads"] += 1
        audit.counts["rows_skipped_illegal_farmer"] += 1
        # expert[0] < 0 intentionally makes the BC trainer skip this row.
    else:
        expert[0] = farmer_id
        audit.counts["unit_heads_labeled"] += 1
        if not farmer_quantity_exact:
            audit.counts["unit_quantity_lossy"] += 1

    for hand in range(DIRECT_HANDS):
        head = 1 + hand
        hand_action = padded_hands[hand] if hand < observed_hands else ["PASS"]
        action_id, quantity_exact = _unit_action_id(hand_action)
        audit.counts["unit_heads_seen"] += 1
        if action_id is None or not _head_legal(mask, head, action_id):
            audit.counts["illegal_unit_heads"] += 1
            continue
        expert[head] = action_id
        audit.counts["unit_heads_labeled"] += 1
        if not quantity_exact:
            audit.counts["unit_quantity_lossy"] += 1

    for cohort in range(OVERFLOW_COHORTS):
        head = 1 + DIRECT_HANDS + cohort
        indices = range(DIRECT_HANDS + cohort, observed_hands, OVERFLOW_COHORTS)
        cohort_actions = [padded_hands[index] for index in indices]
        audit.counts["unit_heads_seen"] += 1
        if not cohort_actions:
            action_ids = [(0, True)]
        else:
            action_ids = [_unit_action_id(value) for value in cohort_actions]
            audit.counts["overflow_cohort_rows"] += 1
        ids = {value[0] for value in action_ids}
        if None in ids or len(ids) != 1:
            audit.counts["overflow_cohort_conflicts"] += 1
            continue
        action_id = int(next(iter(ids)))
        if not _head_legal(mask, head, action_id):
            audit.counts["illegal_unit_heads"] += 1
            continue
        expert[head] = action_id
        audit.counts["unit_heads_labeled"] += 1
        audit.counts["overflow_cohort_labeled"] += bool(cohort_actions)
        audit.counts["unit_quantity_lossy"] += sum(
            not quantity_exact for _, quantity_exact in action_ids
        )

    market_result = _encode_market_orders(action.get("market", []), audit)
    market_legal = market_result is not None
    market_exact = False
    if market_result is not None:
        encoded_orders, market_exact = market_result
        for slot in range(MARKET_SLOTS):
            head = UNIT_HEADS + 3 * slot
            if slot < len(encoded_orders):
                command, quantity_id = encoded_orders[slot]
                values = (1, command, 0 if quantity_id is None else quantity_id)
                active_values = 2 if quantity_id is None else 3
            else:
                values = (0, 0, 0)
                active_values = 1 if slot == len(encoded_orders) else 0
            for node in range(active_values):
                if not _head_legal(mask, head + node, values[node]):
                    market_legal = False
                    audit.counts["illegal_market_heads"] += 1
            expert[head:head + 3] = values
        if not market_legal:
            expert[UNIT_HEADS:] = -1.0
            audit.counts["market_mask_rejected_rows"] += 1
        else:
            audit.counts["market_usable_rows"] += 1

    farmer_usable = expert[0] >= 0
    unit_usable = bool(np.all(expert[:UNIT_HEADS] >= 0))
    if farmer_usable and unit_usable and market_legal:
        audit.counts["whole_row_head_representable"] += 1
        if market_exact:
            audit.counts["whole_row_exact_quantity"] += 1
    audit.counts["rows"] += 1
    packed = np.packbits(mask, bitorder="little")
    if packed.shape != (MASK_BYTES,):
        raise RuntimeError(f"packed mask shape mismatch: {packed.shape}")
    return encoded_obs, expert, packed, audit


def _validate_episode(
    episode: dict[str, Any], minimum_version: tuple[int, ...], expected_steps: int,
    exact_version: tuple[int, ...] | None = None,
) -> str | None:
    if episode.get("name") != "kaggriculture":
        return "wrong_environment"
    module_version = _version_tuple(episode.get("module_version", "0"))
    if exact_version is not None and module_version != exact_version:
        return "wrong_module_version"
    if module_version < minimum_version:
        return "old_module_version"
    configuration = episode.get("configuration", {})
    if int(configuration.get("episodeSteps", expected_steps)) != expected_steps:
        return "wrong_episode_steps"
    steps = episode.get("steps")
    if not isinstance(steps, list) or len(steps) != expected_steps:
        return "incomplete_steps"
    if episode.get("statuses") != ["DONE", "DONE"]:
        return "not_done"
    rewards = episode.get("rewards")
    if not isinstance(rewards, list) or len(rewards) != 2:
        return "missing_rewards"
    for step in steps:
        if not isinstance(step, list) or len(step) != 2:
            return "bad_step_shape"
        for player in range(2):
            record = step[player]
            if not isinstance(record.get("observation"), dict):
                return "missing_observation"
            if not isinstance(record.get("action"), dict):
                return "missing_action"
    return None


def _selected_players(
    episode: dict[str, Any], mode: str, min_final_money: float
) -> list[int]:
    rewards = [float(value) for value in episode["rewards"]]
    if mode == "winner":
        best = max(rewards)
        players = [player for player, reward in enumerate(rewards) if reward == best]
    else:
        players = [0, 1]
    return [player for player in players if rewards[player] >= min_final_money]


def _build_trajectory(
    episode: dict[str, Any], player: int, expected_steps: int
) -> tuple[np.ndarray, np.ndarray, np.ndarray, Audit]:
    observations = np.empty((expected_steps, OBS_SIZE), dtype=np.uint8)
    experts = np.empty((expected_steps, NUM_HEADS), dtype="<f4")
    masks = np.empty((expected_steps, MASK_BYTES), dtype=np.uint8)
    audit = Audit()
    # Kaggle stores the action chosen from observation t in record t + 1,
    # alongside the resulting observation.  Record zero contains only the
    # framework's default PASS action; the terminal record has no following
    # decision.  Align 719 real labels with observations 0..718 and pad the
    # final recurrent row with expert[0] = -1 so the BC kernel ignores it.
    for turn in range(expected_steps):
        observation = episode["steps"][turn][player]["observation"]
        if int(observation.get("player", player)) != player:
            raise ValueError(f"turn {turn}: observation player mismatch")
        observation.setdefault("player", player)
        observation.setdefault("step", turn)
        if turn + 1 < expected_steps:
            action = episode["steps"][turn + 1][player]["action"]
            row_obs, row_expert, row_mask, row_audit = _build_row(
                observation, action
            )
        else:
            row_obs = np.asarray(CODEC.encode_observation(observation), dtype=np.uint8)
            mask = np.asarray(CODEC.action_mask(observation), dtype=np.bool_)
            row_expert = np.full(NUM_HEADS, -1.0, dtype="<f4")
            row_mask = np.packbits(mask, bitorder="little")
            row_audit = Audit()
            row_audit.counts["rows"] += 1
            row_audit.counts["padding_rows"] += 1
        observations[turn] = row_obs
        experts[turn] = row_expert
        masks[turn] = row_mask
        audit.merge(row_audit)
    audit.counts["trajectories"] += 1
    audit.final_money.append(float(episode["rewards"][player]))
    return observations, experts, masks, audit


def _copy_section(source: pathlib.Path, destination, expected: int) -> None:
    copied = 0
    with source.open("rb") as stream:
        while copied < expected:
            chunk = stream.read(min(16 * 1024 * 1024, expected - copied))
            if not chunk:
                raise ValueError(f"truncated temporary section: {source}")
            destination.write(chunk)
            copied += len(chunk)


def _write_json_atomic(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent,
        prefix=f".{path.name}.", delete=False
    ) as stream:
        temporary = pathlib.Path(stream.name)
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
    temporary.replace(path)


def _write_manifest_atomic(path: pathlib.Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ("episode_id", "source", "player", "agent", "final_money", "winner",
              "module_version", "rows")
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", newline="", dir=path.parent,
        prefix=f".{path.name}.", delete=False
    ) as stream:
        temporary = pathlib.Path(stream.name)
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Import official elite Kaggriculture replays as BC v2"
    )
    parser.add_argument("inputs", nargs="+", help="JSON/JSON.GZ/ZIP paths or globs")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--audit-json", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--audit-only", action="store_true")
    parser.add_argument("--limit", type=int, default=0, help="accepted episode limit")
    parser.add_argument("--minimum-version", default="1.32.7")
    parser.add_argument(
        "--exact-version",
        help="accept only this exact simulator version (recommended for BC)",
    )
    parser.add_argument("--steps", type=int, default=EXPECTED_STEPS)
    parser.add_argument("--players", choices=("both", "winner"), default="both")
    parser.add_argument("--min-final-money", type=float, default=0.0)
    args = parser.parse_args()
    if not args.audit_only and args.output is None:
        parser.error("--output is required unless --audit-only is used")
    if args.steps < 1:
        parser.error("--steps must be positive")
    if args.limit < 0:
        parser.error("--limit cannot be negative")

    inputs = _expand_inputs(args.inputs)
    audit = Audit()
    manifest_rows: list[dict[str, Any]] = []
    section_paths: list[pathlib.Path] = []
    section_streams = []
    output = args.output
    if output is not None:
        output = output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
    try:
        if not args.audit_only:
            assert output is not None
            for label in ("obs", "expert", "mask"):
                stream = tempfile.NamedTemporaryFile(
                    mode="w+b", dir=output.parent,
                    prefix=f".{output.name}.{label}.", delete=False
                )
                section_streams.append(stream)
                section_paths.append(pathlib.Path(stream.name))

        accepted = 0
        minimum_version = _version_tuple(args.minimum_version)
        exact_version = (
            _version_tuple(args.exact_version) if args.exact_version else None
        )
        for source, episode in _iter_replays(inputs):
            audit.counts["episodes_seen"] += 1
            module_version = str(episode.get("module_version", "unknown"))
            audit.module_versions[module_version] += 1
            reason = _validate_episode(
                episode, minimum_version, args.steps, exact_version
            )
            if reason is not None:
                audit.skip_reasons[reason] += 1
                continue
            players = _selected_players(episode, args.players, args.min_final_money)
            if not players:
                audit.skip_reasons["player_filter"] += 1
                continue
            built = []
            try:
                for player in players:
                    built.append((player, _build_trajectory(episode, player, args.steps)))
            except Exception as error:  # keep a corrupt replay from killing a daily job
                audit.skip_reasons[f"conversion:{type(error).__name__}"] += 1
                print(f"skip {source}: {error}", file=sys.stderr)
                continue

            audit.counts["episodes_accepted"] += 1
            accepted += 1
            rewards = [float(value) for value in episode["rewards"]]
            agents = episode.get("info", {}).get("Agents", [])
            best = max(rewards)
            for player, (obs, expert, mask, trajectory_audit) in built:
                audit.merge(trajectory_audit)
                if not args.audit_only:
                    section_streams[0].write(obs.tobytes(order="C"))
                    section_streams[1].write(expert.tobytes(order="C"))
                    section_streams[2].write(mask.tobytes(order="C"))
                agent_name = ""
                if player < len(agents) and isinstance(agents[player], dict):
                    agent_name = str(agents[player].get("Name", ""))
                manifest_rows.append({
                    "episode_id": episode.get("info", {}).get(
                        "EpisodeId", episode.get("id", "")
                    ),
                    "source": source,
                    "player": player,
                    "agent": agent_name,
                    "final_money": rewards[player],
                    "winner": int(rewards[player] == best),
                    "module_version": module_version,
                    "rows": args.steps,
                })
            if accepted % 25 == 0:
                print(
                    f"accepted={accepted} trajectories={audit.counts['trajectories']} "
                    f"rows={audit.counts['rows']}",
                    flush=True,
                )
            if args.limit and accepted >= args.limit:
                break

        for stream in section_streams:
            stream.flush()
            stream.close()
        section_streams.clear()

        games = int(audit.counts["trajectories"])
        count = games * args.steps
        if games == 0:
            raise RuntimeError("no replay trajectories passed validation and filters")
        if not args.audit_only:
            assert output is not None
            expected_sizes = (
                count * OBS_SIZE,
                count * NUM_HEADS * 4,
                count * MASK_BYTES,
            )
            for path, expected in zip(section_paths, expected_sizes):
                if path.stat().st_size != expected:
                    raise RuntimeError(
                        f"temporary section size mismatch: {path} "
                        f"{path.stat().st_size} != {expected}"
                    )
            with tempfile.NamedTemporaryFile(
                mode="wb", dir=output.parent,
                prefix=f".{output.name}.final.", delete=False
            ) as stream:
                final_temporary = pathlib.Path(stream.name)
                stream.write(HEADER.pack(
                    MAGIC, VERSION, count, OBS_SIZE, NUM_HEADS, MASK_BYTES,
                    games, args.steps,
                ))
                for path, expected in zip(section_paths, expected_sizes):
                    _copy_section(path, stream, expected)
            final_temporary.replace(output)

        audit_path = args.audit_json
        if audit_path is None and output is not None:
            audit_path = output.with_suffix(output.suffix + ".audit.json")
        if audit_path is not None:
            _write_json_atomic(audit_path, audit.as_dict())
        manifest_path = args.manifest
        if manifest_path is None and output is not None:
            manifest_path = output.with_suffix(output.suffix + ".players.tsv")
        if manifest_path is not None:
            _write_manifest_atomic(manifest_path, manifest_rows)

        report = audit.as_dict()
        print(json.dumps({
            "episodes": report["counts"].get("episodes_accepted", 0),
            "trajectories": games,
            "rows": count,
            "whole_row_head_representable": report["ratios"][
                "whole_row_head_representable"
            ],
            "market_exact": report["ratios"]["market_exact"],
            "market_usable": report["ratios"]["market_usable"],
            "overflow_cohort_conflict": report["ratios"][
                "overflow_cohort_conflict"
            ],
            "output": str(output) if output is not None else None,
        }, indent=2))
        return 0
    finally:
        for stream in section_streams:
            stream.close()
        for path in section_paths:
            path.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
