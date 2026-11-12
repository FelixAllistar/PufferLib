# SWAT role task held-out evaluation, suite v1 (role contract v2)

## Scripted bar, before and after the rules of engagement

Scripted matchup (every role scripted). Before: the route-following motel officer of swat/roles-eval-motel, which fired on any free suspect on sight. The unmodified merge with swat/integration2 reproduces those numbers exactly. After: swat/integration3, where the motel officer follows the rules of engagement of roe.h. It challenges before lethal force, returns fire when hurt, confirms every shot with a call, holds fire on a fleeing target, and keeps calls and shots in alternate decisions. Annex cases are unchanged. Means with 95% intervals.

The eval's ROE violations column counts force on protected people only (civilians, surrendered, restrained). Lethal ROE violations are roe.h's count (debrief.lethal_violations): lethal force on a person who was not a perceived lethal threat, here always an armed suspect not yet challenged. They are not an eval metric. They come from a probe on the same 32 held-out episodes per motel case, with sim.c's swat_roe_force counting each violation by the victim's ROE state.

| Case | Completion % before | Completion % after | Civ. harmed (any) % after | Officer casualties before / after | Suspects arrested before / after | Suspects killed before / after | ROE violations after | Lethal ROE violations (roe.h) before / after | Time to clear s after | Officer rounds before / after |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | 34 [24, 47] | 34 [24, 47] | 0 [0, 6] | 0.84 [0.61, 1.08] / 0.84 [0.61, 1.08] | 0.02 [-0.02, 0.05] / 0.02 [-0.02, 0.05] | 0.44 [0.31, 0.56] / 0.44 [0.31, 0.56] | 0.02 [-0.02, 0.05] | n/a / n/a | 16.2 [15.0, 17.4] | 1.0 [0.7, 1.2] / 1.0 [0.7, 1.2] |
| annex_escape | 23 [15, 35] | 23 [15, 35] | 0 [0, 6] | 0.91 [0.67, 1.14] / 0.91 [0.67, 1.14] | 0.05 [-0.01, 0.10] / 0.05 [-0.01, 0.10] | 0.30 [0.18, 0.41] / 0.30 [0.18, 0.41] | 0.00 [0.00, 0.00] | n/a / n/a | 15.6 [14.2, 17.0] | 1.0 [0.7, 1.3] / 1.0 [0.7, 1.3] |
| motel_standard | 28 [16, 45] | 9 [3, 24] | 0 [0, 11] | 0.97 [0.70, 1.24] / 1.12 [0.89, 1.36] | 0.22 [0.07, 0.37] / 0.44 [0.18, 0.70] | 0.62 [0.39, 0.86] / 0.25 [0.09, 0.41] | 0.00 [0.00, 0.00] | 0.28 / 0.00 | 72.0 [67.5, 76.5] | 2.6 [1.7, 3.5] / 0.9 [0.4, 1.4] |
| motel_dispersed | 12 [5, 28] | 6 [2, 20] | 0 [0, 11] | 0.50 [0.30, 0.70] / 0.72 [0.53, 0.91] | 0.53 [0.31, 0.76] / 0.94 [0.65, 1.23] | 0.69 [0.47, 0.90] / 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.94 / 0.00 | 62.7 [35.2, 90.2] | 3.7 [3.0, 4.5] / 0.9 [0.5, 1.4] |
| motel_mixed | 6 [2, 20] | 9 [3, 24] | 0 [0, 11] | 0.41 [0.20, 0.61] / 0.50 [0.30, 0.70] | 0.50 [0.30, 0.70] / 1.06 [0.80, 1.32] | 0.91 [0.68, 1.14] / 0.12 [0.00, 0.25] | 0.00 [0.00, 0.00] | 0.78 / 0.06 | 75.2 [61.6, 88.8] | 3.5 [2.7, 4.4] / 1.7 [1.0, 2.3] |
| motel_busy | 6 [2, 20] | 6 [2, 20] | 0 [0, 11] | 0.38 [0.16, 0.59] / 0.59 [0.41, 0.77] | 0.81 [0.56, 1.06] / 0.78 [0.47, 1.09] | 0.59 [0.39, 0.80] / 0.12 [0.00, 0.25] | 0.00 [0.00, 0.00] | 1.22 / 0.00 | 74.1 [48.8, 99.4] | 4.0 [3.3, 4.7] / 1.0 [0.6, 1.4] |

## Current bar (full report)

Scripted baselines (route-following motel officer under the rules of engagement, roe.h) and legal-random officers, 2026-11-12, swat/integration3, Linux cloud container (4 vCPU, CPU only). 12 jobs in 258 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Time to clear s | Officer rounds | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 34 [24, 47] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.44 [0.31, 0.56] | 0.02 [-0.02, 0.05] | 0.84 [0.61, 1.08] | 0.02 [-0.02, 0.05] | 16.2 [15.0, 17.4] | 1.0 [0.7, 1.2] | 0.11 [0.10, 0.11] | 0.2 [0.2, 0.3] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | n/a | 5.9 [5.5, 6.3] | 0.12 [0.12, 0.12] | 0.3 [0.3, 0.3] |
| annex_escape | scripted | scripted | 64 | 23 [15, 35] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.30 [0.18, 0.41] | 0.05 [-0.01, 0.10] | 0.91 [0.67, 1.14] | 0.00 [0.00, 0.00] | 15.6 [14.2, 17.0] | 1.0 [0.7, 1.3] | 0.11 [0.10, 0.11] | 0.3 [0.2, 0.3] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.22 [0.07, 0.36] | 0.03 [-0.01, 0.08] | n/a | 5.6 [5.2, 6.0] | 0.16 [0.15, 0.17] | 0.4 [0.4, 0.4] |
| motel_standard | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.25 [0.09, 0.41] | 0.44 [0.18, 0.70] | 1.12 [0.89, 1.36] | 0.00 [0.00, 0.00] | 72.0 [67.5, 76.5] | 0.9 [0.4, 1.4] | 5.01 [4.31, 5.70] | 14.0 [13.2, 14.8] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | n/a | 8.7 [7.9, 9.5] | 1.44 [1.32, 1.57] | 14.1 [13.8, 14.4] |
| motel_dispersed | scripted | scripted | 32 | 6 [2, 20] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.94 [0.65, 1.23] | 0.72 [0.53, 0.91] | 0.00 [0.00, 0.00] | 62.7 [35.2, 90.2] | 0.9 [0.5, 1.4] | 4.00 [3.62, 4.38] | 13.7 [13.1, 14.3] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | n/a | 9.3 [8.5, 10.2] | 1.54 [1.37, 1.70] | 14.1 [13.6, 14.6] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.12 [0.00, 0.25] | 1.06 [0.80, 1.32] | 0.50 [0.30, 0.70] | 0.00 [0.00, 0.00] | 75.2 [61.6, 88.8] | 1.7 [1.0, 2.3] | 4.24 [3.89, 4.60] | 14.9 [14.2, 15.6] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | n/a | 9.6 [8.6, 10.6] | 1.39 [1.33, 1.46] | 14.0 [13.8, 14.2] |
| motel_busy | scripted | scripted | 32 | 6 [2, 20] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.12 [0.00, 0.25] | 0.78 [0.47, 1.09] | 0.59 [0.41, 0.77] | 0.00 [0.00, 0.00] | 74.1 [48.8, 99.4] | 1.0 [0.6, 1.4] | 4.39 [4.09, 4.70] | 14.3 [13.6, 14.9] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | n/a | 9.6 [8.9, 10.3] | 1.73 [1.60, 1.87] | 14.0 [13.8, 14.3] |

## Previous bar: route-following motel officer before the rules of engagement (full report)

Scripted baselines (route-following motel officer) and legal-random officers, 2026-11-12, Linux cloud container (4 vCPU, CPU only). 12 jobs in 325 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Time to clear s | Officer rounds | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 34 [24, 47] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.44 [0.31, 0.56] | 0.02 [-0.02, 0.05] | 0.84 [0.61, 1.08] | 0.02 [-0.02, 0.05] | 16.2 [15.0, 17.4] | 1.0 [0.7, 1.2] | 0.13 [0.12, 0.14] | 18.7 [-8.8, 46.2] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | n/a | 5.9 [5.5, 6.3] | 0.14 [0.13, 0.14] | 30.0 [4.3, 55.6] |
| annex_escape | scripted | scripted | 64 | 23 [15, 35] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.30 [0.18, 0.41] | 0.05 [-0.01, 0.10] | 0.91 [0.67, 1.14] | 0.00 [0.00, 0.00] | 15.6 [14.2, 17.0] | 1.0 [0.7, 1.3] | 0.12 [0.11, 0.13] | 12.8 [-4.8, 30.5] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.22 [0.07, 0.36] | 0.03 [-0.01, 0.08] | n/a | 5.6 [5.2, 6.0] | 0.17 [0.16, 0.19] | 11.3 [-4.0, 26.7] |
| motel_standard | scripted | scripted | 32 | 28 [16, 45] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.62 [0.39, 0.86] | 0.22 [0.07, 0.37] | 0.97 [0.70, 1.24] | 0.00 [0.00, 0.00] | 68.1 [63.2, 73.1] | 2.6 [1.7, 3.5] | 5.87 [5.14, 6.60] | 451.2 [405.0, 497.4] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | n/a | 8.7 [7.9, 9.5] | 1.63 [1.47, 1.80] | 455.3 [420.0, 490.6] |
| motel_dispersed | scripted | scripted | 32 | 12 [5, 28] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.69 [0.47, 0.90] | 0.53 [0.31, 0.76] | 0.50 [0.30, 0.70] | 0.00 [0.00, 0.00] | 66.2 [57.6, 74.8] | 3.7 [3.0, 4.5] | 4.14 [3.67, 4.61] | 473.8 [429.6, 518.1] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | n/a | 9.3 [8.5, 10.2] | 1.71 [1.52, 1.91] | 445.4 [412.3, 478.4] |
| motel_mixed | scripted | scripted | 32 | 6 [2, 20] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.91 [0.68, 1.14] | 0.50 [0.30, 0.70] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | 69.1 [45.6, 92.7] | 3.5 [2.7, 4.4] | 4.14 [3.77, 4.50] | 432.7 [394.8, 470.5] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | n/a | 9.6 [8.6, 10.6] | 1.57 [1.50, 1.64] | 453.9 [406.4, 501.5] |
| motel_busy | scripted | scripted | 32 | 6 [2, 20] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.59 [0.39, 0.80] | 0.81 [0.56, 1.06] | 0.38 [0.16, 0.59] | 0.00 [0.00, 0.00] | 77.1 [24.1, 130.2] | 4.0 [3.3, 4.7] | 5.00 [4.43, 5.56] | 427.2 [391.6, 462.8] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | n/a | 9.6 [8.9, 10.3] | 1.97 [1.80, 2.13] | 430.1 [401.7, 458.6] |

## Older bar: pure scripted officer everywhere (before the motel officer)

Scripted baselines and legal-random officers, 2026-11-11, Linux cloud container (4 vCPU, CPU only). 12 jobs in 212 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Time to clear s | Officer rounds | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 34 [24, 47] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.44 [0.31, 0.56] | 0.02 [-0.02, 0.05] | 0.84 [0.61, 1.08] | 0.02 [-0.02, 0.05] | 16.2 [15.0, 17.4] | 1.0 [0.7, 1.2] | 0.14 [0.13, 0.15] | 39.3 [3.2, 75.5] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | n/a | 5.9 [5.5, 6.3] | 0.14 [0.14, 0.15] | 124.0 [73.8, 174.2] |
| annex_escape | scripted | scripted | 64 | 23 [15, 35] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.30 [0.18, 0.41] | 0.05 [-0.01, 0.10] | 0.91 [0.67, 1.14] | 0.00 [0.00, 0.00] | 15.6 [14.2, 17.0] | 1.0 [0.7, 1.3] | 0.14 [0.13, 0.14] | 124.5 [71.7, 177.2] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.22 [0.07, 0.36] | 0.03 [-0.01, 0.08] | n/a | 5.6 [5.2, 6.0] | 0.19 [0.17, 0.20] | 58.2 [24.1, 92.4] |
| motel_standard | scripted | scripted | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | n/a | 0.0 [0.0, 0.0] | 0.44 [0.41, 0.46] | 732.0 [649.9, 814.1] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | n/a | 8.7 [7.9, 9.5] | 1.81 [1.65, 1.97] | 574.8 [507.5, 642.2] |
| motel_dispersed | scripted | scripted | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | n/a | 0.0 [0.0, 0.0] | 0.55 [0.52, 0.58] | 593.1 [525.5, 660.6] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | n/a | 9.3 [8.5, 10.2] | 1.83 [1.63, 2.03] | 608.9 [535.8, 682.0] |
| motel_mixed | scripted | scripted | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | n/a | 0.0 [0.0, 0.0] | 0.59 [0.53, 0.65] | 626.9 [563.2, 690.6] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | n/a | 9.6 [8.6, 10.6] | 1.80 [1.72, 1.88] | 605.9 [533.3, 678.5] |
| motel_busy | scripted | scripted | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | n/a | 0.0 [0.0, 0.0] | 0.67 [0.63, 0.71] | 590.7 [512.7, 668.7] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | n/a | 9.6 [8.9, 10.3] | 2.17 [1.99, 2.35] | 474.1 [420.7, 527.6] |
