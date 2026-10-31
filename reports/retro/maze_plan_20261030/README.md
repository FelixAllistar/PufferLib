# Mario maze castles: plan for the next 5060 Ti box (2026-10-30)

Not run yet: Felix will rent a box later; the local GTX 1060 (3 GB, 4 cores) is not used for this.

## Where we are

Best policies (saved/retro/watch_20261021/, README there): `fresh_1b.bin` (new model, 106/256 single-level panel
clears and still rising at 1B) and `focusfix2_1200m.bin` (best full-game reach, 11.7 levels). Never cleared: 4-4, 7-4,
8-4 (maze castles) and 5-3, 8-1, 8-3 (pits and precision).

Maze rule (ROM tables at 0x407B, reports/retro/diagnose_4_4_20261020): at certain pages the game checks Mario's height
and rewinds him 4 pages (1,024 px) if he is on the wrong floor. 4-4 needs the upper floor at loop 1 (page 5) and the
ground at loop 2 (page 9); the policy learns "stay high" and repeats it at loop 2. 7-4: a three-check section rewinds
from x 2,300 to 1,250. 8-4: a loop at x 1,250 rewinds to 250. Evidence the levels are learnable: from a seeded
bottom-corridor state a model learned the rest of 4-4 past Bowser in 100M steps (`seed44_cleared44.bin`) and archive
exploration got through parts of 7-4; an 8-4 archive reached x 2,565, past its loop.

## The idea (Felix): reset on a wrong path, start at the choice

- `env.maze_loop_terminal = 1` (new, ocean/retro/retro.h): a detected loop-back ends the episode as a terminal, so a
  wrong floor costs only that attempt and earns nothing more.
- Practice starts just before each check: `ocean/retro/maze_cells.cpp` keeps a policy archive's cells in 4-4, 7-4 and
  8-4 (where it piles up in front of the checks it fails) plus the 4-4 bottom-corridor seed, for
  `env.archive_init_path`.
- Fine-tune `fresh_1b.bin`, keeping most episodes on ordinary levels (archive level focus on the maze castles only for
  a share of starts) so other levels are not forgotten.

## Steps on the box

1. Sync the tree; build retro and the tools (Makefile; maze_cells like seed_maze: CPU only).
2. `maze_cells saved/retro/watch_20261021/fresh_1b.bin fresh_1b.bin.archive.gz maze_cells.archive.gz
   saved/retro/watch_20261021/seed44_only.archive.gz`; check it reports cells in all three levels (if 7-4 or 8-4 has
   few, take cells from the focusfix2 run's archive instead).
3. Short test on 4-4 only (spawn 4-4, maze_loop_terminal 1, archive from step 2, ~100M): clears per attempt and loop
   count per episode should move within the first tens of millions of steps if the idea works.
4. Then all levels with maze focus: load fresh_1b, its own archive plus the maze cells, archive_level_focus for the
   three castles at about 30% of archive starts, maze_loop_terminal 1, 1-2B steps; evaluate with the usual
   32 x 8 panel and 64 full games (reports/retro/confirm_fresh_seed_20261020 protocol).
5. 5-3, 8-1, 8-3 (pits) need practice starts too but no maze rule; same archive focus without the terminal.
