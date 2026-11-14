#!/usr/bin/env python3
"""Turns the CSV that Benchmark.bat writes (CRET_BENCH_CSV, render/gprof.c) into markdown tables.

    python3 tools/gpubench_table.py gpu_bench.csv [--md out.md]

Table 1: GPU ms per frame for each shot (rows) and run (columns: presets, AO off).
Table 2: GPU ms per pass, averaged over the shots, for each run, with the
difference to the first run; passes under 0.05 ms in every run are left out.
"""
import argparse
import csv
import sys
from collections import defaultdict


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--md", help="also write the tables to this file")
    a = ap.parse_args()

    total = {}                      # (run, shot) -> gpu ms
    passes = defaultdict(list)      # (run, pass) -> [gpu ms per shot]
    runs, shots, names = [], [], []
    with open(a.csv, newline="") as f:
        for r in csv.DictReader(f):
            run, shot, p, g = r["run"] or "-", r["shot"], r["pass"], float(r["gpu_ms"])
            if run not in runs: runs.append(run)
            if p == "total":
                if shot not in shots: shots.append(shot)
                total[(run, shot)] = g
            else:
                if p not in names: names.append(p)
                passes[(run, p)].append(g)
    if not runs:
        sys.exit("no rows in " + a.csv)

    out = []
    out.append("GPU ms per frame (fps in brackets)\n")
    out.append("| shot | " + " | ".join(runs) + " |")
    out.append("|---" * (len(runs) + 1) + "|")
    for s in shots:
        cells = []
        for run in runs:
            g = total.get((run, s))
            cells.append("" if g is None else f"{g:.2f} ({1000 / g:.0f})" if g > 0 else f"{g:.2f}")
        out.append(f"| {s} | " + " | ".join(cells) + " |")
    means = []
    for run in runs:
        v = [total[(run, s)] for s in shots if (run, s) in total]
        means.append(f"**{sum(v) / len(v):.2f}**" if v else "")
    out.append("| mean | " + " | ".join(means) + " |")

    def mean(run, p):
        v = passes.get((run, p))
        return sum(v) / len(v) if v else 0.0
    out.append("\nGPU ms per pass, mean over shots (difference to " + runs[0] + ")\n")
    out.append("| pass | " + " | ".join(runs) + " |")
    out.append("|---" * (len(runs) + 1) + "|")
    for p in sorted(names, key=lambda p: -mean(runs[0], p)):
        if max(mean(run, p) for run in runs) < 0.05:
            continue
        base = mean(runs[0], p)
        cells = [f"{base:.2f}"] + [f"{mean(run, p):.2f} ({mean(run, p) - base:+.2f})" for run in runs[1:]]
        out.append(f"| {p} | " + " | ".join(cells) + " |")
    text = "\n".join(out) + "\n"
    print(text)
    if a.md:
        with open(a.md, "w") as f:
            f.write(text)


if __name__ == "__main__":
    main()
