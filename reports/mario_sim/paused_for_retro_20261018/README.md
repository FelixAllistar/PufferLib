# Simulator work paused for a new pixel/ROM experiment — 2026-10-18

The user chose to stop the current semantic-simulator sweep, preserve the work,
and return to the older real-ROM pixel workflow on Vast, in `ssh_tmux:0.0`.
The follow-up request is a broad new sweep of PPO, exploration/reward and rollout
settings. This is a change of experiment, not evidence that the simulator is
incorrect or that any particular encoder/model size caused the failures.

## Location and preserved state

- Local checkout: `/home/felix/puffertank/pufferlib`.
- Vast: `ssh -p 25023 root@ssh5.vast.ai`, checkout `/workspace/PufferLib`.
- One active trainer is `/workspace/PufferLib/puffer`. It is now being rebuilt
  for `retro`; its active editable configuration is `config/retro.ini`.
- This same report directory exists remotely. It contains the archived semantic
  trainer named `puffer`, `mario_sim.ini`, `sweep_worker.py`, GP history, rung
  history, `stop_manifest.json`, and `snapshot.json`.
- The archived simulator binary SHA-256 is
  `3279a3b2cac4c89f21b5ca3dd6842aabc34d147742d891a94cd85e7d31826ca5`.
- All simulator checkpoints, sidecars, logs, ROM evaluations and runtime/reset
  assets remain in place. No new RAM dumps or large dataset downloads were made.

The scheduler was stopped first to prevent another proposal. The active worker
was then asked to stop, saving at **260,046,848 steps** and completing its fixed
ROM evaluation. The scheduler was terminated after the result was preserved.
There are **33 completed trials in GP history**, ending at run 37; run IDs have
gaps from invalid preflight proposals. No completed worker failed.

Interrupted run 38 is **separate from GP history**:

```text
logs/mario_sim/sweep_1792307197716_0038.eval/trial.json
checkpoints/mario_sim/sweep_1792307197716_0038/0000000260046848.bin
score = 0.19353400735296103
reason = interrupted
requested budget = 756,138,048
actual budget = 260,046,848
```

Its checkpoint, curriculum/practice sidecars and complete fixed evaluation were
preserved. The compact trial/config/score/per-level reports are also copied to
`interrupted_run38/` here. A later history merge must use its actual trained
budget, not the requested budget. Resuming the archived history as-is simply
continues without observing this additional interrupted trial.

## What we had been trying

Earlier 2B-step hyperparameter sweeps identified useful configurations, but their
training progress scores were not fixed ROM win rates. A later 10B sweep and
long continuations showed that more training could substantially degrade a
policy. In one newer-simulator series, approximately 5.37B/9.13B checkpoints
cleared 336/349 of 1,024 individual-level attempts, while the 10B final cleared
only 9/1,024 and lost its FPG performance. An older pre-update model cleared
475/1,024. These are historical, separately budgeted evaluations, not controlled
evidence for an encoder, reward or simulator-fidelity explanation.

The simulator was expanded and validated using the existing local/reference
data, including the existing data on D:, rather than collecting another large
HDD dump. Work includes runtime/register behavior, natural continuing games,
progress bookkeeping, and 104 encounter-practice starts spanning 13 levels.
Full-game play, encounter practice and FPG share one 1,461-value semantic
encoder and a recurrent policy. The model reads terrain, player/control state
and typed objects; it does not train on the ROM framebuffer in `mario_sim`.

The latest search tested smaller/cheaper models and a broad range of learning,
resource and reward-independent training controls. Training ran the CPU
simulator with the policy on GPU. The frame mixture stayed fixed at 50% ordinary
games, 25% encounter practice and 25% FPG. Practice/frontier progression was
reset for each fresh trial; this was not continuation of the best trial.

## Latest sweep and score

```text
command: ./puffer sweep
config: config/mario_sim.ini
history: logs/mario_sim/sweep_20261017.history
rungs: logs/mario_sim/sweep_20261017.rungs.jsonl
worker: ocean/mario_sim/sweep_worker.py
target: 500 completed trials, one GPU / one active trial
```

Ranges were 200M–3B steps, 4K–32K actors, widths 32–256, one to three layers,
horizons 16–256, minibatches 1K–32K, LR 1e-5–.002, replay .25–2, gamma
.95–.9999, GAE .2–.999, entropy 1e-6–.03, annealing and standard PPO/V-trace
controls. Exact ranges/defaults are in the archived configuration and snapshots.
Widths 512/1024 were discussed but **not added**; no successful 32K-actor trial
had completed. Model size remains confounded with other settings and budget.

The initial clear-only score had a floor at zero. On 2026-10-17 we replaced it
with version 2 and rescored all six then-finished checkpoints before resuming
the same GP history. Migration/code/tests are recorded in
`reports/mario_sim/sweep_score_v2_20261017/`.

```text
fixed score v2 = .50 * individual-level clear rate
               + .25 * bounded continuing-game progress
               + .25 * mean FPG success across 17 start bands
```

The fixed ROM panel uses sampled FP32 CPU inference with independent recurrent
states, seed 137, and 58 panels:

- All 32 individual levels, four attempts each: first death/clear or 6,000 frames.
- Sixteen FPG attempts at each band: 3, 4, 5, 6, 8, 12, 16, 24, 32, 48, 64,
  96, 128, 192, 256, 320, 425 recorded frames before the glitch.
- Four fresh 1-1 attempts, plus 32 continuing games starting once from every
  original stage, natural lives, 108,000-frame cap.

Continuing progress is capped per game at 3,400 novel forward pixels before
averaging. Frontiers persist across lives; revisits and pipe teleports do not
earn extra distance. FPG depth counts recorded successful continuation frames,
not pixels or an episode timeout. The farthest band ends at the 1-1 pipe exit;
the full FPG average includes very easy starts beside the flagpole.

All completed/interrupted fixed evaluations passed ROM/native parity with
zero mismatches. That is evidence for the tested trajectories, not a proof of
all simulator behavior. Training CSV `score` is a different progress proxy;
read `final/score.json` or `trial.json` for the actual search objective.

## Results when paused

| Run | Fixed score | Trained steps | Level clears / 128 | All-band FPG | FPG at 425 / 16 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 23 | .322989 | 697.1M | 4 | 87.1% | 15 |
| 33 | .309816 | 597.8M | 0 | 92.3% | 7 |
| 24 | .307723 | 773.5M | 2 | 87.9% | 7 |
| 34 | .306638 | 949.5M | 0 | 95.2% | 14 |
| 28 | .301843 | 967.3M | 0 | 93.0% | 14 |
| 21 | .301252 | 763.9M | 5 | 70.6% | 0 |
| 37 | .284136 | 804.0M | 3 | 69.1% | 0 |

Run 23 remained the overall winner. Run 21 had the most individual-level clears
and 42.1% continuing-game progress; run 37 recovered three clears and 39.8%
progress after runs 28–36 all got zero individual-level clears. These are attempt
counts, not counts of unique solved levels. Each level has only four attempts,
so these small differences need larger confirmation panels.

Strong configurations generally used width 256, LR near .002, short horizons
16–32 and replay above one. A 256×1 model also scored .302. Entropy and momentum
were not consistently low: run 33 used entropy .00179 with decay and momentum
.914. No controlled causal conclusion follows from these correlations. FPG
improved substantially while whole-level competence remained weak.

## Returning to this work

Read `snapshot.json`, the archived config and version-2 migration notes first.
Preserve all checkpoint sidecars. The binary archived here is a reproducible
fallback for the current runtime assets; restoring it would replace the active
Retro `./puffer`. The runtime assets were not changed for the pixel experiment.
Use the archived config as `config/mario_sim.ini` and the existing history to
resume the GP rather than mixing pixel/ROM scores or a changed objective into it.
GP observations can be replayed/refitted; exact optimizer/RNG continuation of
the search is not guaranteed. Network checkpoints restore policy weights,
not optimizer/scheduler/RNG state.

Future work discussed but not performed: controlled model-size comparisons,
encoder reaction/causal probes, reward/reset-mixture changes, and larger fixed
ROM confirmation panels. The user's current direction is the pixel experiment.
