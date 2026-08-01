#!/usr/bin/env python3
"""Evaluate a checkpoint population with native two-policy matches."""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULT = re.compile(
    r"games=(\d+)/(\d+)\s+A=([0-9.]+)\s+B=([0-9.]+)\s+draw=([0-9.]+)"
)


def checkpoints(path: Path, count: int) -> list[Path]:
    paths = sorted(path.glob("*.bin")) if path.is_dir() else [path]
    if not paths:
        raise FileNotFoundError(f"no checkpoints in {path}")
    if count <= 0 or count >= len(paths):
        return paths
    indices = [round(i * (len(paths) - 1) / (count - 1)) for i in range(count)]
    return [paths[i] for i in indices]


def match(env: str, a: Path, b: Path, games: int,
        overrides: list[str]) -> tuple[float, float]:
    command = [
        str(ROOT / "puffer"), "match", env,
        f"base.load_model_path={a}",
        f"base.load_enemy_model_path={b}",
        f"base.num_games={games}",
        *overrides,
    ]
    result = subprocess.run(
        command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=True,
    )
    rows = RESULT.findall(result.stdout)
    if not rows:
        raise RuntimeError(result.stdout)
    _, _, score, _, draw = rows[-1]
    return float(score), float(draw)


def write_csv(path: Path, labels: list[str], matrix: list[list[float]]) -> None:
    with path.open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["checkpoint", *labels])
        for label, row in zip(labels, matrix):
            writer.writerow([label, *(f"{value:.6f}" for value in row)])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("env")
    parser.add_argument("path", type=Path)
    parser.add_argument("--count", type=int, default=8)
    parser.add_argument("--games", type=int, default=65536)
    parser.add_argument("--output", type=Path, default=Path("payoff_matrix"))
    parser.add_argument("--override", action="append", default=[],
        help="additional section.key=value override; repeat as needed")
    args = parser.parse_args()

    paths = [path.resolve() for path in checkpoints(args.path, args.count)]
    labels = [path.stem for path in paths]
    size = len(paths)
    scores = [[0.5] * size for _ in paths]
    draws = [[0.0] * size for _ in paths]
    matches = []

    for i in range(size):
        for j in range(i + 1, size):
            score_ij, draw_ij = match(
                args.env, paths[i], paths[j], args.games, args.override)
            score_ji, draw_ji = match(
                args.env, paths[j], paths[i], args.games, args.override)
            score = 0.5 * (score_ij + 1.0 - score_ji)
            draw = 0.5 * (draw_ij + draw_ji)
            scores[i][j] = score
            scores[j][i] = 1.0 - score
            draws[i][j] = draws[j][i] = draw
            matches.append({
                "a": labels[i], "b": labels[j], "score": score,
                "draw": draw, "a_as_slot_0": score_ij,
                "b_as_slot_0": score_ji,
            })
            print(f"{labels[i]} vs {labels[j]}: score={score:.3f} draw={draw:.3f}")

    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    write_csv(output.with_suffix(".scores.csv"), labels, scores)
    write_csv(output.with_suffix(".draws.csv"), labels, draws)
    with output.with_suffix(".json").open("w") as file:
        json.dump({
            "env": args.env, "games_per_orientation": args.games,
            "checkpoints": [str(path) for path in paths],
            "scores": scores, "draws": draws, "matches": matches,
        }, file, indent=2)
        file.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
