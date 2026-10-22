# Mario all-levels restart plan (2026-10-21)

## Where we are

Fresh-seed results, 8 tries per level (`../confirm_fresh_seed_20261020/`):

| Level | Parent | Trial 2 | Trial 31 | Focus fix2 (1.2B) |
|---|---|---|---|---|
| 1-3 | 0 | 2 | 2 | 7 |
| 2-2 | 6 | 7 | 4 | 3 |
| 4-4 | 0 | 0 | 0 | 0 |
| 5-3 | 0 | 0 | 0 | 0 |
| 6-3 | 8 | 4 | 6 | 3 |
| 7-4 | 0 | 0 | 0 | 0 |
| 8-1 | 0 | 0 | 0 | 0 |
| 8-2 | 8 | 8 | 2 | 0 |
| 8-3 | 0 | 0 | 0 | 0 |
| 8-4 | 0 | 0 | 0 | 0 |
| **Total /256** | 196 | 193 | 185 | 151 |

Every level not listed is cleared 7–8 times out of 8 by at least the parent and trial 2.

- **Never cleared:** 4-4, 5-3, 7-4, 8-1, 8-3, 8-4.
  - 4-4 is solved apart from one drop (`../diagnose_4_4_20261020/`).
  - 7-4 and 8-4 are also maze castles; the ROM loop table has entries for worlds 7 and 8.
  - 8-1 was starved by the old 3,400 px progress cap, now removed.
  - 5-3 and 8-3 have not been traced.
- **Best overall:** trial 2, with fresh combined score 83.2 (193/256 levels, 7.67 levels per full game).
- **Forgetting:** long level-focus training raised full-game reach (11.7 levels per game) but lost
  isolated-level skill (151/256).

## What's new since those runs (local tree, uncommitted; deployed to the Mario box)

- **Progress reward:** x underflow counts as 0, novelty is capped at 64 px per step, and there is no
  3,400 px area cap.
- **Evaluator:** reads 4-4 progress correctly.
- **New env options, default off:**
  - `archive_level_focus`
  - `maze_loop_penalty`
  - `archive_depth_priority`
- **Archive ledger layout changed:** older `.archive.gz` files are rejected.
- **Seed tool** `ocean/retro/seed_maze.cpp`, plus the 4-4 bottom-corridor seed state.

## Plan

0. **Trace the unexplained levels first** (CPU only, about 30 min). Run `trace_level` with trial 2 on
   5-3, 7-4, 8-1, 8-3 and 8-4, to find each blocker. If 7-4 or 8-4 is a maze drop like 4-4, make a
   seed for it with the same search. Don't spend sweep time on levels with a known wall.
1. **Seed archive.** Start each trial with an archive holding only the seed cells (4-4 bottom corridor,
   plus any from step 0). Use `archive_capacity_mb = 1024`, or new cells evict the seeds.
2. **Sweep before any long run.**
   - **Start:** trial 2's weights (best fresh combined score).
   - **Budget:** fixed reward, all 32 levels, 200M steps per trial, about 25 min of training plus eval
     at ~137K steps/s.
   - **Objective:** the existing combined score: level panel + 100 × game win rate + mean levels per
     game. That already penalizes forgetting.
   - **Search:** the archive mix (start/end/anneal, level focus, depth priority), LR, entropy and
     `maze_loop_penalty`. PPO shape stays as in trial 2.
   - **Size:** about 60 trials, roughly a day on the Mario box.
3. **Long run** of the winner, 1–2B steps, keeping the seeded archive.
4. **Confirm** on fresh seeds with the same protocol as `../confirm_fresh_seed_20261020/`: 32 levels × 8
   and 64 full games. Compare per level with trial 2.

## Step 0 result: blockers on the never-cleared levels (trial 2, 16 traces each)

| Level | Clears | Furthest x (median / max) | Blocker |
|---|---|---|---|
| 5-3 | 0/16 | 1,689 / 2,226 | Pit falls at x≈1,050, 1,250–1,300 and 1,700 (platform jumps) |
| 7-4 | 0/16 | 2,302 / 2,311 | Maze. Second 3-check section sends Mario from x≈2,300 back to ≈1,250 (34 times) |
| 8-1 | 0/16 | 2,905 / 3,714 | Pit falls at x≈2,500–2,700. Trial 2 trained with the old 3,400 px cap, so the rest had no signal |
| 8-3 | 0/16 | 2,333 / 2,544 | Falls and hits spread through the level |
| 8-4 | 0/16 | 1,275 / 1,310 | Maze loop from x≈1,250 back to ≈250 (21 times) |

The tracer's "back to x≈0" jumps on 5-3, 8-1 and 8-3 are pit deaths its death check misses, not loops.

Archive evidence says only 4-4 needs a seed:
- **4-4 and 7-4.** The seeded 4-4 run's archive (`retro_seed2_20261021`) holds 373 cells in 7-4 at
  x 40–3,276, with 673 recorded successes. Natural episodes that cleared 4-4 from the seed kept
  playing into world 7, and archive exploration got through 7-4's maze sections. That run therefore
  did clear 4-4.
- **8-4.** The fix2 focus archive holds 8-4 cells up to x 2,565, in the same area, past the ≈1,250
  loop that stops trial 2. Exploration finds that route too.
- **5-3, 8-1 and 8-3.** Precision and pit skill problems. They need practice from archived starts,
  which the sweep provides; 8-1 also gets reward past 3,400 px now.

So the restart seeds only 4-4.

## Open choices

- **Starting weights.** Trial 2 keeps existing skills but was trained on the buggy progress reward.
  From scratch is cleaner but much slower. Plan: trial 2.
- **Seeds for 7-4 and 8-4.** Not needed (see step 0).

## Launched (2026-10-21)

- **Where:** Mario box, `/workspace/PufferLib-focus`, tmux `restart`, plain `./puffer sweep`. Log and
  history are in `logs/retro_restart_20261021/`; checkpoints in `checkpoints/retro_restart_20261021/`.
- **Config:** `retro_restart.ini` here, deployed as `config/retro.ini`. The 4-4 config is kept as
  `config/retro.ini.4-4_20261021`.
- **Start:** `saved/retro/restart_t2_seed_20261021.bin` is trial 2's weights and config.
  `.archive.gz` holds only the 4-4 seed cell; the trainer logs "loaded 1 archived cells".
- **Search:** 11 dimensions (the archive mix, level focus, depth priority, maze loop penalty, LR,
  entropy) and 60 trials of 200M steps. Trial 0 is trial 2's settings plus the seed and the 1 GB
  archive.
- **Preflight:** `make -C ocean/retro sweep-test` passes. `tests/test_training_config.cpp` now lists
  this sweep's 11 dimensions. `sweep_worker.py` has `panel_levels` and its original 32-level error
  text, in both the local tree and on the box.
- **Speed:** about 110K decisions/s, so roughly 30 min of training plus evaluation per trial, about
  40 h for 60 trials. Trainer VRAM is about 14.2 GiB, against the sweep's 15,360 MiB guard.

## Plan change: fresh model with an end-first curriculum (2026-10-21)

The user judged that tweaking the old lineage would not work. The restart sweep was stopped after 17
trials (best 84.08, trial 12; history kept).

- **New default-off options** in `retro_archive.h`, unit-tested in `tests/test_archive.cpp`:
  - `archive_backward`: weights each pick by `exp(B·(1−phase)·(depth/deepest−1))`. `depth` is the
    cell's summed frontier x in its own level.
  - `archive_init_path`: seeds an empty bank from a file and ignores that file's saved phase.
- **Run:** `run_fresh.sh`, tmux `fresh`, output in `checkpoints/retro_fresh_20261021`.
  - No loaded weights; LR 0.001 annealed; entropy 1e-4.
  - 5-minute episodes (18,000 frames), all-level spawns.
  - Starting archive: the 9,847 cells of `retro_seed2_20261021`, covering 29/32 levels and including
    the 4-4 seed. Capacity 1 GB.
  - Backward strength 4; archive 0.9 → 0.3; level focus 0.5; 300M steps.

**Result at 300M**, with the sweep panel seed 20260907 (32 levels × 8 plus 16 full games):
- **Levels:** 61/256 clears, level-panel score 23.9.
- **Full games:** mean 4.94 levels per game, no wins.
- **Per level:**
  - All of world 1, plus 2-1 and 3-2: 8/8.
  - 5-3: 1/8. The old lineage never cleared 5-3.
  - 8-1: 0/8 but already 0.68 progress.
  - 4-4: 0/8, 0.27 progress.
- **Comparison:** the old lineage sat at 184–196/256, after 838M from-scratch steps plus several
  hundred million steps of continuation.
- **Open question:** whether end-first helps cannot be told yet. That needs the same run with
  `archive_backward = 0`.

**Control** (`retro_fresh_ctrl_20261021`): identical, except `archive_backward = 0`. Same eval:

| | End-first (backward 4) | Control (backward 0) |
|---|---|---|
| Level clears /256 | 61 | 68 |
| Mean progress | 0.488 | 0.489 |
| Full games (levels per run) | 4.94 | 5.00 |

- **No measurable gain from end-first** at 300M, with one seed per arm. The arms differ by single
  levels in both directions. End-first is ahead on 3-2 (8 vs 5), 5-3 (1 vs 0), 6-1 (3 vs 1) and
  6-2 (1 vs 0). The control is ahead on 2-4 (4 vs 0), 3-3 (4 vs 2), 4-1 (7 vs 1) and 4-2 (3 vs 0).
- **Both fresh models** are far below the old lineage's 184–196 at this budget.

**Continuation to ~1B** (`run_fresh_ctrl_1b.sh`, tmux `fresh_1b`):
- Loads the control's 300M weights and its 12,096-cell archive. Same settings, except LR 5e-4
  cosine and no `archive_init_path`.
- 700M more steps, with a checkpoint every 1,144 rollouts (~300M).
- The step counter restarts, so checkpoint names are relative: +300M ≈ 600M total, +600M ≈ 900M,
  final ≈ 1.0B.
- Each checkpoint gets the same panel plus 16 full games (`/workspace/watch_fresh_1b.sh`, tmux
  `fresh_1b_eval`), logged to `/workspace/fresh_1b_evals.log`.
