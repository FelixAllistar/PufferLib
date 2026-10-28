# Long run to 2B (started 2026-10-26, box 57608917)

The final sweep was flat after 18 trials (vs_league_late 0.467-0.493; held-out gains of only ~+2 over the 1B start), so
the user chose long runs over more sweeping. The sweep is stopped, not deleted: resume with `./puffer sweep` after
reinstalling `20261026_final_sweep/pokemon.ini` as config/pokemon.ini (history in its sweep.history).

- Settings: final-sweep trial 14's exact config (`make_config.py`; LR 5.2e-4 annealed to 0.26x, entropy 1.1e-4 annealed
  to 0.29x, magnet 0.033, replay 1), from the Metamon-teams 1B checkpoint, total 2B steps, same 348-team pool and LeagueV3.
  Installed as config/pokemon.ini (the sweep's: config/pokemon.ini.final_sweep).
- tmux `long2b` (training, log train.log), `long2b_eval` (`long_eval.py`: held-out mirror vs Kakuna / TaurosV0 /
  SyntheticRLV2 on every checkpoint, evals.txt).
- Baselines (held-out mirror, 1568 games, +/- 2.4): 1B start 50.4 / 59.7 / 68.2; trial 14 at +150M 50.9 / 62.4 / 68.6.
- Results so far (held-out mirror, evals.txt): 125M 48.8/61.2/68.0, 375M 50.2/61.7/70.3, 500M 52.5/61.3/70.5,
  750M 52.8/63.9/70.9, 1000M 53.9/63.8/70.7 (Kakuna / TaurosV0 / SyntheticRLV2).
- Side test (tmux long2b_fp, freepick_test.py -> freepick.txt): the 1250M checkpoint vs Metamon on its 28 competitive
  teams (448 games each) as pure free-pick (drafts species and moves) and as the full game on zam_egg_cloy_don.
- Free-pick side test at 1250M (freepick.txt): free-pick Kakuna 44.2 / TaurosV0 57.8, full game Kakuna 65.7 / TaurosV0 69.9;
  its SyntheticRLV2 leg ran out of GPU memory beside training (dropped; the battery covers free-pick).
- Full battery (tmux long2b_battery, battery.py -> battery.txt / battery/battery.json): waits for training to end and every
  1B-2B checkpoint to be scored, stops the tester, picks the best checkpoint by held-out A, then runs Kakuna / TaurosV0 /
  SyntheticRLV2 / Abra on unseen A + B, our turf, competitive mirror, full game and free-pick, one eval at a time.
