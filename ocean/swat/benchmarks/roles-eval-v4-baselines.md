# SWAT role task held-out evaluation, suite v4 (role contract v5)

## Suite v4 bar

Suite v4 (swat/shout-hearing) keeps the eight v3 cases at their indices, with their seeds and metrics, and adds `annex_contact_reload` and `motel_contact_reload` (cases 8 and 9 of seed stream 1). In those, each episode the officers start with 1 to 3 rounds in the seated carbine magazine (drawn from the episode seed: `officer_magazine_min` 1, `officer_magazine` 3) and in sight of the nearest suspect (`roles_contact`), so a reload under fire is forced early. New metrics per officer and per suspect: reloads started while seeing an armed enemy, reloads disturbed (the reload-disturbed weapon event: damage or a sprint), deaths with a reload in progress, reloads completed.

Episodes and seeds: every case and matchup plays the same episodes; episode `e` of case `c` uses sim seed `swat_roles_eval_seed(1, c, e)` and layout seed `swat_roles_eval_layout(1, c, e)` (`roles_split.h`). The JSON has every episode's seeds. 960 episodes per matchup (64 per annex case, 32 per motel case); scripted and random-floor matchups, 1,920 episodes in all.

| Case | Episodes | First seed | First layout seed | Last seed |
| --- | --- | --- | --- | --- |
| annex | 64 | 3408825361 | 2343128055 | 3148733321 |
| annex_escape | 64 | 3156819777 | 4081419847 | 3143573895 |
| motel_standard | 32 | 4149764153 | 3261218430 | 3117285047 |
| motel_dispersed | 32 | 2151910703 | 2675501114 | 2314911675 |
| motel_mixed | 32 | 3635162025 | 3569116897 | 2344372367 |
| motel_busy | 32 | 3496718249 | 3934472381 | 4205306189 |
| annex_low_ammo | 64 | 3697401317 | 3312513439 | 3354352895 |
| motel_low_ammo | 32 | 3123460285 | 3880732111 | 4123813823 |
| annex_contact_reload | 64 | 2188822409 | 3017898592 | 2658524033 |
| motel_contact_reload | 32 | 2571801215 | 3717762174 | 2989563247 |

Findings on the contact reload cases (scripted officers):

- Annex: 0.77 reloads per officer begin with an armed suspect in view, 1.16 are disturbed (wounds; the scripted officer does not take cover first), 0.48 deaths mid-reload per officer and only 0.30 reloads complete. Completion is 17% (the full-magazine annex case: 33%).
- Motel: officers start inside the suspect's room. 0.20 reloads per officer begin with an armed suspect in view; every officer completes one reload, none is disturbed and none dies reloading. Completion 53% with 0 lethal ROE violations.
- Scripted suspects never reload in any case (all suspect columns are 0): they rarely empty a magazine.
- The random floor starts about one reload under fire per officer in the annex case and completes none.

## Full report

Suite v4 plays the cases and seeds of suite v1, plus two low-ammunition cases in which each officer starts with 3 rounds in the seated primary magazine and full spares. ROE violations count officer engagements with force on protected people or officers or lethal force on a person who was not a perceived lethal threat (roe.h); the lethal column counts the second kind alone. Reloads completed, reloads interrupted and dry decisions (begun alive with an empty weapon, not reloading, while it had a spare magazine) are per officer.

Suite v4 adds two contact reload cases: the officers start with 1 to 3 rounds in the seated primary magazine (drawn per episode) and in sight of the nearest suspect. The reload pressure table counts, per officer and per suspect, reloads started while seeing an armed enemy, reloads broken by damage or a sprint, deaths with a reload in progress, and reloads completed.

Scripted baselines and legal-random officers, 2026-11-13, swat/shout-hearing (heard commands, role contract v5), Linux cloud container (4 vCPU, CPU only). 20 jobs in 470 s wall on 4 threads

Means per episode with 95% confidence intervals (Wilson for rates, Student t for means). Time to clear averages completed episodes only. Sim cost is wall clock.

| Case | Matchup | Policy | N | Completion % | Civ. harmed (any) % | Civ. harmed | Suspects killed | Suspects arrested | Officer casualties | ROE violations | Lethal violations | Time to clear s | Officer rounds | Reloads | Reloads interrupted | Dry decisions | Sim ms/decision | Reset ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 33 [23, 45] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.38 [0.25, 0.50] | 0.03 [-0.01, 0.08] | 0.84 [0.62, 1.07] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.8 [15.0, 18.6] | 1.0 [0.8, 1.3] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.16 [0.15, 0.17] | 0.3 [0.3, 0.4] |
| annex | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.02, 0.12] | 0.00 [0.00, 0.00] | n/a | 5.9 [5.5, 6.3] | 0.00 [0.00, 0.00] | 14.47 [13.77, 15.17] | 7.51 [2.96, 12.05] | 0.19 [0.18, 0.19] | 0.4 [0.4, 0.5] |
| annex_escape | scripted | scripted | 64 | 34 [24, 47] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.41 [0.28, 0.53] | 0.03 [-0.01, 0.08] | 0.73 [0.52, 0.95] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.7 [14.9, 18.5] | 1.2 [1.0, 1.5] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.16 [0.15, 0.17] | 0.3 [0.3, 0.3] |
| annex_escape | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.19 [0.05, 0.33] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 5.7 [5.4, 6.1] | 0.00 [0.00, 0.00] | 14.20 [13.20, 15.19] | 9.62 [4.85, 14.40] | 0.24 [0.22, 0.25] | 0.5 [0.5, 0.5] |
| motel_standard | scripted | scripted | 32 | 31 [18, 49] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.28 [0.12, 0.45] | 0.75 [0.46, 1.04] | 0.88 [0.60, 1.15] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 67.5 [64.7, 70.3] | 1.0 [0.5, 1.5] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 5.07 [4.55, 5.60] | 17.7 [17.3, 18.1] |
| motel_standard | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.44 [0.23, 0.64] | 0.00 [0.00, 0.00] | n/a | 8.8 [8.0, 9.7] | 0.00 [0.00, 0.00] | 49.27 [47.10, 51.43] | 151.38 [99.18, 203.57] | 2.12 [1.94, 2.30] | 18.5 [17.8, 19.2] |
| motel_dispersed | scripted | scripted | 32 | 19 [9, 35] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.09 [0.80, 1.39] | 0.59 [0.39, 0.80] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 70.7 [66.9, 74.4] | 0.9 [0.5, 1.3] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 3.04 [2.73, 3.35] | 16.8 [16.4, 17.2] |
| motel_dispersed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.4 [8.5, 10.3] | 0.00 [0.00, 0.00] | 49.53 [47.95, 51.11] | 229.88 [179.96, 279.79] | 2.22 [1.96, 2.48] | 19.2 [18.1, 20.3] |
| motel_mixed | scripted | scripted | 32 | 9 [3, 24] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.19 [0.04, 0.33] | 1.00 [0.74, 1.26] | 0.66 [0.40, 0.91] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 80.1 [63.8, 96.4] | 1.2 [0.7, 1.8] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 4.32 [3.58, 5.07] | 18.6 [17.9, 19.3] |
| motel_mixed | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.41 [0.20, 0.61] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.6, 10.7] | 0.02 [-0.02, 0.05] | 48.34 [46.50, 50.19] | 204.86 [142.31, 267.41] | 2.13 [1.99, 2.27] | 18.8 [18.2, 19.4] |
| motel_busy | scripted | scripted | 32 | 12 [5, 28] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.12 [0.00, 0.25] | 0.69 [0.39, 0.98] | 0.66 [0.46, 0.85] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 68.6 [64.2, 73.1] | 0.7 [0.2, 1.1] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 4.23 [3.71, 4.75] | 17.4 [16.9, 17.8] |
| motel_busy | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.41 [0.23, 0.59] | 0.00 [0.00, 0.00] | n/a | 9.6 [8.9, 10.4] | 0.00 [0.00, 0.00] | 47.89 [45.84, 49.94] | 191.45 [134.54, 248.37] | 2.70 [2.39, 3.01] | 19.1 [18.2, 20.0] |
| annex_low_ammo | scripted | scripted | 64 | 41 [29, 53] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.50 [0.37, 0.63] | 0.02 [-0.02, 0.05] | 0.75 [0.51, 0.99] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 16.4 [15.3, 17.5] | 1.1 [0.9, 1.4] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.17 [0.16, 0.19] | 0.4 [0.3, 0.4] |
| annex_low_ammo | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.05 [-0.01, 0.10] | 0.00 [0.00, 0.00] | n/a | 4.5 [4.1, 4.9] | 0.00 [0.00, 0.00] | 15.38 [14.63, 16.12] | 23.12 [13.90, 32.35] | 0.18 [0.18, 0.19] | 0.5 [0.4, 0.5] |
| motel_low_ammo | scripted | scripted | 32 | 50 [34, 66] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.19 [0.04, 0.33] | 1.06 [0.75, 1.38] | 0.72 [0.40, 1.04] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 74.8 [71.4, 78.1] | 1.1 [0.7, 1.6] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 5.07 [4.55, 5.59] | 17.1 [16.4, 17.8] |
| motel_low_ammo | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.06 [-0.03, 0.15] | 0.12 [0.00, 0.25] | 0.00 [0.00, 0.00] | n/a | 7.1 [6.4, 7.8] | 0.00 [0.00, 0.00] | 50.56 [48.64, 52.49] | 227.16 [172.76, 281.55] | 2.32 [2.10, 2.54] | 19.6 [18.3, 21.0] |
| annex_contact_reload | scripted | scripted | 64 | 17 [10, 28] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.34 [0.22, 0.46] | 0.00 [0.00, 0.00] | 1.05 [0.81, 1.28] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.5 [1.4, 1.6] | 2.0 [2.0, 2.0] | 0.30 [0.20, 0.41] | 1.16 [0.89, 1.44] | 0.00 [0.00, 0.00] | 0.11 [0.10, 0.11] | 0.3 [0.3, 0.4] |
| annex_contact_reload | policy_officers | random | 64 | 0 [0, 6] | 0 [0, 6] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.91 [1.80, 2.01] | 0.02 [-0.02, 0.05] | 0.00 [0.00, 0.00] | n/a | 1.0 [0.8, 1.3] | 0.00 [0.00, 0.00] | 2.12 [1.31, 2.92] | 0.03 [-0.02, 0.08] | 0.10 [0.10, 0.10] | 0.3 [0.3, 0.3] |
| motel_contact_reload | scripted | scripted | 32 | 53 [36, 69] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.56 [0.32, 0.80] | 1.41 [1.15, 1.66] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 77.6 [74.8, 80.3] | 2.5 [1.9, 3.1] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.20 [0.03, 0.38] | 5.40 [4.98, 5.83] | 17.8 [16.9, 18.7] |
| motel_contact_reload | policy_officers | random | 32 | 0 [0, 11] | 0 [0, 11] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.00 [0.65, 1.35] | 0.03 [-0.03, 0.09] | 0.00 [0.00, 0.00] | n/a | 3.9 [2.8, 5.0] | 0.00 [0.00, 0.00] | 27.19 [18.22, 36.16] | 148.77 [77.09, 220.44] | 6.45 [3.40, 9.50] | 18.5 [17.8, 19.3] |

## Reloads under pressure

| Case | Matchup | Policy | N | Officer reloads under fire | Officer reloads disturbed | Officer deaths reloading | Officer reloads done | Suspect reloads under fire | Suspect reloads disturbed | Suspect deaths reloading | Suspect reloads done |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| annex | scripted | scripted | 64 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| annex | policy_officers | random | 64 | 0.01 [-0.01, 0.02] | 0.12 [0.06, 0.19] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| annex_escape | scripted | scripted | 64 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| annex_escape | policy_officers | random | 64 | 0.06 [-0.01, 0.13] | 0.19 [0.12, 0.26] | 0.01 [-0.01, 0.02] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_standard | scripted | scripted | 32 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_standard | policy_officers | random | 32 | 0.00 [0.00, 0.00] | 0.36 [0.21, 0.51] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_dispersed | scripted | scripted | 32 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_dispersed | policy_officers | random | 32 | 0.05 [-0.05, 0.14] | 0.41 [0.26, 0.55] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_mixed | scripted | scripted | 32 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_mixed | policy_officers | random | 32 | 0.00 [0.00, 0.00] | 0.50 [0.33, 0.67] | 0.00 [0.00, 0.00] | 0.02 [-0.02, 0.05] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_busy | scripted | scripted | 32 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_busy | policy_officers | random | 32 | 0.00 [0.00, 0.00] | 0.45 [0.26, 0.64] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| annex_low_ammo | scripted | scripted | 64 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| annex_low_ammo | policy_officers | random | 64 | 0.00 [0.00, 0.00] | 0.10 [0.05, 0.16] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_low_ammo | scripted | scripted | 32 | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_low_ammo | policy_officers | random | 32 | 0.02 [-0.02, 0.05] | 0.44 [0.27, 0.61] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| annex_contact_reload | scripted | scripted | 64 | 0.77 [0.59, 0.96] | 1.16 [0.89, 1.44] | 0.48 [0.37, 0.60] | 0.30 [0.20, 0.41] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| annex_contact_reload | policy_officers | random | 64 | 0.97 [0.81, 1.12] | 0.28 [0.18, 0.38] | 0.06 [0.02, 0.10] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_contact_reload | scripted | scripted | 32 | 0.20 [0.07, 0.33] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 1.00 [1.00, 1.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
| motel_contact_reload | policy_officers | random | 32 | 0.31 [0.17, 0.46] | 0.27 [0.13, 0.40] | 0.02 [-0.02, 0.05] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] | 0.00 [0.00, 0.00] |
