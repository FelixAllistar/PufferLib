# Preserved score-reward policy

Run: `checkpoints/retro_cnn_128x120_retry/retro/1789649527428/`.
Final checkpoint: `0000000499974144.bin`, 499,974,144 decisions.
Actual shape: **64×60**, 187,224 parameters, despite the old directory name.
SHA256: `ddb6c573eba45f4e0b6ecbaef0c6e3ddd81591a5d18e4c3fca60ffb61c8e0cec`.

Checkpoint, EMA weights, sidecar, experiment config, trainer and viewer are
copied here. The score-only run's final logged clear rate was 1.0 on 1-1;
episode time was capped at 3000 frames and did not measure time to clear.
The new speed reward keeps the same input/architecture and four-frame actions.
It does not overwrite or retrain these files. A new run defaults to fresh
weights, in `checkpoints/retro_cnn_64x60_speed`.

Watch the saved policy from the repository root:

```sh
artifacts/retro-score-policy-20260917/retro watch artifacts/retro-score-policy-20260917/0000000499974144.bin --inspect
```

Config values in this archive describe the old score-based experiment.

## Reward change validation

Replayed this policy deterministically on 1-1 with four-frame actions under
both reward formulas. Policy inputs and trajectory boundaries matched exactly.
It cleared at native frame **2419** (reported on decision ending at 2420), then
continued to the existing 3000-frame timeout. Old return: 26150.3; new return:
0.854091. The small return is a different objective/scale, not a performance
regression. This replay does not prove learning quality under the new reward.

Passed CPU ROM/reference and reward tests; early/late clear ordering, zero
point rewards, death/progress scales, bounded clear-only bonus and clear-time
denominator; 600 inspector decisions with 18 resets; and a fresh async PPO
4096-decision save/reload smoke test across all 32 levels. Smoke output lives
under `/tmp/retro-speed-reward-smoke.mdt_13_o`, not the normal checkpoint tree.

## GPU/rollout diagnostic

The original run's phase log showed roughly 0.20–0.22 seconds of learning
followed by 0.85–1.34 seconds waiting for rollouts in sampled intervals. The
final async update collects no next rollout, explaining the dashboard's
misleading final `Env 0 ms` and `Train 100%` breakdown.

Short current-machine tests used this explicit policy with its old rewards,
128 agents, horizon 256, minibatch 4096, replay 1, four CPU workers, frameskip 4.
Twelve updates per run, excluding the first two and final async drain; buffer
counts 1/2/4 were repeated in reverse order. Mean rates (weighted by time):

| Buffers | OpenMP wait | Decisions/s |
|---|---|---:|
| 1 | PASSIVE | 11,559 |
| 2 | PASSIVE | 9,606 |
| 4 | PASSIVE | 10,379 |
| 1 | ACTIVE | 5,643 |

ACTIVE used two subsequent runs. These are short throughput diagnostics under
current host load, not comparisons of learning quality or direct predictions
of a six-hour run. No buffer, batch, model, optimizer, or shared trainer code
was changed. Keep one buffer and launch with `OMP_WAIT_POLICY=PASSIVE`.
Reproduction: `tests/bench_retro_buffers.py`; raw artifacts:
`/tmp/retro-buffer-profile.uum0djhe` and `/tmp/retro-buffer-profile.k9p8gvyg`.
