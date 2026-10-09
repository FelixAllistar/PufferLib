# FPG backward curriculum

The default experiment starts extremely close to a flagpole glitch and moves
backward along a successful 1-1 ROM trajectory as the policy learns. It uses
423 real save states, spanning 3–425 recorded frames before the FPG. The first
start is grounded, at X=3158, ten pixels left of the pole. Later starts include
real mid-jump states from the same route. There are no synthetic RAM edits.

## Train, resume and view

The rebuilt binary and assets are available locally. From the repository root:

```sh
# Continue the completed 268M semantic run, with a small speed bonus:
build/mario_fpg_time/puffer train

# Fresh run:
build/mario_fpg_time/puffer train --base.load_model_path=None

# Resume a checkpoint produced by the semantic encoder:
build/mario_fpg_time/puffer train --base.load_model_path=path/to/semantic_checkpoint.bin

# View a semantic policy at its saved median curriculum difficulty:
build/mario_fpg_time/puffer eval path/to/semantic_checkpoint.bin
```

The [config](../../config/mario_fpg_time.ini) uses a 268,435,456-frame training
budget, 4,096 agents, a 64-wide two-layer MinGRU and all 64 controller masks.
Each action advances one video frame. Episodes allow 1,800 frames and end on FPG success,
an ordinary flag grab, death or timeout. The 32-frame rollout horizon does not
reset episodes. The default loads
`checkpoints/mario_fpg_time/1791497853012/0000000268435456.bin` and its saved
curriculum progress. Each command trains for another full budget; optimizer
state and the training step counter start fresh.

**Earlier RAM-policy checkpoints have a different architecture and are rejected.**
Their files are preserved;
the previous local executable is `build/mario_fpg_time/puffer_ram_baseline`.
Use an explicit semantic checkpoint for evaluation when old and new runs coexist.

## Full-playfield semantic encoder

The observation covers the entire 256-by-208 gameplay region, screen rows
32–239. The top 32 HUD rows are excluded. The terrain grid has 17 columns and
13 rows of 16-pixel tiles: the extra column preserves both partially visible
edges during scrolling. An aligned camera has 16 visible columns and one masked
padding column. No player-centered crop is taken.

Three branches feed the same recurrent policy and value head:

| Branch | Input and network |
| --- | --- |
| Terrain | 12 semantic channels over 17×13 cells; 3×3 convolutions with 8 channels, strides 2 then 1; flatten 7×9×8 to 64 features |
| Player | 64 decoded physics and control features through a 32-unit ReLU layer |
| Objects | Shared 32→32 ReLU encoder per typed object; masked max and mean pooling plus object count |

The concatenated 161 features pass through a 64-unit ReLU layer, then the
existing two-layer MinGRU and 64-action/value head. The default network has
79,088 parameters, including 50,352 in the encoder. Every input terrain cell
reaches the CNN; its first strided layer reduces resolution after reading the
full playfield.

Terrain channels describe known cells, blocking, head collision, bumpable
blocks, pipes, climbable tiles, goals, coins, springs, partial coverage and
player-relative X/Y. Unknown parser columns and offscreen padding are distinct
from known empty space. Player features include signed velocity, subpixel and
tile phase, screen position, collision box, movement state, previous buttons,
physics timers and visible pole offsets. HUD score, lives, coin count, game
timer and world/level labels are absent, as are audio and sprite bookkeeping.

The 49 object slots cover six enemy slots with space for firebar segments,
nine hammers, two player fireballs and two bouncing blocks. Long firebars use
their reserved duplicate enemy slot for all twelve segments. Ordinary enemies,
shells, platforms, powerups, Bowser's flame and both Bowser bodies use their
decoded types and geometry. Cosmetic explosions, brick debris and score effects
are omitted. Objects carry relative position, size, observed motion with a
validity mask, state bits and relevant timers; firebar geometry uses the ROM's
quantized rotation tables. Per-object state decoding distinguishes platform
partner markers from enemy defeat states.

The transport contains 1,461 values: 64 player fields, 221 categorical tile IDs
and 49×24 object fields. Tile IDs are expanded into semantic channels before
any learned operation. Object types select a one-hot weight column; the network
never treats IDs as ordered magnitudes. CPU and CUDA share the same decoder,
and object motion history resets with each episode. Training and rollout use
device kernels without host synchronization inside the encoder.

Each checkpoint gets `.curriculum` and `.curriculum.json` sidecars. They preserve
per-actor difficulty and mastery counters, and report the distribution of actor
frontiers. Resume checks the exact model and reset-bank fingerprints, restores
progress, and starts new episodes. Keep these files alongside the weights.
`curriculum_resume=0` deliberately starts a new curriculum from loaded weights.

Evaluation freezes advancement and disables easier practice. Its default band
is the saved median actor frontier; `--env.curriculum_eval_frames=32`, for
example, selects the 25–32-frame band explicitly. An older checkpoint without a
sidecar starts at the configured initial band. Use the simulator/ROM evaluator
below for complete, individually finished evaluation episodes.

## How the curriculum advances

All actors train the same policy. Each tracks its own recent outcomes, avoiding
host synchronization during GPU rollouts. Difficulty bands end at these recorded
remaining-frame counts:

```text
3, 4, 5, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 320, 425
```

An actor initially samples only the three-frame start. At each later level,
80% of resets sample the newly introduced band; 20% revisit an earlier band.
An earlier band is chosen uniformly, then a state within that band is chosen
uniformly. **Only episodes from the current frontier count toward promotion.**

Promotion needs at least 80% FPG success in each of two consecutive windows of
16 frontier episodes: at least 13 wins in each window. It advances exactly one
band. A failed window clears the qualifying streak and keeps the current band.
The final band ends at the original pipe exit. “Frames” here means the recorded
successful continuation length, not a shortened episode timeout.

| Config key | Default | Meaning |
| --- | ---: | --- |
| `curriculum` | 1 | Enable backward start selection |
| `curriculum_adaptive` | 1 | Advance during training |
| `curriculum_start_frames` | 3 | Initial band; choose a boundary listed above |
| `curriculum_window` | 16 | Frontier episodes per mastery window |
| `curriculum_confirmations` | 2 | Consecutive qualifying windows |
| `curriculum_threshold` | 0.8 | Required success fraction |
| `curriculum_replay` | 0.2 | Fraction of easier practice after level zero |
| `curriculum_resume` | 1 | Restore saved actor progress when loading a model |
| `curriculum_eval_frames` | 0 | Zero uses saved median; another boundary fixes the evaluation band |

Logs report `curriculum_frames`, `curriculum_start_frames`, `frontier_perf`,
`frontier_fraction` and `curriculum_promotions`. The ordinary `perf` includes
easier practice and should be read alongside difficulty. Checkpoint JSON reports
show actor counts per band; these are not weighted by episode length.
The broad sweep below logs these training metrics but ranks a fixed evaluation
panel, since it also varies promotion requirements.

## Broad hyperparameter sweep

From the repository root:

```sh
# Inspect the resolved ranges and budget without launching training:
python3 ocean/mario_fpg_time/sweep.py --dry-run

# 128 fresh trials, 67,108,864 training frames each, one at a time:
python3 ocean/mario_fpg_time/sweep.py

# Optional budget overrides (steps must be a multiple of 131,072):
python3 ocean/mario_fpg_time/sweep.py --runs 32 --steps 134217728

# Rank completed trials while a sweep is running:
python3 ocean/mario_fpg_time/sweep.py --summary reports/mario_fpg_time/sweep_TIMESTAMP
```

The [sweep profile](profiles/sweep.ini) overlays the default and Mario configs
and uses the existing native PROTEIN search. It starts each trial with fresh
weights and curriculum, independently of the normal continuation configuration.
The first trial uses the profile's baseline, then PROTEIN proposes candidates.
There are 27 varying parameters:

| Group | Settings |
| --- | --- |
| Model and batches | Hidden width 32–256, 1–4 recurrent layers, 512–2,048 agents, horizon 16–64, minibatch 512–4,096 |
| PPO and optimizer | Learning rate, LR annealing/floor, entropy coefficient and annealing/floor, gamma, GAE lambda, replay ratio, policy clip, value clip, value coefficient, gradient limit, momentum |
| Timing reward | Bonus 0–0.3, target scale 0.5–1.5, slack 0–16 frames, power 0.5–2 |
| Curriculum | Mastery window, confirmation count, promotion threshold, easier replay fraction |

The full viewport, encoder branch sizes, single-frame controller actions,
original ROM reset bank, three-frame initial band and training seed remain
fixed. Rollout/batch dimensions are powers of two with compatible ranges.
The largest allowed configuration was exercised on the local 3GB GPU; it can
use the full device memory, so these bounds leave little room for concurrent GPU work.
CUDA graph mode and synchronous execution follow the normal config.

Each trial receives exactly the same training frame budget. After training
exits, its frozen checkpoint runs 16 complete sampled episodes at each exact
start depth `3, 8, 16, 32, 64, 80, 96, 128, 192, 256, 320, 425`, seed 137,
with a fixed 1,800-frame cap. Both the simulator and ROM run independently.
The search score is the fraction of the 192 episodes that actually achieve
FPG, equally weighting the twelve starts. Incomplete or mismatched evaluations
fail the trial. Reward scale, timing target and promotion thresholds cannot
inflate this score. These starts come from the training bank; this measures
the learned approach, not unseen-level generalization or a full new-game clear.
Recheck promising candidates with more episodes and new seeds before extending
their training.

The default totals 8,589,934,592 training frames. At the earlier 83K steps/sec
baseline that alone is about 29 hours; larger models, replay and CPU/ROM
evaluation add time. `max_suggestion_cost` guides the search, not a time limit.
This is a prepared long run; a dry run does not launch it. Use one training or
sweep job at a time on the local GPU.

Each sweep writes under a new `reports/mario_fpg_time/sweep_TIMESTAMP/` directory:
the resolved `config.ini`, parent `sweep.log`, per-trial training logs, final
weights and curriculum sidecars, evaluation configs, episode traces, and
`result.json` with score, parameters, checkpoint hash and complete per-depth
results. Failed trials retain `failure.json`; three worker failures stop the
search. Final checkpoints are retained; intermediate checkpointing is disabled
for the sweep. The Python launcher uses only the standard library.

## Historical RAM-policy learning trial

A fresh 16,777,216-frame run took 207.8 wall seconds. Its median actor frontier
moved from 3 recorded frames to 4 by 4.2M training frames, 8 by 8.4M, and 16 by
13.6M. It finished at the 13–16-frame band, where it was still learning.

Frozen policies were then compared on the same exact start states, using 128
complete stochastic episodes per depth at seed 137. The baseline has the same
initialization seed and zero learning rate. Counts below are actual FPGs:

| Recorded frames before FPG | Untrained | After 16.8M frames |
| ---: | ---: | ---: |
| 3 | 13/128 | 128/128 |
| 4 | 6/128 | 128/128 |
| 6 | 3/128 | 128/128 |
| 8 | 0/128 | 125/128 |
| 12 | 0/128 | 118/128 |
| 16 | 0/128 | 9/128 |
| 24 | Not measured | 2/128 |
| 32 | Not measured | 1/128 |

Every evaluated policy trajectory matched between the native simulator and ROM.
These results show learning at the close end and functioning automatic
progression. They do not establish a solution from the full pipe exit, or that
this curriculum outperforms every fixed-start alternative. The starts are from
the training bank; the evaluation seed changes action sampling, not the level.

Evidence, configuration, progress reports and fingerprints are in
[`results/curriculum_20261004`](results/curriculum_20261004/summary.json).
These results predate the semantic encoder and do not measure its learning.

## Build and validate

With the local NTSC ROM, annotated disassembly, action tapes, Raylib and CUDA
installed, run from the repository root:

```sh
make -f ocean/mario_fpg_time/Makefile curriculum
make -f ocean/mario_fpg_time/Makefile -j2 test curriculum-test semantic-test transfer
CUDA_HOME=/usr/local/cuda ./build.sh mario_fpg_time build/mario_fpg_time/puffer --cu
```

The builder loads the original full NES pipe-exit state, replays its successful
425-frame continuation, and saves each eligible earlier state. Every state has
its own full NES snapshot and recorded successful suffix. It checks serialization
and 175,792 additional controller frames against the ROM. The CUDA verifier
checks the same 846 traces. The timing-table builder independently replays all
423 successful suffixes, totaling 90,522 frames.

CPU and CUDA adapter tests exercise automatic advancement, retention, failed
windows, fixed evaluation, checkpoint persistence and corruption guards. The
curriculum CUDA test covers 40,960 decisions, bitwise states, observations and
mastery counters, and direct launches plus CUDA graphs. Two scripted controls
traverse all 17 levels. The previous fixed-pipe adapter checks remain in `test`.

Semantic tests cover viewport borders and scrolling, parser residency, HUD
independence, projectile pools, firebars, motion history and recorded ROM states.
Encoder tests compare CPU/CUDA FP32 and BF16 outputs, cover empty and full
object sets, check object-order invariance, numerically check gradients in all
seven layers, and exercise CUDA graph replay and recurrent checkpoint inference.
The numerical gradient checks run in FP32.

The [semantic validation report](results/semantic_encoder_20261008/summary.json)
records these checks and 1,195,952 decoded ROM frames. All 136 evaluated policy
episodes matched the simulator and ROM, including observations, recurrent
inference, actions and terminal results.

On the local GTX 1060 3GB, matched 1,048,576-step runs with learning rate zero
measured approximately 83,000 steps/sec for the semantic model and 113,000 for
the RAM model, using the 4,096-agent BF16 configuration. These are
end-to-end training rates at the closest start. GPU memory use, including the
display, was 1.77 GiB versus 1.87 GiB.

A separate 4,194,304-step learning check raised frozen-policy FPG success at
the exact three-frame start from 5/64 to 62/64 episodes, using the same sampling
seed. It took 52.8 seconds and advanced 1,107 of 4,096 actor frontiers to the
four-frame band. This verifies early learning; it does not establish progress
past the previous 96-frame plateau. The test checkpoint and its curriculum
sidecars are at
`checkpoints/mario_fpg_time/encoder_semantic_v1_learning_20261008/0000000004194304.bin`.

## Simulator-to-ROM comparison

For one exact recorded start depth:

```sh
build/mario_fpg_time/sim2rom \
  path/to/semantic_checkpoint.bin \
  config/mario_fpg_time.ini build/mario_fpg_time/transfer_check \
  128 137 0 build/mario_fpg_time/curriculum 16
```

The trailing arguments are episode count, seed, deterministic argmax (`1`) or
stochastic sampling (`0`), bank directory, exact reference depth, and optional
objective (`fpg`, the default, or `clear`). Omitting the bank and depth evaluates
the original 102 pipe-exit starts. These have their own saved ROM states and
remain available for a comparison across multiple routes and pipe-exit states.

Use `new-game` in place of the bank directory, with reference depth `0`, to boot
an unmodified new game through the title screen and begin at the first playable
frame of 1-1. The `clear` objective keeps running after flag contact until the
ROM advances its stage/world, or the player dies or reaches `env.max_frames`.
For full-level evaluation, use an evaluation INI with a suitable frame cap
(the October 8 evaluation uses 6,000). Every episode resets policy memory and
object history. `trajectories.csv` samples position, motion and actions every
16 frames; episode reports distinguish flag contact, FPG and level transition.

Each engine receives its corresponding initial state once and then runs
independently. Separate recurrent policy states and action RNGs are compared
along with every observation, logit, action, RAM byte, CPU register, frame clock
and terminal outcome. Every requested episode finishes or times out; mismatches
fail the command. Reports contain counts, completion checks and fingerprints.
Both sides use the repository's FP32 CPU inference implementation; this does
not assert identical sampling to GPU BF16 inference.

The [268M-step semantic run evaluation](results/eval_1791497853012_20261008/summary.json)
tests checkpoint `1791497853012/0000000268435456.bin`. With sampled actions and
a 6,000-frame cap, it cleared 0/64 new-game 1-1 attempts (14 deaths, 50 timeouts)
and 46/64 attempts from the canonical pipe exit (18 deaths). All pipe-exit
finishes were ordinary clears. Greedy actions timed out from both starts.
Exact 64- and 72-frame curriculum starts achieved 58/64 FPGs each; 80-, 88-
and 96-frame starts achieved 0/64 each. All 522 evaluation episodes matched
between simulator and ROM. The actor frontiers remained at the 96-frame band.

## Reward and timing estimates

The current configuration gives +1 for an actual FPG plus a speed bonus of
up to 0.1, and zero for all other outcomes. With estimate `E` and actual frames `T`:

```text
target = max(1, time_target_scale * E + time_slack_frames)
speed_score = min(1, target / T) ^ time_power
reward = 1 + time_bonus * speed_score     on an actual FPG
```

The config sets bonus 0.1, target scale 1, slack 8 and power 1. There is no progress
shaping or imitation loss. Recorded actions build and validate the start bank;
they are never supplied to the learner. A finish within its target earns 1.1;
one taking twice the target earns 1.05. `--env.time_bonus=0` restores the sparse
+1 comparison. Reward clipping remains disabled so the bonus reaches training.

Every curriculum state has a recorded successful time. These are feasible
continuations, not optimality claims. The small timing table is required for
curriculum ordering and must retain the recorded suffix lengths; the builder
rejects offline search on this bank. Lookup and curriculum updates stay on the
GPU during CUDA training. The shared PPO trainer is unchanged.

The [fixed pipe experiment](results/pipe_20261003/summary.json) and its
[config](results/pipe_20261003/config.ini) are preserved. To use that bank with
the current binary, set `curriculum=0`, select
`build/mario_fpg_time/pipe/bank.bin`, and select
`build/mario_fpg_time/pipe_targets.bin`. Its original terrain and 102 grounded
starts 502–583 pixels from the flag are unchanged. The earlier
[near-start experiment](results/near_starts_20261003/config.ini) is also archived.
