# B1.1: easier league opponents early (2026-10-22 to 23, B1 box)

League opponents play a uniformly random move with probability ε, falling linearly to 0 at half of
training (`env.expert_random_start`, `env.expert_random_until = 0.5`). Everything else is the run's
original recipe. Scores are win rate vs SyntheticRLV2 / TaurosV0, 144 games each (about ±4 points).

## gar_egg_zap, B1 recipe (25% league games), 700M

| Run | 350M | 700M |
|---|---|---|
| B1, ε 0 (baseline) | 50.7 / 52.1 | **61.8 / 58.0** |
| ε 0.5 → 0 | 46.5 / 41.3 | **61.8 / 55.6** |
| ε 0.9 → 0 | 45.1 / 45.1 | **55.6 / 56.9** |

Easy opponents early do not help a recipe that already learns: ε 0.5 ties B1, ε 0.9 is slightly
behind (within about 1–1.5 standard errors).

## Free-pick (drafts its own team), 94% league games, from scratch, ε 0.9 → 0, 2B

Same as free-pick v1 (`../20261022_freepick_league/`), which never learned (0% at every checkpoint).

| Checkpoint | 250M | 500M | 750M | 1.0B | 1.25B | 1.5B | 1.75B | 2.0B |
|---|---|---|---|---|---|---|---|---|
| SyntheticRLV2 | 1.4 | 7.6 | 17.4 | 18.1 | 35.1 | 33.0 | 33.7 | 36.1 |
| TaurosV0 | 4.2 | 6.9 | 16.7 | 31.6 | 37.2 | 42.4 | **49.7** | 42.7 |

**Easy opponents early made free-pick learn.** It converged on Alakazam, Articuno, Jolteon, Lapras,
Tauros, Zapdos (Jolteon in 99.8% of free drafts, this exact team in 66%).

## Free-pick continued +2B at ε 0 (from the 2B checkpoint above)

| +Checkpoint | 0.75B | 1.0B | 1.25B | 1.5B | 1.75B | 2.0B |
|---|---|---|---|---|---|---|
| SyntheticRLV2 | 36.5 | 33.3 | 30.2 | 26.4 | 41.3 | 42.7 |
| TaurosV0 | 37.5 | 41.3 | 35.8 | 34.4 | 39.2 | 34.4 |

**No gain from 2B more steps:** it stays in the 30s to low 40s.

Logs: `*/evals.log`; scripts `run_gar_eps.sh`, `queue.sh`, `continue_freepick.sh`.
