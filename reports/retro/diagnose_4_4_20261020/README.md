# Why nothing passes 4-4 (2026-10-20)

The policy checked is the hard-level-focus continuation (trial 31 + 400M decisions). The tool is
`ocean/retro/trace_level.cpp`, a CPU tracer that uses the same inference and sampling as `sweep_eval` and logs
deaths and backward x jumps. It ran on the Goofspiel box. Archive cells come from the run's final `.archive.gz`.

## Findings

1. **4-4 reward bug (x underflow).** At the start of 4-4 Mario can step left of x=0. The x position
   (`robs_x`, page*256+x) then wraps to about 65,535. `retro_progress` clamps x to [0, 3400] (`retro.h:297`),
   so the wrap counts as reaching x=3400. That gives an instant fake "new pixels" reward of about 3,360 px
   (26 checkpoints). It also leaves the area frontier at 3400, so **real progress through 4-4 earns nothing
   for the rest of the episode**.
   - Tracing the first 3,000 frames of all 32 levels, 8 attempts each, found this only in 4-4: 7/8 attempts walked left and wrapped.
   - Every isolated 4-4 attempt in the fresh-seed panel shows exactly 26 checkpoints, all from this jackpot.
   - The evaluator's `furthest` also uses the raw wrapped x, so 4-4 panel progress reads 1.0. That is wrong.
2. **Maze loop.** 4-4's first loop sends Mario from x≈1,020 back to about 0 when he stays on the ground path (y=176).
   The traced attempts loop there 4–13 times each and almost never get past x≈1,900. The archive's 48 4-4 cells all
   sit at x ≤ 1,915, so level focus has nothing past the loop to practice from.
3. **3,400-pixel progress cap (separate issue).** The same clamp stops progress reward after 3,400 px in any
   area. 8-1 is longer than that and is never cleared (0/8 on every policy checked). Its second half has no
   progress signal. 2-3, 4-1, 6-2, 7-3 and 8-2 also reach the cap, but they still clear.

## Proposed fix (not applied)

In `retro_progress`, treat x ≥ 0x8000 as an underflow (x=0), not a jump to 3400. Replace the per-area 3,400
clamp with a per-step novelty cap, for example at most 64 new px per call (Mario moves ≤ ~12 px per
4-frame decision). That still blocks glitch jackpots, without starving long levels. This changes reward
semantics, so results after the fix are a new experiment and are not comparable with the current sweep history.

## Loop rule confirmed (2026-10-21)

The ROM (`ocean/retro/roms/smb1_ntsc.nes`) holds the loop tables back to back at file offset 0x407B:

| Table | Bytes |
| --- | --- |
| LoopCmdWorldNumber | `03 03 06 06 06 06 06 06 07 07 07` |
| LoopCmdPageNumber | `05 09 04 05 06 08 09 0a 06 0b 10` |
| LoopCmdYPosition | `40 b0 b0 80 40 40 80 40 f0 f0 f0` |

They are followed by `LDA $6D; SEC; SBC #$04; STA $6D`, which moves Mario back 4 pages (1,024 px)
on a failed check. 4-4 (world index 3) has two checks:

- **Loop 1** (page 5, Mario near x≈1,020): Mario must stand at y=0x40 (64), the upper floor.
- **Loop 2** (page 9, Mario near x≈2,040): Mario must stand at y=0xB0 (176), the ground.

`$06D9`/`$06DA` (multi-loop counters) never change in 4-4; they are only used by 7-4.

The trace used trial 0001 of the 4-4 sweep: `trace_level`, 32 attempts, seed 20261023, 30,000 frames.
- Furthest x was 2,051. No attempt cleared.
- **Loop 1:** passed on most attempts. The 10 failures were Mario standing on the ground (y=176).
- **Loop 2:** failed 25 times.
  - 12 times standing on the upper floor (y=64).
  - 10 times mid-jump.
  - 3 times at y=128.
  - Never on the ground (y=176).

So the policy learned "stay high" for loop 1 and carries it into loop 2, where it is wrong. The
progress reward only pays after x≈2,050, so nothing rewards dropping to the ground before the check.

The tracer's other "to start" jumps have the y-high byte at 3–4: Mario fell below the screen. Those
are pit deaths that `robs_dead` did not flag; the episode restarts the level from x=0.

## First fix attempt: maze route reward (2026-10-21) — no clear

New env option `maze_route_reward`, default 0, so existing configs are unchanged. After a loop-back
in an area (a 900+ px x drop between two consecutive tracked frames), standing on a new floor band in
an already passed 128 px column pays once per episode. Unit test is in `tests/test_archive.cpp`.

Check run: trial 0001 of the 4-4 sweep, unchanged except `maze_route_reward = 0.5`. 100M steps,
`checkpoints/retro_maze_20261021/`, tmux `mazecheck`.

- **Score:** panel 0.906 (0/32 clears) vs 0.910 before. Trace over 32 attempts: no clear,
  furthest x 2,053.
- **Loop 2:** the agent now also tries the middle corridor (11 failures at y=128, 10 at y=64). It
  never reaches the bottom corridor. The archive has no bottom-floor cells past x≈1,550.
- **Exploited:** loop-1 failures rose from 10 to 65. Failing loop 1 on purpose opens route bonuses
  for the upper platforms in columns 0–1,000. Per-episode bounding didn't stop it, because one failed
  loop unlocks many columns at once.

The bottom corridor is entered through a narrow gap in the middle floor right next to a lava pit
(x≈1,580). That drop is the real exploration problem, and a floor-novelty bonus doesn't reach it.

## Second attempt: loop penalty + depth priority (2026-10-21) — no clear

The route bonus was replaced by `maze_loop_penalty`: a penalty per detected loop-back, default 0.
Also new: `archive_depth_priority` (default 0), which weights archive cell picks by
1 + depth × (y band / 7). Unit tests are in `tests/test_archive.cpp`.

Run: trial 0001, unchanged except `maze_loop_penalty = 1` and `archive_depth_priority = 2`.
100M steps, `checkpoints/retro_maze2_20261021/`.

- **Score:** panel 0.889 (0/32 clears). Trace over 32 attempts: no clear, median furthest x 1,951.
  The trace's max of 65,535 is the tracer reading the raw underflowed x.
- **Loops:** fewer than before, 7 loop-1 and 5 loop-2 failures. Pit deaths went up instead.
- **Bottom corridor:** still not reached. The archive has no bottom-floor (y band 5) cells past x≈1,550.

Neither shaping change gets Mario through the lava-side gap into the bottom corridor. That drop is
the remaining blocker.

## Seeded archive: the rest of 4-4 is learnable (2026-10-21)

**Seed.** `ocean/retro/seed_maze.cpp` searched random button sequences from archived 4-4 cells at
x 1,450–1,650, holding B+Right once Mario stood on the bottom floor. It found one state standing in
the bottom corridor at x=1,659. Its `verify` mode confirms that running right from that state passes
loop 2 (x 2,109, no loop-back). The state was added to the second check run's final archive and saved
as `saved/retro/maze_seed_20261021.bin(.archive.gz)`; the weights are the usual parent's.

**Run 1** (`retro_seed_20261021`, trial 0001 settings, 128 MB archive). The bank was full and 4-4
was its most-represented level, so eviction removed the seed before it was ever selected. That run
tested nothing.

**Run 2** (`retro_seed2_20261021`, the same settings with `archive_capacity_mb = 1024`, no
eviction). The seed was selected and the agent learned the rest of the level from it:
- The final archive has 27 bottom-corridor cells and 43 cells past loop 2.
- They reach x≈2,770, which is past Bowser at the axe (screenshot checked). Those end cells record
  failures only, so a full clear is not confirmed.
- From the real level start nothing changed: panel 0.906, 0/32 clears, furthest 2,052. The policy
  still never drops into the bottom corridor on its own.

**Conclusion.** Everything after the bottom-corridor entrance is learnable in 100M steps. The only
blocker is that single lava-side drop at x≈1,520, which neither random play nor the policy finds from
the level start.
