# ROM evaluation

| Existing game/FPG tests | run0_10b | run1_10b |
| --- | ---: | ---: |
| Continuing-game mean progress (pixels) | 5547.53125 | 4299.125 |
| Continuing-game clear-reward events | 49 | 34 |
| Starting game episodes with any clear-reward event | 17/32 | 18/32 |
| Full-game wins | 0 | 0 |
| Fresh 1-1 clears | 9/32 | 28/32 |
| FPG depth 96 | 64/64 | 0/64 |
| FPG depth 425 | 63/64 | 0/64 |

Continuing-game clear-reward events include flag contacts and can precede a completed level transition.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. Simulator/ROM parity failures are reported as invalid tests.

| Level | run0_10b | run1_10b |
| --- | ---: | ---: |
| 1-1 | 9/32 | 28/32 |
| 1-2 | 28/32 | 14/32 |
| 1-3 | 30/32 | 31/32 |
| 1-4 | 8/32 | 13/32 |
| 2-1 | 32/32 | 27/32 |
| 2-2 | 6/32 | 0/32 |
| 2-3 | 17/32 | 31/32 |
| 2-4 | 32/32 | 18/32 |
| 3-1 | 30/32 | 28/32 |
| 3-2 | 20/32 | 27/32 |
| 3-3 | 32/32 | 29/32 |
| 3-4 | 0/32 | 3/32 |
| 4-1 | 23/32 | 32/32 |
| 4-2 | 25/32 | 0/32 |
| 4-3 | 0/32 | 0/32 |
| 4-4 | 0/32 | 0/32 |
| 5-1 | 2/32 | 21/32 |
| 5-2 | INVALID | 7/32 |
| 5-3 | 0/32 | 32/32 |
| 5-4 | 3/32 | 0/32 |
| 6-1 | 10/32 | 31/32 |
| 6-2 | 0/32 | 16/32 |
| 6-3 | 0/32 | 0/32 |
| 6-4 | 0/32 | 11/32 |
| 7-1 | 2/32 | 9/32 |
| 7-2 | 2/32 | 0/32 |
| 7-3 | 10/32 | 28/32 |
| 7-4 | 0/32 | 0/32 |
| 8-1 | 0/32 | 0/32 |
| 8-2 | 0/32 | 0/32 |
| 8-3 | 0/32 | 0/32 |
| 8-4 | 0/32 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| run0_10b | INVALID | — | — |
| run1_10b | 436/1024 | 20/32 | 2/32 |

Full counts, deaths, timeouts, success durations and commands: `summary.json` and `per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.
