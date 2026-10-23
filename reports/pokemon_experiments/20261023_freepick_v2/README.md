# Free-pick v2: forced cores annealed off (2026-10-23, window box)

Free-pick with easy opponents early (ε 0.9 → 0 by half), from scratch, 94% league games, 2B, MinGRU,
plus `env.force_core_until = 0.5`: the share of drafts forced to include a 3-species core falls from
0.75 to 0 by mid-training (v1 kept it at 0.75 throughout). Scores are SyntheticRLV2 / TaurosV0,
fully free-pick, 144 games each.

| Checkpoint | 250M | 500M | 750M | 1.0B | 1.25B | 1.5B | 1.75B | 2.0B |
|---|---|---|---|---|---|---|---|---|
| SyntheticRLV2 | 2.1 | 7.6 | 11.1 | 27.8 | 32.6 | 29.2 | 33.7 | 29.9 |
| TaurosV0 | 6.9 | 6.9 | 13.2 | 22.9 | 27.8 | 29.2 | 33.3 | 31.9 |

**Verdict: no better than v1** (v1 best 33.7 / 49.7 at 1.75B, final 36.1 / 42.7). Annealing the forced
cores did not lift the plateau. It drafted the same core as v1: Alakazam, Articuno, Jolteon, Lapras,
Zapdos in nearly every draft, with Gengar 6th in 55% (v1 had Tauros).

Logs: `run/evals.log`; scripts `queue_window_box.sh`, `eval_dir.sh`.
