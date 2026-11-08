# Gen 3: top teams, forced states and the league (2026-11-08)

## Results (Vast 59640753, destroyed 2026-11-08 03:20 UTC; everything in /mnt/d/puffer_media/box_backups/gen3_box_59640753)
Both arms: structured 256x2, 500M, matchup recipe. Control = the encoder A/B's structured arm (every seat a uniform
pool team). Smogon arm = 75% of pool seats from the 95 Smogon ADV teams (smogon_top.txt), the rest uniform.

| arm, 500M | vs max-damage (policy / magnet) | vs MCTS-200 (policy / magnet) | vs Kakuna, 30 games (policy / magnet) |
|---|---|---|---|
| control, all teams | **86.6%** / 86.3% | 43.3% / **47.1%** | **9** / 8 |
| Smogon teams 75% | 83.2% / 84.5% | 39.5% / 40.3% | 3 / 5 |
| flat net, all teams (encoder control) | 48.4% / 48.8% | 6.7% / 8.4% | - |
| earlier best: flat 1024x2 full recipe, 2.1B | ~78.5% | - | 8 |

- The new encoder at 256x2 and 500M steps (24 minutes) matches the best earlier Gen 3 model on Kakuna: 9/30 vs 8/30
  for the flat 1024x2 at 2.1B.
- The Smogon arm is worse everywhere, Kakuna included: 3-5 vs 8-9 of 30. Kakuna's eval teams are Metamon's
  "competitive" set, and 19 of its 20 teams match a Smogon training team, so the arm trained on those very teams and
  still lost more.
- 30 games is noisy (±2.5 wins), but nothing points the other way.
- Possible reasons, none tested:
  - 95 teams at 75% of seats is too narrow for 256x2 at 500M: it sees each team ~3M times and the field rarely;
  - the Smogon sets differ from the Metamon replay sets the rest of the pool uses;
  - what helps is a curated set mixed in at a lower share, or after a broad base (the user's order: good base policy
    and good base comps first, then explore).
- Kakuna setup on a fresh box needed fixes:
  - amago 3.4 passes float save_every (cast to int, amago_env.py:257);
  - there is no flash-attn wheel for torch 2.11 / Blackwell: superkazam.gin was switched to amago's
    SlidingWindowFlexAttention (patch in the backup's workspace/);
  - the client starts before Metamon downloads its teams;
  - both arms' checkpoints share a basename, and kakuna_eval.sh names its results by basename (queue4.sh uses
    kakuna_ck/ links).
- Results: ../20261107_gen3_encoder_ab/kakuna/summary.txt and evals.txt here (backup on D:).

## Team sources (smogon/: sources.tsv, raw pastes, teams/, split.js, merge.js)
- [ADV Resource Directory](https://www.smogon.com/forums/threads/adv-resource-directory-info-hub-samples-resources-more.3790099/):
  32 current sample teams.
- [Older sample teams](https://www.smogon.com/forums/threads/adv-ou-sample-teams.3687813/), superseded: 20 new teams.
- [Teams Through The Ages](https://www.smogon.com/spoink/atU) (M Dragon): 43 influential teams, NetBattle to
  WCoP/SPL.
- Not used:
  - [ADV Team Index Project](https://www.smogon.com/forums/threads/adv-team-index-project.3790096/): ~4,300
    forum teams, no quality filter;
  - the Tournament Directory and the usage dashboard.
- merge.js appends the 480 new sets to the ou3 table as build/pokemon3/ou3s; old indices are unchanged. Only 56 of the
  536 Smogon sets were already in the 172K Metamon pool.
- The own-policy team ranking (score.sh, rank.py; Steps 1-2 below) was not run. Its first try ran out of GPU memory at
  8,192 agents with training buffers; use 4,096 agents and minibatch 1,024.


The user's points:
- Training on every Metamon gen3ou team (172,594, sampled uniformly) is too much at once. Gen 1 learned on the best teams
  first, weighted up, and widened later.
- Gen 1 also forced the policy into the states that matter (forced cores, reset-state banks, key-point starts).
- Use the trainer's league (`base.league`, the chess Kirby loop): several models of any size trained in one run.

## What Gen 3 has done so far
- Pool: every team file behind the OU positions build (build/pokemon3/ou3/train.txt), uniform, no quality filter. 1,000
  teams are held out for evals.
- Gen 1 by contrast:
  - OMNI started on 13 curated league teams (`learner_team_pool`);
  - the final sweep used 348 teams;
  - only the giant run widened to 3,605 (348 + samples from each Metamon set).
- Gen 3 jumped straight to 172K teams. The 2026-11-07 encoder A/B also used no start states or league members beyond 4
  frozen snapshots of itself (selfplay, 25% of games).

## Step 1: rank the teams with our own policy (score.sh, rank.py)
- The rule from Gen 1: judge teams with our best policy, never with Metamon.
- Checkpoint: the encoder A/B's structured 256x2 at 500M. Pick policy vs magnet: at 500M the policy scores 86.6% /
  43.3% vs max-damage / MCTS-200, the magnet 86.3% / 47.1%. The magnet is better against MCTS, so it is the default
  scorer.
- Both seats play the checkpoint (sampled actions), each with a random training-pool team. env.draft_log records each
  game's two teams and result.
- rank.py gives each team a Laplace win rate against the field and writes top500/1000/2000.txt in pool format.
- Cost: ~40 games per team = 3.5M games. Inference only on the whole 5060 Ti, so minutes, not hours (to be
  measured).
- Bias to watch: a 256x2 policy may pilot some teams badly, so the ranking favours teams that are easy to pilot. Re-rank
  with each stronger model (that is the Kirby loop: the strategy set grows as the policy improves).

## Step 2: weight the top teams (env change, built 2026-11-08, not yet compiled on a box)
- `env.top_pool = FILE` (pool format) and `env.top_fraction` = share of pool seats that take a top team. The rest still
  draw from the full pool, so nothing is dropped.
- Proposed start: top 1,000 at 50%. Anneal toward the full pool later.

## Step 3: forced states
- What exists:
  - env.positions (Showdown turn-1 states, used by the randbat runs);
  - Gen 1's forced cores for drafting (env.force_core_prob).
- Not built for Gen 3: mid-game start states, e.g. Gen 1's reset_state_bank or Mario's key-point starts.
- Candidate source: states from our own games where the result swung, or where we lost to MCTS-200 / Kakuna. These are
  the states where it plays badly.
- Build after steps 1-2 are measured.

## Step 4: the league (trainer `base.league`, committed 511101de3 + 60a6e525d)
Format (src/pufferl.cu, above `struct Member`), one member per line:

    NAME HIDDENxLAYERS [frozen=PATH | init=PATH] [opponents=A,B] [weight=W] [train.KEY=VALUE ...]
    frozen members: pool=A,B swap=STEPS pfsp=hard|var|uniform (PFSP snapshot pool); learners: reset=STEPS

- Learners train side by side, each with its own optimizer, magnet and buffers. Round robin by default.
- Chess Kirby v2 (reports/chess_20261101/kirby2 on the chess box, 6B, running):
  - main 1024x2 (weight 3) plays itself and two PFSP snapshot banks;
  - exM (1024x2, init = main) and exL (fresh 256x2) are exploiters that reset every 1.5B / 2B.
- Correction (the user, 2026-11-08): the chess leagues were implemented poorly and say little about leagues. A league
  of good, diverse, appropriately scaled opponents always helps; a bad league hurts. Gen 1 Pokemon gained ~9 points
  from 25% frozen-league games. Max-damage, MCTS-200 and Kakuna stay the outside judges.
- The Gen 3 env takes league seats the same way it takes selfplay banks (0 = learner, k = frozen bank). Members
  differ only in size: the structured net's widths are global (policy.pokemon_embed / pokemon_mid).

## Proposed order (one training run per box)
1. score.sh on the 500M structured magnet -> top1000.txt.
2. 256x2 structured, 500M, top_fraction 0.5 vs the encoder A/B's structured arm (top_fraction 0). Judge on the 1,000
   held-out teams (not in either pool), vs max-damage and MCTS-200, policy and magnet. Answers: do top teams help?
3. 1024x2 structured with the winner, plus drafting (free_fraction 0.3) and Kakuna every 1B.
4. Then the league version of 3 (main + exploiters + PFSP pool) against 3 as the control.

## Results and resume point (2026-11-08 03:15 UTC; box 59640753 destroyed at the user's request)
Everything from the box (both report dirs with all checkpoints, its ocean/pokemon3 + config copy, the Kakuna setup
scripts and the patched superkazam.gin) is in /mnt/d/puffer_media/box_backups/gen3_box_59640753. The box's
ocean/pokemon3 and config/pokemon3.ini were identical to the local working tree (uncommitted Gen 3 encoder work:
pk3_net*.h, pk3_mlp.cuh, pokemon3_encoder.cu, top_pool, client/policy_server fixes). Text results are copied here and in
../20261107_gen3_encoder_ab.

Step 2 ran on the 95 Smogon sample teams (smogon/, env.top_fraction 0.5) instead of the own-policy top 1,000 (Step 1
was parked). 256x2 structured, 500M, vs the encoder A/B's structured arm (control: all teams):

| run (500M) | vs max-damage (policy / magnet) | vs MCTS-200 (policy / magnet) | vs Kakuna, 30 games (policy / magnet) |
|---|---|---|---|
| control (all teams) | 86.6% / 86.3% | 43.3% / 47.1% | 9 / 8 wins |
| Smogon teams 50% | 83.2% / 84.5% | 39.5% / 40.3% | 3 / 5 wins |

- Kakuna deals both sides Metamon's "competitive" gen3ou teams, so it is off-distribution for the Smogon arm (half its
  training seats were 95 teams). 17/60 vs 8/60 is ~2 standard errors: the Smogon weighting did not help on these teams
  and may have hurt. Not tested: Kakuna with our side on the Smogon teams.
- Kakuna fix: no flash-attn for torch 2.11/Blackwell, and Metamon parses the model's gin file after gin_overrides, so
  patch_kakuna.py's override was overwritten. superkazam.gin itself now sets SlidingWindowFlexAttention, window 96
  (backup: workspace/superkazam.gin on D:; original kept as superkazam.gin.orig on the box). kakuna_setup_box.sh still
  runs patch_kakuna.py; a new box needs the same gin edit.
- kakuna_eval.sh names results by checkpoint basename: two runs' 0000000499908608.bin collide (queue4.sh used
  renamed links). The client sometimes sends a move when Showdown wants a switch ("[Invalid choice] Can't move: You
  need a switch response", 1-3 per 30 games); the battle recovers.

Next (not started): Step 1 own-policy ranking (score.sh with the control's 500M magnet -> top1000.txt) and a top-1000
arm vs the control; or the 1024x2 control recipe with Kakuna every 1B (Step 3 of the proposed order).
