# Ladder run (2026-10-28, box 57608917)

Goal: the single best model for the public ladder (user: held-out tests no longer matter; train on every team).
- Pool (harvest_pool.py, pool_counts.txt): 4150 unique teams. Giant pool 3605; Smogon 256 (random OU teams from Smogon
  sets); former held-out A/B/C 28 each; our turf 28; every ladder opponent team from the saved battle logs, 177 unique
  (183 usable battles), completed from Metamon's ~58K real teams (revealed species and moves kept), weighted 10x.
  5743 pool slots. Every team checked in our engine. PK_TEAM_POOL_MAX raised to 16384.
- Training: giant run recipe from its 4B checkpoint, 2B steps, 30% free-pick, 25% vs LeagueV4 (tmux ladder_run).
- Team variants (team_variants.py, variants.txt): 26 Smogon-informed versions of the ladder team, each vs the 4B model on
  the current ladder team (2048 games) and vs Kakuna (full game, 448). Best vs current: one sleeper (Exeggutor Sleep
  Powder) + standard Snorlax (Earthquake) + standard Alakazam (Seismic Toss) + Zapdos for Lapras, 62.9%.
- Match mode keeps an explicit env.opponent_team now (pokemon_base.h), so a team can be tested against a different team.
