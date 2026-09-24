# TrianglePath

A fully observed weighted triangle. Choose left or right at each row, collecting
the apex through the bottom cell. Episodes last `height - 1` steps. An exact
backward-DP oracle reports `optimal`, `regret`, and `perf = score / optimal`.

This is the CPU/GPU simulator port from `5c`. The cell generator,
observations, actions, reset timing, and three reward modes are preserved.
The adapter follows `ocean/squared/squared.h`: `obs_t` precedes `pufferenv.h`,
`Agent` owns the I/O pointers, and the environment implements `puf_*` directly.
The oracle and simulator share this one header, following `SKILL_ISSUES.md`.

The config retains height 6, cell range 1–9, seed 42, and reward mode 2 from the
old experiment. Trainer, network, and sweep settings inherit upstream defaults;
the old 4B-step experiment and custom trainer keys are not a new baseline.
Dense mode emits unscaled cell rewards, which the stock trainer clamps to
`[-1, 1]`; terminal modes 1 and 2 are bounded by 1.

From the repository root:

```bash
bash build.sh trianglepath build/trianglepath_cpu --cpu
./build/trianglepath_cpu --headless --base.eval_episodes=32
bash build.sh trianglepath build/puffer_trianglepath
./build/puffer_trianglepath train
bash build.sh trianglepath build/puffer_trianglepath_gpu --cu
./build/puffer_trianglepath_gpu train --vec.num_buffers=1
make -C ocean/trianglepath cuda-test
uv run --no-project --with pytest python -m pytest -q \
    ocean/trianglepath/tests/test_trianglepath.py
```

Both native trainers use CUDA inference/learning. Use `--float` when building
for a pre-Ampere GPU. There is no renderer in either the legacy or current port.
The GPU simulator retains the legacy state layout and kernel rules while using
upstream's `puf_vec_create` / `puf_bind_stream` interface. Its exact DP oracle
shares the CPU implementation. GPU reset explicitly clears reward/terminal
outputs; neither backend requires core trainer changes.

The CPU suite compares legacy traces and checks the DP oracle against brute
force, in optimized and sanitized builds. CUDA tests cover all three reward
modes, heights 2/6/16/64, zero/constant/full-byte cell values, and non-default
streams: 12 cases of 16 environments by 256 steps. Observations match bytewise;
rewards, terminals and logged metrics match within 1e-6. As in the legacy GPU
adapter, explicit vector reset restarts its seeded RNG streams; automatic
episode resets advance those streams and match CPU trajectories.
