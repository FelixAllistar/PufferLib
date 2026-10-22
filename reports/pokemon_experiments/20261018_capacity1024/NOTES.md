# Fresh 1024x2 cross-size experiment — 2026-10-18

User requested fresh larger learner, 1024 hidden x 2 recurrent layers, long runs
against good frozen policies. Corrected budget: **1 billion initial steps,
500 million–2 billion sweep range**, 500-trial default. This is not a restart
of the stopped H64 continuation sweep or round-robin league.

## Setup

- Remote: `/workspace/PufferLib/reports/pokemon_experiments/20261018_capacity1024`.
- Frozen BF16 trainer SHA256: `07849fd021b6534648cdbcddf98375ae855a9e31c12b5f9829c607d0a2f93a78`.
- Learner: fresh H1024 L2, 9,601,056 stored FP32 weights; no H64 initialization.
- Frozen opponents: H64 L2. `pool_manifest.json` records original paths and
  weight/config hashes. Eight banks: master r26, master sweep50, gar_egg_mie r36,
  mie_egg_cloy r13, jynx_egg_zap r13, zam_egg_cloy_don r13, mie_egg_don r13,
  gar_egg_zap r13. Chosen for results in the focused comparison and team diversity;
  small differences among those estimates are uncertain.
- Fixed learner Starmie team remains `421,366,389,395,512,445`, lead 121.
- Trial zero: 2048 agents, horizon32, minibatch2048, LR0.0003, replay3.710475,
  expert fraction .5. Sweeps fraction .25/.5/.75, agents2048/4096,
  horizon16/32/64, minibatch1024/2048/4096, plus PPO controls in `pokemon.ini`.
  All 54 layout/fraction combinations have divisible learner minibatches.
- Steps count ALL agent slots. At .5 expert fraction, 1B total represents .75B
  learner-slot steps. This definition matches prior experiments.
- Cosine LR annealing always on; floor .01–.5 (starts .1). No opponent rotation.
- Ordinary `config/pokemon.ini` and existing experiments are unchanged.

## Objective and validation

`sweep_metamon.py` uses the native worker hook and unchanged 784-byte result ABI.
Trainer exits before Metamon loads on the GPU. Each final checkpoint is scored
with TaurosV0: argmax learner, sampled Metamon, native-private-v1, 8 games per
each of three opponent teams = 24 games, alternating seats, seed20261025.
Wins score1, draws .5. Failures never substitute training win rate. Reports retain
weight/config/adapter/source hashes, outcomes, settings and wall time.
Oak is disabled; sweep optimizes Metamon score. No automatic larger confirmation
queue is launched. Promising results need independent larger confirmation.

24 games is a noisy screen (~20 percentage point 95% sampling half-width near
50%, rough independent-binomial approximation). Fixed seeds improve consistency,
but repeated optimization can overfit the panel. This is not a clean isolated
capacity comparison: training length, LR, opponent pool and objective also change.

Checks before launch:

1. H1024 vs H64: 1,048,576 steps, 2048 agents/horizon32/minibatch2048,
   completed, ~52k steps/s, ~1.4GiB VRAM, finite logged losses.
2. Maximum planned memory layout: 4096 agents/horizon64/minibatch4096/fraction.25,
   1,048,576 steps completed, ~3.4GiB VRAM. Full native sweep -> worker -> trainer
   -> 6 Metamon games -> native sweep history passed. 0W/0D/6L is only a smoke
   result, not an estimate for the billion-step policy. Total worker40.86s.
3. Four Python unit tests: packet format, score validation, wrong/incomplete
   evaluation rejection, every proposed layout's learner divisibility.

At ~52k steps/s, first 1B trial is roughly5.4h plus initialization/evaluation;
other settings change runtime. 500 trials is a cap, not a near-term completion
promise (months on this GPU). Instance has ~12GiB free; worker refuses new trials
below2GiB. Native controller stops after32 failures, so storage exhaustion leads
to immediate failed starts and then stop, not more expensive training. All
initial/final checkpoints are retained; archival/space expansion is needed long
before500 trials. Nothing is automatically deleted.

## Operation

`setup.py` validates selected originals and creates isolated config/manifest.
`launch.sh` verifies trainer hash and refuses to overwrite existing history.
Live window: `pokemon:capacity1024`; per-trial progress every30s, complete native
dashboard in `logs/pokemon/<run>.metamon/train.log`. Evaluation logs/results in
the same directory; native sweep history `capacity.history`. View with
`tmux attach -t pokemon`. Window stays visible on completion/failure.

Launched trial0: `sweep_1792363119924_0000`. ControllerPID601290,
workerPID601298, trainerPID601299. Verified first786,432 steps, finite losses,
~52.8k steps/s, ~2.3GiB allocated VRAM in the production run. First-trial remaining
time displayed ~5h15m at that check. Pool unchanged; prior jobs stay stopped.

### Read-only status view (2026-10-19 00:06 UTC)

User requested clearer output only if training remained untouched. Added
`ocean/pokemon/benchmarks/watch_sweep.py`: reads existing logs/results and /proc,
refreshes a stationary terminal panel every5s, best/latest completed Metamon
scores first. Pending evaluations are never shown as zero. Three unit tests pass.
No periodic evaluations were added; Metamon still runs only after each trial.

Reused the already-dead `metamon-followup` pane%27 for the viewer and named it
`pokemon:capacity1024`. Original running pane%28 remains intact, now named
`pokemon:capacity1024-raw`. No trainer/worker/controller edits, signals, or
restarts. SamePIDs601290/601298/601299 verified alive at1h27m elapsed, ~276M
steps,52.8kSPS. Closing the viewer cannot stop training. Both windows retained;
no additional windows created. Raw output and all previous evaluation files kept.

### Approximately10% snapshot evaluations (2026-10-19, after trial2 started)

User requested feedback every10% rather than waiting a full1B-step run. Interpreted
as evaluation cadence, not a reduction in the already chosen500M–2B budgets.
The currently running second trial remains final-only. New workers, beginning
with trial3, use the same24-game CUDA Metamon evaluation at each~10% checkpoint.
This supersedes the final-only monitoring description above; final sweep ranking
and total budgets remain unchanged.

Implementation in `sweep_metamon.py` sets the trainer's checkpoint interval to
`ceil(floor(total_steps / rollout_steps) * .1)` epochs before starting it. It
does not change any other training settings, rotate frozen banks, stop early,
restart, resume, or signal an active trainer. Each intermediate immutable weight
file is hard-linked into its own evaluation input directory with a frozen copy
of its matching config. The original final-only worker was archived remotely as
`sweep_worker_before_periodic.py` before deployment.

One Metamon evaluator at a time runs alongside training; GPU competition and
snapshot saves can add overhead. Failed monitoring checks are recorded without
terminating training. If evaluations lag, they queue; remaining checks finish
after training. The final checkpoint is still evaluated after the trainer exits,
and only its score is returned to PROTEIN. No intermediate-score early stopping
or changed optimizer objective. Every evaluation uses24games, seed20261025,
same private protocol, argmax learner/sampled Metamon.

Dashboard now separates **Best seen** (any evaluated snapshot, including finals)
from **Best completed** (final checkpoints), shows latest interim score and a
per-trial sequence. A best-of-ten noisy peak is not confirmed improvement.
Reports: `logs/pokemon/<run>.metamon/snapshot_evaluations.json`, with individual
`snapshots/<step>/games/summary.json`. All checkpoint weights remain retained;
hard-linked evaluator inputs consume no additional weight storage. About11
weight files/run (~403MiB plus logs) increases archival needs. Existing2GiB
free-space guard remains enabled; no prior files are deleted.

Validation:12unit/integration-style tests pass, including approximate10% schedules
across all agent/horizon/budget bounds, nine interim plus final pipe result,
evaluations dispatched while mocked training is active, single evaluator at a
time, intermediate failure isolation, frozen config/hardlink ownership, and
separate dashboard interim/final bests. These tests do not benchmark concurrent
GPU overhead; that remains to be observed when the next real trial reaches10%.

At deployment, active trial2 trainerPID652729 and worker652728 remain unchanged;
controllerPID601290 remains unchanged. Its saved config SHA256 is
`c0716ddc3be3c2895a918dd32c0460b4a46f03104bdf4f93db4ae99e6f0d17f4`.
No modifications to trainer binary, experiment INI, or running worker arguments.
