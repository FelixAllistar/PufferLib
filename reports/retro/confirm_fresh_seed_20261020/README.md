# Fresh-seed confirmation of the archive-sweep leaders (2026-10-20)

Run on the Goofspiel box's spare CPUs (`/workspace/mario_eval`), not on the sweep box. The evaluator is
`build/retro/sweep_eval`, the same binary the sweep uses. The run first reproduced trial 31's original
16-game sweep panel exactly (seed 20260907, mean 8.125 levels), confirming the setup matches.

The full game panel is 64 games per policy: seeds 20261020 and 20261021, 32 each, because the evaluator caps repeats at 32.
The level panel is all 32 levels × 8 tries, seed 20261020.

| Policy | Level clears /256 | Level score | Full-game mean levels | Got past 1-3 | Fresh combined | Sweep combined |
|---|---|---|---|---|---|---|
| Parent `pixel_197` | 196 | 76.72 | 2.00 | 0/64 | **78.72** | 79.11 |
| Trial 2 | 193 | 75.55 | 7.67 | 57/64 | **83.22** | 81.53 |
| Trial 31 | 185 | 72.42 | 8.14 | 59/64 | **80.56** | 83.28 |

Levels cleared per full game. Count *n* means it died in level *n*+1 of the 1-1…8-4 sequence; no warps were taken.

- Parent: always 2, so it always dies in 1-3.
- Trial 2: 2×7, 5×30, 6×6, 8×2, 11×2, 14×2, 15×15.
- Trial 31: 1×1, 2×4, 4×2, 5×32, 6×3, 13×1, 14×2, 15×19.

Conclusions:

- Both sweep winners really do get past 1-3, unlike the parent.
- Trial 31's level-panel lead (192 on the sweep seed) did not hold on a fresh seed (185). On the fresh panel trial 2 has the best combined score.
- Two walls account for most deaths: **2-2** (5 cleared, about half of all games) and **4-4** (15 cleared). No game got past 4-4.

## Hard-level-focus continuation (trial 31 + 400M decisions, `archive_level_focus=1`, `archive_end=0.4`)

Run from `/workspace/PufferLib-focus` on the Mario box. Same protocol as the tables above.

- Full games: mean **9.86** levels. Distribution: 5×27, 6×4, 7×1, 11×3, 12×1, 15×28.
  - It never died before 2-2.
  - 28/64 games reached 4-4, but none got past it.
- Level panel: **159/256** (score 62.25), well below trial 31's 185.
- Fresh combined score: 72.1, versus 83.2 for trial 2.

Focus improved full-game reach but caused heavy forgetting on isolated levels.

## Same continuation with the progress-reward fix (`retro_focus_fix_20261020`)

Changes: x underflow is treated as 0, novelty is capped at 64 px per decision, and the 3,400 px area cap is removed (see `../diagnose_4_4_20261020/`).

- Full games: mean **10.77** levels, the best so far. 35/64 games reach 4-4 and none get past it.
  Distribution: 4×1, 5×22, 6×3, 9×1, 11×1, 12×1, 15×35.
- Level panel: 175/256 (score 68.51). That is better than the unfixed focus run's 159, but still below trial 31's 185. 4-4 0/8, 8-1 0/8.
- 4-4 trace (16 attempts): no left-wraps (before: 7/8). Max x reached is about 2,050 (before: about 1,020, rarely 1,900).
  It now gets through the first maze loop and is stuck at the second.

## Fixed focus run + 800M more decisions (`retro_focus_fix2_20261020`, 1.2B total on top of trial 31)

- Full games: mean **11.72** levels, the best so far. 42/64 games reach 4-4 and none get past it. Distribution: 5×17, 6×4, 11×1, 15×42.
- Level panel: 151/256 (score 59.12), down from 175. 4-4 0/8, 8-1 0/8.
- 4-4: the furthest loop point is still x≈2,043, the second maze loop.

More training pushes more runs to 4-4 but trades away isolated-level skill, and does not solve the second loop.
Because the archive never sees a state past x≈2,050, level focus cannot help there. That loop is an exploration problem.
