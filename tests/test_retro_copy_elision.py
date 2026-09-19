"""Compare full PPO updates before/after Retro's observation copy elision.

Use explicit binaries and a read-only checkpoint. Synchronous, single-worker
runs compare actual saved weights with CUDA graphs disabled and enabled. All
outputs are isolated from the user's checkpoint tree. Run with an idle GPU.
"""
import argparse
from array import array
import hashlib
import math
import os
from pathlib import Path
import subprocess
import tempfile


def train(binary, checkpoint, root, graphs):
    root.mkdir(parents=True)
    command = [str(binary), "train", "retro", f"base.load_model_path={checkpoint}",
               f"base.checkpoint_dir={root}", f"base.log_dir={root}/logs",
               "base.seed=73", "base.async=0", f"base.cudagraphs={graphs}",
               "base.checkpoint_interval=0", "base.checkpoint_on_interrupt=0",
               "base.eval_epoch_mult=0", "base.perf_log=1",
               "vec.total_agents=8", "vec.num_buffers=1", "vec.num_threads=1",
               "policy.hidden_size=128", "policy.num_layers=2",
               "vec.worker_wait=block", "env.cpu_backend=blocks", "env.render_backend=wide",
               "env.spawn_levels=all", "env.frameskip=1", "env.max_frames=256",
               "train.horizon=128", "train.minibatch_size=256", "train.replay_ratio=1",
               "train.total_timesteps=6144"]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, OMP_WAIT_POLICY="PASSIVE"))
    output = result.stdout + result.stderr
    (root / "stdout.log").write_text(output)
    assert result.returncode == 0, output[-6000:]
    checkpoints = list(root.glob("retro/*/0000000000006144.bin"))
    assert len(checkpoints) == 1, f"missing final checkpoint under {root}"
    data = checkpoints[0].read_bytes()
    values = array("f")
    values.frombytes(data)
    assert len(values) == 351064 and all(math.isfinite(value) for value in values)
    return data


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("checkpoint", type=Path)
    args = parser.parse_args()
    baseline = args.baseline.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    checkpoint = args.checkpoint.resolve(strict=True)
    before = checkpoint.read_bytes()
    root = Path(tempfile.mkdtemp(prefix="retro-copy-parity."))
    print(f"Test artifacts: {root}", flush=True)
    for graphs in (-1, 1):
        old = train(baseline, checkpoint, root / f"graphs{graphs}-baseline", graphs)
        new = train(candidate, checkpoint, root / f"graphs{graphs}-candidate", graphs)
        assert old != before and new != before, "PPO did not update weights"
        assert old == new, f"PPO weights differ with base.cudagraphs={graphs}; see {root}"
        print(f"PASS graphs={graphs}: 6 PPO updates, all 351,064 saved weights bit-identical; "
              f"sha256={hashlib.sha256(new).hexdigest()}", flush=True)
    assert checkpoint.read_bytes() == before, "input checkpoint was modified"
