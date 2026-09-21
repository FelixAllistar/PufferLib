"""Validate bounded-pilot checkpoints and summarize losses/closed-loop money."""
import argparse
import json
import pathlib
import re

import numpy as np


def key_values(line):
    return {key: float(value) for key, value in
            re.findall(r"([A-Za-z_]+)=([-+0-9.eE]+)", line)}


def summarize(directory):
    directory = directory.resolve()
    root = directory.parents[2]
    assert (directory / "PASS.txt").is_file(), "Pilot did not finish"
    report = {"pilot": str(directory), "ppo": {}, "bc": {}, "checkpoints": []}
    files = []
    for scale in ("small", "full"):
        lines = (directory / f"ppo_{scale}.log").read_text().splitlines()
        report["ppo"][scale] = [key_values(line) for line in lines if line.startswith("[perf]")]
        assert len(report["ppo"][scale]) == 2, f"Expected two {scale} PPO updates"
        files.extend(sorted((root / "checkpoints" / "kaggriculture" /
                             f"multi22_{directory.name}_{scale}").glob("*.bin")))
    for variant in ("actor", "joint"):
        lines = (directory / f"{variant}_100.log").read_text().splitlines()
        epochs = [line for line in lines if line.startswith("BC epoch ")]
        supervised = [line for line in lines if line.startswith("BC supervised:")]
        assert epochs[-1].startswith("BC epoch 100 "), "BC did not finish 100 epochs"
        item = {"initial": key_values(supervised[0]), "final": key_values(supervised[-1]),
                "final_accuracy": key_values(epochs[-1]), "evaluation": []}
        for seat in (0, 1):
            text = (directory / f"{variant}_100_eval_seat{seat}.log").read_text()
            records = [json.loads(line) for line in text.splitlines() if line.startswith('{"env/')]
            assert len(records) == 1, "Missing or ambiguous completed evaluation"
            log = records[0]
            assert log["env/n"] >= 64 and log["env/root_fraction"] == 1
            item["evaluation"].append({"learner_seat": seat, **{
                key: log[key] for key in ("env/n", "env/money", "env/opponent_money",
                                         "env/production_units", "env/win_rate")}})
        report["bc"][variant] = item
        files.append(directory / f"{variant}_100.bin")
    assert len(files) == 6, "Expected four PPO and two BC checkpoints"
    for path in files:
        weights = np.fromfile(path, dtype=np.float32)
        assert weights.size == 4025560 and np.isfinite(weights).all(), path
        for suffix, expected in (("policy_version", 5), ("executor_version", 2),
                                 ("macro_mode", 2), ("hidden_size", 512), ("num_layers", 3)):
            assert int(path.with_name(path.name + "." + suffix).read_text()) == expected, path
        report["checkpoints"].append({"path": str(path), "float_parameters": int(weights.size),
                                      "finite": True, "max_abs": float(np.abs(weights).max())})
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pilot", type=pathlib.Path)
    print(json.dumps(summarize(parser.parse_args().pilot), indent=2, allow_nan=False))
