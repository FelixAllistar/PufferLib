"""Fresh-game BC check: matched maps, both seats, sampling vs masked argmax.

No training, sweeps, or checkpoint changes. Each output directory must be new.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--games", type=int, default=128)
    parser.add_argument("--widths", type=int, nargs="+", default=[1024, 256])
    parser.add_argument("--trace", action="store_true")
    args = parser.parse_args()
    assert args.games > 0 and set(args.widths) <= {256, 512, 1024}
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for width in args.widths:
        for greedy in (0, 1):
            for seat in (0, 1):
                name = f"h{width}_greedy{greedy}_seat{seat}"
                agents = 1 if args.trace else min(32, args.games)
                options = {
                    "base.load_model_path": ROOT / f"saved/kaggriculture/compact_v7/h{width}_l2/bc.bin",
                    "base.teacher_model_path": "None", "base.eval_episodes": args.games,
                    "base.eval_agents": agents, "base.eval_greedy": greedy,
                    "base.async": 0, "base.cudagraphs": 1, "base.seed": 73,
                    "policy.hidden_size": width, "policy.num_layers": 2,
                    "vec.total_agents": agents, "vec.num_buffers": 1,
                    "vec.num_threads": 1 if args.trace else 8,
                    "vec.num_policies": 1, "vec.hist_policy_percent": 0,
                    "selfplay.enabled": 0, "env.num_agents": 1,
                    "env.learner_seat": seat, "env.bot_policy": 1,
                    "env.reset_state_prob": 0, "train.horizon": 32,
                    "train.minibatch_size": 32, "train.teacher_kl_coefficient": 0,
                    "sweep.metric": "root_money",
                }
                command = [str(args.binary.resolve()), "eval", "--headless"]
                command += [f"--{key}={value}" for key, value in options.items()]
                environment = os.environ.copy()
                environment.pop("KAG_EVAL_TRACE", None)
                if args.trace:
                    environment["KAG_EVAL_TRACE"] = str((args.output / f"{name}.jsonl").resolve())
                log = args.output / f"{name}.log"
                with log.open("x") as stream:
                    process = subprocess.run(command, cwd=ROOT, env=environment,
                                             stdout=stream, stderr=subprocess.STDOUT)
                if process.returncode:
                    raise RuntimeError(f"evaluation failed ({process.returncode}): {log}")
                match = re.search(r"CUDA_EVAL env=\S+ score=(\S+) perf=(\S+) games=(\d+) params=(\d+) draw=(\S+)", log.read_text())
                assert match, log
                result = dict(width=width, greedy=bool(greedy), seat=seat,
                              cash=float(match[1]), score=float(match[2]), games=int(match[3]),
                              parameters=int(match[4]), draw=float(match[5]), command=command)
                results.append(result)
                print(json.dumps({k: v for k, v in result.items() if k != "command"}), flush=True)
                with (args.output / "results.json").open("w") as stream:
                    json.dump(results, stream, indent=2)


if __name__ == "__main__":
    main()
