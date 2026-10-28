# Giant run (started 2026-10-27, box 57608917)

The 2B long run beat all four Metamon models on piloting; this run widens training to every Metamon gen1ou team set and
adds free-pick drafting in the same run (the user asked for both in one giant run; Metamon itself trained on ~5.3M human
replays plus self-play over these team sets, so training on them is fair).

- Start: the 2B long run best checkpoint (1875M): held-out A Kakuna 56.1 / TaurosV0 65.1 / SyntheticRLV2 73.8; free-pick
  vs Kakuna 41.3. Settings: the long run config (final-sweep trial 14), 4B steps, checkpoint about every 250M.
- Pool (build_pool.py, make_teams.sh): 3605 teams = the final sweep 348 + exact-set teams sampled from all seven Metamon
  gen1ou sets (hl_05_26 700, gl_05_26 700, modern_replays_v2 600, modern_replays 500, paper_replays 500, paper_variety 300;
  only teams our engine plays exactly; deduplicated).
- Free-pick: env.pool_free_fraction = 0.3 (new env option, pokemon_base.h): each learner seat drafts its whole team freely in
  30% of games instead of taking a pool team.
- League: LeagueV4 (build_league_v4.py, leagues/pokemon/league_v4_2b1875): 1875M piloting LeagueV3 16 teams + 8 LeagueV3
  banks (the 1B checkpoint); 25% of games.
- Held out (never in the pool): A hl_05_26 sample28, B modern_replays_v2 sample28, C gl_05_26 sample28 (new,
  reports/pokemon_benchmarks/metamon_gl_05_26_sample28), our turf.
- tmux giant (training, train.log), giant_eval (giant_eval.py: held-out A vs Kakuna/TaurosV0/SyntheticRLV2 + free-pick vs
  Kakuna on every checkpoint, newest first, evals.txt). config/pokemon.ini = this config (previous: config/pokemon.ini.long2b).
