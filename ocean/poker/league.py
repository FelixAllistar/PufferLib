"""Small frozen-bank self-play stints using the existing native PPO trainer."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(args):
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    assert binary.is_file()
    assert 1 <= args.pool <= 16 and 1 <= args.layers <= 4
    assert 8 <= args.hidden <= 128 and args.hidden % 8 == 0
    assert 0 <= args.rules_fraction <= 1 and args.stints > 0
    observations = 16 + 15 * args.players
    expected = 4 * (args.hidden * (observations + 9)
        + args.layers * 3 * args.hidden ** 2)
    pool = [path.resolve() for path in args.opponents]
    learner = args.learner.resolve() if args.learner else None
    for path in pool + ([learner] if learner else []):
        assert path.stat().st_size == expected, path
    with (directory / "runs.jsonl").open("x") as log:
        for stint in range(args.stints):
            bank = pool[-args.pool:]
            manifest = directory / f"opponents_{stint:03d}.txt"
            manifest.write_text(f"poker_v1 {args.players} {args.hidden} {args.layers} "
                f"{len(bank)}\n" + "".join(f"{path}\n" for path in bank))
            name = f"stint_{stint:03d}"
            options = {
                "base.run_id": name, "base.checkpoint_dir": directory / "checkpoints",
                "base.log_dir": directory / "logs", "base.checkpoint_interval": 0,
                "base.eval_episodes": 0, "base.load_model_path": learner or "None",
                "base.seed": args.seed + stint, "env.seed": args.seed + stint,
                "env.num_players": args.players,
                "env.native_league": manifest if bank else "None",
                "env.rules_fraction": args.rules_fraction, "env.bot_policy": 1,
                "policy.hidden_size": args.hidden, "policy.num_layers": args.layers,
                "train.total_timesteps": args.steps, "selfplay.enabled": 0,
                "vec.total_agents": args.agents, "vec.num_buffers": 1,
                "vec.num_threads": args.threads, "vec.num_policies": 1,
                "train.horizon": args.horizon, "train.minibatch_size": args.agents,
            }
            command = [str(binary), "train"] + [f"--{k}={v}" for k, v in options.items()]
            record = {"stint": stint, "command": command, "opponents": [
                {"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                for p in bank]}
            print(f"{name}: {len(bank)} frozen banks, steps={args.steps}", flush=True)
            with (directory / f"{name}.log").open("x") as output:
                subprocess.run(command, cwd=ROOT, stdout=output,
                    stderr=subprocess.STDOUT, check=True)
            learner = max((directory / "checkpoints" / "poker" / name).glob("*.bin"))
            assert learner.stat().st_size == expected
            record["checkpoint"] = str(learner)
            record["sha256"] = hashlib.sha256(learner.read_bytes()).hexdigest()
            log.write(json.dumps(record) + "\n")
            log.flush()
            pool.append(learner)
    print(f"Final checkpoint: {learner}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/poker/puffer")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--players", type=int, choices=(2, 6, 9), default=6)
    parser.add_argument("--stints", type=int, default=10)
    parser.add_argument("--steps", type=int, default=1000000)
    parser.add_argument("--pool", type=int, default=8)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--layers", type=int, default=2)
    parser.add_argument("--agents", type=int, default=1024)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--horizon", type=int, default=32)
    parser.add_argument("--seed", type=int, default=73)
    parser.add_argument("--rules-fraction", type=float, default=0.25)
    parser.add_argument("--learner", type=Path)
    parser.add_argument("--opponents", type=Path, nargs="*", default=[])
    run(parser.parse_args())
