# Mario simulation

There are two Mario environments:

- [`retro`](../retro/README.md): ROM execution with pixels and its pixel encoder.
- `mario_sim`: the translated ROM engine on CPU/CUDA, with one semantic encoder
  shared by full-game play and flagpole-glitch (FPG) practice.

[`config/mario_sim.ini`](../../config/mario_sim.ini) owns all simulator settings.
`env.mode` selects `game`, `fpg`, or `mixed`. The default is `mixed`: 50% of
simulation frames go to ordinary full-game actors, 25% to encounter practice,
and 25% to FPG actors. Pools are fixed for
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
target 1,800 frames). A clear requires castle victory or a forward stage
transition. Flag contact is logged separately and does not pay the completion
reward. `game.reward_on_exit=0` restores the historical flag-contact control.
Repeated frames in the finish sequence do not
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
states. Ordinary game starts have no synthetic RAM edits; the practice pool's
route states in `practice_v3` do carry one (small Mario, see "Curriculum
teacher" below). This includes ground, underground, water
and castle stages. The existing offline generator can vary enemy types and item
contents; arbitrary new terrain geometry is not implemented. Those experimental
variants are outside this training distribution.

## Encounter practice

`game.practice_fraction=0.25` assigns a separate fixed actor pool to natural
encounters in difficult levels. With `fpg.fraction=0.25`, frame shares are 50%
ordinary game, 25% encounter practice, and 25% FPG. The default practice share is
25%. Evaluation disables encounter practice and retains natural stage-entry starts.

The current bank, `build/mario_sim/practice/bank.bin`, contains 104 natural ROM
states from 13 levels: 1-1, 2-2, 4-3, 4-4, 5-4, 6-3, 6-4, 7-2, 7-4, and all
four world-8 stages. Each has four approach bands with two states per band.
Bands are ordered by observed progress, not by a verified remaining solution.
An actor moves one band earlier after at least six successes in eight frontier
attempts; two windows below two successes move it later again. Easier replay
defaults to 20%. An attempt ends on the first death, an actual level exit, or
`game.practice_max_frames` (6,000 by default). Resetting in a later area pays no
progress reward. `.practice` checkpoint sidecars preserve these counters and
verify the weights and bank fingerprints.

`practice_perf`, deaths/timeouts, promotions/demotions, and `practice_W_L_perf`
are separate from ordinary game and FPG metrics. Practice does not enter the
sweep score. The 4-4 bank combines entry phases and early-section states;
4-4 and 7-4 do not cover complete maze routes. No full-level solution
is claimed for the bank, and later rooms of 8-4 still need targeted coverage.

The bank builder reuses existing reference routes and bounded policy rollouts
in memory. It writes compact action tapes, selected ROM snapshots, and short
validation clips, with a 16 MiB output cap. The current complete output is about
5.4 MB. It does not require another full trace dump:

```sh
make -f ocean/mario_sim/fpg.mk build/mario_sim/fpg/build_practice_bank
build/mario_sim/fpg/build_practice_bank NEW_OUTPUT CONFIG MODEL [ANOTHER_MODEL]
build/mario_sim/runtime/verify_cuda NEW_OUTPUT/validation.bin
```

The builder checks 240 frames per selected state against the ROM on CPU.
The CUDA check validates the first 16 frames per state separately. Preserve
the generated bank and provenance together; building refuses an existing output.

### Curriculum teacher

With `game.practice_teacher=1` (the training config) the practice pool keeps its
fixed actor share, but the start state comes from a bandit teacher instead of the
per-actor band machine above. The math is shared with retro
(`ocean/retro/curriculum_teacher.h`); this environment only feeds it counters.

- **Groups.** Every practice state belongs to one group: its loaded area
  (`stage<<16 | area data pointer`, the same key as the game's progress
  frontiers) and section `x / practice_group_width` (256 px). A reset draws a
  group from the teacher's distribution, then a state uniformly inside it.
- **Signals.** Natural-play deaths: when an ordinary game actor loses a life,
  its last tracked area and x credit that section and `practice_upstream_groups`
  sections before it with a linear kernel, decaying with
  `practice_death_half_life` agent steps and scaled by `4p(1-p)` of the group's
  practice success rate. Learning progress: `|fast - slow|` EMAs of practice
  attempt outcomes (`practice_lp_fast`, `practice_lp_slow`, after
  `practice_lp_min_attempts`). Staleness: steps since the group was last drawn.
  Each signal is ranked, mixed with `practice_weight_deaths/lp/stale`, and mixed
  with a uniform `practice_floor`.
- **Attempts.** A practice attempt wins on a level exit or
  `practice_success_distance` px of progress in its starting area; it loses on
  death, timeout or `practice_attempt_frames`. Episodes still end at the first
  death or exit, as before. FPG and practice actors never credit deaths.
- **Signed x.** x is read as a signed 16-bit value (`smb_game_x`): the ROM's
  loop-back does `Player_PageLoc -= 4`, which wraps below page 4, so the
  unsigned reading jumped to 64,512+ for a few frames. The progress reward x is
  now `clamp(smb_game_x, 0, 3400)`. This is a deliberate fix of legacy
  behaviour: before it, those wrapped frames counted as x 3400 and pinned the
  area's reward frontier at the cap (retro already treated the wrapped
  position as 0). It also stops a wrapped frame from winning a practice
  attempt by distance before the loop is seen.
- **Mazes and pipes.** Game and practice actors also track `game.raw_x`
  (signed, uncapped x) and `game.maze_loops`: a drop of at least 900 px between
  two consecutive tracked frames in one area, the ROM's 4-4/7-4/8-4 castle
  loop-back (always exactly 1,024 px with signed x). When an area is regained
  after an untracked gap (pipe, vine, transition) in the same stage with no
  life lost in between, the move is a forward transition if
  `(previous area, new area)` is a pair of the route table, and a wrong exit if
  it jumps back 256+ px in the same area or goes from one route area to another
  off the table. Leaving or entering an area the route never visits produces
  neither. For an ordinary game actor a loop-back or wrong exit is a failure
  like a lost life: it credits the last tracked area and section with the same
  kernel (`teacher_loop_events`, `teacher_wrong_exit_events`; deaths, loops and
  wrong exits together are `teacher_death_events`). Game rewards and episodes
  do not change. A practice attempt also wins on a forward transition and
  loses on a loop-back or wrong exit; with `game.practice_loop_terminal=1`
  (the training config) that also ends the practice episode as a death. Correct
  8-4 pipes stay in one area pointer and jump forward in x, so they win by
  distance. Deaths are credited at the uncapped x (no 3,400 px limit).
- **On-route loops.** The first 8-4 pipe can only be entered after the
  loop-back at x 1282 (the pipe is still on screen after the throw-back), so
  the reference route itself loops once. The bank builder flags the route
  state just before each movie loop (`SMB_BANK_ROUTE_LOOP`; v3 has exactly one,
  8-4 `a3fc` x 1217) and the host turns them into a loop table. The first
  loop-back within 512 px after a flagged x is passed: not a loss, not
  terminal, not credited, counted in `teacher_route_loops_passed`. A second
  one before tracking is lost (a pipe or death) is an ordinary loop failure.
  After a passed loop, failures less than 256 px past the landing point are
  credited 1,024 px further on, at the real place in the level. 4-4 and 7-4
  loops are never passed.
- **Route table.** Built at start-up from the practice bank's route entries
  (flag `SMB_BANK_ROUTE`): sorted by stage and movie frame, consecutive entries
  in different areas of one stage give a pair. v3 has six: 1-2, 2-2, 4-2 and
  7-2 into their exit areas, and 8-4 `a3fc -> aec2 -> a3fc`. 1-1's coin room
  has no route state, so 1-1 has no pair. `teacher_route_pairs` logs the count;
  `game_maze_loops` is the mean per completed game episode.
- **Refresh.** Devices only sample from a float CDF and add integer counters.
  Every `practice_refresh_steps` steps (per actor) the host folds the counter
  deltas into the groups, re-ranks them and uploads the new CDF. On CUDA the
  refresh is stream-ordered (counter copy, host callback, CDF copy), so it also
  runs inside the trainer's captured rollout graph; there it happens once per
  `practice_refresh_steps` rounded up to whole rollouts. The CPU path refreshes
  after each stepped buffer.
- **Bank.** `build/mario_sim/practice_v2` has up to 48 states per stage (308
  states, 79 groups), built with `--per-stage 48` from the same reference routes
  and two policies as the original bank. Stages with few distinct approach
  states keep fewer (4-4 has four). Without `--per-stage` the builder writes the
  original two-per-band bank byte for byte.
- **v3 bank (training config).** `build/mario_sim/practice_v3` is v2 plus
  `--route MOVIE.fm2`: the warpless TAS replayed from power-on (offset -2) adds
  the first tracked, standing state of every (area, 64 px) bin in all 32 stages
  (at most four per 256 px section): 1,170 route states, 1,478 in all, 400
  groups. Per stage: 1-1 16, 1-2 34, 1-3 23, 1-4 26, 2-1 38, 2-2 49, 2-3 37,
  2-4 30, 3-1 36, 3-2 46, 3-3 24, 3-4 30, 4-1 47, 4-2 41, 4-3 27, 4-4 41,
  5-1 31, 5-2 37, 5-3 26, 5-4 29, 6-1 35, 6-2 38, 6-3 29, 6-4 26, 7-1 35,
  7-2 50, 7-3 42, 7-4 46, 8-1 71, 8-2 34, 8-3 41, 8-4 55. The movie's Mario is
  big or fire from the middle of 1-3 on, while real arrivals are small, so 1,112 of them are
  snapshotted as small Mario (`$0754=1`, `$0756=0`, flag `SMB_BANK_EDITED`;
  the replay itself continues unedited). Each route state passes the same
  native-vs-ROM checks as the others (0 dropped); `verify_cuda` passed all
  1,478 clips. The output is 61 MB (cap 64 MiB with `--route`). Route
  candidates between a movie loop-back and the next untracked frame (thrown-back
  coordinates) are dropped; v3 has none. Without
  `--route` the builder's output is unchanged byte for byte.
- **Checkpoints.** A teacher run writes `checkpoint.teacher` instead of
  `.practice`: the group statistics, verified against the practice bank and the
  weights like `.practice`. Resume matches groups by key and restarts the
  step-based stamps. `practice_teacher=0` restores the band machine and its
  sidecar unchanged.
- **Metrics.** `teacher_groups`, `teacher_entropy`, `teacher_attempts`,
  `teacher_attempt_win_rate`, `teacher_death_events`, `teacher_mean_progress`,
  `teacher_top_stage` (0-31), `teacher_top_section`, `teacher_top_prob` and
  `teacher_W_L_share` (probability mass per practice stage). The sweep score
  is unchanged.

```sh
build/mario_sim/fpg/build_practice_bank NEW_OUTPUT --per-stage 48 CONFIG MODEL [ANOTHER_MODEL]
make -f ocean/mario_sim/fpg.mk teacher-test     # also part of game-test
```

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

The default starts fresh (`base.load_model_path=None`).
To warm-start, pass `--base.load_model_path=path/to/checkpoint.bin`; also set
`--fpg.curriculum_resume=1` to restore its saved practice frontiers. Historical
checkpoint paths remain valid. New runs save under `checkpoints/mario_sim` and
`logs/mario_sim`. Curriculum sidecars store only the FPG actors in mixed mode.
Game metrics (`game_clears`, `game_wins`, `game_deaths`, `game_timeouts`) use game
episodes as their denominator; FPG metrics use FPG episodes.

## Build and run

Commands below run from the repository root. The World/NTSC ROM at
`ocean/retro/roms/smb1_ntsc.nes` and the annotated disassembly at
`build/mario_sim/reference/smbdis_complete.asm` are committed, along with the
game and FPG reset banks and curriculum timing table (about 3.5 MiB total).
The timing table fingerprints engine sources so it works across compiler and GPU
changes while still rejecting changes to the engine or reset bank.
The default build compiles the CPU simulator and GPU policy; the CUDA simulator
is compiled only with `--cu`. Optional CUDA simulator builds target the installed
GPU by default; set `CUDA_ARCH=sm_XX` to target another GPU explicitly.

```sh
# Fresh checkout with the committed reset banks:
CUDA_HOME=/usr/local/cuda ./build.sh mario_sim

# Optional: regenerate the reset banks and timing table:
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

# Optional CUDA simulation backend, using the same executable path:
CUDA_HOME=/usr/local/cuda ./build.sh mario_sim --cu
./puffer train --vec.num_buffers=1
```

The defaults use 4,096 actors, four buffers, twelve CPU threads, a 128-frame
rollout, gamma 0.998788059, and a 256-wide, two-layer policy.
Async and CUDA graphs are enabled;
learning-rate and entropy annealing are disabled. These are starting settings for the mixed task; the
previous FPG training results do not establish full-game competence. The viewer
renders semantic geometry; `retro` provides the ROM pixel display.

`config/mario_sim.ini` is the single settings file for normal training. It
preserves the optimizer and architecture from the 10B sweep's run 0 (the 6.44B
checkpoint cleared 475/1,024 individual ROM attempts), with actual-exit rewards,
retention and the 50/25/25 mixture. Normal training requests 10B frames with fresh weights.
Run `./puffer train` from the repository root. For a continuation, set
`base.load_model_path` in that same config or pass it as a CLI override;
`curriculum_resume=1` is already enabled. The optimizer and step counter restart.
The new simulator's training gains have not yet been measured.

Useful manual controls are `--game.practice_fraction=0` to omit encounter
practice, and `--game.reward_on_exit=0 --fpg.curriculum_retention=0` to restore
the old reward/curriculum behavior. Use distinct run IDs or output directories.
No comparison batch is launched automatically.

## Model, optimizer and step-budget sweep

Use the canonical trainer and [config/mario_sim.ini](../../config/mario_sim.ini):

```sh
CUDA_HOME=/usr/local/cuda ./build.sh mario_sim
./puffer sweep
```

Native PROTEIN proposes settings and saves its observations to the configured
`sweep.history_path`. The Mario [worker](sweep_worker.py) starts that same trainer,
monitors its speed/runtime, and returns a fixed ROM evaluation through the native
result pipe. It creates no alternate trainer or editable training profile. Each
trial starts with fresh weights and curricula. The simulator, rewards, and
50/25/25 game/practice/FPG mixture stay fixed.

The target is 500 completed trials, with one running at a time. Trial zero uses
the 4096-actor, 256-by-2 control with a 500M-step budget through
`sweep.trial_timesteps`; the normal training budget remains 10B. Later proposals search:

| Parameter | Range |
| --- | --- |
| Training frames | 200M–3B, logarithmic |
| Actors | 4096–32768, powers of two |
| Hidden width / recurrent layers | 32–256, powers of two / 1–3 |
| Horizon / minibatch | 16–256 / 1024–32768, powers of two |
| Learning rate / entropy | 1e-5–0.002 / 1e-6–0.03, logarithmic |
| LR decay / entropy decay | Each independently off or on |
| Final LR / entropy fraction | Each 0–0.5 of its starting coefficient |
| Gamma / GAE | 0.95–0.9999 / 0.2–0.999 |
| Replay ratio | 0.25–2 |
| Policy clip / value clip | 0.1–0.9 / 0.01–5 |
| Value coefficient / gradient norm | 0.1–4 / 0.1–1.5 |
| Momentum | 0.5–0.999 |
| V-trace / rho / c | Off or on / 1–4 / 0.1–1 |

Preflight rejects invalid minibatch layouts, excessive CUDA graph work, and
estimated VRAM above 14336 MiB. The estimate includes the fixed semantic encoder
and rollout observations: a small recurrent core alone does not make every
32K-actor horizon fit. The worker also checks measured resident memory and
rejects throughput below 150K steps/second over a 30-second window after warmup
(about half the measured 297K control throughput on the Vast instance).
Each trial has a four-hour training limit and a ten-minute startup timeout.
Evaluation has its own ten-minute overall limit. `max_suggestion_cost` guides
PROTEIN proposals; the worker enforces the actual runtime limit.

All trials use the same sampled-action seeds and frozen ROM panels:

- Four first-life attempts at every one of the 32 natural level starts, capped
  at 6000 frames each.
- Sixteen FPG attempts each at all 17 fixed curriculum depths from 3 through 425.
- A continuing, three-life game from every stage, capped at 108000 frames, and
  four fresh 1-1 attempts.

Score version 2 is `fixed_eval_score = 0.50 * all_level_clear_rate +
0.25 * bounded_game_progress + 0.25 * mean_FPG_success`. Progress is the mean
of `min(1, novel_pixels / 3400)` across the 32 continuing games. The existing
progress frontiers persist across lives and exclude revisits and teleport
distance. The 17 FPG bands have equal weight, so mastering easier starts gives
credit before a policy can solve depth 96. Both signals are bounded; completed
levels remain the largest component. This gives short trials useful feedback
before their first full clear. Exits, deaths, every per-level outcome, the
96/256/425 FPG rate, and the legacy clear-only score are also retained.
Simulator/ROM parity failures invalidate the test.
This score is a different objective from old training `sweep_score` values.
The small panels guide the search; finalists still need the full repeated ROM
protocol below and independent training seeds.

Long trials also receive the same fixed tests near 500M, 1B, and 2B frames.
Automatic learning pruning requires at least 20 prior tests at the same rung
and actor count (so larger batches get comparable per-actor curriculum exposure),
and a score more than 0.10 below that rung's bottom decile. Thus short trials
and learning speed do not alone trigger pruning. Stopped trials are saved and
reported at their actual trained budget. Decayed schedules still follow the
original requested budget, which should be considered when comparing a pruned
checkpoint to a short completed run.

The worker saves recovery weights about every 100M frames, retains the last two
and evaluated rung/final checkpoints, and removes its successful trajectory
CSVs. It leaves earlier experiments intact. `*.progress.csv` retains entropy,
KL, value loss, throughput, and memory for diagnosing regressions. Each
`*.eval/final` contains fixed-test summaries and `per_level.csv`.

Ctrl-C stops the sweep; the active worker requests a recovery save. Run the
same `./puffer sweep` command to restore completed GP observations and continue
toward 500. Wait for the worker to finish saving before restarting. History
requires identical parameter names/ranges and should only be reused with the
same simulator and objective. The legacy `sweep.py` launcher assumes fixed
budgets and is not used for this search.
When changing the scoring version, finish the active trial and stop the parent
before rescoring completed checkpoints and replacing its history. Replaying
unmodified clear-only history would mix different targets in the GP.

## Verification and evidence

```sh
make -f ocean/mario_sim/fpg.mk game-test progress-test curriculum-test semantic-test
make -f ocean/mario_sim/fpg.mk build/mario_sim/fpg/test_practice build/mario_sim/fpg/test_parity
build/mario_sim/fpg/test_practice
build/mario_sim/fpg/test_parity 17 ocean/mario_sim/reference/parity_5_2.buttons
build/mario_sim/fpg/test_parity 8 ocean/mario_sim/reference/parity_3_1.buttons
make -f ocean/mario_sim/runtime.mk environment
build/mario_sim/runtime/test_runtime
python3 -m unittest ocean/mario_sim/test_sweep_worker.py
# Frozen semantic policy against independent ROM execution from each stage:
build/mario_sim/fpg/sim2rom MODEL config/mario_sim.ini OUTPUT 32 137 0 all-stages 0 game
# Full frozen-policy evaluation: continuing game, fresh 1-1, FPG, and
# 32 single-life attempts at each of the 32 individual levels:
python3 ocean/mario_sim/evaluate_rom.py --checkpoint model MODEL \
  --config EVALUATION_INI --output REPORT_DIRECTORY --workers 4
# Compare another checkpoint by adding: --checkpoint previous PREVIOUS_MODEL
```

`evaluate_rom.py` reports each level separately in `per_level.csv` and its
Markdown report. A per-level attempt ends on a completed level transition,
the first death, or 6,000 frames. A flag touch alone is not a completed level.
Each checkpoint uses the same 32 action-sampling seeds at every level. Use
`--levels-only` to extend an existing game/FPG evaluation. `--level-config`
can provide an explicit first-level frame limit in `[fpg].max_frames`.
The native evaluator also accepts `level:W-L` (for example, `level:8-4`) in
place of `all-stages`, with the `clear` objective for a single-level test.
Continuing-game exit totals and flag contacts remain separate from these
first-level clear rates; continuing episodes can record multiple exits.

The two compact parity tapes reproduce prior failures at 5-2/frame 2010 and
3-1/frame 2334, then continue for 240 frames. The fix matches QuickNES's cached
LDA read of PPU status at a frame boundary; LDX retains the uncached read path.
It preserves reset/trace binary layouts. Both original failures had identical
RAM and differed in CPU A/P, so this fix alone does not explain learning failure.
`sim2rom` now saves only the failing one-frame fixtures, the input tape, and
register diagnostics, instead of requiring a new large trace dump. Its optional
last argument selects the first episode index while preserving sampling seeds.

For trained-policy sensitivity checks:

```sh
make -f ocean/mario_sim/fpg.mk build/mario_sim/fpg/probe_policy
build/mario_sim/fpg/probe_policy CONFIG MODEL NEW_OUTPUT.csv
```

This probes terrain, entities, motion, water cues and player velocity on bounded
rollouts from all stage roots and the practice states. Each observation change
starts from identical recurrent carry; an unchanged-input control must match
exactly. CSVs include total variation in action probabilities and jump/right
probability changes. These are FP32 sensitivity diagnostics, not correctness
tests. Different checkpoints drive different rollout trajectories, so aggregate
sensitivity values are not a matched comparison of policy quality. Swimming
and water flags and observed platform velocities already exist in observation
v1. Maze loop counters are not explicit features; whether memory resolves the
ambiguity remains an open question. No encoder tensors or observation fields
were changed by this simulator update.

The [consolidation checks](results/consolidated_20261009/summary.json) record the
CPU/CUDA/ROM results and bounded training/resume/sweep smoke checks.

The game checks compare natural starts in every stage and continuing episodes
against the independent ROM, including semantic observations. The CUDA adapter
check covers all three modes plus encounter practice, auto-resets, rewards,
logs, observations, CUDA Graphs and both curricula's persistence.
Earlier engine qualification and its limits
are documented in [RUNTIME_FIDELITY.md](RUNTIME_FIDELITY.md), [RESULTS.md](RESULTS.md)
and the [experiment audit](EXPERIMENT_AUDIT.md). This is not exhaustive coverage
of every possible full-game route.

The obsolete `mario_lab` and `mario_fpg` implementations and configs were removed.
`mario_fpg_time` was merged here. Historical measurements remain under `results/`,
including `legacy_lab/` and `legacy_fpg/`; logs and checkpoints retain their
original provenance. The native sweep uses fixed ROM panels through
[sweep_worker.py](sweep_worker.py); the historical training metrics remain logged.
