# Bomberman on PufferLib 5.0

CPU environment port from `5c` at `036cf4251`. The simulator, 1200-float
canonical observation, six legal-action masks, reward rules, reverse curriculum
and Raylib environment renderer are preserved. `policy_0_score` and
`policy_1_score` expose the existing seat scores to the upstream match API.

```bash
make -C ocean/bomberman test
./build.sh bomberman puffer_bomberman --float
./puffer_bomberman train
./build.sh bomberman bomberman_view --cpu
```

Training uses CPU simulation and GPU PPO. The standalone viewer uses upstream
`src/puffercpu.c`. Environment selection is compiled in; do not pass
`bomberman` as an extra argument to the resulting binary.

The richer play/watch tool is also available:

```bash
make -C ocean/bomberman viewer
./ocean/bomberman/build/viewer play latest
./ocean/bomberman/build/viewer watch PATH_A.bin PATH_B.bin
./ocean/bomberman/build/viewer selftest PATH.bin 32
make -C ocean/bomberman viewer-test CHECKPOINT=PATH.bin
```

It loads simulator/network settings from a sibling `config.ini` or the matching
`logs/bomberman/RUN.ini`, including checkpoints in a different install. Visual
sessions default to 30,000 ticks; append the saved deadline for matched timing.
With no model argument, play/watch selects the latest checkpoint, not a bundled
champion. The new upstream masked sampler replaces the old custom sampler;
identical stochastic action sequences across versions are not promised.
Headless inference tests pass with old and new checkpoints; interactive window
behavior still needs visual qualification.

The default config starts fresh with mirror selfplay. Frozen-league presets
and exact old/new checkpoint inference parity remain migration work.
The legacy implementation remains in the
`archive/5c-before-unification-20260924` recovery ref.

## GPU simulator

The legacy CUDA simulator now uses upstream's vector create/reset/step/close
API and bound stream. One log shell represents one match; each shell reports
its agent count for the upstream log reducer. No shared runtime changes are
needed for this single-policy path.

```sh
make -C ocean/bomberman gpu-test NVCC=/usr/local/cuda/bin/nvcc CUDA_ARCH=sm_61
./build.sh bomberman puffer_bomberman_gpu --cu --float
./puffer_bomberman_gpu train --vec.num_buffers=1 --vec.num_policies=1 \
    --selfplay.enabled=0 --env.reverse_curriculum=0
```

The SM61 suite checks 4,096 match transitions against the CPU simulator,
including two/four agents, immediate/longer episode limits, non-default streams,
recreation, log metadata and CUDA graph replay. Match states are byte-identical;
observations/rewards/logs pass their existing floating-point tolerances.
Native FP32 training completes 2,048 steps with CUDA graphs off and on;
the two runs save byte-identical final weights. These are smoke tests, not
learning-performance benchmarks.

Limitations preserved from the old GPU path: reverse curriculum and rendering
are unavailable, and it does not bind CPU-style legal-action masks. The current
trainer also rejects frozen-opponent/multi-policy GPU use without a policy-row
setup hook. CPU mode remains the default and retains its masks/curriculum.
Do not use the GPU path as a matched masked-CPU training comparison.
