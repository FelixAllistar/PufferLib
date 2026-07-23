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

The action heads are movement `[9]` and upgrade choice `[3]`. Observation v9
contains 396 floats, including explicit boss and obstacle geometry.

## Layout

```text
puffer_survivors.h       native 5c ABI, config mapping, and logging
puffer_survivors.cu      native GPU ABI
ps_*.h                   shared CPU simulation, content, observations, renderer
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
