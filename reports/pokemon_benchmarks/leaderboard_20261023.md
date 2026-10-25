# Pokémon leaderboard vs Metamon (2026-10-24)

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
| 1 | OMNI_MASTER 1024x2 on zam_egg_cloy_don @2B | – | – | – | 77.1 | 69.4 | **73.2** |
| 2 | OMNI_MASTER on zam_egg_cloy_don @1.5B | – | – | – | 71.2 | 67.4 | **69.3** |
| 3 | OMNI_MASTER 1024x2 on gar_egg_zap @1B | – | – | – | 69.8 | 65.6 | **67.7** |
| 4 | OMNI_MASTER 1024x2 on zam_egg_zap @1B | – | – | – | 76.0 | 59.0 | 67.5 |
| 5 | OMNI_MASTER on zam_egg_cloy_don @1B | – | – | – | 64.6 | 69.4 | 67.0 |
| 6 | OMNI_MASTER on gar_egg_mie_zap @2B | – | – | – | 64.9 | 68.8 | 66.8 |
| 7 | OMNI_MASTER on zam_egg_zap @2B | – | – | – | 71.5 | 59.7 | 65.6 |
| 8 | OMNI_MASTER on gar_egg_zap @1.5B | – | – | – | 66.7 | 62.2 | 64.5 |
| 9 | OMNI_MASTER on mie_egg_zap @2B | – | – | – | 68.1 | 60.4 | 64.2 |
| 10 | Free-pick from Smogon OMNI, no forcing (own team) @500M | – | – | – | 72.6 | 55.2 | 63.9 |
| 11 | Free-pick from Smogon OMNI, slow (own team) @625M | – | – | – | 70.5 | 55.6 | 63.0 |
| 12 | Big B1 1024x2 warm restart (gar_egg_zap) @+750M | – | – | – | 65.3 | 56.6 | 61.0 |
| 13 | Big B1 1024x2 (gar_egg_zap) @750M | – | – | – | 60.4 | 60.1 | 60.2 |
| 14 | B0 gar_egg_zap | 81.2 | 74.0 | 81.2 | 62.5 | 57.6 | 60.0 |
| 15 | B1 gar_egg_zap @700M | – | – | – | 61.8 | 58.0 | 59.9 |
| 16 | Free-pick from OMNI (own team) @250M | – | – | – | 62.8 | 56.6 | 59.7 |
| 17 | Smogon OMNI on gar_egg_zap (Smogon sets) @2B | – | – | – | 59.4 | 59.7 | 59.5 |
| 18 | Free-pick from Smogon OMNI, fast (own team) @500M | – | – | – | 63.9 | 54.9 | 59.4 |
| 19 | B1 gar_egg_zap, easy opponents ε 0.5 @700M | – | – | – | 61.8 | 55.6 | 58.7 |
| 20 | B1 gar_egg_mie_zap @700M | – | – | – | 59.7 | 56.6 | 58.2 |
| 21 | B0 zam_egg_cloy_don | 84.0 | 83.0 | 74.0 | 58.3 | 58.0 | 58.1 |
| 22 | B1 zam_egg_zap @700M | 88.2 | 84.7 | 86.5 | 58.7 | 57.3 | 58.0 |
| 23 | B1 zam_egg_mie @700M | 87.5 | 79.9 | 79.2 | 60.4 | 53.1 | 56.8 |
| 24 | B0 gar_egg_mie | 87.5 | 83.3 | 82.6 | 64.9 | 48.6 | 56.8 |
| 25 | B0 mie_egg_zap | 84.4 | 76.7 | 83.3 | 56.2 | 57.3 | 56.8 |
| 26 | B0 gar_egg_mie_zap | 83.3 | 80.6 | 78.8 | 56.9 | 55.6 | 56.2 |
| 27 | B1 gar_egg_zap, easy opponents ε 0.9 @700M | – | – | – | 55.6 | 56.9 | 56.2 |
| 28 | B1 jynx_egg_zap @700M | – | – | – | 66.7 | 45.8 | 56.2 |
| 29 | Free-pick from OMNI (own team) @125M | – | – | – | 61.8 | 49.0 | 55.4 |
| 30 | B1 jynx_mie_don @700M | – | – | – | 54.9 | 55.6 | 55.2 |
| 31 | B1 gar_egg_mie @700M | – | – | – | 50.7 | 59.0 | 54.9 |
| 32 | B0 jynx_mie_don | 77.1 | 76.4 | 71.5 | 55.2 | 54.2 | 54.7 |
| 33 | B1 on 5.0 build (gar_egg_zap) @700M | – | – | – | 55.9 | 52.8 | 54.3 |
| 34 | Transformer self-play (zam_egg_mie) @350M | – | – | – | 55.6 | 52.8 | 54.2 |
| 35 | Long run, Starmie fixed sets @700M | – | – | – | 57.3 | 50.7 | 54.0 |
| 36 | B0 zam_egg_mie | 90.6 | 86.1 | 83.3 | 52.1 | 53.8 | 53.0 |
| 37 | B1 mie_egg_zap @700M | – | – | – | 56.2 | 49.3 | 52.8 |
| 38 | Transformer + league (gar_egg_zap) @700M | – | – | – | 57.3 | 47.9 | 52.6 |
| 39 | B1 mie_egg_cloy @700M | – | – | – | 51.0 | 52.8 | 51.9 |
| 40 | B0 mie_egg_cloy | 69.4 | 79.5 | 79.2 | 46.5 | 56.2 | 51.4 |
| 41 | B1 jynx_egg_gar_zap @700M | – | – | – | 49.3 | 52.8 | 51.0 |
| 42 | Window memory + league (gar_egg_zap) @700M | – | – | – | 49.0 | 52.8 | 50.9 |
| 43 | B0 jynx_egg_zap | 81.6 | 79.2 | 76.4 | 55.2 | 46.5 | 50.9 |
| 44 | Window memory self-play (zam_egg_mie) @700M | – | – | – | 51.0 | 50.0 | 50.5 |
| 45 | B0 jynx_egg_gar_zap | 80.2 | 69.8 | 72.9 | 57.6 | 43.1 | 50.4 |
| 46 | League gen 2 (gar_egg_zap) @700M | – | – | – | 48.6 | 52.1 | 50.4 |
| 47 | Transformer self-play (zam_egg_mie) @700M | – | – | – | 54.2 | 46.2 | 50.2 |
| 48 | B0 zam_egg_zap | 85.4 | 81.2 | 82.6 | 56.2 | 43.8 | 50.0 |
| 49 | Magnet off (gar_egg_zap) @700M | – | – | – | 48.6 | 50.0 | 49.3 |
| 50 | A/B PufferNet self-play control (zam_egg_mie) @700M | – | – | – | 50.7 | 45.5 | 48.1 |
| 51 | B1 zam_egg_gar @700M | 81.9 | 83.3 | 79.2 | 46.9 | 49.3 | 48.1 |
| 52 | B0 zam_egg_gar | 83.0 | 79.2 | 79.9 | 41.7 | 53.5 | 47.6 |
| 53 | B0 mie_egg_don | 76.4 | 74.3 | 70.8 | 51.0 | 44.1 | 47.5 |
| 54 | Sweep trial 0, Starmie fixed sets (200M) | 80.6 | 72.9 | 81.2 | 39.6 | 54.9 | 47.2 |
| 55 | Sweep trial 27, Starmie fixed sets (512×2) | 81.9 | 86.8 | 79.5 | 45.8 | 47.9 | 46.8 |
| 56 | rr20m league master, Starmie fixed sets | 81.2 | 76.0 | 79.2 | 45.8 | 43.8 | 44.8 |
| 57 | B1 mie_egg_don @700M | – | – | – | 51.0 | 38.2 | 44.6 |
| 58 | r26 baseline, Starmie fixed sets | 83.3 | 85.4 | 78.5 | 42.4 | 46.2 | 44.3 |
| 59 | Free-pick meta rotation (own team) @1B | – | – | – | 44.8 | 41.0 | 42.9 |
| 60 | Free-pick move pairs (own team) @375M | – | – | – | 40.6 | 43.8 | 42.2 |
| 61 | Self-play only (gar_egg_zap) @700M | – | – | – | 49.3 | 34.4 | 41.8 |
| 62 | Free-pick, easy opponents (own team) @1.75B | – | – | – | 33.7 | 49.7 | 41.7 |
| 63 | B1 zam_egg_cloy_don @700M | – | – | – | 37.2 | 45.1 | 41.2 |
| 64 | Free-pick +2B at ε 0 (own team) @+1.75B | – | – | – | 41.3 | 39.2 | 40.2 |
| 65 | Free-pick team, species fixed @350M | – | – | – | 41.0 | 34.0 | 37.5 |
| 66 | Free-pick v2, cores annealed (own team) @1.75B | – | – | – | 33.7 | 33.3 | 33.5 |
| 67 | Free-pick, no easy opponents (own team) | – | – | – | 0.7 | 0.7 | 0.7 |


Free-pick rows play a team they drafted themselves; all others play a fixed team. Transformer sweep (TaurosV0 only): best 52.8 (trial 0).

## Takeaways (2026-10-23)

- **OMNI_MASTER (one 256x2 policy for all 13 league teams) is the new best:** 64-67 on every team scored so far, above each team's specialist.
- **Free-pick works when started from OMNI with mostly self-play:** it drafts a standard OU team (Alakazam / Chansey / Lapras / Rhydon / Starmie / Tauros) and scores ~62 at 125-250M, against ~40 for every earlier free-pick run.
- **Best policies are still the PufferNet league recipe on gar_egg_zap** (B0 60.0, B1 59.9). Nothing since has beaten it.
- **League games matter most:** +9 (zam_egg_mie) and +12 / +24 (gar_egg_zap) over self-play.
- **EMAgnet helps:** about 10 points over magnet off on gar_egg_zap.
- **Memory/encoder variants did not beat MinGRU PufferNet with the league:** entity Transformer about 7 behind, window memory about 9 behind; in self-play both are within noise of or slightly above MinGRU.
- **Free-pick learns only with easy opponents early** and plateaus in the 30s–40s; more steps and annealing forced cores did not help. It always drafts the Jolteon / Lapras / Zapdos / Articuno / Alakazam core.
- **Easy opponents early do not help fixed-team runs** (ε 0.5 ties B1, ε 0.9 slightly behind).
