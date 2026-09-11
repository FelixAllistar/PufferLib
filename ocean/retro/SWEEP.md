# ROM training sweep

Run from `/home/felix/puffertank/pufferlib`:

```bash
./build.sh retro
./puffer sweep retro
```

This uses the same native Protein sweep machinery as the other environments.
The default is **16 trials × 16,777,216 training decisions**, one trial at a
time on one GPU. The first trial uses the current defaults, followed by random
exploration and model-guided suggestions. All trials start fresh with the same
training seed; they never load a moving `latest` checkpoint.

At the earlier 17.3k PPO SPS, the training alone would take about 4.3 hours.
Budget additional time for initialization, evaluation and slower sampled
settings; this is an estimate, not a measured sweep runtime.

A shorter first screen:

```bash
./puffer sweep retro sweep.max_runs=8 sweep.trial_timesteps=8388608
```

Do not use `train.total_timesteps` to set the sweep budget: the native retro
hook deliberately replaces it with `sweep.trial_timesteps`. Ordinary
`./puffer train retro` keeps its separate, longer training budget. Choose
trial budgets divisible by 262,144 with the default agent count, so all sampled
rollout lengths receive exactly the same number of decisions. The default and
shorter budgets above satisfy this.

## What is searched

| Setting | Range | Purpose |
| --- | --- | --- |
| `train.learning_rate` | 0.0001–0.003, log | Update size |
| `train.ent_coef` | 0.0003–0.03, log | Exploration |
| `train.gamma` | 0.997–0.9998, logit | Credit across delayed rewards |
| `train.gae_lambda` | 0.90–0.995, logit | Advantage bias/variance |
| `train.horizon` | 64, 128, 256 | Recurrent rollout/unroll length |
| `train.replay_ratio` | 1–4, integer | Learning work per collected batch |

The native launcher couples the potential-shaping discount to the sampled
`train.gamma`; there is no independent shaping-gamma sweep.

ROM execution, all 32 starts, one-frame controls, all 64 button combinations,
reward ratios/scale, policy architecture and vector hardware settings stay
fixed. This is a learning-settings sweep, not a search over different games or
over weakened control fidelity. The architecture remains 128 × 2, compatible
with the current ROM viewer contract. No viewer changes are needed.

`sweep_only` excludes inherited generic search ranges, including training
duration and architecture. Final checkpoints alone are scored at equal step
budgets; intermediate scores are not mixed into the objective. There is no
early performance pruning. `sweep.max_suggestion_cost` is an optimizer cost
hint, **not a hard timeout**. Slow trials still finish their step budget.

## Rewards and clipping

`env.reward_scale=1/16` multiplies completion, death, score and potential
shaping together, preserving their ratios. With the default weights:

- Completion contributes +0.625, and death −0.0625.
- Shaping is `(gamma * Phi(next) - Phi(previous)) / 16`, with zero terminal
  potential and `Phi(x)=clamp(x/3400, 0, 1)`.
- The total lies within [−0.125, +0.6875]. The enabled ±1 reward clip therefore
  leaves it unchanged.

The launcher refuses a scale/weight combination whose conservative bound
exceeds the configured clip. Do not reintroduce component-wise normalization
or independently clip the completion bonus. This change does not modify
physics, subpixels, input timing or any ROM RAM.

## Sparse-aware evaluation

Every final checkpoint gets exactly two seeded attempts at **each** of the
32 starts (64 attempts). An attempt stops at its first source-level clear,
death/game-over, or 3,600 emulated frames. It never reuses an auto-reset as a
second attempt. Training still continues naturally across ordinary level
transitions; this stopping rule applies only to the evaluation panel.

The panel uses stochastic policy sampling with private, fixed per-case RNG
streams. Cases start with zero recurrent state. Worker scheduling does not
change those streams. CPU float32 inference uses the saved native policy
weights; it is not a bit-exact comparison against CUDA bfloat16 inference.

The ranking is:

```text
N = fixed number of attempts
P = mean clamp((furthest_original_area_x - starting_x) / 3400, 0, 1)
score = 100 * (number_of_clears + 0.5 * P) / N
```

One extra clear beats the entire possible progress contribution. Progress
only breaks ties among equal-clear candidates, including the common case
where none can finish a level yet. Only coordinates in the original
world/stage/area and level-data pointer contribute to this proxy, excluding
bonus-room coordinate jumps. The last terminal frame is not added to it.
The proxy is not a calibrated percentage of each level: rightward movement
can still be a dead end, so inspect actual clears for finalists.

Coins, in-game score, survival time and shaped episode return do not enter
the panel score. A warp counts as clearing its source, not every skipped
level. Never compare scores between different panel budgets, repeat counts
or sampling modes as if they were the same benchmark.

The shared native hook writes the metric name `sweep/exact_score`. Here
"exact" is just that hook's historical name: **this is a policy evaluation,
not a hardware-parity test or a guarantee of learning every glitch**. The
ROM/core limitations in [README.md](README.md) still apply.

## Find, watch and extend a good trial

The sweep prints its results TSV path, normally
`logs/retro/protein_sweep_<timestamp>.tsv`. Each completed row contains the
panel score, elapsed cost, actual training steps, run ID and all six sampled
settings. Rank complete rows by `score` (higher is better); a tiny positive
score with zero clears is only early progress, not a solved level.

Artifacts for each run:

```text
checkpoints/retro_rom/retro/<run_id>/<steps>.bin           policy weights
checkpoints/retro_rom/retro/<run_id>/<steps>.bin.ini       full configuration
checkpoints/retro_rom/retro/<run_id>/<steps>.bin.eval.tsv  every panel attempt
logs/retro/<run_id>.ini                                  training metrics
```

Watch the chosen checkpoint explicitly; `latest` means newest, not best:

```bash
./retro watch checkpoints/retro_rom/retro/RUN/STEPS.bin --random
```

Replace `RUN` and `STEPS` with the selected artifact. You can repeat the same
panel without running training or opening the viewer:

```bash
./build/retro/sweep_eval checkpoints/retro_rom/retro/RUN/STEPS.bin
```

For checkpoints predating the sidecars, add `--config logs/retro/RUN.ini`.
For a separate, more thorough finalist comparison, give **every finalist**
the same larger panel, for example `--frames 7200 --repeats 4 --seed 20260908`.
Keep the default stochastic sampling when comparing against sweep scores;
`--deterministic` is an optional, separate greedy diagnostic.

Copy the six winning settings into `[train]` in `config/retro.ini` (leave the
`[sweep.train.*]` ranges alone), then train longer from scratch:

```bash
./puffer train retro train.total_timesteps=67108864 base.seed=73
```

Repeat promising configurations with additional `base.seed` values such as
74 and 75. The native trainer uses **`base.seed`**, not `train.seed`, for
initialization and action sampling. Compare their actual clear counts on a
common held-out panel. Do not treat the luckiest short seed as the winning
hyperparameters. Loading a short checkpoint explicitly is also possible, but
that continues from weights only, not a restored optimizer/RNG state.

## Validation

```bash
make -C ocean/retro test -j2
```

This includes existing 65,536-frame same-core differential tests plus reward
scale/bound, gamma-coupling, sweep-shape and panel scoring/action-layout tests.
A bounded two-trial native smoke sweep also verified fresh workers, finite
PPO updates, final scoring, metadata and per-level report generation. Its
262,144-step trials are plumbing tests, not a tuning result. Their artifacts
were kept outside the normal checkpoint tree so they do not replace `latest`.
