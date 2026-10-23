# Window Transformer memory instead of MinGRU (2026-10-23)

`policy.network = window`: each step attends over the last 32 turns (2 layers, width 128, 4 heads),
replacing the MinGRU recurrent memory. PufferNet encoder in both arms; only the memory changes. Scores
are SyntheticRLV2 / TaurosV0, 144 games each (about ±4 points).

| Arm | Box | 350M | 700M | Compare (same setup) |
|---|---|---|---|---|
| Self-play, zam_egg_mie | window box | 51.0 / 47.6 | **51.0 / 50.0** | MinGRU 50.7 / 45.5; entity Transformer 54.2 / 46.2 |
| B1 league recipe, gar_egg_zap | B1 box | 56.2 / 47.9 | **49.0 / 52.8** | MinGRU (B1) 61.8 / 58.0 |

**Verdict: no gain.** In self-play it is level with the entity Transformer and about 2 points above
MinGRU (within noise). With the league it is about 9 points behind MinGRU at 700M. Speed: 33–36K
steps/s. Correctness checks passed before launch (training matches rollout, GPU matches the CPU
evaluator, 142 gradient checks).

Logs: `selfplay_zam/evals.log`, `league_gar/evals.log`; scripts `run_window.sh`, `eval_window.sh`.
