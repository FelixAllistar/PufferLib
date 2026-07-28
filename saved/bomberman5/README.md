# Bomberman baseline 5

Current pre-rescaling baseline. It combines a from-scratch 1B-step curriculum
run with a 100M-step, full-game continuation using a 400-tick deadline.

- Parent run: `from_scratch_curriculum_1bn`
- Continuation run: `from_scratch_1bn_rush_800` (the name says 800, but the
  resolved run config correctly records `max_ticks = 400`)
- Final continuation checkpoint: `99,942,400`
- Exact cumulative training from random initialization: `1,099,890,688` steps
- Saved model: `model.bin`
- SHA-256: `35e80a90e05b07e143ab75413159e8d6f7a46826ffc29e149af6c1e97472c68f`
- Architecture: 1200 observations, hidden 128, 2 MinGRU layers, 6 actions

This policy is qualitatively capable but remains conservative late in games.
It was trained under the original raw reward configuration, which Puffer
clamped per step to `[-1, 1]`. Consequently, values such as kill `+70`, timeout
`-2`, and self-kill near `-101` all saturated, while pickup `+0.5` and soft-block
`+0.3` remained unsaturated and disproportionately large.

This saved model is the parent/control for the normalized reward experiment.

```bash
./bomberman watch saved/bomberman5/model.bin
./bomberman play saved/bomberman5/model.bin
```
