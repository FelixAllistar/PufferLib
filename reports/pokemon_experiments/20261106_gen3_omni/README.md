# Gen 3 OU: fresh model with the Gen 1 recipe (started 2026-11-06 16:15 UTC, Vast 59392729)

The user asked for a new model with every Gen 1 trick, not a continuation of the OU fine-tune (20261106_gen3_ou).
1024x2 from random weights, 4B steps (train.sh), tmux gen3omni (train, watch).

- OMNI team pool: every seat plays one of 172,594 Metamon gen3ou teams with exact sets (build_sets.js -> build/pokemon3/ou3:
  411,078 distinct sets, teams as six set indices). The env builds each battle itself (pk3_new_teams: leads in, their
  switch-in abilities by the engine in speed order). Check vs Showdown's own turn-1 states (showdown/check_teams.*):
  1998/2000 identical; the 2 misses are Trace copying Intimidate (the engine fires the copied Intimidate, Showdown's Gen 3
  does not).
- Free-pick: 30% of seats draft their own team in the same episode, before the battle: species from the 128 most used
  (draft.txt), then one of its 6 most used sets; no species twice; first pick leads. Draft observation: own picks in own
  slots, the candidate sets in the (hidden) opponent slots, four draft features. Actions 128 (battle uses 0-9).
- Forced cores: half of the learner's drafts start with one of the 256 most common species pairs (cores.txt), annealed
  to 0 at 2B steps.
- League: 25% of games vs 4 frozen snapshots of this run (core selfplay pool of its checkpoints, rotated every 200M).
- EMAgnet 0.03 (tau 0.01), entropy 0.01 -> 0.001, lr 5e-4 -> 1.25e-4, gamma 0.999, replay_ratio 1.
- Evals (watch.sh, evals.txt): every 250M, head to head vs the previous evaluated checkpoint on the 1,000 held-out teams;
  every 500M, 30 games vs Kakuna on Metamon's competitive teams (Showdown bridge; same protocol as the earlier runs, where
  the randbat model went 2/120 and the OU fine-tune 0-1/30).
- Training logs: draft_share, draft_score, draft_vs_pool (drafted team vs a pool team), forced_core_share, vs_league.

Previous model kept: checkpoints/pokemon_final/20261106_gen3_ou_2B.bin (md5 9b9ca51e1b079484d906303f2ead31e4), the OU
fine-tune's final checkpoint (held-out vs its randbat start 75.5% at 1.85B; Kakuna 0-1 of 30 per check). It uses the old
observation (5171) and 10 actions, so it does not load in this env.
