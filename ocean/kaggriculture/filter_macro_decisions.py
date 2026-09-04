#!/usr/bin/env python3
"""Keep only strategic opening decision changes in a mode-2 BC dataset.

The recurrent observations, masks, trajectory count, and 720-row layout are
preserved byte-for-byte.  Rows that are padding, outside the opening window,
routine executor work (HOLD/HARVEST/MAINTAIN), or an unchanged consecutive
macro decision receive ``expert[0] = -1`` and therefore contribute no BC loss.
The full observation stream still advances the recurrent state.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import shutil
import struct

import numpy as np


HEADER = struct.Struct("<8I")
MAGIC = 0x4B414742
VERSION = 2
OBS_BYTES = 1280
EXPERT_HEADS = 47
MASK_BYTES = 133

MACRO_HOLD = 0
MACRO_HARVEST = 29
MACRO_MAINTAIN = 30
DEFAULT_ROUTINE = frozenset((MACRO_HOLD, MACRO_HARVEST, MACRO_MAINTAIN))


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(16 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def filter_dataset(
    source: pathlib.Path,
    output: pathlib.Path,
    *,
    opening_steps: int = 61,
    routine_macros: frozenset[int] = DEFAULT_ROUTINE,
    source_manifest: pathlib.Path | None = None,
    output_manifest: pathlib.Path | None = None,
    report: pathlib.Path | None = None,
) -> dict[str, object]:
    with source.open("rb") as stream:
        header = HEADER.unpack(stream.read(HEADER.size))
    magic, version, count, obs_bytes, heads, mask_bytes, games, steps = header
    if magic != MAGIC or version != VERSION:
        raise ValueError(f"unsupported KAGB dataset: {source}")
    if (count, obs_bytes, heads, mask_bytes) != (
        games * steps, OBS_BYTES, EXPERT_HEADS, MASK_BYTES
    ):
        raise ValueError(f"expected mode-2 KAGB dimensions, got {header}")
    if not 1 <= opening_steps <= steps:
        raise ValueError(f"opening_steps must be in [1, {steps}], got {opening_steps}")

    obs_offset = HEADER.size
    expert_offset = obs_offset + count * obs_bytes
    mask_offset = expert_offset + count * heads * 4
    experts = np.memmap(
        source, dtype="<f4", mode="r", offset=expert_offset,
        shape=(games, steps, heads), order="C",
    )
    filtered = np.array(experts, copy=True)
    kept = collections.Counter()
    skipped = collections.Counter()
    signatures = collections.Counter()
    for game in range(games):
        previous_row_signature: tuple[int, int, int] | None = None
        for turn in range(steps):
            row = filtered[game, turn]
            macro = int(row[0])
            signature = tuple(int(value) for value in row[:3]) if macro >= 0 else None
            reason: str | None = None
            if macro < 0:
                reason = "already_unlabeled"
            elif turn >= opening_steps:
                reason = "after_opening"
            elif macro in routine_macros:
                reason = "routine"
            elif signature == previous_row_signature:
                reason = "unchanged_consecutive_decision"
            if reason is None:
                kept[str(macro)] += 1
                signatures[",".join(map(str, signature))] += 1
            else:
                row[:] = -1.0
                skipped[reason] += 1
            # Compare against the actual preceding expert row, including a
            # routine action.  Thus repeated strategic orders separated by a
            # real HOLD/maintenance frame remain distinct demonstrated events.
            previous_row_signature = signature

    output.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as src, output.open("wb") as dst:
        dst.write(src.read(HEADER.size + count * obs_bytes))
        dst.write(filtered.astype("<f4", copy=False).tobytes(order="C"))
        src.seek(mask_offset)
        shutil.copyfileobj(src, dst, length=16 * 1024 * 1024)
    expected_size = HEADER.size + count * (obs_bytes + heads * 4 + mask_bytes)
    if output.stat().st_size != expected_size:
        raise ValueError(
            f"filtered dataset size {output.stat().st_size} != {expected_size}"
        )

    if source_manifest is None:
        source_manifest = source.with_suffix(source.suffix + ".players.tsv")
    if output_manifest is None:
        output_manifest = output.with_suffix(output.suffix + ".players.tsv")
    if source_manifest.is_file():
        output_manifest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_manifest, output_manifest)

    payload: dict[str, object] = {
        "format": "kaggriculture_macro_decision_dataset_v1",
        "input": str(source),
        "output": str(output),
        "games": games,
        "steps": steps,
        "opening_steps": opening_steps,
        "routine_macros": sorted(routine_macros),
        "kept_decisions": int(sum(kept.values())),
        "kept_by_macro": dict(kept),
        "skipped": dict(skipped),
        "top_signatures": dict(signatures.most_common(30)),
        "manifest": str(output_manifest) if output_manifest.is_file() else None,
        "sha256": {"input": _sha256(source), "output": _sha256(output)},
    }
    if report is None:
        report = output.with_suffix(output.suffix + ".decision_filter.json")
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path)
    parser.add_argument("--output-manifest", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--opening-steps", type=int, default=61)
    parser.add_argument(
        "--routine-macros", default="0,29,30",
        help="comma-separated mode-2 macro IDs to make loss-free",
    )
    args = parser.parse_args()
    routine = frozenset(
        int(value) for value in args.routine_macros.split(",") if value.strip()
    )
    result = filter_dataset(
        args.dataset, args.output, opening_steps=args.opening_steps,
        routine_macros=routine, source_manifest=args.manifest,
        output_manifest=args.output_manifest, report=args.report,
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
