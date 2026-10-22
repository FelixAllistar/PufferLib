# ROM evaluation

| Existing game/FPG tests | steps_2_00b | steps_1_61b | steps_1_07b | steps_0_54b |
| --- | ---: | ---: | ---: | ---: |
| Continuing-game mean progress (pixels) | 2762.75 | 2001.03125 | 1467.5 | 957.5 |
| Continuing-game completed level exits | 11 | 6 | 2 | 1 |
| Continuing-game flag contacts | 8 | 6 | 2 | 1 |
| Starting game episodes with any clear-reward event | 9/32 | 6/32 | 2/32 | 1/32 |
| Full-game wins | 0 | 0 | 0 | 0 |
| Fresh 1-1 clears | 32/32 | 28/32 | 22/32 | 12/32 |
| FPG depth 96 | 63/64 | 64/64 | 36/64 | 36/64 |
| FPG depth 425 | 64/64 | 63/64 | 53/64 | 0/64 |

Flag contacts and completed level exits are counted separately; repeated exits can occur across different attempts.

32 sampled attempts per level per checkpoint, seed 137. Each attempt starts naturally at that level with fresh policy state and stops at a completed level transition, the first death, or 6,000 frames. Clear counts in this table describe only the starting level. Simulator/ROM parity failures are reported as invalid tests.

| Level | steps_2_00b | steps_1_61b | steps_1_07b | steps_0_54b |
| --- | ---: | ---: | ---: | ---: |
| 1-1 | 32/32 | 28/32 | 22/32 | 12/32 |
| 1-2 | 9/32 | 1/32 | 0/32 | 0/32 |
| 1-3 | 0/32 | 0/32 | 0/32 | 0/32 |
| 1-4 | 25/32 | 5/32 | 0/32 | 0/32 |
| 2-1 | 0/32 | 0/32 | 0/32 | 0/32 |
| 2-2 | 0/32 | 0/32 | 0/32 | 0/32 |
| 2-3 | 8/32 | 7/32 | 0/32 | 0/32 |
| 2-4 | 21/32 | 2/32 | 0/32 | 0/32 |
| 3-1 | 0/32 | 0/32 | 0/32 | 0/32 |
| 3-2 | 12/32 | 5/32 | 5/32 | 2/32 |
| 3-3 | 11/32 | 1/32 | 1/32 | 0/32 |
| 3-4 | 0/32 | 0/32 | 0/32 | 0/32 |
| 4-1 | 16/32 | 14/32 | 6/32 | 0/32 |
| 4-2 | 0/32 | 0/32 | 0/32 | 0/32 |
| 4-3 | 5/32 | 1/32 | 0/32 | 0/32 |
| 4-4 | 0/32 | 0/32 | 0/32 | 0/32 |
| 5-1 | 5/32 | 7/32 | 2/32 | 1/32 |
| 5-2 | 0/32 | 0/32 | 0/32 | 0/32 |
| 5-3 | 0/32 | 0/32 | 0/32 | 0/32 |
| 5-4 | 14/32 | 0/32 | 0/32 | 0/32 |
| 6-1 | 11/32 | 4/32 | 2/32 | 0/32 |
| 6-2 | 0/32 | 0/32 | 0/32 | 0/32 |
| 6-3 | 2/32 | 0/32 | 0/32 | 0/32 |
| 6-4 | 0/32 | 0/32 | 0/32 | 0/32 |
| 7-1 | 1/32 | 1/32 | 0/32 | 0/32 |
| 7-2 | 1/32 | 0/32 | 0/32 | 0/32 |
| 7-3 | 4/32 | 3/32 | 0/32 | 0/32 |
| 7-4 | 0/32 | 0/32 | 0/32 | 0/32 |
| 8-1 | 0/32 | 0/32 | 0/32 | 0/32 |
| 8-2 | 0/32 | 1/32 | 0/32 | 0/32 |
| 8-3 | 0/32 | 0/32 | 0/32 | 0/32 |
| 8-4 | 0/32 | 0/32 | 0/32 | 0/32 |

| Checkpoint | Clears / attempts | Levels with any clears | Levels with all attempts cleared |
| --- | ---: | ---: | ---: |
| steps_2_00b | 177/1024 | 16/32 | 1/32 |
| steps_1_61b | 80/1024 | 14/32 | 0/32 |
| steps_1_07b | 38/1024 | 6/32 | 0/32 |
| steps_0_54b | 15/1024 | 3/32 | 0/32 |

Full counts, deaths, timeouts, success durations and commands: `summary.json` and `per_level.csv`. Continuing-game totals and the separate FPG tests measure different tasks.


## Previous-run comparison

Same 32 attempts per level, seed 137, 6,000-frame first-life limit. Previous results come from saved validated ROM reports. The old 6.44B control reproduced its previous 32/32 on 1-3 in the current remote evaluator.

| Model | Clears / 1024 | Percent | Levels with clears | Fresh 1-1 /32 | FPG96 /64 | FPG256 /64 | FPG425 /64 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| old_steps_2_15b | 346 | 33.8% | 17/32 | 29 | 62 | 46 | 62 |
| old_steps_6_44b | 475 | 46.4% | 21/32 | 25 | 64 | 60 | 61 |
| steps_0_54b | 15 | 1.5% | 3/32 | 12 | 36 | 0 | 0 |
| steps_1_07b | 38 | 3.7% | 6/32 | 22 | 36 | 42 | 53 |
| steps_1_61b | 80 | 7.8% | 14/32 | 28 | 64 | 5 | 63 |
| steps_2_00b | 177 | 17.3% | 16/32 | 32 | 63 | 2 | 64 |

The final checkpoint is strongest in this run and remains below the previous 2.15B checkpoint overall. It gains 4-3 (5/32), 5-4 (14/32), and 6-3 (2/32), which were unsolved in both previous comparison models. The final checkpoint clears 2-2 in 0/32 and 7-2 in 1/32; all world-8 levels are 0/32; the 1.61B checkpoint cleared 8-2 once. The final model received about 38% fewer ordinary-game frames than the old 2.15B model. Multiple training changes were combined, so these results do not isolate a cause.

## Exact-start FPG audit, final checkpoint

| Reference frames | Successes /64 |
| --- | ---: |
| 3 | 64 |
| 4 | 64 |
| 5 | 61 |
| 6 | 61 |
| 8 | 61 |
| 12 | 64 |
| 16 | 64 |
| 24 | 62 |
| 32 | 64 |
| 48 | 61 |
| 64 | 62 |
| 96 | 63 |
| 128 | 58 |
| 192 | 51 |
| 256 | 2 |
| 320 | 64 |
| 425 | 64 |

At the 256-frame start, the final policy grabbed the ordinary flag in 62/64 attempts and performed FPG in 2/64. The old 2.15B checkpoint performed FPG in 46/64; the old 6.44B checkpoint in 60/64. The new policy scored 42/64 there at 1.07B, then 5/64 at 1.61B. All of these simulator/ROM comparisons passed. The retention settings did not prevent this specific reset-state regression.

Detailed previous/new level comparison: `comparison_per_level.csv`; all-depth audit: `fpg_all_depths.csv`; machine-readable totals and limitations: `comparison.json`.
