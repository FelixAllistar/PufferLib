# Mario Lab procedural platformer

> **Fidelity reset — 2026-10-03:** Training is on hold. Prior synthetic results
> do not validate the encoder, reward, curriculum or training-budget conclusions.
> Raw results and ROM replays are preserved. See the
> [experiment audit](../mario_sim/EXPERIMENT_AUDIT.md) and expanded reconstruction work.

The active work is [full-game feasibility and fidelity](../mario_sim/README.md).

The FPG controller has a [bounded ROM/native qualification](../mario_fpg/FIDELITY.md).
Mario Lab's older movement/entity controller has not passed that gate. Its ROM
tile adapter now shares the corrected 32-column parser-residency decoder, but
that adapter fix alone does not qualify this simulator's physics.
The long-course generator below remains an optional ablation. The [continuing curriculum](CONTINUING_TRAINING.md) remains a historical proposal
whose learning rationale must be tested again after qualification.

Mario Lab is the first CUDA001 experiment in the [Mario experiment register](../retro/EXPERIMENTS.md).
It trains on original generated platformer courses using an original approximate
controller. Synthetic training has no ROM or asset dependency. A separate
reference adapter now evaluates frozen semantic policies in real SMB1 1-1;
see [transfer results and failures](TRANSFER_RESULTS.md). Full-game speed records
remain an evaluation goal.

## Historical training setup

These commands and configurations are retained for reproducibility. They remain
under the training hold above.

From the repository root, activate your environment and build the named native
trainer once:

```sh
source /home/felix/puffertank/venv/bin/activate
CUDA_HOME=/usr/local/cuda bash build.sh mario_lab build/mario_lab/puffer --cu --float
```

Edit [config/mario_lab.ini](../../config/mario_lab.ini), then train directly:

```sh
./build/mario_lab/puffer train
```

The native trainer reads `config/default.ini` plus `config/mario_lab.ini`.
No Python launcher or flags are required. Checkpoints go under
`checkpoints/mario_lab/RUN_ID/`; the native log includes the resolved training
INI under `logs/mario_lab/`. `base.load_model_path = None` starts fresh;
set it to a checkpoint path to continue matching weights with a fresh optimizer.
The default network has hidden size 128 and two MinGRU layers. The earlier
transfer checkpoint uses size 64, so select 64 if loading that checkpoint.

The current config is the full-level experiment:

| Setting | Value |
| --- | --- |
| Geometry | Complete episodes, random 256–512-tile surface length |
| Generation | Random 40–96-tile sections with varied motif spacing, sizes and combinations |
| Underground | Optional randomized entrance/return pair and 48–128-tile bonus room |
| Controller | Measured small-player profile, mode 2 |
| Resets | Root starts only; practice disabled |
| Episode cap | 8,000 game frames |
| Policy | Semantic MLP → two MinGRU layers, hidden size 128 |
| Parallel environments | 256, one buffer |
| Rollout horizon | 256 frames, with recurrent memory carried across rollouts |
| Discount / GAE | 0.9995 / 0.99 |
| Training budget | 500 million frames across all environments |

`length` is the maximum surface width; `length_min=0` requests a fixed width.
`section_min/max` and `spacing_min/max` change layout variation. With `course=0`,
the seven `mix_*` weights select styles throughout one continuous level. They do
not schedule separate training stages. Explicit course IDs still permit family
comparisons. Increasing the PPO horizon extends each gradient sequence; it does
not require the level to finish within a rollout.

This config is a new experiment, not the configuration behind the earlier 7/64
real 1-1 result. It has no demonstrated transfer score yet. Enemy models and
mechanics remain approximate; length and randomization alone do not fix those.
See [full-level qualification and measurements](FULL_LEVEL_RESULTS.md).

To watch or evaluate a native checkpoint:

```sh
./build/mario_lab/puffer eval latest
./build/mario_lab/puffer eval latest --headless --base.eval_episodes=256
```

The native evaluation is a vector smoke/score check and may finish with other
episodes outstanding. Use the complete-episode CPU evaluator for an exact panel:

```sh
./build/mario_lab/eval_policy CHECKPOINT.bin --base.eval_episodes=64 --env.split=1
```

Build evaluation/manual tools as needed:

```sh
make -C ocean/mario_lab test sanitize gpu-test viewer policy-eval
./build/mario_lab/viewer play 73 0
```

Manual controls are arrows, X/Space for jump, Z/Shift for run. The `script` and
`trace` viewer modes use a diagnostic controller, not an expert teacher.

## Preserved short-course experiments

The native full-level config is the normal training entry point. Historical short
experiments remain in `logs/mario_lab/experiments/`. Portable configs live in
[presets/short_transfer.ini](presets/short_transfer.ini) and
[presets/legacy_baseline.ini](presets/legacy_baseline.ini). Copy a preset to
`config/mario_lab.ini` when deliberately running that comparison; restore
[presets/full_levels.ini](presets/full_levels.ini) for the full-level experiment.
Each preset starts fresh unless you edit `base.load_model_path`.

`run.py`, `curriculum.py`, `evaluate.py` and `transfer.py` remain optional archival
helpers for the earlier bounded experiments. The short-course training harness
uses `presets/legacy_baseline.ini`, preserving its prior defaults independently
of the native full-level config. It snapshots inputs/executables and accepts
`--parent`, numeric `--set` overrides and a measured `--profile`. Synthetic family
panels and ROM bundles retain complete replay tapes and hashes. See the saved
[transfer results](TRANSFER_RESULTS.md) for their exact protocols.

## Practice starts and adaptive sampling

`practice_prob` mixes authored practice starts with ordinary full-course starts.
Four independent weights select gap takeoff/landing, pipe entry, underground
exit, and the complete pipe route: `practice_gap`, `practice_entry`,
`practice_exit`, and `practice_route`. These starts generate gap or underground
geometry independently of the full-course `course` setting. When `course=0`,
the seven `mix_flat` through `mix_composite` weights select full-course families.
The default uses equal weights and disables practice.

Gap starts vary approach distance, tile/subpixel position, speed and previous
jump-button state. Entry/route starts vary between approaches and the pipe top;
exit starts begin near the underground exit. These are authored synthetic states,
not TAS snapshots or trajectories verified reachable from a root start. Episode
clocks, frontier rewards and policy memory start fresh.

`practice_short_goals=1` gives the selected practice task its own endpoint. Gaps
require a grounded landing beyond the chosen pit; pipe tasks require the relevant
transfer to finish. `practice_short_goals=0` keeps the original full-course goal
while changing the starting state. `practice_frames` sets the practice deadline
independently of `max_frames`. For example, a reset-only gap comparison is:

```sh
python3 ocean/mario_lab/run.py --steps 10485760 --agents 128 --hidden 64 \
  --course 2 --difficulty 0 --length 48 \
  --set env.practice_prob=0.8 --set env.practice_gap=1 \
  --set env.practice_entry=0 --set env.practice_exit=0 --set env.practice_route=0 \
  --set env.practice_short_goals=0 --set env.practice_frames=2400
```

Training `perf` pools completed full courses and practice tasks. Separate
`root_clear_rate`, `gap_clear_rate`, `entry_clear_rate`, `exit_clear_rate`,
`route_clear_rate` and `practice_fraction` metrics expose that mixture. Practice
probability is per reset, so short tasks can account for a larger fraction of
completed episodes than simulated frames. A type with no completed episodes has
a zero rate. Both evaluation paths explicitly
disable practice, including evaluations of older run configs. Use
`evaluate.py --length 64 --difficulty 1 --max-frames 2400` to compare policies
trained with different course lengths or practice deadlines.

The bounded coordinator evaluates all seven families before each training round:

```sh
python3 ocean/mario_lab/curriculum.py logs/mario_lab/experiments/RUN_DIRECTORY \
  --rounds 2 --steps 10485760 --episodes 32 --practice-prob 0.35
```

It increases full-course weights for failing families while retaining a positive
weight for every family. Gap and underground failures also choose the four
practice weights. Every round saves its parent, settings, assessment and candidate
run; all candidates remain available. Selection maximizes the weakest family
clear rate, then the mean family clear rate. This uses validation split 1 for
development, not the final test stream. Rounds are bounded; `--rounds 0` evaluates
only. Continuations resume weights with a fresh optimizer. Selection currently
uses reliability; clear times are recorded separately.

## Current simulation

One decision advances one native simulation frame. Coordinates and velocities
use integers with 256 units per pixel and 16-pixel tiles. `x` is the collision
box's left edge; `y` is its feet. The legacy player is 12 by 16 pixels. Horizontal
acceleration, braking, walk/run limits, jump impulse, held/released gravity,
maximum fall speed and hold duration are explicit configuration values.

The controller supports left/right, run, jump edge/release, walls, ceilings,
ground, pits, simple walking enemies, stomps and bounce. Holding jump through a
landing does not create a fresh jump. Default coefficients are hypotheses for
calibration, not values certified against the Mario ROM.

The current optional `physics_mode=2` profile uses measurements from natural emulator
starts: slower acceleration and a 2.5px/frame run limit, a 10-by-12 collision box,
standing/running jump settings, gravity after movement, and a jump-release latch.
Re-pressing jump during ascent does not restore low gravity. Horizontal speed is
quantized symmetrically to sixteenths of a pixel. This is an original approximate
controller, not an exact mechanics port. Flat-ground trajectory agreement does
not establish collision, enemy, pipe or powerup equivalence.
Mode 2 also brakes grounded movement while Down is held, and cancels simultaneous
Left+Right before either backend applies the input. Mode 1 retains the earlier
measured controller without these button corrections. Mode 0 remains available in the legacy
preset; the native training config selects mode 2. This makes controller comparisons explicit in saved configs.

Course IDs are 0 mixed sampling, 1 flat, 2 gaps, 3 pipe obstacles, 4 stairs,
5 walkers, 6 an underground route, and 7 composite. `generator_mode=0` preserves
short seeded templates, with widths of 48–192 tiles. `generator_mode=1` supports
48–512-tile complete levels, random length ranges, changing sections, density
variation, obstacle sizes, overhead blocks and up to 64 authored walkers across
the world. Only the nearest eight visible walkers enter the full generator's
unchanged observation contract. In fixed-family evaluations, section styles stay
within that family. Difficulty 0 through 2 changes gap/obstacle/enemy parameters;
the full generator also samples section difficulty up to that maximum.
All geometry is authored/generated here. No original levels have been copied
into the engine.

An enterable surface pipe leads down into a separate underground room: 40 tiles
in the old generator, or 48–128 with varied overhead geometry in the full generator.
The full generator also varies entry/return positions and heights.
Down while centered on the entrance activates it. Walking right into the
underground exit returns to a later surface pipe. Each transfer freezes movement
for 24 subsequent frames; the transition is visible to the policy. Course 6
requires returning from underground before completion. Composite courses permit the pipe route as
an optional shortcut. Pipe geometry exists independently of palette/theme.

Water, moving platforms, powerups, shells, firebars, bosses, original enemy
activation/camera behavior, original flag/castle timing and glitches are not yet
implemented. There is no claim of full-game equivalence. The initial generator
has structural bounds and deterministic seeds; exhaustive course solvability
is still an open validation task.

## Observation and action contracts

`ML_VERSION=5`, 1,128 float observations. The input size and action head remain
compatible with earlier policy weights; snapshot structs are version-specific.

| Range | Meaning |
| --- | --- |
| 0 through 23 | Velocity; position within tile and pixel; grounded/rising/falling; facing; previous jump/run; jump phase; room mode; pipe transition and availability; task condition; task time remaining; controller parameters; small-player form |
| 24 through 87 | Eight relative walker records: active, dx, dy, vx, vy, type, width, height |
| 88 through 1127 | 16 by 13 egocentric grid, five channels per cell |

The grid includes four columns behind the player and eleven ahead. Its horizontal
anchor stays six pixels right of the collision box's left edge across profiles;
its vertical anchor follows the feet, with eight rows above them.
Channels are solid collision, overhead brick, usable pipe opening, unsupported
space, and current task goal. A short pipe-entry goal marks the entrance.
Unsupported space is empty geometry with no solid below it;
its signal is independent of the displayed sky/background/pit color.

There is no absolute world x, level ID, course ID, seed or palette input. Goal
and pipe cues appear only within the grid. Entity positions are relative, and
entities farther than twelve tiles are hidden. Room and task flags are explicit.
Grid coordinates retain screen directions; they do not flip when facing left.

Actions are all 64 combinations of `A=1`, `B=2`, `Up=4`, `Down=8`, `Left=16`,
`Right=32`. Up is reserved. Both directions cancel horizontal input. This is
the six-bit policy layout used by retro; converting to NES controller bytes
still requires retro's controller-mask mapping. Mode 2 projects opposing
directions to neutral in both the synthetic core and ROM evaluator. ROM traces
save both the raw policy choice and the applied action; replays use applied inputs.

## Reward termination and snapshots

The default pays 10 on completion and up to 2 more for early completion,
minus 1 on death or timeout. Optional frontier shaping pays 0.001 per new
forward pixel. Each room retains its maximum frontier for the episode. Pipe
teleports establish a new baseline and do not pay distance. This shaping is
not potential-based and is explicitly ablatable with `progress_reward=0`.

Clears, deaths and external frame caps end tasks. The adapter returns the final
transition reward/terminal together with the next episode's observation. Logs
separate clears, deaths, timeouts, pipe visits, pipe returns and clear frames.
Recurrent state resets through PufferLib's ordinary terminal path. Frame caps
are finite-horizon task failures for this initial experiment.

`MLState` contains all game state, geometry, RNG, button history, timers and
frontier accounting, with no pointers. Copying it clones a synthetic situation.
A persistent snapshot must additionally bind the controller config, generator
version and serialization format. A portable disk snapshot bank is planned;
raw structs must not be treated as a cross-version or NES snapshot format.

## Validation and results

CPU tests check movement and collisions, pipe transitions, task completion,
enemy contact, palette invariance, horizontal translation invariance, exact
snapshot replay and 800 legacy seeded layouts. Another 400 full-level layouts
check variable widths, section/motif coverage, optional pipes, geometry beyond
the old storage limit, enemy records from the extended pool and long completion. CUDA tests compare states, rewards,
terminals, every observation and episode logs against CPU across resets,
non-multiple batch sizes and CUDA graph execution. Practice tests cover 400
seeded starts, grounded gap landings, transfer timing and both goal modes.

The benchmark includes a scripted input controller, simulation, observations
and automatic resets. It emits JSON with GPU, batch, elapsed wall time and
throughput. It excludes PPO and policy inference. See run artifacts for measured
results in [RESULTS.md](RESULTS.md) and [CURRICULUM_RESULTS.md](CURRICULUM_RESULTS.md),
including the limitations of the initial comparisons.

## ROM measurement and transfer

Provide the locally owned, verified ROM described in [retro's setup](../retro/README.md).
The training engine does not read that ROM. Build and run reference measurements
or a frozen synthetic checkpoint evaluation from the repository root:

```sh
make -C ocean/mario_lab rom-bridge sim-probe
python3 ocean/mario_lab/transfer.py calibrate --label controller-comparison
python3 ocean/mario_lab/transfer.py evaluate --run logs/mario_lab/experiments/RUN_DIRECTORY \
  --episodes 32 --traces 4 --label real-1-1
```

The bridge boots to a natural playable 1-1 start without RAM writes. A live
block-buffer ring, player kinematics and entity RAM feed the same egocentric
semantic observation contract. It uses one native frame per policy decision,
empty recurrent memory and recorded action RNG seeds. Attempts include zero
through 31 initial idle frames; those frames count toward completion time.
The endpoint is first death, the first 1-1 black-screen RTA split, or the frame
cap. Every complete action tape is replayed and compared with the full serialized
NES state. ROM execution here measures and evaluates; it never updates weights.

Bundles under `logs/mario_lab/transfers/` save the checkpoint/config, executable,
source hashes, ROM identity, outcomes, action tapes, sampled frames and detailed
traces for the first requested attempts. Those traces include the exact 1,128-float
pre-action observations and button probabilities. Open `trace_viewer.html` for an
offline frame slider over the policy input and gameplay samples. To regenerate it:

```sh
python3 ocean/mario_lab/inspect_transfer.py logs/mario_lab/transfers/BUNDLE_DIRECTORY
```

Reference samples and ROM-derived frames stay in ignored local artifacts.
Large-player form, unsupported enemies and missing ring cells are diagnostics,
not established simulator support. Transfer scores on repeatedly inspected 1-1
are development evidence, not an untouched generalization test.
