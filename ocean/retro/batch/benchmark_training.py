#!/usr/bin/env python3
"""Matched real-PPO ABBA comparison, with warmup/drain updates excluded.

Uses one explicit checkpoint, identical seeds/hyperparameters, and private
artifact directories. An optional authorized pause has an independent watchdog
and pidfd, so the original training process is resumed even if this script dies.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import sys
import tempfile
import time


def dashboard_rows(text, batch):
    rows = {}
    for block in text.split("╭"):
        epoch = re.search(r"\bEpoch\s+(\d+)", block)
        uptime = re.search(r"\bUptime\s+(.+?)\s{2,}Model", block)
        if not epoch or not uptime:
            continue
        scale = {"ms": .001, "s": 1, "m": 60, "h": 3600, "d": 86400}
        seconds = sum(int(n)*scale[u] for n, u in re.findall(r"(\d+)\s*(ms|s|m|h|d)", uptime[1]))
        rows.setdefault(int(epoch[1]), dict(steps=int(epoch[1])*batch, uptime=seconds))
    return list(rows.values())


def run_case(binary, checkpoint, root, candidate, updates, deadline):
    root.mkdir(parents=True)
    batch = 64*128
    cmd = [str(binary), "train", "retro", f"base.load_model_path={checkpoint}",
           f"base.checkpoint_dir={root}", f"base.log_dir={root}/logs",
           "base.checkpoint_interval=0", "base.checkpoint_on_interrupt=0",
           "base.perf_log=1", "base.eval_epoch_mult=0", "base.seed=73", "base.async=1",
           "vec.total_agents=64", "vec.num_buffers=1", "vec.num_threads=4",
           f"vec.worker_wait={'block' if candidate else 'spin'}",
           f"env.cpu_backend={'blocks' if candidate else 'reference'}", f"env.render_backend={'wide' if candidate else 'reference'}",
           "env.frameskip=1", "env.full_render=0", "env.idle_loop_skip=1",
           "env.spawn_levels=1-1", "train.horizon=128", "train.minibatch_size=2048",
           f"train.total_timesteps={batch*updates}"]
    env = dict(os.environ, OMP_WAIT_POLICY="PASSIVE")
    child = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    selector = selectors.DefaultSelector()
    selector.register(child.stdout, selectors.EVENT_READ)
    output = bytearray()
    stopped_baseline = False
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(.1):
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                output.extend(chunk)
                # Older binaries ignore eval_epoch_mult=0. Their final policy
                # is already saved before the final training dashboard; end
                # only this disposable benchmark child before post-train eval.
                if not candidate and not stopped_baseline:
                    if re.search(rb"\bEpoch\s+" + str(updates).encode() + rb"\s", output):
                        child.send_signal(signal.SIGTERM)
                        stopped_baseline = True
            if child.poll() is not None and not selector.get_map():
                break
        else:
            raise TimeoutError("benchmark deadline reached")
        text = output.decode(errors="replace")
        (root / "stdout.log").write_text(text)
        assert child.returncode == 0 or (stopped_baseline and child.returncode == -signal.SIGTERM), text[-4000:]
        records = []
        for line in text.splitlines():
            if line.startswith("[perf] "):
                records.append({k: float(v) for k, v in re.findall(r"(\w+)=([\d.e+-]+)", line)})
        if not records:
            records = dashboard_rows(text, batch)
        # Async startup collects two horizons; the final update drains without
        # collecting another horizon. Neither is a steady-state throughput sample.
        intervals = []
        for before, after in zip(records, records[1:]):
            if after["steps"] >= batch*updates:
                continue
            intervals.append(dict(steps=after["steps"]-before["steps"],
                                  seconds=after["uptime"]-before["uptime"]))
        assert len(intervals) >= updates-2 and all(i["seconds"] > 0 for i in intervals), records
        checkpoint_files = list(root.glob("retro/*/*.bin"))
        assert len(checkpoint_files) == 1, checkpoint_files
        result = dict(candidate=candidate, command=cmd, intervals=intervals,
                      steps=sum(i["steps"] for i in intervals),
                      seconds=sum(i["seconds"] for i in intervals),
                      checkpoint_sha256=hashlib.sha256(checkpoint_files[0].read_bytes()).hexdigest())
        result["sps"] = result["steps"]/result["seconds"]
        (root / "result.json").write_text(json.dumps(result, indent=2)+"\n")
        return result
    finally:
        selector.close()
        if child.poll() is None:
            child.kill()
        child.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate", type=Path)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--baseline", type=Path, default=Path("puffer_retro_batch"))
    parser.add_argument("--updates", type=int, default=8)
    parser.add_argument("--pause-pid", type=int, help="Only with permission; same process resumes within 175 seconds")
    args = parser.parse_args()
    assert args.updates >= 4
    args.checkpoint = args.checkpoint.resolve(strict=True)
    args.candidate = args.candidate.resolve(strict=True)
    args.baseline = args.baseline.resolve(strict=True)
    root = Path(tempfile.mkdtemp(prefix="retro-training-abba."))
    print(f"Benchmark artifacts: {root}", flush=True)
    pidfd = None
    watchdog = None
    deadline = time.monotonic()+1200
    try:
        if args.pause_pid:
            pidfd = os.pidfd_open(args.pause_pid)
            command = Path(f"/proc/{args.pause_pid}/cmdline").read_bytes().split(b"\0")
            assert len(command) > 2 and command[1:3] == [b"train", b"retro"], command
            assert b"puffer" in command[0], command
            # EOF on parent death or a 175-second timeout both resume the
            # stable pidfd target. This does not load/restart/kill that trainer.
            code = "import select,signal,sys; select.select([sys.stdin],[],[],175); signal.pidfd_send_signal(int(sys.argv[1]),signal.SIGCONT)"
            watchdog = subprocess.Popen([sys.executable, "-c", code, str(pidfd)],
                                        stdin=subprocess.PIPE, pass_fds=(pidfd,))
            signal.pidfd_send_signal(pidfd, signal.SIGSTOP)
            deadline = time.monotonic()+165
            print(f"Paused trainer {args.pause_pid}; independent auto-resume watchdog armed", flush=True)
        results = []
        for index, candidate in enumerate([False, True, True, False]):
            result = run_case(args.candidate if candidate else args.baseline,
                              args.checkpoint, root / f"{index}-{'candidate' if candidate else 'baseline'}",
                              candidate, args.updates, deadline)
            results.append(result)
            print(f"{'candidate' if candidate else 'baseline'}: {result['sps']:.0f} SPS "
                  f"({result['steps']:.0f} measured decisions / {result['seconds']:.3f}s)", flush=True)
        rates = {name: sum(r['steps'] for r in results if r['candidate']==flag)/
                       sum(r['seconds'] for r in results if r['candidate']==flag)
                 for name, flag in [('baseline', False), ('candidate', True)]}
        report = dict(runs=results, sps=rates, speedup=rates['candidate']/rates['baseline'],
                      active_trainer_paused=bool(args.pause_pid),
                      all_final_weights_equal=len({r['checkpoint_sha256'] for r in results})==1)
        (root / "summary.json").write_text(json.dumps(report, indent=2)+"\n")
        print(json.dumps({k: v for k, v in report.items() if k!='runs'}, indent=2), flush=True)
    finally:
        if pidfd is not None:
            signal.pidfd_send_signal(pidfd, signal.SIGCONT)
            if watchdog:
                watchdog.stdin.close()
                watchdog.wait(timeout=5)
            os.close(pidfd)
            print("Original trainer resumed", flush=True)


if __name__ == "__main__":
    main()
