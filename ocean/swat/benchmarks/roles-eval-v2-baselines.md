# SWAT role task held-out evaluation, suite v2 (role contract v4)

## Review bar: swat/roe-hands after the review fixes

Two review fixes on swat/roe-hands. Ray channel 11 now reads a weapon (1) or empty hands (0) only when the observer, whatever its role, could judge the hands within the ROE officer's sight limits (`SWAT_ROE_OFFICER_RANGE` 24 m, `SWAT_ROE_HALF_FOV` 60 degrees of the aim); otherwise 0.5. Before, it judged 30 m all round, so a ray could show a weapon no ROE observer had seen. And a shooter whose hands are seen empty is no longer hostile-firing for the rest of the 2 s window. Suite, cases, seeds and metrics are unchanged. "previous" is swat/roe-hands before these fixes, the bar below this section in the earlier report and nested under "previous" in the JSON.

| Case | Scripted completion % previous | Scripted completion % review | Scripted lethal violations previous / review | Scripted ROE violations previous / review | Suspects arrested previous / review | Suspects killed previous / review | Officer casualties previous / review | Officer rounds previous / review | Random floor completion % previous / review |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | 19 [11, 30] | 22 [14, 33] | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.03 | 0.33 / 0.34 | 0.97 / 0.89 | 1.0 / 1.0 | 0 / 0 |
| annex_escape | 23 [15, 35] | 30 [20, 42] | 0.00 / 0.00 | 0.00 / 0.00 | 0.03 / 0.05 | 0.33 / 0.36 | 0.92 / 0.86 | 0.9 / 1.0 | 0 / 0 |
| motel_standard | 9 [3, 24] | 9 [3, 24] | 0.00 / 0.00 | 0.00 / 0.00 | 0.31 / 0.31 | 0.31 / 0.31 | 1.12 / 1.12 | 0.7 / 0.7 | 0 / 0 |
| motel_dispersed | 9 [3, 24] | 9 [3, 24] | 0.00 / 0.00 | 0.00 / 0.00 | 0.97 / 0.97 | 0.00 / 0.00 | 0.66 / 0.66 | 1.0 / 1.0 | 0 / 0 |
| motel_mixed | 9 [3, 24] | 9 [3, 24] | 0.00 / 0.00 | 0.00 / 0.00 | 0.97 / 0.94 | 0.09 / 0.09 | 0.56 / 0.56 | 1.3 / 1.3 | 0 / 0 |
| motel_busy | 3 [1, 16] | 3 [1, 16] | 0.00 / 0.00 | 0.00 / 0.00 | 0.66 / 0.66 | 0.09 / 0.09 | 0.75 / 0.75 | 0.7 / 0.7 | 0 / 0 |

- Lethal ROE violations stay 0 in every case and matchup, for the scripted officer and the random floor.
- Scripted episodes that differ from the previous bar in any recorded field (wall clock aside): annex 21 of 64 (13 with a changed outcome), annex_escape 22 of 64 (16), motel_standard 3, motel_dispersed 3, motel_mixed 5 (one with an arrest fewer) and motel_busy 1 of 32. The random floor replays the previous bar exactly in every case.
- The annex changes come from channel 11: the scripted baselines (roles_baselines.h: armed above 0.75, hands hidden 0.25-0.75) now read a person beyond 24 m or more than 60 degrees off the observer's aim as hands hidden rather than armed or unarmed, and the baselines' observation-hash coins see the new values. The annex has no tactical rules, so the ROE fix cannot act there. Annex completion rises from 19 to 22% and annex_escape from 23 to 30%, both inside the previous 95% intervals.
- On the motel cases completion is unchanged (9/9/9/3%); the changed episodes differ only in length, except one motel_mixed episode with one arrest fewer.

## Current bar (full report)

Suite v2 plays the cases and seeds of suite v1. ROE violations count officer engagements with force on protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); the lethal column counts the second kind alone.

Scripted baselines and legal-random officers, 2026-11-13, swat/roe-hands after the review fixes, Linux cloud container (4 vCPU, CPU only). 12 jobs in 376 s wall on 4 threads.

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 22 [14, 33] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.34 [0.22, 0.46] | 0.03 [-0.01, 0.08] | 0.89 [0.68, 1.10] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 15.7 [14.6, 16.8] | 1.0 [0.7, 1.2] | 0.15 [0.14, 0.16] | 0.3 [0.3, 0.3] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.00 [0.00, 0.00] | n/a | 5.9 [5.5, 6.3] | 0.17 [0.17, 0.18] | 0.4 [0.4, 0.4] |
| annex_escape | scripted | scripted | 64 | 30 [20, 42] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.36 [0.24, 0.48] | 0.05 [-0.01, 0.10] | 0.86 [0.62, 1.09] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 15.3 [14.3, 16.3] | 1.0 [0.7, 1.2] | 0.15 [0.14, 0.16] | 0.3 [0.3, 0.3] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.12 [0.02, 0.23] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 5.7 [5.3, 6.1] | 0.22 [0.20, 0.24] | 0.5 [0.4, 0.5] |
| motel_standard | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.31 [0.14, 0.48] | 0.31 [0.10, 0.53] | 1.12 [0.93, 1.32] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 77.6 [73.0, 82.2] | 0.7 [0.3, 1.1] | 6.73 [5.79, 7.67] | 19.7 [17.9, 21.5] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | 0.00 [0.00, 0.00] | n/a | 8.8 [8.0, 9.7] | 1.98 [1.81, 2.15] | 18.2 [17.7, 18.6] |
| motel_dispersed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.97 [0.67, 1.27] | 0.66 [0.46, 0.85] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 64.1 [60.8, 67.4] | 1.0 [0.6, 1.4] | 5.54 [5.04, 6.05] | 17.9 [17.0, 18.9] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.4 [8.5, 10.3] | 2.10 [1.86, 2.34] | 18.0 [17.5, 18.6] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.94 [0.66, 1.21] | 0.56 [0.34, 0.79] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 82.5 [74.5, 90.5] | 1.3 [0.7, 2.0] | 5.94 [5.33, 6.55] | 19.0 [18.1, 19.9] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.6, 10.7] | 2.03 [1.90, 2.16] | 18.1 [17.5, 18.7] |
| motel_busy | scripted | scripted | 32 | 3 [1, 16] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.66 [0.34, 0.97] | 0.75 [0.55, 0.95] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 68.5 [68.5, 68.5] | 0.7 [0.3, 1.1] | 6.39 [5.88, 6.90] | 18.5 [17.7, 19.3] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.9, 10.4] | 2.59 [2.26, 2.92] | 18.9 [18.0, 19.7] |

## Previous bar: swat/roe-hands before the review fixes (role contract v4)

### Hands bar: swat/roe-hands next to swat/integration4

swat/roe-hands makes the head stop deciding sight. A person is seen when any of three body probes (head, chest, hips) reaches them, and whether they are armed is decided by the hands: a weapon seen in hand, both hands seen empty, or hands hidden (roe.h, ARCHITECTURE.md). Role contract v4 puts that reading in ray channel 11 (weapon 1, hidden 0.5, empty 0), and the scripted motel officer fires only when its reticle reads 1; its edge-of-cover rule is gone, and it never repeats a challenge at a suspect whose hands it cannot see. Suite, cases, seeds and metrics are unchanged. "previous" is swat/integration4 (role contract v3), the bar below this section in the earlier report and nested under "previous" in the JSON.

| Case | Scripted completion % previous | Scripted completion % hands | Scripted lethal violations previous / hands | Scripted ROE violations previous / hands | Suspects arrested previous / hands | Suspects killed previous / hands | Officer casualties previous / hands | Officer rounds previous / hands | Random floor completion % previous / hands |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | 19 [11, 30] | 19 [11, 30] | 0.00 / 0.00 | 0.00 / 0.00 | 0.00 / 0.00 | 0.33 / 0.33 | 0.97 / 0.97 | 1.0 / 1.0 | 0 / 0 |
| annex_escape | 23 [15, 35] | 23 [15, 35] | 0.00 / 0.00 | 0.00 / 0.00 | 0.03 / 0.03 | 0.33 / 0.33 | 0.94 / 0.92 | 0.9 / 0.9 | 0 / 0 |
| motel_standard | 9 [3, 24] | 9 [3, 24] | 0.00 / 0.00 | 0.00 / 0.00 | 0.31 / 0.31 | 0.31 / 0.31 | 1.12 / 1.12 | 0.7 / 0.7 | 0 / 0 |
| motel_dispersed | 9 [3, 24] | 9 [3, 24] | 0.00 / 0.00 | 0.00 / 0.00 | 0.88 / 0.97 | 0.00 / 0.00 | 0.62 / 0.66 | 0.9 / 1.0 | 0 / 0 |
| motel_mixed | 9 [3, 24] | 9 [3, 24] | 0.00 / 0.00 | 0.00 / 0.00 | 0.91 / 0.97 | 0.09 / 0.09 | 0.59 / 0.56 | 1.2 / 1.3 | 0 / 0 |
| motel_busy | 0 [0, 11] | 3 [1, 16] | 0.00 / 0.00 | 0.00 / 0.00 | 0.66 / 0.66 | 0.06 / 0.09 | 0.72 / 0.75 | 0.7 / 0.7 | 0 / 0 |

- Lethal ROE violations stay 0 in every case and matchup, for the scripted officer and the random floor.
- Scripted episodes that differ from the previous bar in any recorded field (wall clock aside): annex 0 of 64, annex_escape 3 of 64, motel_standard 1, motel_dispersed 3, motel_mixed 5 and motel_busy 5 of 32. The random floor differs in 1 annex_escape and 1 motel_busy episode. The annex has no tactical rules, so its changes come from the three-probe sight the scripted suspects use to find officers (sim.c swat_sim_bot_inputs) and from the baselines' observation-hash coins, which see the new weapon channel values.
- On the motel cases the officer now holds fire at hidden hands and is no longer held back at the edge of cover when the weapon in hand is in view. motel_busy completes 1 of 32 (previously 0), motel_dispersed and motel_mixed arrest slightly more suspects, all within the intervals.

### Full report, swat/roe-hands before the review fixes

Suite v2 plays the cases and seeds of suite v1. ROE violations count officer engagements with force on protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); the lethal column counts the second kind alone.

Scripted baselines and legal-random officers, 2026-11-13, swat/roe-hands, Linux cloud container (4 vCPU, CPU only). 12 jobs in 372 s wall on 4 threads.

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 19 [11, 30] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.33 [0.21, 0.45] | 0.00 [0.00, 0.00] | 0.97 [0.75, 1.19] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.3 [14.3, 18.3] | 1.0 [0.7, 1.2] | 0.16 [0.15, 0.17] | 0.3 [0.3, 0.4] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.00 [0.00, 0.00] | n/a | 5.9 [5.5, 6.3] | 0.17 [0.17, 0.18] | 0.4 [0.4, 0.4] |
| annex_escape | scripted | scripted | 64 | 23 [15, 35] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.33 [0.21, 0.45] | 0.03 [-0.01, 0.08] | 0.92 [0.69, 1.15] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 15.7 [13.7, 17.7] | 0.9 [0.6, 1.1] | 0.15 [0.14, 0.16] | 0.3 [0.3, 0.3] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.12 [0.02, 0.23] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 5.7 [5.3, 6.1] | 0.23 [0.21, 0.24] | 0.5 [0.5, 0.5] |
| motel_standard | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.31 [0.14, 0.48] | 0.31 [0.10, 0.53] | 1.12 [0.93, 1.32] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 74.4 [56.7, 92.1] | 0.7 [0.3, 1.1] | 6.70 [5.77, 7.64] | 18.3 [16.9, 19.6] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | 0.00 [0.00, 0.00] | n/a | 8.8 [8.0, 9.7] | 2.00 [1.83, 2.17] | 18.0 [17.8, 18.3] |
| motel_dispersed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.97 [0.67, 1.27] | 0.66 [0.46, 0.85] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 63.7 [57.5, 69.8] | 1.0 [0.6, 1.4] | 5.50 [5.02, 5.98] | 18.2 [17.3, 19.0] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.4 [8.5, 10.3] | 2.11 [1.87, 2.36] | 18.5 [17.8, 19.2] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.97 [0.70, 1.24] | 0.56 [0.34, 0.79] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 74.6 [65.8, 83.4] | 1.3 [0.7, 2.0] | 5.94 [5.34, 6.54] | 19.0 [18.1, 19.8] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.6, 10.7] | 2.01 [1.88, 2.14] | 17.9 [17.4, 18.3] |
| motel_busy | scripted | scripted | 32 | 3 [1, 16] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.66 [0.34, 0.97] | 0.75 [0.55, 0.95] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 68.1 [68.1, 68.1] | 0.7 [0.3, 1.1] | 6.37 [5.87, 6.87] | 18.5 [17.7, 19.4] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.9, 10.4] | 2.54 [2.26, 2.83] | 18.9 [18.3, 19.4] |

## Previous bar: swat/integration4 (role contract v3)

### Merged bar: swat/integration4 next to swat/roles-contract-v3

swat/integration4 merges swat/reload-muzzle (damage, sprint, cancel, a weapon switch and death interrupt a reload; the drawn weapon is pulled back at cover) into swat/roles-contract-v3, and the scripted upkeep now holds a reload to the end, so a sprint cannot cut it. Suite, cases and seeds are unchanged.

| Case | Scripted completion % v3 | Scripted completion % merged | Random floor completion % v3 / merged | Scripted lethal violations v3 / merged | Scripted ROE violations v3 / merged | Random floor officer rounds v3 / merged |
| --- | --- | --- | --- | --- | --- | --- |
| annex | 19 [11, 30] | 19 [11, 30] | 0 / 0 | 0.00 / 0.00 | 0.00 / 0.00 | 5.92 / 5.92 |
| annex_escape | 23 [15, 35] | 23 [15, 35] | 0 / 0 | 0.00 / 0.00 | 0.00 / 0.00 | 5.70 / 5.72 |
| motel_standard | 9 [3, 24] | 9 [3, 24] | 0 / 0 | 0.00 / 0.00 | 0.00 / 0.00 | 8.69 / 8.84 |
| motel_dispersed | 9 [3, 24] | 9 [3, 24] | 0 / 0 | 0.00 / 0.00 | 0.00 / 0.00 | 9.34 / 9.41 |
| motel_mixed | 9 [3, 24] | 9 [3, 24] | 0 / 0 | 0.00 / 0.00 | 0.00 / 0.00 | 9.59 / 9.63 |
| motel_busy | 0 [0, 11] | 0 [0, 11] | 0 / 0 | 0.00 / 0.00 | 0.00 / 0.00 | 9.59 / 9.63 |

- Every scripted episode (256 of 256) replays roles-contract-v3 exactly, field for field apart from wall clock. A scratch probe counted no reload by a scripted officer in any case (an officer fires at most a few rounds of a full magazine), so no officer reload starts and none can be interrupted. The drawn-weapon clamp moves no shot (it only moves the drawn pose; muzzle_blocked and the shot origin are as before).
- The random floor changes in 12 of 256 episodes (annex_escape 2, motel_standard 4, motel_dispersed 2, motel_mixed 3, motel_busy 1). Random officers press reload, sprint and take hits, and an interrupted reload frees the weapon early: a chambered round, or a seated magazine, can fire again at once instead of after the full reload. In 11 of those episodes an officer fires one or two rounds more (one fires one fewer), and in one annex_escape episode a suspect fires once more and the episode runs 2 ticks longer. No outcome, casualty, arrest or ROE count changed.
- Lethal ROE violations stay 0 in every case and matchup.
- With each motel officer left one round at tick 30 (a scratch probe, 32 motel_standard and 32 motel_mixed episodes), the officer without the reload hold restarted its reload 1,840 times for 128 needed reloads, 1,717 of them cut by its own sprint between waypoints. With the hold it starts 128, no sprint cuts one, and the one reload a wound interrupted restarted.

### Scripted bar and random floor, suite v1 (role contract v2) and suite v2 (role contract v3)

Suite v2 plays exactly the cases and seeds of suite v1 (seed stream 1). It changes only the ROE metrics: ROE violations now count officer engagements with force on protected people or officers *or* lethal force on a person who was not a perceived lethal threat under roe.h, and lethal violations (debrief.lethal_violations) are reported on their own. v1 ROE violations are v2 protected_violations. "v1" is the previous bar, benchmarks/roles-eval-v1-baselines.md (swat/integration3, role contract v2, 2026-11-12); its lethal ROE violations come from the probe in that report, since suite v1 did not record them. "v2" is swat/roles-contract-v3: role contract v3 adds the visible-weapon ray channel and the motel officer uses it (no shot at a person whose reticle ray shows no weapon, an armed suspect challenged before an empty-handed one, and no challenge shot past the edge of nearby cover).

| Case | Scripted completion % v1 | Scripted completion % v2 | Random floor completion % v1 | Random floor completion % v2 | Scripted ROE violations v1 | Scripted ROE violations v2 | Scripted lethal violations v1 / v2 | Random floor ROE violations v1 | Random floor ROE violations v2 (lethal) | Suspects arrested v1 / v2 | Suspects killed v1 / v2 | Officer casualties v1 / v2 | Officer rounds v1 / v2 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | 34 [24, 47] | 19 [11, 30] | 0 [0, 6] | 0 [0, 6] | 0.02 [-0.02, 0.05] | 0.00 [0.00, 0.00] | n/a / 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.05 [-0.02, 0.12] (0.00) | 0.02 / 0.00 | 0.44 / 0.33 | 0.84 / 0.97 | 1.0 / 1.0 |
| annex_escape | 23 [15, 35] | 23 [15, 35] | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | n/a / 0.00 [0.00, 0.00] | 0.03 [-0.01, 0.08] | 0.05 [-0.01, 0.10] (0.00) | 0.05 / 0.03 | 0.30 / 0.33 | 0.91 / 0.94 | 1.0 / 0.9 |
| motel_standard | 9 [3, 24] | 9 [3, 24] | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 / 0.00 [0.00, 0.00] | 0.44 [0.23, 0.64] | 0.44 [0.23, 0.64] (0.00) | 0.44 / 0.31 | 0.25 / 0.31 | 1.12 / 1.12 | 0.9 / 0.7 |
| motel_dispersed | 6 [2, 20] | 9 [3, 24] | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 / 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.41 [0.23, 0.59] (0.00) | 0.94 / 0.88 | 0.00 / 0.00 | 0.72 / 0.62 | 0.9 / 0.9 |
| motel_mixed | 9 [3, 24] | 9 [3, 24] | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 / 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.41 [0.20, 0.61] (0.00) | 1.06 / 0.91 | 0.12 / 0.09 | 0.50 / 0.59 | 1.7 / 1.2 |
| motel_busy | 6 [2, 20] | 0 [0, 11] | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 / 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.41 [0.23, 0.59] (0.00) | 0.78 / 0.66 | 0.12 / 0.06 | 0.59 / 0.72 | 1.0 / 0.7 |

Lethal violations per episode on the four motel cases went from 0 / 0 / 0.06 / 0 to 0 / 0 / 0 / 0 (standard, dispersed, mixed, busy). With sim-driven occupants (roles_suspects=0, roles_civilians=0, which can drop the weapon and feint) a probe over the same 128 motel episodes found 0 lethal violations, against 7 for the v1 officer.

Why the numbers moved:

- Per motel case 7 (standard), 20 (dispersed), 20 (mixed) and 21 (busy) of 32 scripted episodes replay v1 exactly. The rest differ because the officer now acts differently, or because the scripted roles' coin draws changed (next point). The two v1 lethal violations (motel_mixed episodes 24 and 27) were shots at an armed suspect who had been challenged but whom no ROE observer had seen: the reticle reached the body past the edge of a wall or door about a metre away, while the eye-to-head line roe.h uses was blocked. The officer now holds challenge fire in that position. motel_busy completion fell from 2 of 32 to 0 of 32 and motel_dispersed rose from 2 to 3 of 32, within the intervals.
- The annex has no tactical rules, so the weapon channel changes no annex decision. The annex scripted bar still moved (34% to 19%), because the scripted baselines draw their per-decision coins from a hash of the whole observation, and the observation now has 9 more floats. A probe that leaves the weapon channel out of that hash replays all 128 annex and annex_escape episodes exactly as in v1. The policy is unchanged; the two figures are two draws of its coins, and together give 34 of 128 (27%) on the annex. annex_escape happens to complete 15 of 64 both times.
- The random floor plays the same random officer actions as in v1 (the masks and the action stream are unchanged). Its annex, motel_standard, motel_dispersed and motel_mixed episodes replay v1 exactly; on annex_escape (51 of 64) and motel_busy (31 of 32) the scripted occupants' coin draws changed some episodes, and annex_escape's ROE count moved within its interval. The random floor's ROE violations are protected force only (random officers hit each other); it has no lethal violations.

### Full report, swat/integration4

Scripted baselines and legal-random officers, 2026-11-12, swat/integration4, Linux cloud container (4 vCPU, CPU only).

Suite v2 plays the cases and seeds of suite v1. ROE violations count officer engagements with force on protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); the lethal column counts the second kind alone.

swat/integration4: swat/reload-muzzle (reload interrupts, drawn weapon pulled back at cover) merged with swat/roles-contract-v3 (role contract v3, annex contract v4, suite v2), scripted agents hold a reload to the end. 12 jobs in 335 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 19 [11, 30] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.33 [0.21, 0.45] | 0.00 [0.00, 0.00] | 0.97 [0.75, 1.19] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.3 [14.3, 18.3] | 1.0 [0.7, 1.2] | 0.13 [0.12, 0.14] | 0.3 [0.3, 0.3] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.00 [0.00, 0.00] | n/a | 5.9 [5.5, 6.3] | 0.15 [0.15, 0.16] | 0.4 [0.4, 0.4] |
| annex_escape | scripted | scripted | 64 | 23 [15, 35] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.33 [0.21, 0.45] | 0.03 [-0.01, 0.08] | 0.94 [0.71, 1.17] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 15.7 [13.7, 17.7] | 0.9 [0.6, 1.1] | 0.11 [0.11, 0.12] | 0.3 [0.3, 0.3] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.11 [0.01, 0.21] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 5.7 [5.3, 6.1] | 0.19 [0.17, 0.21] | 0.5 [0.4, 0.5] |
| motel_standard | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.31 [0.14, 0.48] | 0.31 [0.10, 0.53] | 1.12 [0.93, 1.32] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 74.4 [56.7, 92.1] | 0.7 [0.3, 1.1] | 6.22 [5.35, 7.10] | 18.1 [16.0, 20.2] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | 0.00 [0.00, 0.00] | n/a | 8.8 [8.0, 9.7] | 1.83 [1.68, 1.99] | 17.4 [16.5, 18.4] |
| motel_dispersed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.88 [0.57, 1.18] | 0.62 [0.43, 0.82] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 63.7 [57.5, 69.8] | 0.9 [0.5, 1.3] | 5.10 [4.63, 5.56] | 16.6 [15.8, 17.5] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.4 [8.5, 10.3] | 1.82 [1.60, 2.03] | 16.3 [15.3, 17.4] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.91 [0.64, 1.17] | 0.59 [0.37, 0.82] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 78.0 [63.0, 92.9] | 1.2 [0.7, 1.8] | 5.32 [4.80, 5.83] | 17.3 [16.4, 18.1] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.6, 10.7] | 1.84 [1.72, 1.96] | 17.0 [16.0, 18.0] |
| motel_busy | scripted | scripted | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.66 [0.34, 0.97] | 0.72 [0.51, 0.93] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | n/a | 0.7 [0.3, 1.0] | 5.77 [5.30, 6.24] | 16.9 [15.9, 17.8] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.9, 10.4] | 2.13 [1.87, 2.39] | 16.9 [15.8, 18.0] |

### Older bars

Suite v1 (role contract v2) and the older bars are in [roles-eval-v1-baselines.md](roles-eval-v1-baselines.md) and its JSON, unchanged. benchmarks/roles-eval-v2-baselines.json nests that report under "previous".
