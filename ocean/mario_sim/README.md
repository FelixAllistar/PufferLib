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

The default loads the completed semantic checkpoint at
`checkpoints/mario_fpg_time/1791505432976/0000000268435456.bin`. Historical
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
CUDA_HOME=/usr/local/cuda ./build.sh mario_sim build/mario_sim/puffer --cu --float

# Default mixed training; use --base.load_model_path=None for fresh weights.
build/mario_sim/puffer train
# Full games from 1-1, or FPG only:
build/mario_sim/puffer train --env.mode=game --game.fixed_stage=0
build/mario_sim/puffer train --env.mode=fpg
# View the shared policy in either mode:
build/mario_sim/puffer eval path/to/semantic_checkpoint.bin --env.mode=game
build/mario_sim/puffer eval path/to/semantic_checkpoint.bin --env.mode=fpg
```

The default uses 1,024 actors, a 128-frame rollout, gamma 0.9999, and the existing
64-wide, two-layer policy. These are starting settings for the mixed task; the
previous FPG training results do not establish full-game competence. The viewer
renders semantic geometry; `retro` provides the ROM pixel display.

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
original provenance. The [FPG sweep](sweep.py) explicitly selects `fpg` mode and
scores a fixed ROM panel. A sweep over the mixed task needs a full-game scoring
panel before its results can rank full-game performance.
