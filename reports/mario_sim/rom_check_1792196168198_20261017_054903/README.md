# ROM evaluation

| Existing game/FPG tests | current_latest |
| --- | ---: |
| Continuing-game mean progress (pixels) | 4473.5 |
| Continuing-game completed level exits | 29 |
| Continuing-game flag contacts | 24 |
| Starting game episodes with any clear-reward event | 16/32 |
| Full-game wins | 0 |
| Fresh 1-1 clears | 32/32 |
| FPG depth 96 | 63/64 |
| FPG depth 425 | 60/64 |

Flag contacts and completed level exits are counted separately; repeated exits can occur across different attempts.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. Simulator/ROM parity failures are reported as invalid tests.

| Level | current_latest |
| --- | ---: |
| 1-1 | 32/32 |
| 1-2 | 0/32 |
| 1-3 | 0/32 |
| 1-4 | 8/32 |
| 2-1 | 24/32 |
| 2-2 | 12/32 |
| 2-3 | 31/32 |
| 2-4 | 27/32 |
| 3-1 | 30/32 |
| 3-2 | 32/32 |
| 3-3 | 0/32 |
| 3-4 | 0/32 |
| 4-1 | 5/32 |
| 4-2 | 0/32 |
| 4-3 | 0/32 |
| 4-4 | 0/32 |
| 5-1 | 20/32 |
| 5-2 | 15/32 |
| 5-3 | 0/32 |
| 5-4 | 28/32 |
| 6-1 | 9/32 |
| 6-2 | 8/32 |
| 6-3 | 0/32 |
| 6-4 | 2/32 |
| 7-1 | 2/32 |
| 7-2 | 3/32 |
| 7-3 | 32/32 |
| 7-4 | 0/32 |
| 8-1 | 0/32 |
| 8-2 | 16/32 |
| 8-3 | 0/32 |
| 8-4 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| current_latest | 336/1024 | 19/32 | 3/32 |

Full counts, deaths, timeouts, success durations and commands: `summary.json` and `per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.
