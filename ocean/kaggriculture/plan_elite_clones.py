#!/usr/bin/env python3
"""Create an auditable, exact-player clone-training plan."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import pathlib
import re
import statistics
import struct
import unicodedata

import numpy as np


HEADER = struct.Struct("<8I")


def _slug(name: str) -> str:
    ascii_name = unicodedata.normalize("NFKD", name).encode(
        "ascii", "ignore"
    ).decode("ascii")
    value = re.sub(r"[^a-z0-9]+", "_", ascii_name.lower()).strip("_")
    if not value:
        value = "agent"
    return value[:64]


def _source(row: dict[str, str]) -> str:
    return pathlib.Path(row["source"].split(":", 1)[0]).stem


def _load_experts(
    dataset: pathlib.Path, manifest_rows: int
) -> tuple[np.memmap, int, int]:
    with dataset.open("rb") as stream:
        raw = stream.read(HEADER.size)
    if len(raw) != HEADER.size:
        raise ValueError(f"truncated BC header: {dataset}")
    _, _, count, row_obs, row_expert, _, games, steps = HEADER.unpack(raw)
    if games != manifest_rows or count != games * steps:
        raise ValueError("BC header and player manifest disagree")
    experts = np.memmap(
        dataset, dtype="<f4", mode="r",
        offset=HEADER.size + count * row_obs,
        shape=(count, row_expert),
    )
    return experts, steps, row_expert


def _fingerprint(
    experts: np.memmap, steps: int, heads: int, indices: list[int]
) -> np.ndarray:
    values = np.concatenate([
        experts[index * steps:(index + 1) * steps]
        for index in indices
    ], axis=0)
    parts = []
    for head in range(heads):
        labels = values[:, head]
        labels = labels[labels >= 0].astype(np.int64, copy=False)
        if labels.size and int(labels.max()) >= 64:
            raise ValueError("expert action exceeds fingerprint vocabulary")
        counts = np.bincount(labels, minlength=64).astype(np.float64)
        counts += 1e-6
        parts.append(counts / counts.sum())
    # Every policy head gets equal mass, regardless of how often it is labeled.
    return np.concatenate([part / heads for part in parts])


def _jsd(left: np.ndarray, right: np.ndarray) -> float:
    if left.shape != right.shape:
        raise ValueError("behavior fingerprint shapes disagree")
    a = left / left.sum()
    b = right / right.sum()
    middle = 0.5 * (a + b)
    return float(0.5 * np.sum(a * np.log(a / middle))
        + 0.5 * np.sum(b * np.log(b / middle)))


def _stable_source_subset(
    values: list[dict[str, str]], experts: np.memmap, steps: int, heads: int,
    maximum_jsd: float,
) -> tuple[list[dict[str, str]], float, int]:
    by_source: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    for row in values:
        by_source[_source(row)].append(row)
    if len(by_source) <= 1:
        return values, 0.0, 0
    fingerprints = {
        source: _fingerprint(
            experts, steps, heads,
            [int(row["_manifest_index"]) for row in rows],
        )
        for source, rows in by_source.items()
    }
    # Complete-link clustering prevents a gradual sequence of updates from
    # bridging two genuinely different policies through one intermediate day.
    clusters: list[list[str]] = []
    for source in sorted(by_source, key=lambda key: -len(by_source[key])):
        for cluster in clusters:
            if all(_jsd(fingerprints[source], fingerprints[other])
                    <= maximum_jsd for other in cluster):
                cluster.append(source)
                break
        else:
            clusters.append([source])
    chosen = max(
        clusters,
        key=lambda cluster: (sum(len(by_source[key]) for key in cluster),
                             len(cluster)),
    )
    stable = [row for source in chosen for row in by_source[source]]
    pair_jsd = [
        _jsd(fingerprints[left], fingerprints[right])
        for index, left in enumerate(chosen)
        for right in chosen[index + 1:]
    ]
    return stable, max(pair_jsd, default=0.0), len(values) - len(stable)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("--dataset", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--minimum-trajectories", type=int, default=40)
    parser.add_argument("--minimum-sources", type=int, default=2)
    parser.add_argument("--minimum-final-money", type=float, default=60_000.0)
    parser.add_argument("--exact-version", default="1.32.7")
    parser.add_argument(
        "--maximum-behavior-jsd", type=float, default=0.02,
        help="maximum cross-day action-label JSD allowed in one clone",
    )
    parser.add_argument("--maximum-agents", type=int, default=0)
    parser.add_argument(
        "--priority-agent", action="append", default=["Ryo Hasegawa"]
    )
    args = parser.parse_args()

    with args.manifest.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    dataset = args.dataset
    if dataset is None:
        suffix = ".players.tsv"
        if not str(args.manifest).endswith(suffix):
            parser.error("--dataset is required for a nonstandard manifest name")
        dataset = pathlib.Path(str(args.manifest)[:-len(suffix)])
    experts, steps, heads = _load_experts(dataset, len(rows))

    grouped: dict[str, list[dict[str, str]]] = collections.defaultdict(list)
    for index, row in enumerate(rows):
        name = row.get("agent", "").strip()
        if (
            not name
            or row.get("module_version", "") != args.exact_version
            or float(row["final_money"]) < args.minimum_final_money
        ):
            continue
        row["_manifest_index"] = str(index)
        grouped[name].append(row)

    entries = []
    for name, values in grouped.items():
        values, behavior_jsd, excluded = _stable_source_subset(
            values, experts, steps, heads, args.maximum_behavior_jsd
        )
        source_counter = collections.Counter(
            _source(row) for row in values
        )
        sources = sorted(source_counter)
        versions = sorted({row.get("module_version", "") for row in values})
        if len(values) < args.minimum_trajectories:
            continue
        if len(sources) < args.minimum_sources:
            continue
        money = [float(row["final_money"]) for row in values]
        dataset_id = hashlib.sha1("\n".join(sorted(
            f"{row.get('source', '')}|{row.get('episode_id', '')}|"
            f"{row.get('player', '')}"
            for row in values
        )).encode()).hexdigest()[:12]
        entries.append({
            "agent": name,
            "slug": _slug(name),
            "dataset_id": dataset_id,
            "trajectories": len(values),
            "sources": len(sources),
            "source_names": ",".join(sources),
            "source_counts": ",".join(
                f"{source}:{source_counter[source]}" for source in sources
            ),
            "versions": ",".join(versions),
            "behavior_jsd_max": behavior_jsd,
            "excluded_trajectories": excluded,
            "wins": sum(int(row.get("winner", "0")) for row in values),
            "money_min": min(money),
            "money_mean": statistics.fmean(money),
            "money_max": max(money),
        })

    priorities = {name: index for index, name in enumerate(args.priority_agent)}
    entries.sort(key=lambda row: (
        priorities.get(row["agent"], len(priorities)),
        -int(row["trajectories"]),
        -float(row["money_mean"]),
        row["agent"].lower(),
    ))
    if args.maximum_agents:
        entries = entries[:args.maximum_agents]
    if not entries:
        raise SystemExit("no agents meet the clone sufficiency thresholds")

    # Make sanitized directory names collision-proof without obscuring normal
    # names. Identity itself always remains the exact manifest agent string.
    used: dict[str, str] = {}
    for row in entries:
        slug = str(row["slug"])
        if slug in used and used[slug] != row["agent"]:
            suffix = hashlib.sha1(row["agent"].encode()).hexdigest()[:8]
            row["slug"] = f"{slug}_{suffix}"
        used[str(row["slug"])] = str(row["agent"])

    fields = (
        "agent", "slug", "dataset_id", "trajectories", "sources", "source_names",
        "source_counts",
        "versions", "behavior_jsd_max", "excluded_trajectories", "wins",
        "money_min", "money_mean", "money_max",
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(entries)
    print(
        f"planned {len(entries)} exact-player clones; first={entries[0]['agent']} "
        f"trajectories={entries[0]['trajectories']} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
