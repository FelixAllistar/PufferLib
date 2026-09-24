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

The default config starts fresh with mirror selfplay. Frozen-league presets,
exact old/new checkpoint inference parity, and the CUDA
simulator adapter remain migration work; do not treat this first CPU port as
qualification of those paths. The legacy implementation remains in the
`archive/5c-before-unification-20260924` recovery ref.
