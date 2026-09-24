#!/usr/bin/env python3
"""Measure live executor movement, revalidation, and acknowledgement latency."""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from collections import Counter, defaultdict
from pathlib import Path


DURATION = re.compile(r"\bduration_ms=(\d+)")


def quantiles(values: list[float]) -> dict[str, float | int]:
    values = sorted(v for v in values if math.isfinite(v) and v >= 0)
    if not values:
        return {"n": 0}

    def at(fraction: float) -> float:
        return values[min(len(values) - 1, round((len(values) - 1) * fraction))]

    return {
        "n": len(values),
        "p50_ms": round(at(0.50), 1),
        "p90_ms": round(at(0.90), 1),
        "p99_ms": round(at(0.99), 1),
        "mean_ms": round(statistics.fmean(values), 1),
    }


def calibrate(paths: list[Path]) -> dict:
    movement: dict[str, list[float]] = defaultdict(list)
    post_move: dict[str, list[float]] = defaultdict(list)
    pointer_total: dict[str, list[float]] = defaultdict(list)
    confirmations: dict[str, list[float]] = defaultdict(list)
    cancellations: Counter[str] = Counter()
    scheduled_at: dict[str, int] = {}
    moved_at: dict[tuple[str, int], int] = {}
    moved_from_schedule: dict[tuple[str, int], int] = {}

    for path in paths:
        with path.open(encoding="utf-8-sig") as stream:
            for line in stream:
                try:
                    event = json.loads(line)
                except (json.JSONDecodeError, UnicodeDecodeError):
                    continue
                event_type = event.get("type")
                payload = event.get("payload") or {}
                action = payload.get("action_id")
                mono_ms = int(event.get("mono_ms") or 0)
                if not action:
                    continue

                if event_type == "scheduled" and payload.get("disposition") == "Immediate":
                    scheduled_at[action] = mono_ms
                elif event_type == "pointer_landing":
                    phase = payload.get("phase")
                    attempt = int(payload.get("attempt") or 0)
                    key = (action, attempt)
                    if phase == "move_complete":
                        match = DURATION.search(payload.get("path_summary") or "")
                        if match:
                            movement[action].append(float(match.group(1)))
                        moved_at[key] = mono_ms
                        if action in scheduled_at:
                            moved_from_schedule[key] = scheduled_at[action]
                    elif phase == "revalidated" and key in moved_at:
                        post_move[action].append(float(mono_ms - moved_at.pop(key)))
                        if key in moved_from_schedule:
                            pointer_total[action].append(
                                float(mono_ms - moved_from_schedule.pop(key))
                            )
                elif event_type == "confirmation":
                    latency = payload.get("latency_ms")
                    if isinstance(latency, (int, float)):
                        confirmations[action].append(float(latency))
                elif event_type == "cancelled":
                    cancellations[action] += 1

    pointer_actions = sorted(set(movement) | set(post_move) | set(pointer_total))
    confirmation_actions = sorted(confirmations)
    return {
        "sources": [str(path) for path in paths],
        "pointer": {
            action: {
                "movement": quantiles(movement[action]),
                "post_move_revalidation": quantiles(post_move[action]),
                "proposal_to_revalidated": quantiles(pointer_total[action]),
                "cancellations": cancellations[action],
            }
            for action in pointer_actions
        },
        "confirmation": {
            action: quantiles(confirmations[action])
            for action in confirmation_actions
        },
        "all_pointer_proposal_to_revalidated": quantiles(
            [value for values in pointer_total.values() for value in values]
        ),
        "all_cancellations": sum(cancellations.values()),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("events", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = calibrate(args.events)
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
