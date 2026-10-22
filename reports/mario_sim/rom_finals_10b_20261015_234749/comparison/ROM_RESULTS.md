# ROM evaluation

| Existing game/FPG tests | steps_2_15b | steps_6_44b | run0_10b | run1_10b |
| --- | ---: | ---: | ---: | ---: |
| Continuing-game mean progress (pixels) | 4124.375 | 8262.59375 | 5547.53125 | 4299.125 |
| Continuing-game clear-reward events | 33 | 77 | 49 | 34 |
| Starting game episodes with any clear-reward event | 16/32 | 17/32 | 17/32 | 18/32 |
| Full-game wins | 0 | 0 | 0 | 0 |
| Fresh 1-1 clears | 29/32 | 25/32 | 9/32 | 28/32 |
| FPG depth 96 | 62/64 | 64/64 | 64/64 | 0/64 |
| FPG depth 425 | 62/64 | 61/64 | 63/64 | 0/64 |

Continuing-game clear-reward events include flag contacts and can precede a completed level transition.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. The † score is completed in the actual ROM, with a failed simulator parity check recorded separately.

| Level | steps_2_15b | steps_6_44b | run0_10b | run1_10b |
| --- | ---: | ---: | ---: | ---: |
| 1-1 | 29/32 | 25/32 | 9/32 | 28/32 |
| 1-2 | 30/32 | 29/32 | 28/32 | 14/32 |
| 1-3 | 31/32 | 32/32 | 30/32 | 31/32 |
| 1-4 | 8/32 | 29/32 | 8/32 | 13/32 |
| 2-1 | 25/32 | 22/32 | 32/32 | 27/32 |
| 2-2 | 0/32 | 6/32 | 6/32 | 0/32 |
| 2-3 | 19/32 | 20/32 | 17/32 | 31/32 |
| 2-4 | 27/32 | 17/32 | 32/32 | 18/32 |
| 3-1 | 9/32 | 32/32 | 30/32 | 28/32 |
| 3-2 | 30/32 | 23/32 | 20/32 | 27/32 |
| 3-3 | 26/32 | 32/32 | 32/32 | 29/32 |
| 3-4 | 0/32 | 27/32 | 0/32 | 3/32 |
| 4-1 | 22/32 | 32/32 | 23/32 | 32/32 |
| 4-2 | 0/32 | 29/32 | 25/32 | 0/32 |
| 4-3 | 0/32 | 0/32 | 0/32 | 0/32 |
| 4-4 | 0/32 | 0/32 | 0/32 | 0/32 |
| 5-1 | 22/32 | 26/32 | 2/32 | 21/32 |
| 5-2 | 0/32 | 27/32 | 10/32† | 7/32 |
| 5-3 | 25/32 | 25/32 | 0/32 | 32/32 |
| 5-4 | 0/32 | 0/32 | 3/32 | 0/32 |
| 6-1 | 18/32 | 31/32 | 10/32 | 31/32 |
| 6-2 | 0/32 | 0/32 | 0/32 | 16/32 |
| 6-3 | 0/32 | 0/32 | 0/32 | 0/32 |
| 6-4 | 8/32 | 0/32 | 0/32 | 11/32 |
| 7-1 | 2/32 | 1/32 | 2/32 | 9/32 |
| 7-2 | 0/32 | 1/32 | 2/32 | 0/32 |
| 7-3 | 15/32 | 9/32 | 10/32 | 28/32 |
| 7-4 | 0/32 | 0/32 | 0/32 | 0/32 |
| 8-1 | 0/32 | 0/32 | 0/32 | 0/32 |
| 8-2 | 0/32 | 0/32 | 0/32 | 0/32 |
| 8-3 | 0/32 | 0/32 | 0/32 | 0/32 |
| 8-4 | 0/32 | 0/32 | 0/32 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| steps_2_15b | 346/1024 | 17/32 | 0/32 |
| steps_6_44b | 475/1024 | 21/32 | 4/32 |
| run0_10b | 331/1024† | 20/32 | 3/32 |
| run1_10b | 436/1024 | 20/32 | 2/32 |

Completed ROM counts, deaths, timeouts and success durations: `ROM_RESULTS.csv`. Commands and strict parity status remain in `summary.json` and `per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.

† Run 0, 5-2: 30 attempts completed with paired simulator/ROM checks. Attempt 30 then hit a CPU-register mismatch at frame 2,010 while RAM matched. Attempts 30 and 31 were completed directly in the ROM using the same reset, policy, per-episode RNG seeds and 6,000-frame limit. A repeat of attempt 29 matched the paired run exactly, including every sampled trace row; attempt 30 also matched its trace up to the original failure. The completed ROM score is 10/32. The strict parity check remains failed; no simulator match is claimed for these two completed attempts. Original diagnostic: `../results/run0_10b/level_5_2/episodes.jsonl`; completion: `../results/run0_10b/level_5_2/rom_completion.json`.
