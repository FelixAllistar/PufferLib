"""Deterministic, observation-safe sampling of replay-state-bank rows.

The state bank is intentionally ordered by replay/index construction rather
than by experimental design.  Taking ``rows[:N]`` therefore over-represents
whatever scenario happened to be written first (the pilot exposed this as a
late-game/hold-heavy sample).  This module keeps the bank format unchanged and
selects a reproducible mixture of time windows, episodes, and the scenario tags
already stored in ``index_rows``.
"""

from __future__ import annotations

import hashlib
import json
from collections import defaultdict
from typing import Any, Iterable


SELECTIONS = ("first", "uniform", "stratified")
TIME_BUCKETS = ("early", "growth", "mid", "late")
SCENARIO_PRIORITY = (
    "early_expansion", "medium_investment", "short_investment",
    "buy_opportunity", "sell_now", "harvest_ready", "maintenance_profitable",
    "recovery", "hold_for_later", "liquidation_1d", "liquidation_3d",
    "liquidation_6d", "carrot_opportunity", "tomato_opportunity",
    "egg_opportunity",
)


def _int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _row_hash(row: dict[str, str], seed: int) -> int:
    key = str(row.get("record_index", ""))
    if not key:
        key = f"{row.get('episode_id', '')}:{row.get('turn', '')}"
    digest = hashlib.sha256(f"{seed}:{key}".encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "little")


def time_bucket(row: dict[str, str], episode_steps: int = 720) -> str:
    """Return a stable coarse season phase for a bank manifest row."""

    turn = max(0, _int(row.get("turn")))
    steps = max(4, int(episode_steps))
    # The default 720-turn game maps to 0-119, 120-239, 240-479, 480+.
    # Fractions preserve the same intent for shorter curriculum episodes.
    boundaries = (steps // 6, steps // 3, (steps * 2) // 3)
    if turn < boundaries[0]:
        return "early"
    if turn < boundaries[1]:
        return "growth"
    if turn < boundaries[2]:
        return "mid"
    return "late"


def row_scenarios(row: dict[str, str]) -> frozenset[str]:
    """Extract S0a scenario tags without opening replay/private state data."""

    raw = row.get("index_rows", "")
    if not raw:
        return frozenset()
    try:
        values = json.loads(raw)
    except (TypeError, ValueError, json.JSONDecodeError):
        return frozenset()
    if not isinstance(values, list):
        values = [values]
    tags: set[str] = set()
    for value in values:
        if not isinstance(value, dict):
            continue
        scenarios = value.get("scenarios", "")
        if isinstance(scenarios, str):
            tags.update(item.strip() for item in scenarios.split(",") if item.strip())
        elif isinstance(scenarios, (list, tuple, set)):
            tags.update(str(item).strip() for item in scenarios if str(item).strip())
    return frozenset(tags)


def _balanced_quotas(limit: int, groups: dict[str, list[dict[str, str]]]) -> dict[str, int]:
    """Allocate a limit across nonempty groups, redistributing remainders."""

    names = [name for name in TIME_BUCKETS if groups.get(name)]
    if not names or limit <= 0:
        return {}
    quotas = {name: min(len(groups[name]), limit // len(names)) for name in names}
    remaining = limit - sum(quotas.values())
    # Round-robin gives earlier phases no special privilege; the order is only
    # a deterministic tie-breaker after all nonempty buckets receive a share.
    cursor = 0
    while remaining > 0:
        name = names[cursor % len(names)]
        if quotas[name] < len(groups[name]):
            quotas[name] += 1
            remaining -= 1
        cursor += 1
        if cursor > limit * (len(names) + 1):
            break
    return quotas


def _choose_stratified(
    rows: list[dict[str, str]], limit: int, seed: int, episode_steps: int,
) -> list[dict[str, str]]:
    groups: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[time_bucket(row, episode_steps)].append(row)
    quotas = _balanced_quotas(limit, groups)
    chosen: list[dict[str, str]] = []
    for bucket in TIME_BUCKETS:
        candidates = groups.get(bucket, [])
        quota = quotas.get(bucket, 0)
        if not candidates or quota <= 0:
            continue
        ordered = sorted(candidates, key=lambda row: (_row_hash(row, seed), _int(row.get("record_index"))))
        remaining = list(ordered)
        selected: list[dict[str, str]] = []
        covered: set[str] = set()
        episodes: set[str] = set()
        while remaining and len(selected) < quota:
            def score(row: dict[str, str]) -> tuple[int, int, int]:
                tags = row_scenarios(row)
                new_tags = len(tags.difference(covered))
                new_episode = int(str(row.get("episode_id", "")) not in episodes)
                # Scenario coverage is the primary goal; episode coverage is
                # next; the hash makes ties deterministic.
                return (new_tags, new_episode, -_row_hash(row, seed))

            row = max(remaining, key=score)
            remaining.remove(row)
            selected.append(row)
            covered.update(row_scenarios(row))
            episodes.add(str(row.get("episode_id", "")))
        chosen.extend(selected)
    # Keep the output stable and easy to audit by bank record order within the
    # selected set, rather than exposing the greedy tag-coverage order.
    chosen.sort(key=lambda row: _int(row.get("record_index")))
    return chosen


def select_rows(
    rows: Iterable[dict[str, str]], limit: int = 0, *, strategy: str = "stratified",
    seed: int = 707, episode_steps: int = 720,
) -> list[dict[str, str]]:
    """Select at most ``limit`` manifest rows reproducibly.

    ``limit=0`` returns every row in source order.  ``first`` preserves the
    historical behavior, ``uniform`` samples globally by stable hash, and
    ``stratified`` balances coarse time windows while covering S0a tags.
    """

    if strategy not in SELECTIONS:
        raise ValueError(f"unknown state-selection strategy: {strategy!r}")
    materialized = list(rows)
    if limit <= 0 or limit >= len(materialized):
        return materialized
    if strategy == "first":
        return materialized[:limit]
    if strategy == "uniform":
        return sorted(
            materialized,
            key=lambda row: (_row_hash(row, seed), _int(row.get("record_index"))),
        )[:limit]
    return _choose_stratified(materialized, limit, seed, episode_steps)


def shard_rows(
    rows: Iterable[dict[str, str]], shard_index: int = 0, shard_count: int = 1,
) -> list[dict[str, str]]:
    """Partition an already-selected row list without overlap.

    The partition is stable in source order and is intended for independent
    native workers.  It is deliberately separate from sampling: a caller can
    first choose a reproducible stratified population and then process every
    selected row exactly once across workers.
    """

    count = int(shard_count)
    index = int(shard_index)
    if count < 1 or index < 0 or index >= count:
        raise ValueError("shard_index must be in [0, shard_count) and shard_count >= 1")
    materialized = list(rows)
    return materialized[index::count]
