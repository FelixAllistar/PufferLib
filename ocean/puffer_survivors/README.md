# Puffer Survivors production cleanup

This folder is a cleaned CPU + CUDA version of the Puffer Survivors environment.
It keeps the CPU environment usable for gameplay, evaluation, and raylib rendering,
while the CUDA vector environment remains the high-throughput training path.

## Main changes

- Shared `PSConfig`, `Log`, constants, and observation offsets for CPU and CUDA.
- Observation v5: 344 floats, including eight explicit nearest-boss features.
- More useful near/mid/far polar rings.
- Dense active-index lists for projectiles, drops, areas, and rendering.
- Stable enemy slots are retained; enemy simulation scans remain contiguous so the
  compiler can still vectorize them and CPU/CUDA traversal stays easier to compare.
- The enemy spatial grid is rebuilt before weapon updates and reused for radial
  attacks and projectile collision.
- CUDA simulator and Puffer vector adapter are separate translation units. No `.cu`
  file includes another `.cu` implementation.
- CUDA log aggregation is reduced on-device into one `Log` and copied once.
- Rendering caches frame/atlas data and has quality levels to reduce draw-call cost.

## Folder layout

```text
binding.c                    Puffer/vecenv entry point
puffer_survivors.h           public CPU environment umbrella header
ps_constants.h               limits, enums, observation version and size
ps_config.h                  shared simulation configuration
ps_log.h                     shared CPU/CUDA log structure
ps_observation_layout.h      stable observation offsets and boss feature indices
ps_defs.h                    common C helpers/includes
ps_state.h                   CPU state and entity pools
ps_content.h                 enemy/weapon/wave content
ps_observation.h             CPU observation encoder
ps_systems.h                 CPU simulation
ps_render.h                  raylib renderer
cuda/ps_cuda_sim.cuh         opaque CUDA simulator API
cuda/ps_cuda_sim.cu          CUDA simulator implementation
cuda/ps_cuda_vec.h           C ABI used by binding.c
cuda/ps_cuda_vec.cu          thin Puffer-facing CUDA adapter
tests/                       smoke tests, syntax stubs, and benchmarks
```

## CPU integration

Keep `binding.c` as the environment binding source and place this folder where
PufferLib can include its `vecenv.h`. The normal CPU path does not require any
CUDA definitions.

The simulation configuration now lives at `env->cfg`, for example:

```c
PSConfig cfg = ps_default_config();
cfg.enemy_cap = 256;
env->cfg = cfg;
```

The binding still accepts the same kwargs used by the uploaded version.

## CUDA integration

When enabling the custom CUDA vec path:

1. Define `PS_ENABLE_CUDA_VEC` while compiling `binding.c`.
2. Compile **both** CUDA translation units:
   - `cuda/ps_cuda_sim.cu`
   - `cuda/ps_cuda_vec.cu`
3. Link both CUDA objects into the same extension as `binding.c`.
4. Pass `cuda_sim=1`, `use_cuda_sim=1`, or `cuda_env=1` in environment kwargs.

Example object compilation from this directory:

```bash
nvcc -O3 --use_fast_math -std=c++17 -I. \
  -c cuda/ps_cuda_sim.cu -o ps_cuda_sim.o

nvcc -O3 --use_fast_math -std=c++17 -I. \
  -c cuda/ps_cuda_vec.cu -o ps_cuda_vec.o
```

Your existing Puffer extension build must then link `ps_cuda_sim.o` and
`ps_cuda_vec.o`. Do not compile the old `puffer_survivors_cuda_single.cu` or
`puffer_survivors_cuda_vec.cu` names alongside these replacements.

## Observation v5

```text
PS_OBS_VERSION = 5
PS_OBS_SIZE    = 344
```

The first 26 player features are unchanged. Eight boss features are inserted at
indices 26–33:

```text
26 boss_present
27 boss_dx
28 boss_dy
29 boss_proximity
30 boss_hp_fraction
31 boss_max_hp
32 boss_closing_speed
33 boss_count
```

The former v4 indices 26–335 move to v5 indices 34–343. See `MIGRATION.md` for
checkpoint migration details.

## Renderer quality

Press `Q` while rendering to cycle quality:

- `0`: low — sprites and simplified effects
- `1`: medium — default
- `2`: high — trails/glows and more expensive effects

Raylib already uses the GPU for rasterization. The renderer changes here reduce
CPU-side draw submission and geometry work; they do not attempt CUDA/OpenGL
interop.

## Verification

Run:

```bash
make verify
```

For a CPU throughput check:

```bash
make bench-cpu
./tests/bench_cpu 64 5000
```

To inspect what your local compiler actually auto-vectorizes:

```bash
./tests/vectorization_report.sh
```

The branch-heavy enemy collision and collection loops are intentionally not
forced through unsafe SIMD pragmas. The report lets you check each target CPU
before attempting platform-specific intrinsics.

With a real CUDA toolkit/GPU:

```bash
make bench-cuda
./tests/bench_cuda 8192 10000
```

See `VERIFY.md` for what was run while preparing this package and the limits of
that verification.
