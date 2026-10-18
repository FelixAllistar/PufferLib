#!/usr/bin/env python3
"""Run paired native matches with a separately frozen opponent shared library."""
import argparse
import csv
import hashlib
from itertools import zip_longest
import json
import math
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_blocks(prefix, blocks, result):
    """Reject truncated or inconsistent evidence even if the native process exits 0."""
    paths = [Path(str(prefix) + f'.{name}.csv') for name in ('control', 'candidate', 'difference')]
    means, squares = [0.0] * 3, [0.0] * 3
    count = 0
    with paths[0].open() as a, paths[1].open() as b, paths[2].open() as c:
        for i, rows in enumerate(zip_longest(csv.DictReader(a), csv.DictReader(b), csv.DictReader(c))):
            if any(row is None or int(row['block']) != i for row in rows):
                raise ValueError('Block evidence has missing, duplicated, or out-of-order rows')
            values = [float(row['mean_bb']) for row in rows]
            if not all(math.isfinite(v) for v in values) or abs(values[1] - values[0] - values[2]) > 1e-9:
                raise ValueError('Paired block difference does not match the two arms')
            count += 1
            for k, value in enumerate(values):
                delta = value - means[k]
                means[k] += delta / count
                squares[k] += delta * (value - means[k])
    if count != blocks:
        raise ValueError(f'Truncated block evidence: expected {blocks}, found {count}')
    for k, name in enumerate(('control', 'candidate', 'paired_gain')):
        half = 196 * math.sqrt(max(0, squares[k]) / ((count - 1) * count))
        reported = (result[name]['bb100'], result[name]['ci95_half_width_bb100'])
        if not all(math.isfinite(value) for value in reported):
            raise ValueError(f'Native {name} summary contains nonfinite values')
        if abs(100 * means[k] - reported[0]) > 1e-6 or abs(half - reported[1]) > 1e-6:
            raise ValueError(f'Native {name} summary does not match independent CSV recomputation')
    return {p.name: {'sha256': sha(p), 'rows': count} for p in paths}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--opponent-library", type=Path, help="Defaults to frozen control")
    parser.add_argument("--players", type=int, choices=(2, 6, 9), required=True)
    parser.add_argument("--hero-style", type=int, default=7)
    parser.add_argument("--opponents", default="7", help="Comma-separated frozen native policy IDs")
    parser.add_argument("--hands", type=int, default=60000, help="Per arm, rounded up to whole seat-rotation blocks")
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--stack-bb", type=float, default=200)
    parser.add_argument("--control-samples", type=int, choices=(0, 32, 64, 128), default=0)
    parser.add_argument("--candidate-samples", type=int, choices=(0, 32, 64, 128), default=0)
    parser.add_argument("--opponent-samples", type=int, choices=(0, 32, 64, 128), default=0)
    parser.add_argument("--output-prefix", type=Path, required=True)
    parser.add_argument("--label", default="paired_frozen_opponents")
    args = parser.parse_args()
    binary = HERE.parents[1] / "build/poker/dual_bench"
    binary.parent.mkdir(parents=True, exist_ok=True)
    source = HERE / "dual_bench.c"
    if not binary.exists() or binary.stat().st_mtime < source.stat().st_mtime:
        # Concurrent screens must not overwrite an executable another screen
        # is starting or running. Publish a fully linked inode atomically.
        with tempfile.TemporaryDirectory(prefix="dual-build-", dir=binary.parent) as temporary:
            built = Path(temporary) / "dual_bench"
            subprocess.run(["cc", "-O3", "-std=c11", "-Wall", "-Wextra", "-o", str(built),
                            str(source), "-ldl", "-lm"], check=True)
            built.replace(binary)
    opponent = args.opponent_library or args.control
    libraries = {"control": args.control.resolve(), "candidate": args.candidate.resolve(),
                 "opponent": opponent.resolve()}
    before = {role: sha(path) for role, path in libraries.items()}
    blocks = (args.hands + args.players - 1) // args.players
    if blocks < 2:
        parser.error("At least two full duplicate-deal blocks are needed")
    args.output_prefix.parent.mkdir(parents=True, exist_ok=True)
    command = [str(binary), *(str(libraries[key]) for key in ("control", "candidate", "opponent")),
               str(args.players), str(args.hero_style), args.opponents, str(blocks), str(args.seed),
               str(round(100 * args.stack_bb)), str(args.control_samples), str(args.candidate_samples),
               str(args.opponent_samples), str(args.output_prefix.resolve()), args.label]
    response = subprocess.run(command, capture_output=True, text=True)
    if response.returncode:
        raise RuntimeError(f"Native paired benchmark failed ({response.returncode}): {response.stderr.strip()}")
    after = {role: sha(path) for role, path in libraries.items()}
    if before != after:
        raise RuntimeError("A library changed during the evaluation; reject these results")
    result = json.loads(response.stdout)
    block_files = verify_blocks(args.output_prefix, blocks, result)
    result.update(label=args.label, hero_style=args.hero_style,
                  opponents=[int(x) for x in args.opponents.split(",")], command=command,
                  libraries={role: {"path": str(path), "sha256": before[role]} for role, path in libraries.items()},
                  harness_sha256=sha(source), block_files=block_files,
                  note="Native history disabled. Timing includes dual-engine synchronization and is not policy inference latency.")
    out = Path(str(args.output_prefix) + ".json")
    with out.open("x") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
