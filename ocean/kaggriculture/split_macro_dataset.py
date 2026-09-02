#!/usr/bin/env python3
"""Split a mode-2 KAGB dataset by complete replay trajectories.

The BC importer writes one manifest row per ``(episode_id, player)`` and then
stores all 720 rows for that trajectory contiguously in each KAGB section.
This utility uses only that manifest metadata to hold out a complete source
day (the latest day by default), so no neighboring turns from a replay can
land in both train and evaluation files.  It never edits the input dataset.

Example::

    python split_macro_dataset.py full.bc \
        --train-output train.bc --holdout-output holdout.bc
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
import re
import struct
from collections import Counter
from typing import Iterable


HEADER = struct.Struct("<8I")
MAGIC = 0x4B414742
VERSION = 2
OBS_BYTES = 1280
EXPERT_BYTES = 47 * 4
MASK_BYTES = 133
ROW_BYTES = OBS_BYTES + EXPERT_BYTES + MASK_BYTES


def _source_day(value: str) -> str:
    match = re.search(r"20\d\d-\d\d-\d\d", str(value))
    return match.group(0) if match else "unknown"


def _read_header(path: pathlib.Path) -> tuple[int, ...]:
    with path.open("rb") as stream:
        values = HEADER.unpack(stream.read(HEADER.size))
    magic, version, count, obs, expert, mask, games, steps = values
    if magic != MAGIC or version != VERSION:
        raise ValueError(f"unsupported KAGB dataset: {path}")
    if (obs, expert, mask, count) != (OBS_BYTES, 47, MASK_BYTES, games * steps):
        raise ValueError(f"expected mode-2 KAGB dimensions, got {values}")
    return values


def _load_manifest(path: pathlib.Path, games: int) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != games:
        raise ValueError(f"manifest rows {len(rows)} != dataset games {games}")
    required = {"episode_id", "source"}
    if not required.issubset(rows[0] if rows else {}):
        raise ValueError(f"manifest lacks required columns: {sorted(required)}")
    return rows


def _write_manifest(path: pathlib.Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0]) if rows else [
        "episode_id", "source", "player", "agent", "final_money", "winner",
        "module_version", "rows", "macro_mode",
    ]
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def _copy_games(
    source: pathlib.Path, destination: pathlib.Path, header: tuple[int, ...],
    game_indices: Iterable[int],
) -> int:
    _, _, _, _, _, _, _, steps = header
    indices = list(game_indices)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as src, destination.open("wb") as dst:
        dst.write(HEADER.pack(
            MAGIC, VERSION, len(indices) * steps, OBS_BYTES, 47, MASK_BYTES,
            len(indices), steps,
        ))
        # Each KAGB section is contiguous, with all observations followed by
        # experts and masks.  Preserve that section ordering in the output;
        # interleaving sections per game would make a malformed dataset.
        for section_offset, section_bytes in (
            (HEADER.size, OBS_BYTES),
            (HEADER.size + header[2] * OBS_BYTES, EXPERT_BYTES),
            (HEADER.size + header[2] * (OBS_BYTES + EXPERT_BYTES), MASK_BYTES),
        ):
            for game in indices:
                src.seek(section_offset + game * steps * section_bytes)
                remaining = steps * section_bytes
                while remaining:
                    chunk = src.read(min(16 * 1024 * 1024, remaining))
                    if not chunk:
                        raise ValueError(f"truncated KAGB section in {source}")
                    dst.write(chunk)
                    remaining -= len(chunk)
    return len(indices)


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(16 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def split(
    dataset: pathlib.Path, manifest: pathlib.Path, train_output: pathlib.Path,
    train_manifest: pathlib.Path, holdout_output: pathlib.Path,
    holdout_manifest: pathlib.Path, holdout_day: str | None = None,
    report: pathlib.Path | None = None,
) -> dict[str, object]:
    header = _read_header(dataset)
    rows = _load_manifest(manifest, header[6])
    days = sorted({_source_day(row.get("source", "")) for row in rows})
    selected_day = holdout_day or (days[-1] if days else "unknown")
    if selected_day not in days:
        raise ValueError(f"holdout day {selected_day!r} not found; available={days}")
    holdout_games = [
        index for index, row in enumerate(rows)
        if _source_day(row.get("source", "")) == selected_day
    ]
    train_games = [index for index in range(len(rows)) if index not in set(holdout_games)]
    if not train_games or not holdout_games:
        raise ValueError(
            f"split would be empty: train={len(train_games)} holdout={len(holdout_games)}"
        )
    _copy_games(dataset, train_output, header, train_games)
    _copy_games(dataset, holdout_output, header, holdout_games)
    _write_manifest(train_manifest, [rows[index] for index in train_games])
    _write_manifest(holdout_manifest, [rows[index] for index in holdout_games])
    payload: dict[str, object] = {
        "input": str(dataset), "input_manifest": str(manifest),
        "holdout_day": selected_day, "source_days": days,
        "input_games": len(rows), "train_games": len(train_games),
        "holdout_games": len(holdout_games), "steps": header[7],
        "mode2_abi": {"observation_bytes": OBS_BYTES, "heads": 47, "mask_bits": 1058},
        "train_output": str(train_output), "train_manifest": str(train_manifest),
        "holdout_output": str(holdout_output),
        "holdout_manifest": str(holdout_manifest),
        "train_source_days": dict(Counter(
            _source_day(rows[index].get("source", "")) for index in train_games
        )),
        "holdout_source_days": dict(Counter(
            _source_day(rows[index].get("source", "")) for index in holdout_games
        )),
        "sha256": {
            "input": _sha256(dataset), "train": _sha256(train_output),
            "holdout": _sha256(holdout_output),
        },
    }
    if report is not None:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--train-output", type=pathlib.Path, required=True)
    parser.add_argument("--train-manifest", type=pathlib.Path)
    parser.add_argument("--holdout-output", type=pathlib.Path, required=True)
    parser.add_argument("--holdout-manifest", type=pathlib.Path)
    parser.add_argument("--holdout-day")
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    manifest = args.manifest or args.dataset.with_suffix(args.dataset.suffix + ".players.tsv")
    train_manifest = args.train_manifest or args.train_output.with_suffix(
        args.train_output.suffix + ".players.tsv"
    )
    holdout_manifest = args.holdout_manifest or args.holdout_output.with_suffix(
        args.holdout_output.suffix + ".players.tsv"
    )
    payload = split(
        args.dataset, manifest, args.train_output, train_manifest,
        args.holdout_output, holdout_manifest, args.holdout_day, args.report,
    )
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
