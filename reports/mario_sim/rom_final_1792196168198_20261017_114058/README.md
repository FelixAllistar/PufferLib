# ROM evaluation

| Existing game/FPG tests | final_10b | steps_8_05b |
| --- | ---: | ---: |
| Continuing-game mean progress (pixels) | 714.46875 | 3617.40625 |
| Continuing-game completed level exits | 1 | 24 |
| Continuing-game flag contacts | 1 | 20 |
| Starting game episodes with any clear-reward event | 1/32 | 14/32 |
| Full-game wins | 0 | 0 |
| Fresh 1-1 clears | 4/32 | 29/32 |
| FPG depth 96 | 0/64 | 63/64 |
| FPG depth 425 | 0/64 | 1/64 |

Flag contacts and completed level exits are counted separately; repeated exits can occur across different attempts.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. Simulator/ROM parity failures are reported as invalid tests.

| Level | final_10b | steps_8_05b |
| --- | ---: | ---: |
| 1-1 | 4/32 | 29/32 |
| 1-2 | 0/32 | 0/32 |
| 1-3 | 0/32 | 0/32 |
| 1-4 | 0/32 | 15/32 |
| 2-1 | 0/32 | 4/32 |
| 2-2 | 0/32 | 3/32 |
| 2-3 | 1/32 | 30/32 |
| 2-4 | 0/32 | 15/32 |
| 3-1 | 0/32 | 23/32 |
| 3-2 | 4/32 | 31/32 |
| 3-3 | 0/32 | 27/32 |
| 3-4 | 0/32 | 0/32 |
| 4-1 | 0/32 | 32/32 |
| 4-2 | 0/32 | 0/32 |
| 4-3 | 0/32 | 18/32 |
| 4-4 | 0/32 | 0/32 |
| 5-1 | 0/32 | 6/32 |
| 5-2 | 0/32 | 21/32 |
| 5-3 | 0/32 | 0/32 |
| 5-4 | 0/32 | 3/32 |
| 6-1 | 0/32 | 25/32 |
| 6-2 | 0/32 | 2/32 |
| 6-3 | 0/32 | 2/32 |
| 6-4 | 0/32 | 0/32 |
| 7-1 | 0/32 | 4/32 |
| 7-2 | 0/32 | 6/32 |
| 7-3 | 0/32 | 6/32 |
| 7-4 | 0/32 | 0/32 |
| 8-1 | 0/32 | 0/32 |
| 8-2 | 0/32 | 4/32 |
| 8-3 | 0/32 | 2/32 |
| 8-4 | 0/32 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| final_10b | 9/1024 | 3/32 | 0/32 |
| steps_8_05b | 308/1024 | 22/32 | 1/32 |

Full counts, deaths, timeouts, success durations and commands: `summary.json` and `per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.
