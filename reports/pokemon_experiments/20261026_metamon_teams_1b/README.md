# Metamon-teams run (2026-10-26, box tmux `mm_train` = dashboard, `mm_eval` = evals)

Goal: a better Metamon on all of Metamon's teams and ours. OMNI-2 sweep trial 23 (best sweep score 0.550, top of the
final cross-play matrix with Nash rating 0.500, t19 0.498, OMNI-1024 2B 0.455) continued for 1B steps with its
settings: LR 5.4e-4 annealed to 22%, entropy 8e-5 annealed to 42%, magnet 0.017, 25% of games vs LeagueV2.

- Pool (`pool.ini`, `build_pool.py`), 48 teams: Metamon's 28 competitive gen1ou teams and Princess Klitty's team (both
  versions) with exact sets (new env team format `sets:` in pokemon_base.h / pokemon_core.h, test_exact_sets), plus our
  18 teams with species fixed (13 league, 4 OMNI-2 free-pick teams, FP-3's late team). Per-team win rates log for pools
  up to 64 teams (PK_POOL_LOG_MAX).
- Evals (`mm_eval.py`, every ~125M checkpoint, into mm_evals.txt): mirror test vs Metamon on its 28 teams (784 games
  per Metamon model) and the full game, zam_egg_cloy_don vs Metamon's own teams (448 games). Baselines (trial 11):
  mirror 45.8 / 51.5 (Tauros / Syn), full 65.1 / 62.9.
- Training log: /train.log on the box (symlinked as train.log here; the tmux shell did not see $R).
- PKGame gained set_moves: state banks saved before this no longer load (unused by these runs).

## Result at 1B (2026-10-26)

| | mirror vs TaurosV0 | mirror vs SyntheticRLV2 | full vs TaurosV0 | full vs SyntheticRLV2 |
|---|---|---|---|---|
| this run, 1B | **62.9** | **68.5** | **72.5** | **77.3** |
| OMNI-2 trial 11 (before) | 45.8 | 51.5 | 65.1 | 62.9 |

Mirror: 1568 games (+/- 2.4); full: 448 games (+/- 4.1). On Metamon's own 28 competitive teams with identical sets on
both sides we now win 63-69%: we out-pilot Metamon on its teams. Caveat: these 28 teams were in our training pool, so
this is not a held-out-teams result (Metamon learned from human games across many teams; these are its own
evaluation teams). Earlier checkpoints (875M, 750M, ...) are being evaluated newest first into mm_evals.txt.

## Held-out test (running 2026-10-26, `heldout.sh`)

Mirror test on 28 teams from Metamon's `hl_05_26` set (high ladder, May 2026), sampled and validated by seed, none in
any training pool (`reports/pokemon_benchmarks/metamon_hl_05_26_sample28`). 1B checkpoint and OMNI-2 trial 11 (trained on
neither set), 1568 games per model, results in heldout.txt.

| held-out mirror (28 hl_05_26 teams, 1568 games, +/- 2.4) | vs TaurosV0 | vs SyntheticRLV2 |
|---|---|---|
| Metamon-teams run, 1B | **59.7** | **68.2** |
| OMNI-2 trial 11 (control) | 53.3 | 60.2 |

We beat both Metamon models on teams no policy of ours ever trained on, with identical teams and sets on both sides.
The run's gain over the control (+6.4 / +8.0) holds on unseen teams, so it is piloting skill, not memorized teams.
(These ladder teams suit us better than the competitive set: the control scores 53/60 here vs 46/52 there.)

## Plan: the final 1024 sweep (to beat Metamon in the usual sense)

"Beating Metamon" in the usual RL sense: a win rate above 50% against its strongest released models (TaurosV0,
SyntheticRLV2) under a protocol that cannot favor us: teams we never trained on, identical teams and sets on both sides
(mirror), both seats, shared seeds, confidence intervals. Plus the full game (each side brings its own teams).
1. Start point: the 1B Metamon-teams checkpoint (or whichever the held-out test favors).
2. Much broader team range: the pool grows from 48 to several hundred teams: Metamon's 28 competitive teams, ~300 sampled
   from its ladder sets (hl_05_26 and modern_replays_v2, exact sets), and our 18. The 28 held-out evaluation teams and a
   second held-out sample stay out of every pool.
3. League: LeagueV3 = the 1B checkpoint piloting ~16 diverse pool teams (Metamon's and ours), replacing LeagueV2.
4. Trials: 150M steps each from the start point; swept: LR and its anneal floor, entropy and its floor, magnet; replay
   pinned to 1 and every inherited default.ini dimension pinned. Score: vs_league_late (league games in the last 20%).
5. Metamon on the side: every new top-3 trial gets the held-out mirror test vs both models (1568 games each).
6. Final: the winning settings run long (1-2B) with annealing; report held-out mirror and full game vs both models,
   with intervals, plus the cross-play matrix against our earlier policies.

Second held-out set (28 teams from Metamon's modern_replays_v2, sampled by seed, disjoint from every training pool and
from the first held-out sample; `heldout2.sh`), 1B checkpoint, 1568 games each: **55.7** vs TaurosV0 (+/- 2.5),
**64.5** vs SyntheticRLV2 (+/- 2.4). Above 50% against both models on both independent held-out team sets.

## Model comparison and margins (2026-10-26; full write-up in the board's "vs Metamon" tab)

| | ours (1B) | TaurosV0 | SyntheticRLV2 |
|---|---|---|---|
| parameters | 9.6M | 62.1M | 200.9M |
| checkpoint | 38 MB | 248 MB | 804 MB |
| architecture | entity encoder + 2-layer MinGRU (1024) | grouped encoder + 4-layer trajectory transformer (d704, 32-turn window) | 5-layer text-token turn encoder + 9-layer trajectory transformer (d1280, 20 heads) |
| search | none (argmax) | none (samples) | none (samples) |
| data | ~3.1B self-play/league decision steps, no human data | human replays + self-play (Tauros mix) | human replays + synthetic self-play |
| scope | Gen 1 OU | Gens 1-4, 9 | Gens 1-4 |

Elo-equivalent margins (95% interval): unseen teams A +68 [51, 86] vs TaurosV0, +133 [114, 151] vs SyntheticRLV2; unseen
teams B +40 [23, 57] / +104 [86, 122]; Metamon's competitive teams +92 / +135; full game +168 / +213; free-pick full game
+33 / +31. Untested: Kakuna (Metamon's stated current best, ~140M, ~82% GXE vs humans in gen1ou) and the ensembles
that reached #1 on the Showdown ladder.

## vs Kakuna, Metamon's stated current best (2026-10-26, `kakuna.sh`)

Kakuna qualified like TaurosV0: 142.8M params (572 MB), every projected word known to its tokenizer, no illegal action
or engine error in 589 decisions, its FlashAttention window (96, 0) reproduced by our backend (windowed models cannot be
compared against upstream full attention, the same as TaurosV0).

| 1B checkpoint vs Kakuna | win rate | Elo |
|---|---|---|
| unseen teams A, mirror (1568) | 50.4 +/- 2.5 | +2 [-15, 20] |
| unseen teams B, mirror (1568) | 46.4 +/- 2.5 | -25 [-42, -8] |
| full game, zam_egg_cloy_don vs its 28 competitive teams (448) | **64.5 +/- 4.4** | **+104 [71, 139]** |

Against Metamon's best, piloting identical unseen teams is even (one set a tie, the other a small loss); with team
choice (the full game) we win clearly. The final sweep's side job now tests every new top trial against Kakuna too.
Free-pick FP-4 (375M, drafting its own team from all of Gen 1) vs Kakuna on its 28 competitive teams (896 games):
46.9 +/- 3.3 (Elo -21 [-44, 1]): about even, a slight loss (it beat TaurosV0 / SyntheticRLV2 54.7 / 54.5).

## Complete battery (2026-10-26; our_turf.sh, battery.sh; also the board's "vs Metamon" tab)

Our win rate +/- 95% (Elo). Mirror: identical teams both sides, 1568-1800 games; full game: 448 games.

| Metamon | unseen A | unseen B | our turf | Metamon competitive | full game |
|---|---|---|---|---|---|
| Kakuna (143M) | 50.4 (+2) | 46.4 (-25) | 46.6 (-24) | 52.1 (+15) | **64.5 (+104)** |
| TaurosV0 (62M) | **59.7 (+68)** | **55.7 (+40)** | **58.1 (+57)** | **62.9 (+91)** | **72.5 (+169)** |
| SyntheticRLV2 (201M) | **68.2 (+133)** | **64.5 (+104)** | **59.6 (+67)** | **68.5 (+135)** | **77.3 (+213)** |
| Abra (58M) | **81.2 (+254)** | **78.6 (+226)** | **69.9 (+147)** | **84.1 (+289)** | **88.2 (+349)** |

Our turf = our 18 teams with our drafted sets (reports/pokemon_benchmarks/our_turf30, export_drafted_teams.py) + Princess
Klitty x2 + 10 random Smogon OU teams. Kakuna pooled over the four mirror sets: 48.8% of 6504 games (about -8 Elo):
level at piloting; we win the full game. Abra and Kakuna qualification reports: reports/pokemon_benchmarks/qualify_20261026/.
