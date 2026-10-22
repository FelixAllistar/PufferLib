# ROM evaluation

| Existing game/FPG tests | steps_9_66b |
| --- | ---: |
| Continuing-game mean progress (pixels) | 2741.40625 |
| Continuing-game completed level exits | 13 |
| Continuing-game flag contacts | 12 |
| Starting game episodes with any clear-reward event | 11/32 |
| Full-game wins | 0 |
| Fresh 1-1 clears | 32/32 |
| FPG depth 96 | 49/64 |
| FPG depth 425 | 64/64 |

Flag contacts and completed level exits are counted separately; repeated exits can occur across different attempts.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. Simulator/ROM parity failures are reported as invalid tests.

| Level | steps_9_66b |
| --- | ---: |
| 1-1 | 32/32 |
| 1-2 | 0/32 |
| 1-3 | 0/32 |
| 1-4 | 5/32 |
| 2-1 | 2/32 |
| 2-2 | 9/32 |
| 2-3 | 24/32 |
| 2-4 | 5/32 |
| 3-1 | 31/32 |
| 3-2 | 0/32 |
| 3-3 | 31/32 |
| 3-4 | 0/32 |
| 4-1 | 29/32 |
| 4-2 | 0/32 |
| 4-3 | 0/32 |
| 4-4 | 0/32 |
| 5-1 | 24/32 |
| 5-2 | 4/32 |
| 5-3 | 0/32 |
| 5-4 | 0/32 |
| 6-1 | 0/32 |
| 6-2 | 3/32 |
| 6-3 | 0/32 |
| 6-4 | 0/32 |
| 7-1 | 6/32 |
| 7-2 | 1/32 |
| 7-3 | 13/32 |
| 7-4 | 0/32 |
| 8-1 | 0/32 |
| 8-2 | 4/32 |
| 8-3 | 3/32 |
| 8-4 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| steps_9_66b | 226/1024 | 17/32 | 1/32 |

Full counts, deaths, timeouts, success durations and commands: `summary.json` and `per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.
