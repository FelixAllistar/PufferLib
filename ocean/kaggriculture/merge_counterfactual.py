#!/usr/bin/env python3
"""Merge non-overlapping counterfactual TSV shards in state order.

Workers preserve source-bank order within each shard.  A heap merge keeps the
state/candidate groups contiguous for ranking objectives without loading the
whole dataset into memory, and verifies that every shard has the same schema.
"""

from __future__ import annotations

import argparse
import csv
import heapq
import json
import pathlib
from typing import Any


def _key(row: dict[str, str], source: int, line: int) -> tuple[int, int, int, int]:
    try:
        record = int(row.get("record_index", "0"))
    except (TypeError, ValueError):
        record = 0
    try:
        player = int(row.get("player", "0"))
    except (TypeError, ValueError):
        player = 0
    return record, player, source, line


def merge(inputs: list[pathlib.Path], output: pathlib.Path) -> dict[str, Any]:
    if not inputs:
        raise ValueError("at least one input shard is required")
    streams = []
    readers = []
    headers: list[str] | None = None
    for path in inputs:
        stream = path.open(encoding="utf-8", newline="")
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames is None:
            stream.close()
            raise ValueError(f"missing header: {path}")
        current = list(reader.fieldnames)
        if headers is None:
            headers = current
        elif current != headers:
            stream.close()
            raise ValueError(f"schema mismatch in {path}")
        streams.append(stream)
        readers.append(reader)
    assert headers is not None
    heap: list[tuple[tuple[int, int, int, int], int, int, dict[str, str]]] = []
    per_shard = [0] * len(readers)
    for source, reader in enumerate(readers):
        row = next(reader, None)
        if row is not None:
            per_shard[source] += 1
            heapq.heappush(heap, (_key(row, source, per_shard[source]), source, per_shard[source], row))
    output.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    previous: tuple[int, int] | None = None
    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=headers, delimiter="\t")
        writer.writeheader()
        while heap:
            _sort_key, source, line, row = heapq.heappop(heap)
            state_key = (int(row.get("record_index", 0)), int(row.get("player", 0)))
            if previous is not None and state_key < previous:
                raise ValueError("input shards are not ordered by record_index/player")
            previous = state_key
            writer.writerow(row)
            total += 1
            next_row = next(readers[source], None)
            if next_row is not None:
                per_shard[source] += 1
                heapq.heappush(
                    heap,
                    (_key(next_row, source, per_shard[source]), source, per_shard[source], next_row),
                )
    for stream in streams:
        stream.close()
    summary = {
        "format": "kaggriculture_counterfactual_merge_v1",
        "inputs": [str(path) for path in inputs],
        "output": str(output),
        "input_shards": len(inputs),
        "rows": total,
        "rows_per_shard": per_shard,
        "schema_fields": headers,
    }
    pathlib.Path(f"{output}.summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args(argv)
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    print(json.dumps(merge([pathlib.Path(value) for value in args.inputs], pathlib.Path(args.output)), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
