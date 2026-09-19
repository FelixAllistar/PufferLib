"""Short buffer-count throughput check, with all outputs in a temporary tree.

Run only when no other trainer/compiler is active. Loads an immutable explicit
checkpoint, repeats 1/2/4 buffers in reverse order, and excludes warm-up/final
drain. Does not change config or the source checkpoint. Not a learning sweep.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--updates", type=int, default=12)
    parser.add_argument("--buffers", type=int, nargs="+", choices=[1, 2, 4], default=[1, 2, 4, 4, 2, 1])
    parser.add_argument("--omp-wait", choices=["PASSIVE", "ACTIVE"], default="PASSIVE")
    args = parser.parse_args()
    if args.updates < 6:
        parser.error("at least six updates required")
    binary = args.binary.resolve(strict=True)
    checkpoint = args.checkpoint.resolve(strict=True)
    digest = hashlib.sha256(checkpoint.read_bytes()).hexdigest()
    root = Path(tempfile.mkdtemp(prefix="retro-buffer-profile."))
    print(f"Artifacts: {root}", flush=True)
    batch = 128 * 256
    results = []
    for i, buffers in enumerate(args.buffers):
        run = root / f"{i}-buffers{buffers}"
        run.mkdir()
        command = [str(binary), "train", "retro", f"base.load_model_path={checkpoint}",
                   f"base.checkpoint_dir={run}", f"base.log_dir={run}/logs",
                   "base.checkpoint_interval=0", "base.checkpoint_on_interrupt=0",
                   "base.perf_log=1", "base.eval_epoch_mult=0", "base.seed=73",
                   "base.async=1", "vec.total_agents=128", f"vec.num_buffers={buffers}",
                   "vec.num_threads=4", "vec.worker_wait=block", "train.horizon=256",
                   "train.minibatch_size=4096", "train.replay_ratio=1",
                   f"train.total_timesteps={batch * args.updates}",
                   # Match the successful checkpoint's rollout distribution.
                   "env.spawn_levels=1-1", "env.frameskip=4", "env.max_frames=3000",
                   "env.score_scale=1", "env.reward_scale=1", "env.completion_reward=1",
                   "env.death_penalty=0", "env.checkpoint_reward=0", "env.completion_time_bonus=0",
                   "train.reward_clip=0"]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180,
                                env=dict(os.environ, OMP_WAIT_POLICY=args.omp_wait))
        output = result.stdout + result.stderr
        (run / "stdout.log").write_text(output)
        if result.returncode:
            raise RuntimeError(f"trainer exit {result.returncode}: {output[-3000:]}")
        records = [dict((k, float(v)) for k, v in re.findall(r"(\w+)=([\d.e+-]+)", line))
                   for line in output.splitlines() if line.startswith("[perf] ")]
        intervals = [(b["steps"] - a["steps"], b["uptime"] - a["uptime"])
                     for a, b in zip(records, records[1:])
                     if a["steps"] >= 2 * batch and b["steps"] < batch * args.updates]
        if len(intervals) < args.updates - 4 or any(t <= 0 for _, t in intervals):
            raise RuntimeError(f"missing steady-state intervals: {records}")
        steps, seconds = map(sum, zip(*intervals))
        data = dict(buffers=buffers, steps=steps, seconds=seconds, sps=steps/seconds,
                    command=command, intervals=intervals, omp_wait=args.omp_wait)
        results.append(data)
        print(f"{i}: buffers={buffers} {data['sps']:.0f} decisions/s", flush=True)
    rates = {str(b): sum(r["steps"] for r in results if r["buffers"] == b)
             / sum(r["seconds"] for r in results if r["buffers"] == b) for b in sorted(set(args.buffers))}
    assert hashlib.sha256(checkpoint.read_bytes()).hexdigest() == digest
    (root / "summary.json").write_text(json.dumps(dict(rates=rates, runs=results), indent=2))
    print("Combined:", rates, flush=True)


if __name__ == "__main__":
    main()
