#!/usr/bin/env python3
"""Select whole recurrent trajectories from a Kaggriculture BC v2 dataset.

The replay importer writes one manifest row per 720-step player trajectory in
the same order as the binary dataset.  This tool filters those rows and copies
the corresponding contiguous slices from each section (observations, expert
heads, and packed masks).  Keeping the copy section-major is essential: simply
concatenating row records would silently corrupt the BC format.
"""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import struct
import tempfile


HEADER = struct.Struct("<8I")
MAGIC = 0x4B414742  # KAGB
VERSION = 2


def _read_header(path: pathlib.Path) -> tuple[int, ...]:
    with path.open("rb") as stream:
        raw = stream.read(HEADER.size)
    if len(raw) != HEADER.size:
        raise ValueError(f"truncated BC header: {path}")
    header = HEADER.unpack(raw)
    magic, version, count, row_obs, row_expert, row_mask, games, steps = header
    if magic != MAGIC or version != VERSION:
        raise ValueError(f"unsupported BC dataset: {path}")
    if games < 1 or steps < 1 or count != games * steps:
        raise ValueError(f"invalid BC dimensions: {path}")
    expected = HEADER.size + count * (row_obs + row_expert * 4 + row_mask)
    if path.stat().st_size != expected:
        raise ValueError(
            f"BC size mismatch: got {path.stat().st_size}, expected {expected}"
        )
    return header


def _read_manifest(path: pathlib.Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if reader.fieldnames is None:
            raise ValueError(f"manifest has no header: {path}")
        fields = list(reader.fieldnames)
        rows = list(reader)
    required = {"agent", "final_money", "winner", "rows"}
    missing = required.difference(fields)
    if missing:
        raise ValueError(f"manifest missing columns: {', '.join(sorted(missing))}")
    return fields, rows


def _copy_trajectory(source, destination, offset: int, size: int) -> None:
    source.seek(offset)
    remaining = size
    while remaining:
        chunk = source.read(min(8 * 1024 * 1024, remaining))
        if not chunk:
            raise ValueError("truncated BC trajectory")
        destination.write(chunk)
        remaining -= len(chunk)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Filter whole trajectories from a BC v2 dataset"
    )
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument(
        "--agent", action="append", default=[],
        help="exact agent name to retain; repeat to retain multiple agents",
    )
    parser.add_argument(
        "--source", action="append", default=[],
        help="archive stem to retain; repeat to keep a behavior-stable set",
    )
    parser.add_argument("--winner-only", action="store_true")
    parser.add_argument("--minimum-final-money", type=float, default=0.0)
    parser.add_argument(
        "--prefix-steps", type=int,
        help="retain only this many leading rows from every trajectory",
    )
    args = parser.parse_args()

    input_path = args.input.resolve()
    output_path = args.output.resolve()
    if input_path == output_path:
        parser.error("input and output must differ")
    manifest_path = args.manifest
    if manifest_path is None:
        manifest_path = input_path.with_suffix(input_path.suffix + ".players.tsv")
    manifest_path = manifest_path.resolve()

    header = _read_header(input_path)
    _, _, _, row_obs, row_expert, row_mask, games, steps = header
    output_steps = args.prefix_steps if args.prefix_steps is not None else steps
    if output_steps < 1 or output_steps > steps:
        parser.error(f"--prefix-steps must be in [1, {steps}]")
    fields, rows = _read_manifest(manifest_path)
    if len(rows) != games:
        raise ValueError(
            f"manifest rows ({len(rows)}) do not match BC games ({games})"
        )
    for index, row in enumerate(rows):
        if int(row["rows"]) != steps:
            raise ValueError(f"manifest row {index} has {row['rows']} steps")

    allowed_agents = set(args.agent)
    allowed_sources = set(args.source)
    selected: list[int] = []
    for index, row in enumerate(rows):
        if allowed_agents and row["agent"] not in allowed_agents:
            continue
        source = pathlib.Path(row.get("source", "").split(":", 1)[0]).stem
        if allowed_sources and source not in allowed_sources:
            continue
        if args.winner_only and str(row["winner"]).lower() not in {
            "1", "true", "yes"
        }:
            continue
        if float(row["final_money"]) < args.minimum_final_money:
            continue
        selected.append(index)
    if not selected:
        raise ValueError("selection produced no trajectories")

    count = len(selected) * output_steps
    section_rows = (row_obs, row_expert * 4, row_mask)
    input_count = games * steps
    section_offsets = (
        HEADER.size,
        HEADER.size + input_count * row_obs,
        HEADER.size + input_count * (row_obs + row_expert * 4),
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=output_path.parent,
            prefix=f".{output_path.name}.", delete=False,
        ) as destination:
            temporary = pathlib.Path(destination.name)
            destination.write(
                HEADER.pack(
                    MAGIC, VERSION, count, row_obs, row_expert, row_mask,
                    len(selected), output_steps,
                )
            )
            with input_path.open("rb") as source:
                for section_offset, row_size in zip(
                    section_offsets, section_rows, strict=True
                ):
                    input_trajectory_size = steps * row_size
                    output_trajectory_size = output_steps * row_size
                    for game in selected:
                        _copy_trajectory(
                            source,
                            destination,
                            section_offset + game * input_trajectory_size,
                            output_trajectory_size,
                        )
        temporary.replace(output_path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)

    output_manifest = output_path.with_suffix(output_path.suffix + ".players.tsv")
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", newline="", dir=output_path.parent,
        prefix=f".{output_manifest.name}.", delete=False,
    ) as stream:
        manifest_temporary = pathlib.Path(stream.name)
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for index in selected:
            output_row = dict(rows[index])
            output_row["rows"] = str(output_steps)
            writer.writerow(output_row)
    manifest_temporary.replace(output_manifest)

    money = [float(rows[index]["final_money"]) for index in selected]
    summary = {
        "input": str(input_path),
        "output": str(output_path),
        "trajectories": len(selected),
        "rows": count,
        "steps_per_trajectory": output_steps,
        "agents": sorted({rows[index]["agent"] for index in selected}),
        "sources": sorted({
            pathlib.Path(rows[index].get("source", "").split(":", 1)[0]).stem
            for index in selected
        }),
        "winner_only": args.winner_only,
        "minimum_final_money": args.minimum_final_money,
        "final_money_min": min(money),
        "final_money_mean": sum(money) / len(money),
        "final_money_max": max(money),
    }
    output_path.with_suffix(output_path.suffix + ".selection.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
