# Pokémon leaderboard vs Metamon (2026-10-22, morning)

**Protocol:** `evaluate_private.py` (native-private-v2), learner argmax, 48 games per panel team (144 total),
seed 20261025. Each cell has a standard error of about ±4 points.

**Ranking:** rows are sorted by the mean of the two strong models, SyntheticRLV2 and TaurosV0.

**Row types:**
- **B0:** an old rr20m league member on its own team.
- **B1:** trial-0 recipe for 700M steps on that team (75% self-play, 25% vs the 13 frozen league members).
- **A/B control:** the same recipe with self-play only.

A dash means the eval was not run.

| # | Policy (team) | Small | Medium | Large | SyntheticRLV2 | TaurosV0 | Strong mean |
|---|---|---|---|---|---|---|---|
| 1 | B0 gar_egg_zap | 81.2 | 74.0 | 81.2 | 62.5 | 57.6 | **60.0** |
| 2 | B1 gar_egg_zap @700M | – | – | – | 61.8 | 58.0 | **59.9** |
| 3 | B0 zam_egg_cloy_don | 84.0 | 83.0 | 74.0 | 58.3 | 58.0 | 58.2 |
| 4 | B1 zam_egg_zap @700M | 88.2 | 84.7 | 86.5 | 58.7 | 57.3 | 58.0 |
| 5 | B1 zam_egg_mie @700M | 87.5 | 79.9 | 79.2 | 60.4 | 53.1 | 56.8 |
| 6 | B0 gar_egg_mie | 87.5 | 83.3 | 82.6 | 64.9 | 48.6 | 56.8 |
| 7 | B0 mie_egg_zap | 84.4 | 76.7 | 83.3 | 56.2 | 57.3 | 56.8 |
| 8 | B0 gar_egg_mie_zap | 83.3 | 80.6 | 78.8 | 56.9 | 55.6 | 56.2 |
| 9 | B0 jynx_mie_don | 77.1 | 76.4 | 71.5 | 55.2 | 54.2 | 54.7 |
| 10 | Long run, Starmie fixed sets @700M | – | – | – | 57.3 | 50.7 | 54.0 |
| 11 | B0 zam_egg_mie | 90.6 | 86.1 | 83.3 | 52.1 | 53.8 | 53.0 |
| 12 | B0 mie_egg_cloy | 69.4 | 79.5 | 79.2 | 46.5 | 56.2 | 51.4 |
| 13 | B0 jynx_egg_zap | 81.6 | 79.2 | 76.4 | 55.2 | 46.5 | 50.9 |
| 14 | B0 jynx_egg_gar_zap | 80.2 | 69.8 | 72.9 | 57.6 | 43.1 | 50.4 |
| 15 | B0 zam_egg_zap | 85.4 | 81.2 | 82.6 | 56.2 | 43.8 | 50.0 |
| 16 | A/B PufferNet self-play control (zam_egg_mie) @700M | – | – | – | 50.7 | 45.5 | 48.1 |
| 17 | B1 zam_egg_gar @700M | 81.9 | 83.3 | 79.2 | 46.9 | 49.3 | 48.1 |
| 18 | B0 zam_egg_gar | 83.0 | 79.2 | 79.9 | 41.7 | 53.5 | 47.6 |
| 19 | B0 mie_egg_don | 76.4 | 74.3 | 70.8 | 51.0 | 44.1 | 47.6 |
| 20 | Sweep trial 0, Starmie fixed sets (200M) | 80.6 | 72.9 | 81.2 | 39.6 | 54.9 | 47.3 |
| 21 | Sweep trial 27, Starmie fixed sets (512×2) | 81.9 | 86.8 | 79.5 | 45.8 | 47.9 | 46.9 |
| 22 | rr20m league master, Starmie fixed sets | 81.2 | 76.0 | 79.2 | 45.8 | 43.8 | 44.8 |
| 23 | r26 baseline, Starmie fixed sets | 83.3 | 85.4 | 78.5 | 42.4 | 46.2 | 44.3 |
| 24 | B1 zam_egg_cloy_don @700M | – | – | – | 37.2 | 45.1 | 41.2 |

## Takeaways

- **League games matter.** On zam_egg_mie, the recipe with 25% frozen-league games (B1) scored 60.4 / 53.1. The
  same recipe with self-play only (A/B control) scored 50.7 / 45.5, about 9 points lower on both models.
- **gar_egg_zap is the best team.** Retraining it (B1) matches the old member: 59.9 vs 60.0.
- **Retraining is mixed.** B1 beats the old member on zam_egg_mie and zam_egg_zap. It is worse on
  zam_egg_cloy_don (41.2 vs 58.2).
- **Weak models don't separate policies.** Small, Medium and Large give 70–91 for everyone.
