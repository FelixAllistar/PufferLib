#!/usr/bin/env python3
"""Report per-turn expert-label ambiguity in a Kaggriculture BC dataset."""

from __future__ import annotations

import argparse
import collections
import mmap
import struct
from pathlib import Path


HEADER = struct.Struct("<8I")
MAGIC = 0x4B414742
VERSION = 2
UNIT_HEADS = 17
HEADS_PER_MARKET_SLOT = 3
MARKET_CONTINUE = 1
QUANTITY_COMMANDS = 19


def head_active(actions: tuple[int, ...], head: int) -> bool:
    if head < UNIT_HEADS:
        return True
    relative = head - UNIT_HEADS
    slot, node = divmod(relative, HEADS_PER_MARKET_SLOT)
    for previous in range(slot):
        if actions[UNIT_HEADS + HEADS_PER_MARKET_SLOT * previous] != MARKET_CONTINUE:
            return False
    continue_head = UNIT_HEADS + HEADS_PER_MARKET_SLOT * slot
    if node == 0:
        return True
    if actions[continue_head] != MARKET_CONTINUE:
        return False
    if node == 1:
        return True
    return actions[continue_head + 1] < QUANTITY_COMMANDS


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--steps", type=int, default=26)
    args = parser.parse_args()

    with args.dataset.open("rb") as stream:
        header = HEADER.unpack(stream.read(HEADER.size))
        magic, version, count, row_obs, row_expert, row_mask, games, steps = header
        if magic != MAGIC or version != VERSION:
            raise SystemExit("not a Kaggriculture BC v2 dataset")
        if count != games * steps:
            raise SystemExit("dataset count does not match games * steps")
        expert_offset = HEADER.size + count * row_obs
        expert_row = struct.Struct(f"<{row_expert}f")
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            print(f"games={games} steps={steps} heads={row_expert}")
            for turn in range(min(args.steps, steps)):
                signatures: collections.Counter[tuple[tuple[int, int], ...]] = (
                    collections.Counter()
                )
                head_counts = [collections.Counter() for _ in range(row_expert)]
                head_samples = [0] * row_expert
                for game in range(games):
                    row = game * steps + turn
                    offset = expert_offset + row * expert_row.size
                    raw = expert_row.unpack_from(data, offset)
                    actions = tuple(int(value) for value in raw)
                    signature = []
                    for head, action in enumerate(actions):
                        if not head_active(actions, head):
                            continue
                        signature.append((head, action))
                        head_counts[head][action] += 1
                        head_samples[head] += 1
                    signatures[tuple(signature)] += 1

                row_mode = signatures.most_common(1)[0][1]
                ambiguous = []
                for head, counts in enumerate(head_counts):
                    if not counts:
                        continue
                    action, mode = counts.most_common(1)[0]
                    if len(counts) > 1:
                        ambiguous.append(
                            f"h{head}:a{action}={mode}/{head_samples[head]}"
                        )
                detail = " ".join(ambiguous) if ambiguous else "none"
                print(
                    f"turn={turn:02d} row_mode={row_mode}/{games} "
                    f"unique_rows={len(signatures)} ambiguous={detail}"
                )


if __name__ == "__main__":
    main()
