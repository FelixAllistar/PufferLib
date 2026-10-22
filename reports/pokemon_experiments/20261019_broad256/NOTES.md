# Broad native Metamon sweep, 2026-10-19

Stopped at the user's request after 27 completed trials (zero failures), during
trial 28. Controller and trainer both terminated; saved artifacts retained.
Best final result: run 7, 27W/0D/45L = 37.5% over 72 games, 64x3, ~27M steps.
Successor: fresh evolving 256x2 league documented in
`../20261019_roundrobin256_50m/NOTES.md`. Do not restart this sweep automatically.

User requested a large sweep capped at 256x3 and 500M steps, encoder checks, and
explicitly approved starting after checks. Run with the ordinary `./puffer sweep`;
there is no external sweep worker or replacement dashboard.

## Search

500 trials, fresh history at this directory's `sweep.history`. First trial is
256x2, 100M steps, 8192 agents, horizon 32, minibatch 4096, LR 0.0005, replay 2.
All trials start from scratch; older incompatible-size weights are not loaded.
Steps count both learner and frozen-policy slots, as in previous experiments.

| Group | Ranges |
| --- | --- |
| Budget | 25M–500M steps, log-scale |
| Recurrent model | width 64/128/256; 1/2/3 layers |
| Rollouts | agents 4096/8192/16384; horizon 16/32/64/128 |
| PPO | minibatch 1024/2048/4096/8192; replay 1–6 |
| Learning/exploration | LR 1e-5–0.003; entropy 1e-5–0.05 |
| Credit assignment | gamma 0.95–0.9999; lambda 0.8–0.995 |
| Losses | policy clip 0.05–0.4; value coefficient 0.1–2; value clip 0.05–1 |
| Optimizer | gradient norm 0.25–3; momentum 0.8–0.99 |
| Schedules | cosine LR stays on, floor 0.01–0.5; entropy anneal off/on, floor 0.01–1 |
| V-trace | off/on; rho cap 1–2; c cap 0.5–1 |
| Opponent mixture | frozen fraction 0/0.25/0.5/0.75/1, remainder current self-play |
| Potential shaping | HP and KO scales independently 0/0.25/0.5 |

25 varying dimensions. Team, rules, seed, benchmark, 2 buffers, synchronous
training and carry-over recurrent state are fixed. Reward clipping remains off
to preserve potential shaping. Existing 14 frozen 64x2 opponents are unchanged.
Every one of the 240 agent/horizon/minibatch/fraction combinations divides exactly.
The previous manual config and executable are backed up remotely under `before/`.
The old fixed sweep bounds conflicted with manual edits and caused the observed
`fixed sweep range must match config` abort; the new defaults are all in range.

## Evaluation

Native dashboard checks every ~10%: six games (2/team) for interim monitoring,
72 games (24/team) at the final checkpoint. Only the final score ranks the sweep;
the best interim score is not an objective. The dashboard labels best-score game
count because six-game bests are especially noisy. Both players use the existing
private-information adapter; learner argmax, sampled TaurosV0, alternating seats,
seed 20261025. Potential shaping does not change benchmark scoring.

This is a small fixed-team native-engine panel, not Showdown ladder performance.
Even 72 games remain a noisy search signal; selecting among 500 trials needs an
independent larger-sample confirmation before improvement claims. No automatic
confirmation queue was added. The Discord 80% claim has no supplied opponent,
sample count or protocol, so no fair numerical comparison is yet possible.

All checkpoints/results are retained; a 2 GiB free-space guard refuses new trials
before disk exhaustion (no deletion). The controller stops after eight failed
trials. The full 500-trial run can still take days; 500M is a per-trial cap, not a
total sweep budget. LR/replay/width and evaluation time strongly affect runtime.

## Encoder audit and qualification

No encoder implementation was changed. It embeds moves/species using identity
plus mechanics features, mean-pools known moves, builds 20 per-Pokémon/entity
embeddings, fuses them with public global state, then uses recurrent memory and
an observation-conditioned action decoder. Move/Pokémon embeddings stay width 32
with 64-wide intermediate MLPs; only fusion/recurrent/query widths scale. This is
a possible representational limitation, not evidence of a bug or proven cause
of the performance plateau. Fixed-team training also limits generalization.

- Existing FP32 CPU/GPU forward parity and 172 finite-difference gradient probes
  passed on Vast.
- New BF16 test passed at H64/H128/H256: CPU forward reference agreement, finite
  hidden gradients, and finite nonzero gradients in all 12 encoder/decoder weight
  tensors. Largest normalized forward error was 0.00257 (tolerance 0.025).
- 17 Python contract tests passed locally; native C lifecycle tests passed locally
  and on Vast. Broad configuration tests exhaust the 240 discrete layouts.
- Maximum-shape native sweep smoke (256x3, 16384 agents, H128, minibatch 8192,
  pure self-play) completed one 2,097,152-step trial plus six-game final Metamon
  evaluation and returned a valid history point. Trainer VRAM ~9.2 GiB; sweep
  controller ~0.88 GiB. Score zero is a short smoke result, not a strength test.
- Minimum-width/full-frozen-opponent native sweep smoke (64x1, 4096 agents,
  H16, minibatch 8192, V-trace on, HP/KO shaping 0.25 each) completed two
  rollouts, six interim games and twelve final games, and returned only the
  final score to native history. Separate interim/final counts worked end to end.

Launch uses the existing idle tmux pane, renamed `pokemon:broad256`, with exactly
`cd /workspace/PufferLib && ./puffer sweep`. No new viewer/window or worker script.
The launch config is snapshotted remotely as `launch_config.ini`.
Initial live verification: controller PID 740444, trainer PID 740449; 2.1M steps
completed with finite losses at ~329K steps/sec. Training-only ETA ~5 minutes;
evaluation pauses add time. Combined controller/trainer VRAM ~3.3 GiB.
Installed executable SHA256:
`747e2e16d903109da7ef0b480afcc7f9b1bf0d6f7d8fd253715af62f128a0e1a`.

The tests establish limited numerical/implementation checks, not architectural
optimality or native/Showdown benchmark equivalence.
