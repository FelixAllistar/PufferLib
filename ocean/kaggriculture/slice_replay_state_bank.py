#!/usr/bin/env python3
"""Slice a verified Kaggriculture reset-state bank without replaying episodes."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
from typing import Any

from build_replay_state_bank import (
    BANK_FORMAT_VERSION,
    BANK_HEADER,
    BANK_MAGIC,
    MANIFEST_FIELDS,
)
from select_replay_state_stage import STAGES


def record_scenarios(row: dict[str, str]) -> set[str]:
    scenarios: set[str] = set()
    for index_row in json.loads(row["index_rows"]):
        scenarios.update(value for value in index_row["scenarios"].split(",") if value)
    return scenarios


def slice_bank(args: argparse.Namespace) -> dict[str, Any]:
    source_bank = pathlib.Path(args.bank)
    source_manifest = pathlib.Path(args.manifest or f"{source_bank}.manifest.tsv")
    output = pathlib.Path(args.output)
    output_manifest = pathlib.Path(args.output_manifest or f"{output}.manifest.tsv")
    summary_path = pathlib.Path(args.summary or f"{output}.summary.json")
    requested = set(args.scenarios.split(",")) if args.scenarios else set(STAGES[args.stage])
    requested.discard("")
    if not requested:
        raise ValueError("at least one scenario is required")

    with source_manifest.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))

    output.parent.mkdir(parents=True, exist_ok=True)
    selected: list[dict[str, str]] = []
    with source_bank.open("rb") as src, output.open("wb+") as dst:
        header = src.read(BANK_HEADER.size)
        if len(header) != BANK_HEADER.size:
            raise ValueError(f"truncated bank header: {source_bank}")
        magic, bank_version, state_version, state_size, record_count, reserved = BANK_HEADER.unpack(header)
        if magic != BANK_MAGIC or bank_version != BANK_FORMAT_VERSION:
            raise ValueError(f"unsupported bank format: {source_bank}")
        if record_count != len(rows):
            raise ValueError(f"manifest has {len(rows)} rows but bank has {record_count} records")
        dst.write(BANK_HEADER.pack(magic, bank_version, state_version, state_size, 0, reserved))

        for row in rows:
            if requested.isdisjoint(record_scenarios(row)):
                continue
            byte_size = int(row["byte_size"])
            if byte_size != state_size:
                raise ValueError(f"record {row['record_index']} has unexpected size {byte_size}")
            src.seek(int(row["byte_offset"]))
            payload = src.read(byte_size)
            if len(payload) != byte_size:
                raise ValueError(f"truncated record {row['record_index']}")
            digest = hashlib.sha256(payload).hexdigest()
            if digest != row["sha256"]:
                raise ValueError(f"checksum mismatch for record {row['record_index']}")
            copied = dict(row)
            copied["record_index"] = str(len(selected))
            copied["byte_offset"] = str(dst.tell())
            selected.append(copied)
            dst.write(payload)

        dst.seek(0)
        dst.write(BANK_HEADER.pack(
            magic, bank_version, state_version, state_size, len(selected), reserved
        ))

    output_manifest.parent.mkdir(parents=True, exist_ok=True)
    with output_manifest.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=MANIFEST_FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(selected)

    summary = {
        "format": "kaggriculture_native_state_bank_slice_v1",
        "source_bank": str(source_bank),
        "source_records": record_count,
        "bank": str(output),
        "manifest": str(output_manifest),
        "record_count": len(selected),
        "scenarios": sorted(requested),
        "native_state_version": state_version,
        "native_state_size": state_size,
    }
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bank", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--output", required=True)
    parser.add_argument("--output-manifest")
    parser.add_argument("--summary")
    parser.add_argument("--stage", choices=tuple(STAGES), default="full")
    parser.add_argument("--scenarios", help="Comma-separated override for --stage")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    print(json.dumps(slice_bank(parse_args(argv)), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
