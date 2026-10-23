# Transformer self-play sweep (2026-10-22 to 23, A/B box): cancelled

Entity Transformer, self-play on zam_egg_mie, 250M per trial, scored at the end vs TaurosV0 (144
games). Searched: attention width 64–256, layers 1–4, learning rate, entropy, clip, replay ratio,
magnet coefficient and rate (`make_sweep_config.py`). Planned 60 trials.

| Trial | Settings | TaurosV0 | Time |
|---|---|---|---|
| 0 | A/B settings (width 128, 2 layers, replay 2) | **52.8** | 1.6 h |
| 1 | width 64, 3 layers, magnet 0.38 | 6.3 | 1.0 h |
| 2 | width 128, 4 layers, replay 3.7 | 45.1 | 4.6 h |

**Cancelled by the user after 3 trials (2026-10-23 06:39 UTC):** trials took 1–4.6 h, so 60 would have
taken about a week. The same settings as trial 0 scored 43.1 in the first (broken) launch, so seed
noise is large. Box destroyed; full backup in `runs/pokemon_ab_box_57154204_backup/`.
