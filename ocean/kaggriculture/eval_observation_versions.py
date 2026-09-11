#!/usr/bin/env python3
"""Run native GPU league batches with each checkpoint's observation layout.

The native evaluator supports one learner layout and one frozen-bank layout
per process. Partition a population into compatible batches, then restore the
original manifest indices. No model conversion or training-config edits occur.
Unversioned historical checkpoints use layout 0. An explicit MODEL.obs_version
sidecar takes precedence over the source run's logged configuration.
"""

import argparse
import configparser
import csv
import json
from dataclasses import dataclass
import math
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def observation_version(checkpoint, root=ROOT):
    path = Path(checkpoint)
    if not path.is_absolute():
        path = root / path
    if not path.is_file():
        raise ValueError(f"Checkpoint does not exist: {path}")
    sidecar = Path(str(path) + ".obs_version")
    if sidecar.is_file():
        value = sidecar.read_text().strip()
    else:
        name = path.name.removesuffix(".emag").removesuffix(".bin")
        run_id = path.parent.name if re.fullmatch(r"\d{16}", name) else None
        if run_id is None:
            match = re.fullmatch(r"(?:run|seed)_(.+)_\d{16}", name)
            if match:
                run_id = match[1]
        metadata = (path.parent / "run_metadata.json" if path.name.removesuffix(".emag").removesuffix(".bin").isdigit()
                    else root / "checkpoints" / "kaggriculture" / str(run_id) / "run_metadata.json")
        if metadata.is_file():
            record = json.loads(metadata.read_text())
            value = record["observation_version"]
            if record.get("run_id") != run_id or type(value) is not int or value not in (0, 1):
                raise ValueError(f"Invalid run metadata: {metadata}")
            return value
        log = root / "logs" / "kaggriculture" / f"{run_id}.ini"
        config = configparser.ConfigParser(interpolation=None, strict=False)
        if run_id and log.is_file():
            config.read(log)
        value = config.get("env", "observation_version", fallback="0").strip()
    if value not in ("0", "1"):
        raise ValueError(f"Invalid observation version {value!r} for {path}")
    return int(value)


@dataclass(frozen=True)
class Policy:
    index: int
    name: str
    checkpoint: str
    version: int
    executor: int = 0


def executor_version(checkpoint, root=ROOT):
    """Executor semantics travel with a model; never inherit active config."""
    path = Path(checkpoint)
    if not path.is_absolute(): path = root / path
    if not path.is_file(): raise ValueError(f'Checkpoint does not exist: {path}')
    sidecar = Path(str(path) + '.executor_version')
    if sidecar.is_file():
        value = sidecar.read_text().strip()
    else:
        name = path.name.removesuffix('.emag').removesuffix('.bin')
        run = path.parent.name if re.fullmatch(r'\d{16}', name) else None
        if run is None:
            match = re.fullmatch(r'(?:run|seed)_(.+)_\d{16}', name)
            if match: run = match[1]
        meta = path.parent / 'run_metadata.json' if name.isdigit() else root / 'checkpoints/kaggriculture' / str(run) / 'run_metadata.json'
        config = configparser.ConfigParser(interpolation=None, strict=False)
        log = root / 'logs/kaggriculture' / f'{run}.ini'
        if run and log.is_file(): config.read(log)
        value = config.get('env', 'macro_executor_version', fallback='0').strip()
        if meta.is_file():
            record = json.loads(meta.read_text())
            if record.get('run_id') != run: raise ValueError(f'Invalid run metadata: {meta}')
            if 'macro_executor_version' in record: value = str(record['macro_executor_version'])
    if value not in ('0', '1'): raise ValueError(f'Invalid executor version {value!r}: {path}')
    return int(value)


def read_manifest(path):
    with open(path, newline="") as source:
        records = list(csv.DictReader(source, delimiter="\t"))
    policies = []
    for index, row in enumerate(records):
        if int(row["id"]) != index:
            raise ValueError(f"Manifest IDs must be contiguous from zero: {path}")
        checkpoint = row["checkpoint"]
        policies.append(Policy(index, row["policy"], checkpoint,
                               observation_version(checkpoint), executor_version(checkpoint)))
    if not policies:
        raise ValueError(f"Empty policy manifest: {path}")
    return policies


def chunks(items, size=8):
    for start in range(0, len(items), size):
        yield items[start:start + size]


def groups(policies):
    return [[p for p in policies if (p.version, p.executor) == version]
            for version in sorted({(p.version, p.executor) for p in policies})]


def matrix_jobs(policies, focal_count=0):
    """Yield (mode, learners, opponents, native_focal_count)."""
    limit = focal_count or len(policies) - 1
    if not 1 <= limit < len(policies):
        raise ValueError("focal_count must be zero or in [1, policy_count - 1]")
    cohorts = groups(policies)
    for cohort in cohorts:
        focal = sum(p.index < limit for p in cohort)
        if len(cohort) < 2 or focal == 0:
            continue
        if len(cohort) <= 9:
            yield "matrix", cohort, [], min(focal, len(cohort) - 1)
        else:
            for i, policy in enumerate(cohort[:-1]):
                if policy.index >= limit:
                    break
                for opponents in chunks(cohort[i + 1:]):
                    yield "screen", [policy], opponents, 0
    for left_index, left in enumerate(cohorts):
        for right in cohorts[left_index + 1:]:
            # Equal opponent sets can share a resident native screen process.
            batches = {}
            for policy in left:
                eligible = [p for p in right if min(p.index, policy.index) < limit]
                for opponents in chunks(eligible):
                    batches.setdefault(tuple(opponents), []).append(policy)
            for opponents, learners in batches.items():
                yield "screen", learners, list(opponents), 0


def write_manifest(path, policies):
    with open(path, "w", newline="") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow(("id", "policy", "checkpoint"))
        writer.writerows((i, p.name, p.checkpoint) for i, p in enumerate(policies))


def remap_row(row, learners, opponents, matrix):
    a, b = int(row[0]), int(row[1])
    left = learners[a].index
    right = (opponents or learners)[b].index
    score, draw, money_a, money_b = map(float, row[2:6])
    games = int(row[6])
    if not all(map(math.isfinite, (score, draw, money_a, money_b))):
        raise ValueError(f"Nonfinite native result: {row}")
    if not 0 <= score <= 1 or not 0 <= draw <= 1 or games < 2:
        raise ValueError(f"Invalid native result: {row}")
    if matrix and left > right:
        left, right = right, left
        score, money_a, money_b = 1 - score, money_b, money_a
    return left, right, score, draw, money_a, money_b, games


def run(args):
    if args.command in ("version", "executor-version"):
        for checkpoint in args.checkpoints:
            print(executor_version(checkpoint) if args.command == 'executor-version' else observation_version(checkpoint))
        return
    matrix = args.command == "matrix"
    learners = read_manifest(args.manifest if matrix else args.candidates)
    opponents = [] if matrix else read_manifest(args.opponents)
    if matrix:
        jobs = list(matrix_jobs(learners, args.focal_count))
        limit = args.focal_count or len(learners) - 1
        expected = {(i, j) for i in range(limit) for j in range(i + 1, len(learners))}
    else:
        jobs = [("screen", left, right, 0)
                for left in groups(learners) for cohort in groups(opponents)
                for right in chunks(cohort)]
        expected = {(a.index, b.index) for a in learners for b in opponents}
    extra = args.overrides
    if extra and extra[0] == "--":
        extra = extra[1:]
    results = {}
    print(f"Version-aware GPU {args.command}: policies={len(learners)} "
          f"pairs={len(expected)} batches={len(jobs)}", flush=True)
    with tempfile.TemporaryDirectory(prefix="kag-versioned-eval-") as temp:
        for job_index, (mode, left, right, focal) in enumerate(jobs):
            left_path = str(Path(temp) / "learners.tsv")
            right_path = str(Path(temp) / "opponents.tsv")
            result_path = str(Path(temp) / "results.tsv")
            write_manifest(left_path, left)
            version_a = left[0].version
            version_b = right[0].version if right else version_a
            command = ["./puffer", "league", "kaggriculture", *extra,
                       f"league.mode={mode}", f"league.output={result_path}",
                       f"league.games={args.games}", f"league.focal_count={focal}",
                       f"league.min_agents={args.min_agents}",
                       f"env.observation_version={version_a}",
                       f"env.frozen_observation_version={version_b}",
                       f"env.macro_executor_version={left[0].executor}",
                       f"env.frozen_macro_executor_version={(right or left)[0].executor}",
                       "env.reset_state_prob=0", "env.reset_state_bank=None",
                       "env.reset_opening_prob=0", "env.reset_opening_turns=0",
                       "env.opening_turns=0", "env.curriculum_enabled=0"]
            if mode == "matrix":
                command.append(f"league.policy_manifest={left_path}")
            else:
                write_manifest(right_path, right)
                command += [f"league.candidate_manifest={left_path}",
                            f"league.opponent_manifest={right_path}"]
            print(f"  batch={job_index + 1}/{len(jobs)} mode={mode} "
                  f"learner_obs={version_a} opponent_obs={version_b}", flush=True)
            subprocess.run(command, cwd=ROOT, check=True)
            with open(result_path, newline="") as source:
                for row in csv.reader(source, delimiter="\t"):
                    result = remap_row(row, left, right, matrix)
                    key = result[:2]
                    if key in results or key not in expected or result[-1] < args.games:
                        raise ValueError(f"Unexpected/duplicate/undersampled pair: {result}")
                    results[key] = result
    if results.keys() != expected:
        raise ValueError(f"Missing {len(expected - results.keys())} native pair results")
    with open(args.output, "w", newline="") as output:
        csv.writer(output, delimiter="\t").writerows(results[key] for key in sorted(results))
    print(f"Wrote {args.output}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("version", "executor-version"):
        version = sub.add_parser(name)
        version.add_argument("checkpoints", nargs="+")
    for name in ("matrix", "screen"):
        command = sub.add_parser(name)
        if name == "matrix":
            command.add_argument("--manifest", required=True)
            command.add_argument("--focal-count", type=int, default=0)
        else:
            command.add_argument("--candidates", required=True)
            command.add_argument("--opponents", required=True)
        command.add_argument("--output", required=True)
        command.add_argument("--games", type=int, required=True)
        command.add_argument("--min-agents", type=int, default=0)
        command.add_argument("overrides", nargs=argparse.REMAINDER)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
