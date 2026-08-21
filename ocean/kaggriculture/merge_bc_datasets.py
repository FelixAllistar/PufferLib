#!/usr/bin/env python3
"""Concatenate compatible Kaggriculture BC v2 datasets.

Each input keeps whole games together.  The trainer's validation split is
therefore still made on game boundaries after the merge, while observations,
expert heads, and masks remain byte-for-byte unchanged.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import tempfile


HEADER = struct.Struct("<8I")
MAGIC = 0x4B414742  # KAGB
VERSION = 2


def read_header(path: pathlib.Path) -> tuple[int, ...]:
    with path.open("rb") as stream:
        raw = stream.read(HEADER.size)
    if len(raw) != HEADER.size:
        raise ValueError(f"truncated BC header: {path}")
    values = HEADER.unpack(raw)
    if values[0] != MAGIC or values[1] != VERSION:
        raise ValueError(f"unsupported BC dataset: {path}")
    magic, version, count, row_obs, row_expert, row_mask, games, steps = values
    if games < 1 or steps < 1 or count != games * steps:
        raise ValueError(f"invalid game dimensions: {path}")
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    if args.output in args.inputs:
        raise SystemExit("output must not also be an input")

    headers = [read_header(path) for path in args.inputs]
    shape = headers[0][3:6] + (headers[0][7],)
    for path, header in zip(args.inputs, headers):
        candidate = header[3:6] + (header[7],)
        if candidate != shape:
            raise ValueError(
                f"incompatible dimensions in {path}: {candidate} != {shape}"
            )

    total_games = sum(header[6] for header in headers)
    total_steps = total_games * shape[3]
    output = args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="wb", dir=output.parent, prefix=f".{output.name}.", delete=False
    ) as stream:
        temporary = pathlib.Path(stream.name)
        stream.write(
            HEADER.pack(
                MAGIC,
                VERSION,
                total_steps,
                shape[0],
                shape[1],
                shape[2],
                total_games,
                shape[3],
            )
        )
        for path, header in zip(args.inputs, headers):
            expected = HEADER.size + header[2] * (header[3] + 4 * header[4] + header[5])
            if path.stat().st_size != expected:
                raise ValueError(f"truncated BC payload: {path}")
            with path.open("rb") as source:
                source.seek(HEADER.size)
                remaining = path.stat().st_size - HEADER.size
                while remaining:
                    chunk = source.read(min(8 * 1024 * 1024, remaining))
                    if not chunk:
                        raise ValueError(f"truncated BC payload: {path}")
                    stream.write(chunk)
                    remaining -= len(chunk)
    temporary.replace(output)
    print(
        f"merged {len(args.inputs)} datasets: {output} "
        f"({total_games} games, {total_steps} steps)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
