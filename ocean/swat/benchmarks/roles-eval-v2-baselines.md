# SWAT role task held-out evaluation, suite v2 (role contract v3)

## Scripted bar and random floor, suite v1 (role contract v2) and suite v2 (role contract v3)

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

## Current bar (full report)

Scripted baselines and legal-random officers, 2026-11-12, swat/roles-contract-v3, Linux cloud container (4 vCPU, CPU only).

Suite v2 plays the cases and seeds of suite v1. ROE violations count officer engagements with force on protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); the lethal column counts the second kind alone.

swat/roles-contract-v3: role contract v3, motel officer with the visible-weapon channel. 12 jobs in 280 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 19 [11, 30] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.33 [0.21, 0.45] | 0.00 [0.00, 0.00] | 0.97 [0.75, 1.19] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.3 [14.3, 18.3] | 1.0 [0.7, 1.2] | 0.11 [0.10, 0.11] | 0.3 [0.2, 0.3] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.00 [0.00, 0.00] | n/a | 5.9 [5.5, 6.3] | 0.12 [0.12, 0.13] | 0.3 [0.3, 0.3] |
| annex_escape | scripted | scripted | 64 | 23 [15, 35] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.33 [0.21, 0.45] | 0.03 [-0.01, 0.08] | 0.94 [0.71, 1.17] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 15.7 [13.7, 17.7] | 0.9 [0.6, 1.1] | 0.11 [0.10, 0.11] | 0.3 [0.2, 0.3] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.11 [0.01, 0.21] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 5.7 [5.3, 6.1] | 0.17 [0.16, 0.18] | 0.4 [0.4, 0.4] |
| motel_standard | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.31 [0.14, 0.48] | 0.31 [0.10, 0.53] | 1.12 [0.93, 1.32] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 74.4 [56.7, 92.1] | 0.7 [0.3, 1.1] | 5.17 [4.47, 5.86] | 15.0 [13.6, 16.5] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | 0.00 [0.00, 0.00] | n/a | 8.7 [7.9, 9.5] | 1.57 [1.44, 1.70] | 15.0 [14.1, 15.8] |
| motel_dispersed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.88 [0.57, 1.18] | 0.62 [0.43, 0.82] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 63.7 [57.5, 69.8] | 0.9 [0.5, 1.3] | 4.29 [3.91, 4.68] | 14.0 [13.3, 14.8] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.3 [8.5, 10.2] | 1.56 [1.38, 1.74] | 14.4 [13.8, 15.0] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.09 [-0.01, 0.20] | 0.91 [0.64, 1.17] | 0.59 [0.37, 0.82] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 78.0 [63.0, 92.9] | 1.2 [0.7, 1.8] | 4.39 [4.00, 4.78] | 14.8 [14.1, 15.5] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.6, 10.6] | 1.47 [1.40, 1.55] | 14.2 [13.6, 14.9] |
| motel_busy | scripted | scripted | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.66 [0.34, 0.97] | 0.72 [0.51, 0.93] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | n/a | 0.7 [0.3, 1.0] | 4.76 [4.38, 5.14] | 14.3 [13.6, 15.0] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.9, 10.3] | 1.81 [1.65, 1.97] | 14.6 [14.0, 15.1] |

## Previous bar

Suite v1 (role contract v2) and the older bars are in [roles-eval-v1-baselines.md](roles-eval-v1-baselines.md) and its JSON, unchanged. benchmarks/roles-eval-v2-baselines.json nests that report under "previous".
