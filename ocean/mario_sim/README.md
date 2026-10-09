# Mario simulation

There are two Mario environments:

- [`retro`](../retro/README.md): ROM execution with pixels and its pixel encoder.
- `mario_sim`: the translated ROM engine on CPU/CUDA, with one semantic encoder
  shared by full-game play and flagpole-glitch (FPG) practice.

[`config/mario_sim.ini`](../../config/mario_sim.ini) owns all simulator settings.
`env.mode` selects `game`, `fpg`, or `mixed`. The default is `mixed`: 75% of
simulation frames go to full-game actors, 25% to FPG actors. Pools are fixed for
an actor's lifetime; the requested fraction is rounded to a whole actor count.
Short FPG episodes therefore cannot crowd out full-game play.

## Full games

`[game]` controls the natural game episodes. By default, each reset samples one
of the 32 original stages uniformly, then starts at that stage's natural entry.
Use `game.fixed_stage=0` to always begin at 1-1, or `0..31` for another stage.
The game continues through flag sequences, pipes, world transitions and life
losses. Game over, the final 8-4 victory, or `game.max_frames` ends an episode.
`game.terminate_on_clear=1` optionally makes it a single-clear task.

Each distinct cleared stage pays `clear_reward` (default 1), plus
`time_bonus * min(1, time_target_frames / frames_in_stage)` (default bonus 0.1,
target 1,800 frames). A clear is recognized at first flag contact, castle victory,
or a forward stage transition. Repeated frames in the finish sequence do not
pay again. The optional life-loss penalty defaults to zero. Death and completion
animations advance through normal game logic.

Full-game actors also receive `game.checkpoint_reward` (default **0.025**) for
every `game.checkpoint_distance` (default **128 pixels**, eight tiles) of new
rightward progress. Each level/loaded area keeps its furthest reached position
for the whole episode, including all lives. Backtracking and retracing ground
after a death cannot collect the same reward again. Area entries and pipe exits
establish position baselines without rewarding teleport distance. Only ordinary
playable movement counts; finish animations do not. As in `retro`, each area
has a 3,400-pixel frontier cap. Set the reward to zero for
the sparse-reward comparison. This reward does not apply to FPG actors.

`game_progress_pixels`, `game_checkpoints`, and `game_checkpoint_reward` report
averages over completed game episodes. Reward/config changes take effect when
starting a new training process; existing semantic checkpoints remain compatible.

The active game distribution uses the original world data and natural reset
states, with no synthetic RAM edits. This includes ground, underground, water
and castle stages. The existing offline generator can vary enemy types and item
contents; arbitrary new terrain geometry is not implemented. Those experimental
variants are outside this training distribution.

## FPG practice

`[fpg]` controls the practice fraction, natural reset bank, backward curriculum,
mastery windows, replay, time bonus, and saved progress. In `fpg` mode every actor
practices FPG regardless of `fpg.fraction`. See [FPG.md](FPG.md) for the curriculum
and fixed ROM evaluation protocol. Its successful continuations and 423 natural
starts are preserved.

The semantic observation contract is unchanged from the trained FPG policy:
1,461 values covering the full playfield, player state, and entities. Both modes
use the same terrain CNN, player/entity branches, 64 controller masks, and one
video frame per action. No mode bit or new weight tensor was added. Existing
semantic FPG weights and curriculum sidecars can warm-start the unified policy;
the older raw-RAM and pixel policies have different architectures.

The default starts fresh (`base.load_model_path=None`, `fpg.curriculum_resume=0`).
To warm-start, pass `--base.load_model_path=path/to/checkpoint.bin`; also set
`--fpg.curriculum_resume=1` to restore its saved practice frontiers. Historical
checkpoint paths remain valid. New runs save under `checkpoints/mario_sim` and
`logs/mario_sim`. Curriculum sidecars store only the FPG actors in mixed mode.
Game metrics (`game_clears`, `game_wins`, `game_deaths`, `game_timeouts`) use game
episodes as their denominator; FPG metrics use FPG episodes.

## Build and run

Commands below run from the repository root. Supply the local World/NTSC ROM at
`ocean/retro/roms/smb1_ntsc.nes` and the annotated disassembly at
`build/mario_sim/reference/smbdis_complete.asm`. ROM-derived build assets stay
local and ignored by Git. Existing local assets have been migrated.

```sh
make -f ocean/mario_sim/runtime.mk -j2 all bank-builder
build/mario_sim/runtime/build_bank build/mario_sim/runtime/generated 16 73
make -f ocean/mario_sim/fpg.mk curriculum
CUDA_HOME=/usr/local/cuda ./build.sh mario_sim

# Default mixed training, fresh weights, CPU simulation and GPU policy:
./puffer train
# Full games from 1-1, or FPG only:
./puffer train --env.mode=game --game.fixed_stage=0
./puffer train --env.mode=fpg
# View the shared policy in either mode:
./puffer eval path/to/semantic_checkpoint.bin --env.mode=game
./puffer eval path/to/semantic_checkpoint.bin --env.mode=fpg

# Optional CUDA simulation backend; it requires exactly one buffer:
CUDA_HOME=/usr/local/cuda ./build.sh mario_sim build/mario_sim/puffer --cu --float
build/mario_sim/puffer train --vec.num_buffers=1
```

The default uses 1,024 actors, two buffers, four CPU threads, a 128-frame rollout,
gamma 0.9999, and a 64-wide, two-layer policy. Async and CUDA graphs are enabled;
learning-rate and entropy annealing are disabled. These are starting settings for the mixed task; the
previous FPG training results do not establish full-game competence. The viewer
renders semantic geometry; `retro` provides the ROM pixel display.

## Fresh 30M-step sweep

The native PROTEIN search has 17 varying parameters, with the actor count,
buffers, threads, 75/25 game/FPG mixture, rewards, and curriculum rules fixed.
Every trial starts with fresh weights and a fresh three-frame FPG curriculum.
Both annealings stay off, including in the worker; reward clipping stays off.

```sh
# Inspect settings without starting trials:
python3 ocean/mario_sim/sweep.py --dry-run
# Start 128 trials using the current CPU-environment ./puffer:
./puffer sweep
# Same search, with all artifacts grouped under reports/mario_sim/sweep_TIMESTAMP:
python3 ocean/mario_sim/sweep.py
# Optional smaller search and result listing:
python3 ocean/mario_sim/sweep.py --runs 32
python3 ocean/mario_sim/sweep.py --summary reports/mario_sim/sweep_TIMESTAMP
```

| Parameter | Range |
| --- | --- |
| Hidden width / recurrent layers | 32, 64, 128 / 1–4 |
| Horizon / minibatch | 32–128 / 1,024–8,192, powers of two |
| Learning rate | 0.0001–1, logarithmic |
| Entropy coefficient | 0.00001–0.1, logarithmic |
| Gamma / GAE lambda | 0.8–0.9999 / 0.2–0.995, logit scale |
| Replay ratio | 0.25–4 |
| Policy clip / value clip | 0.1–0.9 / 0.01–5 |
| Value-loss coefficient / gradient norm limit | 0.1–4 / 0.1–1.5 |
| Momentum | 0.5–0.999, logit scale |
| V-trace / rho clip / c clip | Off or on / 1–4 / 0.1–1 |

These PPO/Muon ranges follow the common ranges used by the Breakout, Craftax,
and NMMO configs. Model and rollout sizes are bounded for the local 3GB card.
V-trace's two clips only matter when it is enabled. Every allowed shape has
compatible minibatches and at least one optimizer update at minimum replay.

Each trial requests **30,000,000 frames**. The trainer uses whole rollouts,
so actual budgets are 29,884,416–29,982,720 depending on horizon (under 0.4%
rounding). The receipt records both requested and completed frames. The default
search requests 3.84 billion frames in total, with one trial at a time. Final
checkpoints are retained; sweep workers disable intermediate checkpoints.

After training exits and releases GPU memory, the worker evaluates its frozen
checkpoint in the **actual NES ROM**, checking the simulator alongside it:

- One natural start at every original stage: 32 game episodes, seed 137,
  with normal lives and a fixed 6,000-frame cap.
- Sixteen episodes at each exact FPG start depth
  `3,8,16,32,64,80,96,128,192,256,320,425`, seed 137, cap 1,800 frames.

The fixed search score is:

```text
progress_fraction = mean(min(new_progress_pixels / 3400, 1))
clear_rate = fraction of game episodes clearing at least one stage
game_score = (progress_fraction + clear_rate) / 2
score = 0.75 * game_score + 0.25 * FPG_success_rate
```

The score remains useful before any full-stage clears. It does not depend on
training reward magnitude or reported curriculum advancement. Results retain
each component, episode traces, model/config fingerprints, and ROM comparison
checks. Missing episodes or simulator/ROM mismatches fail a trial. The ROM
evaluator uses FP32 CPU policy inference; training uses the current binary's
BF16 GPU policy. These are development starts from the original training
distribution; recheck selected candidates with more episodes and new seeds.

Search ranges live in [the environment config](../../config/mario_sim.ini).
[profiles/sweep.ini](profiles/sweep.ini) is a thin launcher overlay. The first
trial uses the baseline values, then PROTEIN proposes candidates. Individual
trial failures are retained in `failure.json`; 16 failures stop the sweep.
`max_suggestion_cost` guides proposals and is not a per-trial kill timer.
The [setup validation](results/sweep_setup_20261009/summary.json) records the
four bounded trial checks, including both memory extremes on the 3GB GPU.

## Verification and evidence

```sh
make -f ocean/mario_sim/fpg.mk game-test progress-test curriculum-test semantic-test
make -f ocean/mario_sim/runtime.mk environment
build/mario_sim/runtime/test_runtime
python3 -m unittest discover -s ocean/mario_sim -p test_sweep.py
# Frozen semantic policy against independent ROM execution from each stage:
build/mario_sim/fpg/sim2rom MODEL config/mario_sim.ini OUTPUT 32 137 0 all-stages 0 game
```

The [consolidation checks](results/consolidated_20261009/summary.json) record the
CPU/CUDA/ROM results and bounded training/resume/sweep smoke checks.

The game checks compare natural starts in every stage and continuing episodes
against the independent ROM, including semantic observations. The CUDA adapter
check covers all three modes, auto-resets, rewards, logs, observations, CUDA
Graphs and curriculum persistence. Earlier engine qualification and its limits
are documented in [RUNTIME_FIDELITY.md](RUNTIME_FIDELITY.md), [RESULTS.md](RESULTS.md)
and the [experiment audit](EXPERIMENT_AUDIT.md). This is not exhaustive coverage
of every possible full-game route.

The obsolete `mario_lab` and `mario_fpg` implementations and configs were removed.
`mario_fpg_time` was merged here. Historical measurements remain under `results/`,
including `legacy_lab/` and `legacy_fpg/`; logs and checkpoints retain their
original provenance. The [mixed sweep](sweep.py) scores both game performance
and FPG success on fixed ROM panels.
