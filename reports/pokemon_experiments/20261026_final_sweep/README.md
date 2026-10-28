# Final 1024 sweep (started 2026-10-26 16:36 UTC, box 57608917)

tmux `final_sweep` (sweep, dashboard), `final_scores` (scores table), `final_heldout` (held-out Metamon side job).

- Start point: the Metamon-teams 1B checkpoint (held-out mirror vs Metamon: 59.7 TaurosV0 / 68.2 SyntheticRLV2).
- Pool (`pool.ini`, `build_pool.py`), 348 teams: the 1B run's 48 (Metamon's 28 competitive teams and Princess Klitty's
  with exact sets, our 18) plus 300 ladder teams with exact sets (150 each from Metamon's hl_05_26 and
  modern_replays_v2, `ladder_*`, sampled by reports/pokemon_benchmarks/sample_teams.py), disjoint from both held-out
  evaluation samples.
- League: LeagueV3 (`leagues/pokemon/league_v3_mm1B`, `build_league_v3.py`): the 1B checkpoint piloting 16 teams
  (8 Metamon competitive, Klitty Jynx version, 7 of ours); 25% of games.
- Trials: 150M steps from the 1B checkpoint; swept LR 2e-5..1e-3 and its anneal floor, entropy 1e-6..1e-2 and its
  floor, magnet 0.01..0.5; every other dimension pinned (replay 1); 500 trials (~19 min each; the user stops it when satisfied). Score: vs_league_late.
  Config: `pokemon.ini` (make_config.py), installed as the box's config/pokemon.ini (previous:
  config/pokemon.ini.metamon_teams_1b).
- Side job (`sweep_heldout.py`): each new top-3 trial gets the held-out mirror test (28 hl_05_26 teams, 1568 games) vs
  both Metamon models into heldout.txt; the scores view shows it as a column.
