#!/usr/bin/env python3
"""Build parity-verified, resumable native KGState reset banks from replays.

The S0a index chooses useful (episode, turn, player) locations. This S0b tool
replays both seats through the native rule core, checks every traversed frame
against the official replay, stores one complete KGState per selected turn,
and proves each stored state resumes by round-tripping and stepping it once.
"""

from __future__ import annotations

import argparse
import collections
import csv
import ctypes
import hashlib
import json
import pathlib
import struct
import sys
from typing import Any

from index_replay_states import expand_inputs, iter_replays, validate_episode, version_tuple
from replay_native import (
    CAction,
    c_action,
    c_snapshot,
    canonical_replay_frame,
    first_difference,
    load_core,
    replay_config,
)


DEFAULT_LIB = pathlib.Path(__file__).with_name("build") / "libkaggriculture.so"
BANK_MAGIC = b"KGRSTB1\0"
BANK_FORMAT_VERSION = 1
BANK_HEADER = struct.Struct("<8sIIIIQ")
MANIFEST_FIELDS = (
    "record_index", "byte_offset", "byte_size", "sha256", "episode_id",
    "source", "module_version", "seed", "turn", "players", "state_keys",
    "expert_actions", "index_rows",
)


def episode_id(episode: dict[str, Any]) -> str:
    info = episode.get("info") or {}
    return str(info.get("EpisodeId", episode.get("id", "unknown")))


def load_targets(path: pathlib.Path) -> dict[tuple[str, str, int], list[dict[str, str]]]:
    targets: dict[tuple[str, str, int], list[dict[str, str]]] = collections.defaultdict(list)
    with path.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            key = (row["source"], row["episode_id"], int(row["turn"]))
            targets[key].append(dict(row))
    if not targets:
        raise ValueError(f"state index is empty: {path}")
    return dict(targets)


def action_pair(frame: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], Any]:
    pair: list[dict[str, Any]] = []
    for player in range(2):
        action = frame[player].get("action")
        pair.append(action if isinstance(action, dict) else {})
    return pair, (CAction * 2)(c_action(pair[0]), c_action(pair[1]))


def assert_parity(lib, state, frame, label: str) -> None:
    differences = first_difference(canonical_replay_frame(frame), c_snapshot(lib, state))
    if differences:
        raise AssertionError(f"{label} parity mismatch: {differences}")


def serialize_state(lib, state, state_size: int) -> bytes:
    buffer = (ctypes.c_ubyte * state_size)()
    if not lib.kg_state_serialize(state, buffer, state_size):
        raise RuntimeError("kg_state_serialize rejected a live native state")
    return bytes(buffer)


def verify_resume(lib, cfg, payload: bytes, frame, next_frame, label: str) -> None:
    restored = lib.kg_create(ctypes.byref(cfg))
    if not restored:
        raise RuntimeError(f"kg_create failed for {label} round trip")
    try:
        buffer = ctypes.create_string_buffer(payload, len(payload))
        if not lib.kg_state_deserialize(restored, buffer, len(payload)):
            raise AssertionError(f"{label} snapshot deserialize failed")
        assert_parity(lib, restored, frame, f"{label} restored")
        _, actions = action_pair(next_frame)
        lib.kg_step(restored, actions)
        assert_parity(lib, restored, next_frame, f"{label} resumed-next")
    finally:
        lib.kg_destroy(restored)


def build_bank(args: argparse.Namespace) -> dict[str, Any]:
    paths = expand_inputs(args.inputs)
    if not paths:
        raise SystemExit("no replay inputs found")
    targets = load_targets(pathlib.Path(args.index))
    lib = load_core(pathlib.Path(args.lib))
    state_size = int(lib.kg_state_serialized_size())
    state_version = int(lib.kg_state_serialization_version())
    output = pathlib.Path(args.output)
    manifest_path = pathlib.Path(args.manifest or f"{output}.manifest.tsv")
    summary_path = pathlib.Path(args.summary or f"{output}.summary.json")
    for path in (output, manifest_path, summary_path):
        path.parent.mkdir(parents=True, exist_ok=True)

    found: set[tuple[str, str, int]] = set()
    incompatible: set[tuple[str, str, int]] = set()
    incompatible_examples: list[str] = []
    records: list[dict[str, Any]] = []
    counts = collections.Counter()
    skipped = collections.Counter()
    with output.open("wb+") as bank:
        bank.write(BANK_HEADER.pack(
            BANK_MAGIC, BANK_FORMAT_VERSION, state_version, state_size, 0, 0
        ))
        for source, episode in iter_replays(paths):
            eid = episode_id(episode)
            episode_targets = {
                turn: rows for (row_source, row_episode, turn), rows in targets.items()
                if row_source == source and row_episode == eid
            }
            if not episode_targets:
                continue
            reason = validate_episode(episode, version_tuple(args.min_version))
            if reason:
                skipped[reason] += 1
                continue
            cfg = replay_config(episode)
            state = lib.kg_create(ctypes.byref(cfg))
            if not state:
                raise RuntimeError(f"kg_create failed for {source} episode {eid}")
            try:
                steps = episode["steps"]
                episode_payloads: list[tuple[int, bytes, list[dict[str, str]], Any]] = []
                episode_frames = 0
                try:
                    for turn, frame in enumerate(steps):
                        assert_parity(lib, state, frame, f"{source} episode={eid} turn={turn}")
                        episode_frames += 1
                        rows = episode_targets.get(turn)
                        if rows is not None:
                            if turn + 1 >= len(steps):
                                raise AssertionError(
                                    f"selected terminal state has no resume action: {eid}:{turn}"
                                )
                            payload = serialize_state(lib, state, state_size)
                            verify_resume(
                                lib, cfg, payload, frame, steps[turn + 1],
                                f"{source} episode={eid} turn={turn}",
                            )
                            next_pair, _ = action_pair(steps[turn + 1])
                            episode_payloads.append((turn, payload, rows, next_pair))
                        if turn + 1 < len(steps):
                            _, actions = action_pair(steps[turn + 1])
                            lib.kg_step(state, actions)
                except AssertionError as error:
                    if not args.skip_incompatible_episodes:
                        raise
                    skipped["parity_incompatible_episode"] += 1
                    incompatible.update((source, eid, turn) for turn in episode_targets)
                    if len(incompatible_examples) < args.max_incompatible_examples:
                        incompatible_examples.append(str(error))
                    continue

                # Commit snapshots only after every frame in the episode has
                # passed. A late mismatch can therefore never leave an earlier
                # state from the same incompatible episode in the bank.
                for turn, payload, rows, next_pair in episode_payloads:
                    offset = bank.tell()
                    bank.write(payload)
                    records.append({
                        "record_index": len(records),
                        "byte_offset": offset,
                        "byte_size": state_size,
                        "sha256": hashlib.sha256(payload).hexdigest(),
                        "episode_id": eid,
                        "source": source,
                        "module_version": episode.get("module_version", ""),
                        "seed": (episode.get("info") or {}).get(
                            "seed", (episode.get("configuration") or {}).get("seed", "")
                        ),
                        "turn": turn,
                        "players": ",".join(sorted({row["player"] for row in rows})),
                        "state_keys": ",".join(row["state_key"] for row in rows),
                        "expert_actions": json.dumps(next_pair, separators=(",", ":")),
                        "index_rows": json.dumps(rows, separators=(",", ":")),
                    })
                    found.add((source, eid, turn))
                    counts["snapshots"] += 1
                    counts["resume_checks"] += 1
                counts["parity_frames"] += episode_frames
                counts["episodes"] += 1
            finally:
                lib.kg_destroy(state)
        missing = sorted(set(targets) - found - incompatible)
        if missing:
            preview = ", ".join(f"{source}:{eid}:{turn}" for source, eid, turn in missing[:5])
            raise RuntimeError(f"{len(missing)} indexed states were not reconstructed; first: {preview}")
        bank.seek(0)
        bank.write(BANK_HEADER.pack(
            BANK_MAGIC, BANK_FORMAT_VERSION, state_version, state_size, len(records), 0
        ))

    with manifest_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=MANIFEST_FIELDS, delimiter="\t")
        writer.writeheader()
        writer.writerows(records)
    summary = {
        "format": "kaggriculture_native_state_bank_v1",
        "bank": str(output),
        "manifest": str(manifest_path),
        "bank_format_version": BANK_FORMAT_VERSION,
        "native_state_version": state_version,
        "native_state_size": state_size,
        "record_count": len(records),
        "counts": dict(sorted(counts.items())),
        "skipped": dict(sorted(skipped.items())),
        "incompatible_target_count": len(incompatible),
        "incompatible_examples": incompatible_examples,
        "guarantees": [
            "both official action streams replayed",
            "every traversed native frame matches the official replay",
            "every stored snapshot round-trips and matches its official frame",
            "every restored snapshot matches the next official frame after one step",
        ],
    }
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="Replay JSON/JSON.GZ/ZIP files, directories, or globs")
    parser.add_argument("--index", required=True, help="S0a replay-state TSV index")
    parser.add_argument("--output", required=True, help="Output binary native state bank")
    parser.add_argument("--manifest", help="Output TSV manifest (default: OUTPUT.manifest.tsv)")
    parser.add_argument("--summary", help="Output JSON summary (default: OUTPUT.summary.json)")
    parser.add_argument("--lib", default=str(DEFAULT_LIB), help="Native rule-core shared library")
    parser.add_argument("--min-version", default="1.32.0")
    parser.add_argument(
        "--skip-incompatible-episodes", action="store_true",
        help="Discard an entire selected episode if any parity/resume check fails",
    )
    parser.add_argument("--max-incompatible-examples", type=int, default=10)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    summary = build_bank(parse_args(argv))
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
