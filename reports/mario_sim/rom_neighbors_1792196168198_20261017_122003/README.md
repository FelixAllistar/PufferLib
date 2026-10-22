# ROM evaluation

| Existing game/FPG tests | steps_4_83b | steps_5_91b | steps_9_13b |
| --- | ---: | ---: | ---: |
| Continuing-game mean progress (pixels) | 3591.09375 | 3084.125 | 4174.21875 |
| Continuing-game completed level exits | 20 | 15 | 32 |
| Continuing-game flag contacts | 18 | 11 | 28 |
| Starting game episodes with any clear-reward event | 12/32 | 11/32 | 16/32 |
| Full-game wins | 0 | 0 | 0 |
| Fresh 1-1 clears | 32/32 | 32/32 | 22/32 |
| FPG depth 96 | 60/64 | 56/64 | 63/64 |
| FPG depth 425 | 64/64 | 26/64 | 63/64 |

Flag contacts and completed level exits are counted separately; repeated exits can occur across different attempts.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. Simulator/ROM parity failures are reported as invalid tests.

| Level | steps_4_83b | steps_5_91b | steps_9_13b |
| --- | ---: | ---: | ---: |
| 1-1 | 32/32 | 32/32 | 22/32 |
| 1-2 | 0/32 | 0/32 | 0/32 |
| 1-3 | 0/32 | 0/32 | 0/32 |
| 1-4 | 8/32 | 32/32 | 11/32 |
| 2-1 | 16/32 | 4/32 | 3/32 |
| 2-2 | 6/32 | 16/32 | 11/32 |
| 2-3 | 32/32 | 31/32 | 6/32 |
| 2-4 | 17/32 | 25/32 | 32/32 |
| 3-1 | 24/32 | 3/32 | 27/32 |
| 3-2 | 30/32 | 16/32 | 30/32 |
| 3-3 | 0/32 | 0/32 | 31/32 |
| 3-4 | 0/32 | 0/32 | 0/32 |
| 4-1 | 6/32 | 31/32 | 29/32 |
| 4-2 | 0/32 | 0/32 | 0/32 |
| 4-3 | 0/32 | 0/32 | 29/32 |
| 4-4 | 0/32 | 0/32 | 0/32 |
| 5-1 | 16/32 | 15/32 | 18/32 |
| 5-2 | 5/32 | 1/32 | 15/32 |
| 5-3 | 0/32 | 0/32 | 0/32 |
| 5-4 | 18/32 | 31/32 | 1/32 |
| 6-1 | 0/32 | 30/32 | 26/32 |
| 6-2 | 8/32 | 6/32 | 7/32 |
| 6-3 | 0/32 | 0/32 | 0/32 |
| 6-4 | 8/32 | 4/32 | 0/32 |
| 7-1 | 2/32 | 14/32 | 3/32 |
| 7-2 | 8/32 | 8/32 | 3/32 |
| 7-3 | 27/32 | 1/32 | 30/32 |
| 7-4 | 0/32 | 0/32 | 0/32 |
| 8-1 | 0/32 | 0/32 | 0/32 |
| 8-2 | 10/32 | 15/32 | 14/32 |
| 8-3 | 0/32 | 2/32 | 1/32 |
| 8-4 | 0/32 | 0/32 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| steps_4_83b | 273/1024 | 18/32 | 2/32 |
| steps_5_91b | 317/1024 | 20/32 | 2/32 |
| steps_9_13b | 349/1024 | 21/32 | 1/32 |

Full counts, deaths, timeouts, success durations and commands: `summary.json` and `per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.
