# SWAT role task held-out evaluation, suite v3 (role contract v5)

## Heard commands (swat/shout-hearing) against 5.0

Suite v3 re-recorded with `roles_eval --suite 3` after commands became heard rather than aimed (role contract v5). Same cases and seeds; a v4 run of these eight cases gives identical episode records. "Before" is 5.0 (a17f1ec) run on the same day: it already differs from the integration5 bar below in 47 motel scripted episodes (12, 8, 12, 5 and 10 in the five motel cases; annex unchanged), so that bar was no longer reproducible on 5.0. Completion % with 95% Wilson intervals, other columns means per episode.

| Case | Matchup | Completion % before | Completion % after | Episodes changed (outcome changed) | Lethal violations before / after | ROE violations before / after | Arrested before / after | Killed before / after | Officer casualties before / after | Civ. harmed before / after |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | 33 [23, 45] | 33 [23, 45] | 0/64 (0) | 0.00 / 0.00 | 0.00 / 0.00 | 0.03 / 0.03 | 0.38 / 0.38 | 0.84 / 0.84 | 0.00 / 0.00 |
| annex | policy_officers | 0 [0, 6] | 0 [0, 6] | 0/64 (0) | 0.00 / 0.00 | 0.05 / 0.05 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 |
| annex_escape | scripted | 34 [24, 47] | 34 [24, 47] | 0/64 (0) | 0.00 / 0.00 | 0.00 / 0.00 | 0.03 / 0.03 | 0.41 / 0.41 | 0.73 / 0.73 | 0.00 / 0.00 |
| annex_escape | policy_officers | 0 [0, 6] | 0 [0, 6] | 0/64 (0) | 0.00 / 0.00 | 0.05 / 0.05 | 0.00 / 0.00 | 0.00 / 0.00 | 0.19 / 0.19 | 0.00 / 0.00 |
| motel_standard | scripted | 12 [5, 28] | 31 [18, 49] | 24/32 (6) | 0.00 / 0.00 | 0.00 / 0.00 | 0.41 / 0.75 | 0.28 / 0.28 | 1.12 / 0.88 | 0.00 / 0.00 |
| motel_standard | policy_officers | 0 [0, 11] | 0 [0, 11] | 0/32 (0) | 0.00 / 0.00 | 0.44 / 0.44 | 0.00 / 0.00 | 0.00 / 0.00 | 0.06 / 0.06 | 0.00 / 0.00 |
| motel_dispersed | scripted | 9 [3, 24] | 19 [9, 35] | 19/32 (3) | 0.00 / 0.00 | 0.00 / 0.00 | 0.94 / 1.09 | 0.00 / 0.00 | 0.75 / 0.59 | 0.00 / 0.00 |
| motel_dispersed | policy_officers | 0 [0, 11] | 0 [0, 11] | 2/32 (0) | 0.00 / 0.00 | 0.41 / 0.41 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 |
| motel_mixed | scripted | 6 [2, 20] | 9 [3, 24] | 23/32 (3) | 0.00 / 0.00 | 0.00 / 0.00 | 1.03 / 1.00 | 0.12 / 0.19 | 0.66 / 0.66 | 0.00 / 0.00 |
| motel_mixed | policy_officers | 0 [0, 11] | 0 [0, 11] | 0/32 (0) | 0.00 / 0.00 | 0.41 / 0.41 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 |
| motel_busy | scripted | 3 [1, 16] | 12 [5, 28] | 18/32 (3) | 0.00 / 0.00 | 0.00 / 0.00 | 0.66 / 0.69 | 0.12 / 0.12 | 0.72 / 0.66 | 0.00 / 0.00 |
| motel_busy | policy_officers | 0 [0, 11] | 0 [0, 11] | 0/32 (0) | 0.00 / 0.00 | 0.41 / 0.41 | 0.00 / 0.00 | 0.00 / 0.00 | 0.06 / 0.06 | 0.00 / 0.00 |
| annex_low_ammo | scripted | 41 [29, 53] | 41 [29, 53] | 0/64 (0) | 0.00 / 0.00 | 0.00 / 0.00 | 0.02 / 0.02 | 0.50 / 0.50 | 0.75 / 0.75 | 0.00 / 0.00 |
| annex_low_ammo | policy_officers | 0 [0, 6] | 0 [0, 6] | 0/64 (0) | 0.00 / 0.00 | 0.05 / 0.05 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 |
| motel_low_ammo | scripted | 34 [20, 52] | 50 [34, 66] | 31/32 (5) | 0.00 / 0.00 | 0.00 / 0.00 | 0.91 / 1.06 | 0.19 / 0.19 | 0.84 / 0.72 | 0.00 / 0.00 |
| motel_low_ammo | policy_officers | 0 [0, 11] | 0 [0, 11] | 1/32 (0) | 0.00 / 0.00 | 0.12 / 0.12 | 0.00 / 0.00 | 0.00 / 0.00 | 0.09 / 0.06 | 0.00 / 0.00 |

- Scripted lethal and ROE violations stay 0 in every case. A first version of this branch let one officer's routine calls keep restarting the ROE response time of a suspect its teammate had challenged (0.06-0.19 lethal violations per motel episode); an overheard order now no longer restarts a running challenge.
- Annex cases are unchanged (the annex keeps its aimed command reach and rule).
- Motel scripted completion rises in every case (standard 12 -> 31%, dispersed 9 -> 19%, busy 3 -> 12%, low ammunition 34 -> 50%; mixed 6 -> 9%): an officer's call now reaches occupants it cannot see, more suspects are arrested (standard 0.41 -> 0.75) and fewer officers are hit.
- The random floor changes in 3 motel episodes and in no outcome.

## Current bar (full report)

Suite v3 plays the cases and seeds of suite v1, plus two low-ammunition cases in which each officer starts with 3 rounds in the seated primary magazine and full spares. ROE violations count officer engagements with force on protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); the lethal column counts the second kind alone. Reloads completed, reloads interrupted and dry decisions (begun alive with an empty weapon, not reloading, while it had a spare magazine) are per officer.

16 jobs in 686 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Reloads | Reloads interrupted | Dry decisions | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 33 [23, 45] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.38 [0.25, 0.50] | 0.03 [-0.01, 0.08] | 0.84 [0.62, 1.07] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.8 [15.0, 18.6] | 1.0 [0.8, 1.3] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.39 [0.34, 0.45] | 0.5 [0.3, 0.7] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.00 [0.00, 0.00] | n/a | 5.9 [5.5, 6.3] | 0.00 [0.00, 0.00] | 14.47 [13.77, 15.17] | 7.51 [2.96, 12.05] | 0.39 [0.37, 0.41] | 0.9 [0.6, 1.2] |
| annex_escape | scripted | scripted | 64 | 34 [24, 47] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.41 [0.28, 0.53] | 0.03 [-0.01, 0.08] | 0.73 [0.52, 0.95] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.7 [14.9, 18.5] | 1.2 [1.0, 1.5] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.37 [0.33, 0.42] | 0.8 [0.5, 1.1] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.19 [0.05, 0.33] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 5.7 [5.4, 6.1] | 0.00 [0.00, 0.00] | 14.20 [13.20, 15.19] | 9.62 [4.85, 14.40] | 0.53 [0.46, 0.59] | 0.9 [0.4, 1.4] |
| motel_standard | scripted | scripted | 32 | 31 [18, 49] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.28 [0.12, 0.45] | 0.75 [0.46, 1.04] | 0.88 [0.60, 1.15] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 67.5 [64.7, 70.3] | 1.0 [0.5, 1.5] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 10.22 [9.02, 11.41] | 31.6 [28.3, 34.9] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | 0.00 [0.00, 0.00] | n/a | 8.8 [8.0, 9.7] | 0.00 [0.00, 0.00] | 49.27 [47.10, 51.43] | 151.38 [99.18, 203.57] | 4.33 [3.94, 4.71] | 37.0 [34.1, 39.8] |
| motel_dispersed | scripted | scripted | 32 | 19 [9, 35] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.09 [0.80, 1.39] | 0.59 [0.39, 0.80] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 70.7 [66.9, 74.4] | 0.9 [0.5, 1.3] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 5.54 [4.87, 6.20] | 32.3 [28.7, 35.8] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.4 [8.5, 10.3] | 0.00 [0.00, 0.00] | 49.53 [47.95, 51.11] | 229.88 [179.96, 279.79] | 4.72 [4.20, 5.24] | 39.8 [37.5, 42.1] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.19 [0.04, 0.33] | 1.00 [0.74, 1.26] | 0.66 [0.40, 0.91] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 80.1 [63.8, 96.4] | 1.2 [0.7, 1.8] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 8.98 [7.33, 10.64] | 37.7 [34.2, 41.2] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.6, 10.7] | 0.02 [-0.02, 0.05] | 48.34 [46.50, 50.19] | 204.86 [142.31, 267.41] | 3.83 [3.48, 4.18] | 32.6 [27.5, 37.8] |
| motel_busy | scripted | scripted | 32 | 12 [5, 28] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.12 [0.00, 0.25] | 0.69 [0.39, 0.98] | 0.66 [0.46, 0.85] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 68.6 [64.2, 73.1] | 0.7 [0.2, 1.1] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 5.15 [4.25, 6.06] | 21.9 [19.1, 24.6] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.9, 10.4] | 0.00 [0.00, 0.00] | 47.89 [45.84, 49.94] | 191.45 [134.54, 248.37] | 3.49 [3.02, 3.96] | 24.7 [21.7, 27.7] |
| annex_low_ammo | scripted | scripted | 64 | 41 [29, 53] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.50 [0.37, 0.63] | 0.02 [-0.02, 0.05] | 0.75 [0.51, 0.99] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.4 [15.3, 17.5] | 1.1 [0.9, 1.4] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.42 [0.34, 0.50] | 0.5 [0.3, 0.7] |
| annex_low_ammo | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 4.5 [4.1, 4.9] | 0.00 [0.00, 0.00] | 15.38 [14.63, 16.12] | 23.12 [13.90, 32.35] | 0.38 [0.36, 0.39] | 1.5 [0.9, 2.2] |
| motel_low_ammo | scripted | scripted | 32 | 50 [34, 66] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.19 [0.04, 0.33] | 1.06 [0.75, 1.38] | 0.72 [0.40, 1.04] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 74.8 [71.4, 78.1] | 1.1 [0.7, 1.6] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 9.68 [8.74, 10.61] | 30.9 [28.1, 33.7] |
| motel_low_ammo | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.12 [0.00, 0.25] | 0.00 [0.00, 0.00] | n/a | 7.1 [6.4, 7.8] | 0.00 [0.00, 0.00] | 50.56 [48.64, 52.49] | 227.16 [172.76, 281.55] | 4.59 [4.03, 5.16] | 36.0 [32.7, 39.3] |


## Previous bar: integration5 (5.0 recording)

## Suite v3 bar against suite v2

Suite v3 (swat/integration5) keeps the six v2 cases and their seeds and adds `annex_low_ammo` and `motel_low_ammo` (cases 6 and 7 of seed stream 1): every officer starts with 3 rounds in the seated carbine magazine, one chambered and full spares, set by `SwatConfig.officer_magazine` when the sim spawns the officers. New per-officer metrics: reloads completed, reloads interrupted (cancel, weapon switch, wound or sprint) and dry decisions (begun alive with an empty weapon, not reloading, while it had a spare magazine). The scripted baselines' coins are now a hash of the episode seed, the agent's roster index and its decision count instead of the whole observation.

"v2 coins" is suite v2 on this tree before the coin change: all 384 episode records equal `roles-eval-v2-baselines.json` (wall clock aside), so the merge of charge-only reload changed no v2 episode, and every difference in the v2 cases below comes from the new coins. Completion % with 95% Wilson intervals; other columns are means per episode (reload columns per officer).

| Case | Scripted completion % v2 coins | Scripted completion % v3 | Scripted episodes changed (outcome changed) | Scripted lethal violations | Scripted ROE violations | Arrested / killed | Officer casualties | Officer rounds | Scripted reloads / interrupted / dry | Random floor completion % | Random reloads / interrupted / dry |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | 22 [14, 33] | 33 [23, 45] | 56/64 (31) | 0.00 | 0.00 | 0.03 / 0.38 | 0.84 | 1.0 | 0.00 / 0.00 / 0.00 | 0 [0, 6] | 0.00 / 14.5 / 7.5 |
| annex_escape | 30 [20, 42] | 34 [24, 47] | 60/64 (38) | 0.00 | 0.00 | 0.03 / 0.41 | 0.73 | 1.2 | 0.00 / 0.00 / 0.00 | 0 [0, 6] | 0.00 / 14.2 / 9.6 |
| motel_standard | 9 [3, 24] | 12 [5, 28] | 25/32 (13) | 0.00 | 0.00 | 0.41 / 0.28 | 1.09 | 0.8 | 0.00 / 0.00 / 0.00 | 0 [0, 11] | 0.00 / 49.3 / 151.4 |
| motel_dispersed | 9 [3, 24] | 6 [2, 20] | 13/32 (4) | 0.00 | 0.00 | 0.91 / 0.00 | 0.78 | 0.9 | 0.00 / 0.00 / 0.00 | 0 [0, 11] | 0.00 / 49.5 / 229.9 |
| motel_mixed | 9 [3, 24] | 9 [3, 24] | 10/32 (2) | 0.00 | 0.00 | 0.97 / 0.09 | 0.59 | 1.3 | 0.00 / 0.00 / 0.00 | 0 [0, 11] | 0.02 / 48.3 / 204.9 |
| motel_busy | 3 [1, 16] | 3 [1, 16] | 11/32 (2) | 0.00 | 0.00 | 0.66 / 0.12 | 0.72 | 0.8 | 0.00 / 0.00 / 0.00 | 0 [0, 11] | 0.00 / 47.9 / 191.5 |
| annex_low_ammo | new | 41 [29, 53] | - | 0.00 | 0.00 | 0.02 / 0.50 | 0.75 | 1.1 | 1.00 / 0.00 / 0.00 | 0 [0, 6] | 0.00 / 15.4 / 23.1 |
| motel_low_ammo | new | 28 [16, 45] | - | 0.00 | 0.00 | 0.88 / 0.19 | 0.84 | 1.1 | 1.00 / 0.00 / 0.00 | 0 [0, 11] | 0.00 / 50.3 / 225.4 |

- Scripted lethal ROE violations are 0 in every case, and so are scripted ROE violations. The random floor has no lethal violations; its ROE violations are protected force only.
- New coins, v2 cases: annex 22 -> 33% and annex_escape 30 -> 34%, motel standard/dispersed/mixed/busy 9/9/9/3 -> 12/6/9/3%. Each v3 rate is inside its v2 interval; annex sits at the interval's top (33 against [14, 33]). The policy is unchanged; these are new coin draws of it, the same effect as the earlier 34 -> 19 -> 22% annex swings, but from now on an observation-layout change cannot cause it. The random floor changes in 14 annex_escape episodes and 1 motel_busy episode (the scripted suspects and civilians draw new coins) and in no outcome on the motel cases; its completion stays 0.
- Low ammunition, scripted officers: each officer completes exactly one reload per episode, a tactical reload from 4 rounds on decision 1 at staging, with nothing in view. A scratch probe over all 96 episodes found no reload started with a free suspect in view, none within 8 decisions of a wound, no officer wounded while reloading, no reload from empty, and no dry decision. After the reload the officers play as in the full-magazine cases (they fire about one round per episode), so the low-ammunition cases test reload at the start and nothing later. Completion (41% annex, 28% motel) is at or above the full-magazine cases on other seeds, inside overlapping intervals.
- Random floor on the low-ammunition cases: 0 reloads completed, 15 (annex) and 50 (motel) reloads interrupted and 23 and 225 dry decisions per officer. That is the stall a learned officer must not show.

## Current bar (full report)

Suite v3 plays the cases and seeds of suite v1, plus two low-ammunition cases in which each officer starts with 3 rounds in the seated primary magazine and full spares. ROE violations count officer engagements with force on protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); the lethal column counts the second kind alone. Reloads completed, reloads interrupted and dry decisions (begun alive with an empty weapon, not reloading, while it had a spare magazine) are per officer.

Scripted baselines and legal-random officers, 2026-11-13, swat/integration5, Linux cloud container (4 vCPU, CPU only). 16 jobs in 518 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Reloads | Reloads interrupted | Dry decisions | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 33 [23, 45] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.38 [0.25, 0.50] | 0.03 [-0.01, 0.08] | 0.84 [0.62, 1.07] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.8 [15.0, 18.6] | 1.0 [0.8, 1.3] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.14 [0.13, 0.15] | 0.3 [0.3, 0.3] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.00 [0.00, 0.00] | n/a | 5.9 [5.5, 6.3] | 0.00 [0.00, 0.00] | 14.47 [13.77, 15.17] | 7.51 [2.96, 12.05] | 0.15 [0.15, 0.15] | 0.4 [0.4, 0.4] |
| annex_escape | scripted | scripted | 64 | 34 [24, 47] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.41 [0.28, 0.53] | 0.03 [-0.01, 0.08] | 0.73 [0.52, 0.95] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.7 [14.9, 18.5] | 1.2 [1.0, 1.5] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.13 [0.12, 0.14] | 0.3 [0.2, 0.3] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.19 [0.05, 0.33] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 5.7 [5.4, 6.1] | 0.00 [0.00, 0.00] | 14.20 [13.20, 15.19] | 9.62 [4.85, 14.40] | 0.19 [0.18, 0.20] | 0.4 [0.4, 0.4] |
| motel_standard | scripted | scripted | 32 | 12 [5, 28] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.28 [0.12, 0.45] | 0.41 [0.17, 0.65] | 1.09 [0.86, 1.32] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 76.8 [71.6, 81.9] | 0.8 [0.3, 1.2] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 5.97 [5.16, 6.77] | 15.5 [14.7, 16.2] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | 0.00 [0.00, 0.00] | n/a | 8.8 [8.0, 9.7] | 0.00 [0.00, 0.00] | 49.27 [47.10, 51.43] | 151.38 [99.18, 203.57] | 1.81 [1.65, 1.96] | 16.8 [15.6, 18.0] |
| motel_dispersed | scripted | scripted | 32 | 6 [2, 20] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.91 [0.63, 1.19] | 0.78 [0.58, 0.98] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 64.3 [26.9, 101.7] | 0.9 [0.5, 1.4] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 4.93 [4.41, 5.44] | 16.6 [15.8, 17.4] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.4 [8.5, 10.3] | 0.00 [0.00, 0.00] | 49.53 [47.95, 51.11] | 229.88 [179.96, 279.79] | 1.80 [1.59, 2.01] | 16.4 [15.8, 17.0] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.97 [0.70, 1.24] | 0.59 [0.35, 0.83] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 75.6 [63.5, 87.7] | 1.3 [0.7, 1.9] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 5.42 [4.81, 6.03] | 16.4 [15.5, 17.2] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.6, 10.7] | 0.02 [-0.02, 0.05] | 48.34 [46.50, 50.19] | 204.86 [142.31, 267.41] | 1.81 [1.69, 1.93] | 16.3 [15.7, 16.9] |
| motel_busy | scripted | scripted | 32 | 3 [1, 16] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.12 [0.00, 0.25] | 0.66 [0.34, 0.97] | 0.72 [0.51, 0.93] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 69.7 [69.7, 69.7] | 0.8 [0.4, 1.2] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 5.46 [5.07, 5.86] | 16.1 [15.5, 16.6] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.9, 10.4] | 0.00 [0.00, 0.00] | 47.89 [45.84, 49.94] | 191.45 [134.54, 248.37] | 2.20 [1.93, 2.47] | 17.1 [16.5, 17.8] |
| annex_low_ammo | scripted | scripted | 64 | 41 [29, 53] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.50 [0.37, 0.63] | 0.02 [-0.02, 0.05] | 0.75 [0.51, 0.99] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.4 [15.3, 17.5] | 1.1 [0.9, 1.4] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.14 [0.13, 0.15] | 0.3 [0.3, 0.3] |
| annex_low_ammo | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 4.5 [4.1, 4.9] | 0.00 [0.00, 0.00] | 15.38 [14.63, 16.12] | 23.12 [13.90, 32.35] | 0.15 [0.14, 0.15] | 0.4 [0.3, 0.4] |
| motel_low_ammo | scripted | scripted | 32 | 28 [16, 45] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.19 [0.04, 0.33] | 0.88 [0.57, 1.18] | 0.84 [0.55, 1.13] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 76.7 [71.7, 81.6] | 1.1 [0.7, 1.5] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 6.43 [5.80, 7.07] | 15.4 [14.7, 16.1] |
| motel_low_ammo | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.12 [0.00, 0.25] | 0.00 [0.00, 0.00] | n/a | 7.2 [6.4, 7.9] | 0.00 [0.00, 0.00] | 50.31 [48.32, 52.31] | 225.44 [170.89, 279.98] | 1.95 [1.75, 2.16] | 16.4 [15.7, 17.1] |
