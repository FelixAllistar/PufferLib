# TrianglePath

A fully observed weighted triangle. Choose left or right at each row, collecting
the apex through the bottom cell. Episodes last `height - 1` steps. An exact
backward-DP oracle reports `optimal`, `regret`, and `perf = score / optimal`.

This is the CPU simulator port from `5c` (`3fa5d14cc`). The cell generator,
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
uv run --no-project --with pytest python -m pytest -q \
    ocean/trianglepath/tests/test_trianglepath.py
```

The native trainer uses CPU simulation and CUDA inference/learning. Use `--float`
when building for a pre-Ampere GPU. There is no renderer. The old CUDA simulator
and its parity suite remain in the original checkout; they are not yet ported
to upstream's `puf_vec_create` / `puf_bind_stream` interface.
