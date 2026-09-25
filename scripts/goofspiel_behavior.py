#!/usr/bin/env python3
"""Group and select Goofspiel checkpoints from native behavior distances."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def read_behavior(path: Path) -> tuple[dict[str, float], dict[tuple[str, str], float]]:
    scores: dict[str, float] = {}
    distances: dict[tuple[str, str], float] = {}
    section = ""
    with path.open() as file:
        for line in file:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            if fields[0] == "policy" and len(fields) == 3:
                section = "policies"
                continue
            if fields[0] == "policy_a":
                section = "pairs"
                continue
            if section == "policies" and len(fields) == 3:
                scores[fields[0]] = float(fields[1])
            elif section == "pairs" and len(fields) == 5:
                distances[(fields[0], fields[1])] = float(fields[3])
                distances[(fields[1], fields[0])] = distances[(fields[0], fields[1])]
    if not scores:
        raise ValueError(f"no policy rows in {path}")
    return scores, distances


def union_find(policies: list[str], distances: dict[tuple[str, str], float],
        threshold: float) -> list[list[str]]:
    parent = {policy: policy for policy in policies}

    def find(policy: str) -> str:
        while parent[policy] != policy:
            parent[policy] = parent[parent[policy]]
            policy = parent[policy]
        return policy

    def join(a: str, b: str) -> None:
        a, b = find(a), find(b)
        if a != b:
            parent[b] = a

    for i, a in enumerate(policies):
        for b in policies[i + 1:]:
            if distances.get((a, b), float("inf")) <= threshold:
                join(a, b)
    groups: dict[str, list[str]] = {}
    for policy in policies:
        groups.setdefault(find(policy), []).append(policy)
    return sorted(groups.values(), key=lambda group: min(group))


def select_diverse(policies: list[str], scores: dict[str, float],
        distances: dict[tuple[str, str], float], minimum: float,
        limit: int, strategy: str) -> list[str]:
    if strategy == "farthest":
        selected = [min(policies, key=lambda item: (scores[item], item))]
        remaining = set(policies) - set(selected)
        while remaining and (not limit or len(selected) < limit):
            policy = max(remaining, key=lambda item: (
                min(distances.get((item, other), 0.0) for other in selected),
                -scores[item], item),)
            selected.append(policy)
            remaining.remove(policy)
        return selected

    selected: list[str] = []
    for policy in sorted(policies, key=lambda item: (scores[item], item)):
        if all(distances.get((policy, other), float("inf")) >= minimum
                for other in selected):
            selected.append(policy)
            if limit and len(selected) == limit:
                break
    return selected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("behavior_tsv", type=Path)
    parser.add_argument("--similarity", type=float, default=0.05,
        help="Jensen-Shannon distance threshold for one behavior group")
    parser.add_argument("--minimum-distance", type=float, default=0.05,
        help="minimum distance between selected representatives")
    parser.add_argument("--max-policies", type=int, default=32)
    parser.add_argument("--strategy", choices=("best", "farthest"),
        default="best", help="selection order for representatives")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    scores, distances = read_behavior(args.behavior_tsv)
    policies = list(scores)
    groups = union_find(policies, distances, args.similarity)
    selected = select_diverse(policies, scores, distances,
        args.minimum_distance, args.max_policies, args.strategy)
    output = args.output or args.behavior_tsv.with_suffix("")
    group_path = output.with_name(output.name + "_groups.tsv")
    selected_path = output.with_name(output.name + "_selected.tsv")
    with group_path.open("w", newline="") as file:
        writer = csv.writer(file, delimiter="\t", lineterminator="\n")
        writer.writerow(("group", "policy", "exact_exploitability"))
        for group, members in enumerate(groups):
            for policy in sorted(members, key=lambda item: (scores[item], item)):
                writer.writerow((group, policy, f"{scores[policy]:.9f}"))
    with selected_path.open("w", newline="") as file:
        writer = csv.writer(file, delimiter="\t", lineterminator="\n")
        writer.writerow(("policy", "exact_exploitability"))
        for policy in selected:
            writer.writerow((policy, f"{scores[policy]:.9f}"))
    print(f"policies={len(policies)} groups={len(groups)} selected={len(selected)}")
    print(f"groups={group_path}")
    print(f"selected={selected_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
