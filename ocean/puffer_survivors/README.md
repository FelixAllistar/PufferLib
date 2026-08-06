# Puffer Survivors

Puffer Survivors is a first-class PufferLib 5c environment. The CPU path uses
the standard `Env`/`Agent` ABI and retains the Raylib renderer. The GPU path is
selected at build time and implements the standard `puf_envs_*` ABI over a
structure-of-arrays CUDA simulator.

There is no Python binding, custom vec adapter, or legacy checkpoint loader.

## Build and run

From the repository root:

```bash
# CPU environment workers
bash build.sh puffer_survivors
./puffer train puffer_survivors

# Native CUDA environment workers
bash build.sh puffer_survivors --gpu
./puffer train puffer_survivors
```

### Play / watch (one binary)

From the repo root:

```bash
bash build.sh puffer_survivors --fast

./puffer_survivors                      # human play (new renderer)
./puffer_survivors watch                # latest checkpoint under checkpoints/puffer_survivors/
./puffer_survivors watch latest
./puffer_survivors watch checkpoints/puffer_survivors/<run>/<step>.bin
./puffer_survivors watch latest --deterministic
```

Always run from the repo root so `config/`, `checkpoints/`, and
`resources/puffer_survivors/` resolve.

`watch` uses the **same** `PS_FAST_RENDER` viewer as human play. Policy
architecture (`hidden_size` / `num_layers`) is inferred from the checkpoint
file size, so a tiny new run is not broken by an unrelated larger model still
sitting in the tree — `latest` is simply the newest `.bin` by ctime.

Controls: WASD/arrows to move (human), `A`/`D` or left/right to select an
upgrade, Space to confirm, and `1`/`2`/`3` for direct card choice. `R` restarts,
`H` toggles hitboxes, `Q` changes FX quality, and Esc quits. Add
`--deterministic` to `watch` for discrete argmax actions; otherwise watch
samples the policy. Watch mode auto-resets on death.

The action heads are movement `[9]` and upgrade choice `[3]`. Observation v10
contains 412 floats: the existing 396-float prefix, explicit boss/obstacle
geometry, and four fixed moving-hazard slots. Each slot encodes active/type and
player-relative `dx/dy`, sorted by nearest distance rather than pool allocation
order. The policy receives raw relative geometry; velocity and collision
predictions are not hand-coded into the input.

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

## Layout

```text
puffer_survivors.h       native 5c ABI, config mapping, and logging
puffer_survivors.cu      native GPU ABI
ps_*.h                   shared CPU simulation, config, content, observations, renderer
ps_geometry.h            shared CPU/CUDA circle/AABB collision primitives
cuda/ps_cuda_sim.cu      internal CUDA SoA mechanics
tests/                   CPU invariants and native CUDA ABI smoke test
```

## Verification

```bash
make -C ocean/puffer_survivors test
make -C ocean/puffer_survivors sanitize
make -C ocean/puffer_survivors cuda-test
```

`cuda-test` requires a usable NVIDIA driver. See `VERIFY.md` for the checks run
during the 5c port.
