# Expanded BC experiment (started September 22, artifact tag 20260923)

## Remote-only recovery

The initial local preparation process exited before publishing its manifest;
318 unique Majkel games survived as readable, parity-checked cached tapes.
Consequently the old remote input-wait job timed out and no expanded BC run
started. The exact cause of the local process exit was not established.

The recovery entry point is `run_expanded_bc_remote.sh`, executed entirely
inside the GPU host's tmux. All three raw archives and the existing tape cache
are transferred first into `qualification/bc_expansion_20260923/remote_inputs`.
The active recovery is tmux `ssh_tmux:bc-remote`, output directory
`/workspace/PufferLib/qualification/bc_expansion_20260923/remote_run.CPV5Ma`.
All three archive SHA-256 checks and the replay-core checksum passed on the
remote before preparation started. The old delivery/wait scripts must not be
used to launch or resume this recovery.
There is no dependency on a continuing local process or a future upload.
The cache-compatible replay core is isolated from the production simulator.
Each attempt gets a new `remote_run.XXXXXX` directory, persistent phase logs,
and `exit_status`. A lock prevents duplicate pipelines. The old local-delivery
and remote-wait instructions below describe the failed historical attempt,
not the recovery workflow.

The user authorized stopping the sweep and proceeding with BC. Sweep parent
2143002 and its active trial-13 child 2179001 were interrupted. Completed
trials/checkpoints were retained. The production `puffer`, default config,
rewards, reset bank, and submitted 299M checkpoint were not replaced.

## Sweep evidence

`protein_sweep_1790096864143.tsv` is a **fine-tuning** sweep: all inspected
startup snapshots load `1790058597151/0000000299335680.bin`. Trials 1–6 and
9–12 mostly preserve 79–82k evaluation money. Rates 0.00704 and 0.00832
collapse (trials 7/8). There is no clean plant/animal/PBRS winner: many knobs
change together. Trials 2/5 differ by only ~39 evaluation money. Their gamma
values already retain approximately 99%/97% of reward weight over 720 steps.
This does not establish that higher gamma alone can teach an earlier opening.

The minibatch 1440 -> 64 warning comes from evaluation: final `.ini` settings
show 16 agents / horizon 8, whereas `.start.ini` records training with 2048
agents / horizon 720 / minibatch 1440. `run_eval` mutates that in-memory INI.
Do not interpret the final `.ini` geometry as the geometry used for training.

Inspected rollout logs start at root money 77,531.9; most noncollapsed trials
end near 74.6–77.7k root money, not a convincing improvement. These training
statistics are not identical to the sweep's final evaluation score.

## Running / queued work

- Local CPU preparation: all 518 exact-name Majkel1337 games available in the
  September 17–19 archives, not a fabricated count of new downloads. Reuse
  previously parity-checked tapes; verify each new game frame-by-frame.
- Expected original episode split: 442 train / 76 validation, before any
  parity rejection. The same seven pilot holdouts are included in validation;
  this is **not an untouched final test set** after pilot tuning.
- Local manifest destination:
  `/home/felix/puffertank/elite_replays/bc_prep_2026-09-23/majkel_all/summary.json`.
- Local tmux `kag-bc-delivery` uploads compact tapes and an atomic portable
  manifest after preparation. It does not copy the multi-GB raw archives.
- Remote tmux `ssh_tmux:bc-expanded` waits for that manifest, then runs the
  bounded comparison. It refuses concurrent puffer/BC jobs, too few accepted
  games, or an existing output directory. Input waiting has a finite deadline.
- Remote root: `/workspace/PufferLib/qualification/bc_expansion_20260923`.
  Logs: `expanded_pipeline.log`, `expanded_comparison/*.log`.
  Completion: `expanded_comparison/COMPLETED.json` (not a strength approval).
- Separate build: `ocean/kaggriculture/build/bc_expansion_20260923/`.
  Production trainer/runtime are unchanged; source fingerprints are rebuilt,
  never bypassed. Returns are regenerated for the frozen current rewards/gamma.

`run_expanded_bc.py` uses the current H256/L2, obs3/policy5/controller2/2.
It runs 40 full-episode epochs each for actor-only uniform BC (SGD LR .003),
actor-only opening-weighted BC (same LR, first 24 turns x16), and an
opening-weighted branch initialized from the original PPO checkpoint (LR
.00005). Each is evaluated reset-free against a common rules mix in both
seats. No model is auto-promoted, no new rewards/leagues are introduced, and
no long PPO run is queued by this script. Retain weak standalone clones for
the intended downstream comparison, not just the standalone winner.

## Small checks already performed

Fresh 64-game labels built under the current source/rewards; trainer preflight
and 20–30-epoch H256/L2 GPU pilots passed. Pilots at SGD .0003/.001/.003 and
PPO initialization are calibration artifacts, not the expanded candidates.
They still underfit: the .003/168-turn-weighted pilot achieved ~14.9% exact
labeled-row validation accuracy, but zero exact root rows. The 24-turn x16
pilot improved root CE to ~9.12 but still had zero exact root rows. These
numbers are not game results and are not evidence BC is ready for promotion.

The opening audit of 64 tapes found BUY_ANIMAL COW on command turn 0 in every
game, BUILD_PASTURE on turn 2, and first BUY_LAND median turn 149. The audit
counts commands, not successful fills; PLACE COW may include shed returns.
This establishes a useful teacher contrast with the user's crops-first model,
not a universal optimal opening.

Local tests: 29 passed, seven optional native cross-build/preflight tests
skipped. An initial run encountered a stale local bridge missing `kag_bc_abi`;
rebuilding the bridge in the new isolated directory resolved those failures.

A separate two-update BC -> eMAG/PPO integration check completed successfully in
`ssh_tmux:bc-ppo-check`, using the PPO-initialized pilot, 256 agents, reset
probability .8, frozen-reference KL .01, tau 0, cutoff .134, and LR .00005.
See `ppo_load_smoke.log` and checkpoints under `bc_expanded_load_smoke` at
184,320 and 368,640 steps. Both updates and saves completed; this is not
evidence of downstream improvement.

## Next gate

Inspect the full dataset rejection/coverage report, opening metrics and closed
loop behavior; then compare BC -> eMAG/PPO, BC -> PPO, and matched non-BC
starts. The old launcher supplies the .01/0/.134 eMAG recipe, but its retired
reward/controller overrides are deliberately not reused. Keep training resets
on and evaluation resets off. Critic pretraining remains a separate experiment:
the previous variance-normalized joint pilot did not learn a useful value
function; adding a coefficient is not an adequate claim of fixing it.
