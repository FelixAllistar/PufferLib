# puffermon_v4 (2026-10-28, box 57608917)

User: train the ladder-run model with full free-pick for another 2B steps; the result is puffermon_v4.
- Start: ladder run final (reports/pokemon_experiments/20261028_ladder, giant 4B + 2B on 4,150 teams; beats the giant 4B
  52.1% / 51.8% head to head on the current / old ladder team).
- Recipe: the ladder run's config with env.pool_free_fraction = 1 (every learner seat drafts its own team; the
  4,150-team pool is no longer used for learner seats), 25% of games vs LeagueV4 on its own teams, 2B steps
  (make_config.py, pokemon.ini). tmux puffermon_v4, train.log.
- Watch: the teams it drafts (species, sleep-move count) and, at the end, our-policy tests of its drafted team vs the
  current ladder team (no Metamon evals).
