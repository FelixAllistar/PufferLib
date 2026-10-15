#!/usr/bin/env python3
"""Score one native checkpoint against the pinned Oak panel in both action modes."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
PANEL = ("zam_egg_mie", "jynx_mie_don", "gar_egg_zap")
SOURCES = ROOT / "reports/pokemon_benchmarks/20261014_all_roster"


def aggregate(rows):
    result = {}
    for mode in ("argmax", "sample"):
        group = [row for row in rows if row["mode"] == mode]
        if len(group) != len(PANEL) or {r["team"] for r in group} != set(PANEL):
            raise ValueError("incomplete Oak panel")
        counts = {key: sum(row[key] for row in group)
                  for key in ("games", "wins", "draws", "losses", "timeouts")}
        if counts["games"] <= 0 or sum(counts[k] for k in ("wins", "draws", "losses")) != counts["games"]:
            raise ValueError("invalid Oak outcome counts")
        result[mode] = {**counts, "score": (counts["wins"] + .5 * counts["draws"]) / counts["games"]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--games", type=int, default=128)
    parser.add_argument("--budget", type=int, default=4096)
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--seed", type=int, default=20261015)
    args = parser.parse_args()
    if args.games < 2 or args.games % 2 or args.workers < 1 or args.budget < 1:
        parser.error("games must be positive and even; workers/budget must be positive")
    checkpoint = args.checkpoint.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Never reuse stale score files.
    digest = hashlib.sha256(checkpoint.read_bytes()).hexdigest()
    started = time.monotonic()
    print(f"\nOak evaluation: {args.games} games x 3 teams x 2 modes; budget={args.budget}", flush=True)

    def evaluate(job):
        mode, team = job
        directory = output / f"{mode}-{team}"
        directory.mkdir()
        shutil.copy2(SOURCES / f"{team}.paste.txt", directory / f"{team}.paste.txt")
        command = [sys.executable, str(Path(__file__).with_name("oak_benchmark.py")),
                   str(checkpoint), "--team", team, "--games", str(args.games),
                   "--budget", str(args.budget), "--seed", str(args.seed),
                   "--output", str(directory), "--fast"]
        if mode == "argmax":
            command.append("--deterministic")
        with (directory / "console.log").open("x") as stream:
            subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                           check=True, timeout=7200)
        files = [p for p in directory.glob("*.json") if not p.name.endswith("-bridge-mismatch.json")]
        if len(files) != 1:
            raise ValueError(f"expected one completed benchmark in {directory}")
        report = json.loads(files[0].read_text())
        if report["checkpoint_sha256"] != digest or report["games"] != args.games:
            raise ValueError("checkpoint hash or game count mismatch")
        row = {key: report[key] for key in ("team", "games", "wins", "draws", "losses", "timeouts")}
        row.update(mode=mode, report=str(files[0].relative_to(output)))
        score = (row["wins"] + .5 * row["draws"]) / row["games"]
        print(f"Oak {mode:6s} {team:12s}: {row['wins']}W/{row['draws']}D/{row['losses']}L ({score:.1%})", flush=True)
        return row

    jobs = [(mode, team) for mode in ("argmax", "sample") for team in PANEL]
    with ThreadPoolExecutor(max_workers=min(args.workers, len(jobs))) as pool:
        rows = list(pool.map(evaluate, jobs))
    scores = aggregate(rows)
    summary = {"checkpoint": str(checkpoint), "sha256": digest, "seed": args.seed,
               "budget": args.budget, "elapsed_seconds": time.monotonic() - started,
               "objective": "argmax score (wins + 0.5 draws) / games",
               "limitations": "Oak sees full state; native pinned engine is authoritative; Oak search RNG is internally seeded.",
               "scores": scores, "panel": rows}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    # Commit scalar results last, only after all six evaluations succeeded.
    temporary = output / "score.tmp"
    temporary.write_text(f"{scores['argmax']['score']:.12g} {scores['sample']['score']:.12g}\n")
    temporary.replace(output / "score.txt")


if __name__ == "__main__":
    main()
