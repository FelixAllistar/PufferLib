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

The default config starts fresh with mirror selfplay. Frozen-league presets,
the richer legacy watch/play CLI, checkpoint inference parity, and the CUDA
simulator adapter remain migration work; do not treat this first CPU port as
qualification of those paths. The legacy implementation remains in the
`archive/5c-before-unification-20260924` recovery ref.
