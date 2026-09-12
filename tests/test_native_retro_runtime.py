"""GPU integration test: real PPO update, graceful signal, checkpoint reload.

Run from the repo root with an explicitly built candidate. All outputs stay in
a fresh temporary directory, never in the user's `latest` checkpoint tree.
"""
import argparse
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import tempfile
import time


def interrupted_run(binary, root, asynchronous, sig, agents=64):
    batch = agents * 128
    cmd = [str(binary), "train", "retro", "env.cpu_backend=blocks", "env.render_backend=wide",
           "base.load_model_path=None", f"base.checkpoint_dir={root}",
           f"base.log_dir={root}/logs", f"base.async={asynchronous}",
           "base.checkpoint_on_interrupt=1", "base.checkpoint_interval=0",
           f"vec.total_agents={agents}", "vec.num_threads=1", "vec.worker_wait=block",
           "train.horizon=128", f"train.minibatch_size={min(2048, batch // 4)}",
           "train.total_timesteps=1048576", "env.max_frames=256"]
    child = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(child.stdout, selectors.EVENT_READ)
    output = bytearray()
    sent = False
    deadline = time.monotonic() + 120
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.2):
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                output.extend(chunk)
                # A completed real PPO update, not initialization or a timer.
                if not sent and (b"Epoch" in output or b"epoch=" in output):
                    child.send_signal(sig)
                    sent = True
            if child.poll() is not None and not selector.get_map():
                break
        else:
            raise AssertionError("trainer failed to save/exit within 120 seconds")
        text = output.decode(errors="replace")
        assert sent and child.returncode == 0, text
        match = re.search(r"Saved interrupted-run policy at step (\d+): (.+)", text)
        assert match, text
        step, filename = int(match[1]), Path(match[2].strip())
        assert step > 0 and step % batch == 0 and filename.stat().st_size > 0
        sidecar = Path(str(filename) + ".ini")
        assert sidecar.is_file(), "missing checkpoint configuration"
        assert not list(filename.parent.glob("*.tmp.*")), "unpublished checkpoint"
        evaluator = Path("build/retro_batch/sweep_eval").resolve()
        result = subprocess.run([str(evaluator), str(filename), "--frames", "16",
                                 "--repeats", "1", "--workers", "1", "--deterministic"],
                                capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stdout + result.stderr
        print(f"PASS async={asynchronous} signal={sig.name}: saved step {step}; "
              "checkpoint reloaded for all 32 levels", flush=True)
    finally:
        selector.close()
        if child.poll() is None:
            child.kill()
        child.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--agents", type=int, default=64, choices=[8, 16, 32, 64])
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix="retro-runtime-test."))
    print(f"Test artifacts: {root}", flush=True)
    interrupted_run(args.binary.resolve(), root / "sync", 0, signal.SIGINT, args.agents)
    interrupted_run(args.binary.resolve(), root / "async", 1, signal.SIGTERM, args.agents)
