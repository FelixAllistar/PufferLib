"""Read saved panels; rank PB then mean clear time, never by legacy scores."""
import argparse
import csv
import io
from pathlib import Path

FPS = 60.0988138974405


def panel(path):
    lines = path.read_text().splitlines()
    rows = list(csv.DictReader(io.StringIO("\n".join(
        line for line in lines if not line.startswith("#"))), delimiter="\t"))
    times = [int(row["clear_frames"]) for row in rows if row["clear"] == "1"]
    return len(rows), times


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--checkpoints", type=Path,
                        default=Path("checkpoints/retro_cnn_64x60_finetune/retro"))
    args = parser.parse_args()
    results = []
    with args.log.open() as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            if row["status"] != "complete":
                continue
            paths = sorted((args.checkpoints / row["run_id"]).glob("*.bin.eval.tsv"))
            if len(paths) != 1:
                raise ValueError(f"Expected one final panel for {row['run_id']}: {paths}")
            attempts, times = panel(paths[0])
            best = min(times) if times else float("inf")
            mean = sum(times) / len(times) if times else float("inf")
            results.append((best, mean, int(row["run"]), attempts, times))
    print("run\tbest_frames\tbest_seconds\tmean_seconds\tclears\tbest_hits")
    for best, mean, run, attempts, times in sorted(results):
        if times:
            print(f"{run}\t{best}\t{best/FPS:.12f}\t{mean/FPS:.12f}"
                  f"\t{len(times)}/{attempts}\t{times.count(best)}")
        else:
            print(f"{run}\tNA\tNA\tNA\t0/{attempts}\t0")
    print("# PB first, mean successful time breaks ties; clears are diagnostic only.")
    print("# Only compare matching ROM, start/finish, levels, budget, seeds and attempt counts.")


if __name__ == "__main__":
    main()
