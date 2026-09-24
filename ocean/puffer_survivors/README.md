# Puffer Survivors

Puffer Survivors is a PufferLib 5.0 environment. The CPU path uses
the standard `Env`/`Agent` ABI and retains the Raylib renderer. The GPU path is
selected at build time and implements `puf_vec_create`, stream binding and
the native `puf_reset`/`puf_step` interface over a
structure-of-arrays CUDA simulator.

There is no Python binding, custom vec adapter, or legacy checkpoint loader.

## Build and run

From the repository root:

```bash
# CPU environment workers
bash build.sh puffer_survivors
./puffer train

# Native CUDA environment workers
bash build.sh puffer_survivors --cu
./puffer train
```

### Play / watch (one binary)

From the repo root:

```bash
make -C ocean/puffer_survivors viewer

./ocean/puffer_survivors/build/viewer # human play
./ocean/puffer_survivors/build/viewer watch latest
./ocean/puffer_survivors/build/viewer watch checkpoints/puffer_survivors/<run>/<step>.bin
./ocean/puffer_survivors/build/viewer watch latest --deterministic
```

Always run from the repo root so `config/`, `checkpoints/`, and
`resources/puffer_survivors/` resolve.

`bash build.sh puffer_survivors --cpu` builds the generic upstream checkpoint
viewer/headless evaluator instead of the interactive human-play executable.
The environment's own viewer uses upstream `src/puffercpu.c` inference; it does
not carry the old CPU network implementation. All renderer assets are tracked.
The native training config retains the three-reward sweep (survival/hurt/XP)
using fixed ranges for other inherited dimensions. Obsolete priority-replay and
expansion settings are removed rather than importing their old trainer code.

`watch` uses the **same** `PS_FAST_RENDER` viewer as human play. Policy
architecture (`hidden_size` / `num_layers`) comes from the `[policy]` section
of `config/puffer_survivors.ini` and must match the checkpoint. `latest` is
simply the newest `.bin` by ctime.

Controls: WASD/arrows to move (human), Shift to dash, `A`/`D` or left/right to select an
upgrade, Space to confirm, and `1`/`2`/`3` for direct card choice. `R` restarts,
`H` toggles hitboxes, `Q` changes FX quality, and Esc quits. Add
`--deterministic` to `watch` for discrete argmax actions; otherwise watch
samples the policy. Watch mode auto-resets on death.

The action heads are movement `[10]` and upgrade choice `[3]`; movement action
9 is Dash. The fixed observation schema contains 337 floats: player and boss state, enemy and drop
sector/ring summaries, nearest-obstacle relative `dx/dy`, weapon state, three
one-hot upgrade cards, and four nearest moving-hazard slots. Friendly weapon
areas and derived danger summaries are intentionally omitted. All
spatial values are translated into player-centered world axes and normalized.
The axes are not rotated by facing direction because movement actions use the
same fixed world axes. Velocity
and time-to-collision features are intentionally omitted so the recurrent
policy can infer changes from consecutive observations. Changing this layout
requires a fresh policy checkpoint.

Gameplay geometry is runtime configuration, not renderer state. Player, enemy,
elite/boss, obstacle, spawn-clearance, projectile, and area radii are defined
in `config/puffer_survivors.ini` and carried through `PSConfig` by CPU play,
CUDA training, and future WASM builds. Circle/AABB geometry is shared by both
backends; Ari K is configured as an AABB and the moving anchor/submarine
hazards use the same shape math. Changing those values, moving-hazard settings,
or the observation layout changes the game and requires a fresh policy run.
Enemy/weapon tuning and the wave tables are list-valued `[env]` config entries
as well. The INI is required for play, CPU training, CUDA training, and WASM;
a missing or invalid value fails fast.

## Weapon coverage and visuals

Every starter weapon has a crowd footprint: Bubble pops into a splash on its
final impact (4x its projectile radius), Whirlpool bursts around the player,
Orbit sweeps pearl-sized areas, Poison Oil leaves persistent pools, Sonar
hits a wide disk, Glacier hits a cone, and Spikes fires a radial fan. Spikes
scales linearly from 8 shots at level 1 to 64 at level 8; piercing still comes
from its existing level/Pierce upgrades. Bubble radius growth and Area upgrades
also enlarge its splash. Burst rings are visual only; they do not hit twice.

The play/watch backdrop uses one drifting water layer, distant reef, and
submerged debris, all scrolling with the same interpolated camera. Ground
effects render beneath obstacles and pickups. Hitboxes default off; `H` still
toggles them. The observation/action layouts are unchanged, but weapon tuning
changes gameplay, so old policies may need retraining.

## Performance measurements

Build the standalone native GPU simulation benchmark with:

```bash
make -C ocean/puffer_survivors NVCC=/usr/local/cuda/bin/nvcc bench-cuda
./ocean/puffer_survivors/tests/bench_cuda 5120 2000 200 3
```

It reports raw simulation throughput and the real `puf_step` wrapper
throughput. Remaining arguments use normal `section.key=value`
overrides, so hot-path A/B tests are reproducible:

```bash
./ocean/puffer_survivors/tests/bench_cuda 5120 2000 200 3 env.moving_obstacle_cap=0
```

Append `--stats` before the overrides to print population snapshots without
including the diagnostic copies in the timed result:

```bash
./ocean/puffer_survivors/tests/bench_cuda 5120 8000 500 1 --stats
```

`--stress` fills the existing pools with stable generated data at their
configured capacities, which isolates saturated hot paths without allocating
entities during the run.

The normal CUDA build uses an occupancy-aware enemy scan: it walks the dense
active list while the pool is sparse, then scans the fixed capacity once the
pool is at least half full. This keeps low-population waves cheap while
preserving coalesced SoA reads after late-game deaths and respawns.

For end-to-end PPO timing, run a short isolated job with `base.profile=1`.
The dashboard's `eval_env`, `eval_model`, `eval_copy`, `train_model`, and
`train_misc` fields are CUDA-event timings; compare identical
`vec.total_agents`, `train.horizon`, `base.async`, and `base.cudagraphs`.
Nsight Systems can use the same command with `--trace=cuda,nvtx,osrt` to show
the rollout and training ranges.

## Layout

```text
puffer_survivors.h       native 5c ABI, config mapping, and logging
puffer_survivors.cu      native GPU ABI
ps_sim.h                 shared CPU/CUDA gameplay and observation simulation
ps_state.h               CPU-only environment state
ps_geometry.h            shared CPU/CUDA circle/AABB collision primitives
cuda/ps_cuda_sim.cu      CUDA SoA allocation, kernels, and GPU ABI glue
tests/                   CPU invariants and native CUDA ABI smoke test
```

## Verification

```bash
make -C ocean/puffer_survivors test
make -C ocean/puffer_survivors sanitize
make -C ocean/puffer_survivors cuda-test
```

`cuda-test` requires a usable NVIDIA driver. CPU and CUDA tests cover inactive
enemy slots on both sides of the dense/capacity scan threshold, terminal rewards,
dash behavior, Bubble splash on circle/AABB targets (including Area scaling,
kills, and a full visual-effect pool), and weapon counts/radii across all levels.
The CPU test also checks pool invariants over 20,000 steps.

## Conversion verification

The 5.0 adapter has passed CPU mechanics tests with ASan/UBSan, CUDA mechanics
and terminal-reward checks, and short FP32 training runs on both CPU and GPU
simulation. The GPU run also passes with CUDA graphs and async enabled. The
play/watch inference test loads a checkpoint produced by the new trainer and
checks deterministic actions, stochastic action ranges and recurrent reset.
Interactive window controls have been compiled, not visually qualified here.
These are functionality tests, not production throughput or learning results.
