# ROM evaluation

| Existing game/FPG tests | steps_2_15b | steps_6_44b |
| --- | ---: | ---: |
| Continuing-game mean progress (pixels) | 4124.375 | 8262.59375 |
| Continuing-game clear-reward events | 33 | 77 |
| Starting game episodes with any clear-reward event | 16/32 | 17/32 |
| Full-game wins | 0 | 0 |
| Fresh 1-1 clears | 29/32 | 25/32 |
| FPG depth 96 | 62/64 | 64/64 |
| FPG depth 425 | 62/64 | 61/64 |

Continuing-game clear-reward events include flag contacts and can precede a completed level transition.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. Simulator/ROM parity failures are reported as invalid tests.

| Level | steps_2_15b | steps_6_44b |
| --- | ---: | ---: |
| 1-1 | 29/32 | 25/32 |
| 1-2 | 30/32 | 29/32 |
| 1-3 | 31/32 | 32/32 |
| 1-4 | 8/32 | 29/32 |
| 2-1 | 25/32 | 22/32 |
| 2-2 | 0/32 | 6/32 |
| 2-3 | 19/32 | 20/32 |
| 2-4 | 27/32 | 17/32 |
| 3-1 | 9/32 | 32/32 |
| 3-2 | 30/32 | 23/32 |
| 3-3 | 26/32 | 32/32 |
| 3-4 | 0/32 | 27/32 |
| 4-1 | 22/32 | 32/32 |
| 4-2 | 0/32 | 29/32 |
| 4-3 | 0/32 | 0/32 |
| 4-4 | 0/32 | 0/32 |
| 5-1 | 22/32 | 26/32 |
| 5-2 | 0/32 | 27/32 |
| 5-3 | 25/32 | 25/32 |
| 5-4 | 0/32 | 0/32 |
| 6-1 | 18/32 | 31/32 |
| 6-2 | 0/32 | 0/32 |
| 6-3 | 0/32 | 0/32 |
| 6-4 | 8/32 | 0/32 |
| 7-1 | 2/32 | 1/32 |
| 7-2 | 0/32 | 1/32 |
| 7-3 | 15/32 | 9/32 |
| 7-4 | 0/32 | 0/32 |
| 8-1 | 0/32 | 0/32 |
| 8-2 | 0/32 | 0/32 |
| 8-3 | 0/32 | 0/32 |
| 8-4 | 0/32 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| steps_2_15b | 346/1024 | 17/32 | 0/32 |
| steps_6_44b | 475/1024 | 21/32 | 4/32 |

Full counts, deaths, timeouts, success durations and commands: `results/summary.json` and `results/per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.

The game/FPG tests above are retained from their completed evaluations. The 2,048 per-level attempts are new. Both 32-episode 1-1 retests reproduced the prior episode records exactly.

All panels validated: True. New per-level simulator/ROM mismatches: 0.
