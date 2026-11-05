# Poker research notes (2026-11-05)

Running list of findings, open questions and planned experiments for the gen11 league and what follows it. Copy onto
the Experiment DAG board (p_gen11 and new rows) when the session is back on the board owner's account.

## Findings

- **Sweep score.** The old poker_score charged a table-wide re-raise "war" rate (~-4 for every model, mostly the
  opponents' doing) and paid for preflop folds. Replaced 2026-11-05 (f6906071) by money only: BB/hand vs the 8 frozen
  models + range/tight/trapper tables. All trials rescored; the sweep keeps its history.
- **The frozen score field rewards aggression against weak models.** With the money score, trial 0 (1B steps, 32,768
  agents) ranked first (+4.2 BB/hand) but in duplicate tables against strong players (lineup.c, 1000 deals x 6
  rotations) it loses ~-190 to -270 BB/100 (PFR 52%, all in 11-13% of hands). The score set includes weak models
  (ablate_*_300m, ab_*_1b) that pay a maniac. The sweep metric needs a strong field (see below).
- **Duplicate results, 100 BB fixed stacks (lineup.c).** Trial 9 is the most consistent winner (+74 to +93 BB/100,
  +/-50). Trial 10 is ~+25 against strong fields and -26 in the web lineup; the web table's carried stacks let its
  aggression snowball (what the user saw: it shoves early and wins big stacks). Range and Fullhouse are around +24 vs
  five strong models: real opponents. Tight ~+12 (a nit: VPIP 6%). Trapper loses -62 to -171: a donor.
- **The user's watching (web table):** trapper, station and tight look useless as opponents; range is the best bot.
- **Stacks in the observation.** Reward and the call-price features are exact (a $2 stack risks $2), but the raw
  amounts (bet to match, every seat's stack, bet history) are chips / 200 BB with no clamp: a 600 BB web stack is 3.0,
  beyond training (sessions start 20-250 BB, reset above 500 BB).
- **No adaptation within a session.** Models clear their recurrent memory every hand (as in training) and see no
  per-opponent statistics; range keeps table statistics. Models cannot learn mid-session that a seat is a maniac.

## Lineup results (2026-11-05, reports/poker_gen11/lineups on the poker box; 1000 duplicate deals x 6 rotations)

- **Sweep trials against one strong field** (t9, gen10_t9, gen8, range, Fullhouse; BB/100, +/-40 to +/-120): trial 9
  +40, 1 +39, 7 +29, 13 +24, 5 +5; then 16 -24, 19 -47, 10 -51, 12 -53, 11 -55, 6 -57, 3 -74, 4 -91, 17 -166, 14 -173,
  0 -189, 15 -308, 8 -326, 2 -355, 18 -356. The winners raise preflop 1-20% of hands; every trial raising 40%+ loses.
  The money score (frozen field) ranked 0, 4, 10, 9 first: it rewards aggression that only weak models pay, and the
  sweep has been drifting toward it (trials 14, 15, 17, 18).
- **Bots against five strong models (BB/100):** range +24, Fullhouse +24, tight +12, rules -13, trapper -62,
  maniac -1723, station -3140. Range and Fullhouse hold their own; tight is a break-even nit (VPIP 6%); trapper,
  maniac and station are donors.
- **Random pool tables** are dominated by who farms the maniac and station hardest (gen10 tops it at +536 while
  losing to strong fields): donor bots distort evaluation, and likely training, toward loose play.
- **Stack depth (web lineup):** at 100 BB t9 +47, gen8 +73 (+/-63); at 250 BB gen8 +314, t9 +227; at 600 BB gen8 +666,
  t9 +389, and t9's play changes (VPIP 57% -> 79%, flop 41% -> 82%) while t10 nearly stops shoving (8% -> 1%). Deep
  stacks are out of distribution and favor loose play: the web table's carried stacks are why a lucky early winner
  snowballs.

## Planned changes

1. **Training bots:** keep range (and Fullhouse if the trainer build can seat it); drop trapper, station, tight from
   the rotation at the next sweep phase (changing opponents mid-sweep would mix two setups in one history).
2. **Sweep score on a strong field:** duplicate tables against the best models (t9, gen10_t9, gen8) + range/Fullhouse,
   not the 8 fixed frozen models.
3. **Observation (gen12):** express bets and stacks capped at what this seat can actually be charged (effective
   stack), so a huge stack's shove looks like the all-in it is for a short stack.
4. **Web table:** cap seat stacks at the training range (or leave the table above it) so the snowball and the
   out-of-range stacks stop deciding sessions.

## Experiments and questions

- **Behavior fingerprints.** Measure behavior by actions in fixed probe states, not weights: a few thousand
  decision points stratified by street, position, facing action and hand-strength bucket, drawn from mixed play (the
  state-set restore exists); each model's action probabilities from its logits (no sampling noise); Jensen-Shannon
  distance per state, weighted by how often the situation occurs. Recurrent models need the hand's public action
  prefix replayed. Then PCA/UMAP + clustering (AURORA-style learned descriptors only if needed), anchored by known
  styles (maniac, station, tight, trapper, range, gen8 must separate). Human labels from HUD stats measured against
  one reference table: VPIP, PFR, 3-bet, fold to 3-bet, c-bet, aggression factor, WTSD, W$SD, check-raise, sizing mix.
- **League use of the map:** opponent selection for coverage, a MAP-Elites archive over learned axes, collapse
  alarms (shove-or-fold), cycle detection (win rate oscillating while the fingerprint returns to an old region).
- **Strength vs style:** a payoff matrix / Elo across the league next to the behavior map.
- **Shadow clones and Kirby.** Hypothesis (the user): small models (256x2, maybe 512x2) learn strategies and exploits
  fast but never generalize; the 1024x2 main model (Kirby) trains against them and absorbs their strategies. Test:
  exploiters of 256x2 / 512x2 / 1024x2 trained against a frozen 1024 main for fixed steps; exploitation (BB/100 vs
  the main) against steps and cost; then the main's gain from training against each. Expectation: exploiters smaller
  than the main unless the game is so hard that big exploiters are needed. Ties to the OMNI + Kirby loop.
- **PFSP lineage blind spots:** all pool snapshots descend from the main agent; AlphaStar needed dedicated exploiters.
  The fingerprint map should show whether the pool covers behaviors the main agent loses to.
- **Bots vs league only:** does training improve without bots once exploiters exist? A/B at 256x2, sequentially.
- **Training length:** the sweep has run 500M-1B only; a long run of the best config with per-checkpoint evals would
  show whether it keeps improving, plateaus or cycles.
- **Adaptation:** cross-hand memory or opponent-statistics features so a model can learn a table within a session.
