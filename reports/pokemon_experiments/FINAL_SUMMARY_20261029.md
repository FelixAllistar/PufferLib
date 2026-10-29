# Pokémon Gen 1 OU: final summary (2026-10-29)

Board: https://claude.ai/artifact/K5HPzZtmDfxh52c5TejVAV (every table and run, kept in sync with this file).

## Result
- A 9.6M-parameter PufferNet (1024×2 MinGRU), trained only by self-play and frozen-league play (no human data, no
  search), beats every Metamon model on every test. Giant 4B checkpoint vs Kakuna (143M, Metamon's best): mirror
  tests 54.8–57.5% on three unseen team sets, our turf and Metamon's teams; full game 64.1%; free-pick 74.3%. TaurosV0
  61.5–79.5%, SyntheticRLV2 65.8–83.1%. (20261027_giant/battery.txt)
- Real Showdown: Kakuna 68–32 and TaurosV0 70–30 on a Showdown server, matching our engine (69.0 / 73.0).
- Public Gen 1 OU ladder: 201–98, peak rating 1595, until the unregistered account felix_pufferlib was
  battle-banned on 29 Oct (~02:20 UTC; no reason or length given). Per-era records in
  20261027_showdown_vs_metamon/era_audit.txt.

## Final model and team
- Model: ladder run (20261028_ladder/checkpoints/pokemon/sweep_1793048711881_0014/0000001999896576.bin): giant 4B
  + 2B steps on 4,150 unique teams (every Metamon set, 256 Smogon OU teams, the former held-out sets, ours, all 177
  ladder opponents' teams ×10), 30% free-pick, 25% LeagueV4. Head to head vs giant 4B: 52.1% / 51.8%. The Metamon
  battery was not re-run on it.
- Team: round-robin winner egg1_std_zapdos (20261028_round_robin/teams/egg1_std_zapdos.paste.txt): Exeggutor (Sleep
  Powder / Psychic / Explosion / Stun Spore), Snorlax (Body Slam / Earthquake / Hyper Beam / Self-Destruct), Tauros
  (Earthquake / Blizzard / Body Slam / Hyper Beam), Alakazam (Psychic / Seismic Toss / Thunder Wave / Recover),
  Chansey (Ice Beam / Thunderbolt / Thunder Wave / Soft-Boiled), Zapdos (Thunderbolt / Drill Peck / Thunder Wave /
  Agility). Beats all 9 other candidates (55–80%) with our policy on both sides; ladder 29–12 (71%), peak 1594.

## Lineage (decision steps)
OMNI-1024 2B (13 league teams) → OMNI-2 trial 23 +100M → Metamon-teams run 1B on 48 teams → long run to 2B on 348
teams (1875M picked) → giant run +4B on 3,605 teams → ladder run +2B on 4,150 teams.

## Late experiments (27–29 Oct)
| Experiment | Result | Where |
|---|---|---|
| Size study, giant recipe from scratch, 2B | Kakuna held-out A: 512×2 49.2 (2.75M params, ~260K sps), 512×3 46.2 (3.54M, ~243K), 2048×2 52.5 (35.9M, ~46K); depth 3 never helps | 20261027_size |
| Team comps vs Kakuna (giant 4B pilots 49 teams) | Old ladder team first (73.9); our teams above human-made ones | 20261027_giant/team_eval.txt |
| Ladder-team variants (26 + 3) | Fewer sleepers wins vs our own policy (Exeggutor-only 53.8–56.6, + Zapdos 62.9) but loses vs Kakuna (59–64 vs 73.9) | 20261028_ladder/variants.txt |
| User edits (Snorlax Rest, Exeggutor Stun Spore) | Even with the old team vs our policy (50.6–53.2); Kakuna 66–70 | same |
| Ladder run vs giant 4B | 52.1% (live team), 51.8% (old peak team) | 20261028_ladder/new_vs_giant.txt |
| Team round robin (10 teams, own policy both sides) | One team beats all others; Nash mix = that team | 20261028_round_robin |
| puffermon_v4: +2B full free-pick | Drafts the old peak team 12/12; 43.9% vs ladder run on the winner team, 53.9% on its own draft | 20261028_puffermon_v4 |
| Argmax vs sampling (new base.enemy_greedy) | 51.0% / 49.8% with seats swapped; no difference | 20261029_argmax_vs_sample |
| Ladder audit | Old team 69%, edited team 64% (noise), winner team 71%; Nivky 34–22 across four team styles | 20261027_showdown_vs_metamon/era_audit.txt |

## Lessons
- Generalists beat specialists on their own teams; training on more teams made piloting general.
- Kakuna is a poor judge of teams: four-sleeper teams exploit it but lose to strong teams. Judge teams with our own
  policy on both sides.
- Free-pick works from a strong pilot with ~80% self-play inside a team pool; full free-pick collapses onto one team.
- More training of the same kind gives little (+2B: 52%); width helps a little, depth 3 not at all.
- The policy is nearly deterministic, so argmax vs sampling does not matter.

## Code
- Showdown bridge: ocean/pokemon/showdown/ (engine parity, tracker, policy_server, client with --teams weighted
  rotation, ladder.sh with LADDER_CK / LADDER_TEAM / LADDER_TEAMS).
- Trainer: base.enemy_greedy in ./puffer match (logged in reports/core_changes_audit.md).

## If resumed
- Search at play time (v2): sample hidden opponent sets from ~58K real teams, short policy rollouts per joint move in
  our exact engine, value-head leaves, solve each turn's simultaneous-move matrix game. Testable offline.
- Score teams against the ladder field weighted by frequency (177 teams); state sets from lost ladder turns.
- Ladder play only with a bot account Showdown has approved.

## Not kept / open
- Box 57608917 (Pokémon box) stays up for poker; all results above are also local and pushed.
- KakunaEnsemble / TaurosEnsemble untested.
