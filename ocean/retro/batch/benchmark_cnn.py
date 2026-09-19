#!/usr/bin/env python3
"""ABBA comparison of checkpoint-compatible CNN implementations.

Both executables get the same explicit checkpoint/configuration. All benchmark
checkpoints/logs stay in a fresh private directory. Never stops other processes;
run only with an idle GPU. Excludes startup and the final asynchronous drain.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def run(binary, checkpoint, root, updates, buffers, agents=64,
        minibatch_size=2048, replay_ratio=2, asynchronous=1):
    root.mkdir(parents=True)
    batch = agents * 128
    cmd = [str(binary), "train", "retro", f"base.load_model_path={checkpoint}",
           f"base.checkpoint_dir={root}", f"base.log_dir={root}/logs",
           "base.checkpoint_interval=0", "base.checkpoint_on_interrupt=0",
           "base.perf_log=1", "base.eval_epoch_mult=0", "base.seed=73", f"base.async={asynchronous}",
           f"vec.total_agents={agents}", f"vec.num_buffers={buffers}", "vec.num_threads=4",
           "vec.worker_wait=block", "env.cpu_backend=blocks", "env.render_backend=wide",
           "train.horizon=128", f"train.minibatch_size={minibatch_size}",
           f"train.replay_ratio={replay_ratio}",
           f"train.total_timesteps={batch*updates}"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=180,
                            env=dict(os.environ, OMP_WAIT_POLICY="PASSIVE"))
    output = result.stdout + result.stderr
    (root / "stdout.log").write_text(output)
    if result.returncode:
        raise RuntimeError(f"benchmark exit {result.returncode}: {output[-6000:]}")
    records = [{key: float(value) for key, value in re.findall(r"(\w+)=([\d.e+-]+)", line)}
               for line in output.splitlines() if line.startswith("[perf] ")]
    intervals = []
    for before, after in zip(records, records[1:]):
        if before["steps"] < batch*2 or after["steps"] >= batch*updates:
            continue
        intervals.append({"steps": after["steps"]-before["steps"],
                          "seconds": after["uptime"]-before["uptime"]})
    if len(intervals) < updates-4 or any(item["seconds"] <= 0 for item in intervals):
        raise RuntimeError(f"missing steady-state timers: {records}")
    steps = sum(item["steps"] for item in intervals)
    seconds = sum(item["seconds"] for item in intervals)
    info = dict(command=cmd, steps=steps, seconds=seconds, sps=steps/seconds,
                intervals=intervals, records=records)
    (root / "result.json").write_text(json.dumps(info, indent=2)+"\n")
    return info


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--updates", type=int, default=10)
    parser.add_argument("--buffers", type=int, default=1, choices=[1, 2, 4])
    parser.add_argument("--agents", type=int, default=64)
    parser.add_argument("--minibatch-size", type=int, default=2048)
    parser.add_argument("--replay-ratio", type=int, default=2)
    parser.add_argument("--async-mode", type=int, default=1, choices=[0, 1])
    args = parser.parse_args()
    if args.updates < 6:
        parser.error("at least six updates are needed for steady-state intervals")
    if (args.agents < args.buffers or args.agents % args.buffers
            or args.minibatch_size < 128 or args.minibatch_size % 128
            or args.agents * 128 % args.minibatch_size or args.replay_ratio < 1):
        parser.error("agents must divide across buffers; minibatches must divide the rollout and use whole horizons")
    checkpoint = args.checkpoint.resolve(strict=True)
    original_hash = hashlib.sha256(checkpoint.read_bytes()).hexdigest()
    root = Path(tempfile.mkdtemp(prefix="retro-cnn-abba."))
    print(f"Benchmark artifacts: {root}", flush=True)
    binaries = {"baseline": args.baseline.resolve(strict=True),
                "candidate": args.candidate.resolve(strict=True)}
    results = []
    for index, name in enumerate(["baseline", "candidate", "candidate", "baseline"]):
        result = run(binaries[name], checkpoint, root / f"{index}-{name}", args.updates,
                     args.buffers, args.agents, args.minibatch_size, args.replay_ratio, args.async_mode)
        result["variant"] = name
        results.append(result)
        print(f"{index} {name}: {result['sps']:.1f} decisions/s", flush=True)
    rates = {}
    for name in binaries:
        subset = [item for item in results if item["variant"] == name]
        rates[name] = sum(item["steps"] for item in subset) / sum(item["seconds"] for item in subset)
    assert hashlib.sha256(checkpoint.read_bytes()).hexdigest() == original_hash
    summary = dict(buffers=args.buffers, agents=args.agents, minibatch_size=args.minibatch_size,
                   replay_ratio=args.replay_ratio, asynchronous=args.async_mode,
                   checkpoint=str(checkpoint), checkpoint_sha256=original_hash,
                   rates=rates, speedup=rates["candidate"]/rates["baseline"], runs=results)
    (root / "summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    print(f"Summary: {rates}; speedup={summary['speedup']:.3f}x", flush=True)
