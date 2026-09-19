"""Check panel task selection and native-frame budgets using a real checkpoint."""
import argparse
import configparser
import csv
import io
from pathlib import Path
import subprocess
import tempfile


def levels(spec):
    if spec == "all":
        return [f"{w}-{s}" for w in range(1, 9) for s in range(1, 5)]
    return [s.strip() for s in spec.split(",")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("checkpoint", type=Path)
    args = parser.parse_args()
    config = configparser.ConfigParser(interpolation=None)
    config.read(str(args.checkpoint) + ".ini")
    base = [str(args.binary.resolve()), str(args.checkpoint.resolve())]
    cases = [
        ("recorded", [], levels(config["env"]["spawn_levels"]), int(config["env"]["frameskip"]), 1),
        ("subset", ["--levels", "2-3,1-1,8-4", "--frameskip", "4"], ["2-3", "1-1", "8-4"], 4, 2),
        ("all", ["--levels", "all", "--frameskip", "1"], levels("all"), 1, 1),
    ]
    with tempfile.TemporaryDirectory(prefix="retro-panel-test.") as directory:
        for name, overrides, expected, skip, repeats in cases:
            output = Path(directory) / (name + ".tsv")
            result = subprocess.run(base + overrides + ["--frames", "17", "--repeats", str(repeats),
                "--workers", "2", "--metric", "speed", "--output", str(output)],
                capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr
            lines = output.read_text().splitlines()
            assert "retro_panel_v2" in lines[0] and f"frameskip={skip}" in lines[0], lines[0]
            data = "\n".join(line for line in lines if not line.startswith("#"))
            rows = list(csv.DictReader(io.StringIO(data), delimiter="\t"))
            assert [row["level"] for row in rows] == expected * repeats, rows
            assert [int(row["replicate"]) for row in rows] == [r for r in range(repeats) for _ in expected]
            assert all(0 < int(row["frames"]) <= 17 for row in rows), rows
            assert all(int(row["clear_frames"]) == 0 for row in rows if row["clear"] == "0"), rows
            assert f"attempts={len(expected) * repeats}" in result.stdout, result.stdout
            print(f"PASS {name}: {len(rows)} attempts, frameskip={skip}, native budget=17", flush=True)
        for options in (["--frameskip", "0"], ["--frameskip", "1.5"], ["--levels", "9-1"], ["--metric", "typo"]):
            result = subprocess.run(base + options, capture_output=True, text=True, timeout=30)
            assert result.returncode != 0, options
        print("PASS invalid task/control/metric options rejected", flush=True)


if __name__ == "__main__":
    main()
