# Poker gen10 sweep (2026-10-30, box 57608917)

Why: gen8 (self-play vs its newest snapshot) plays 90% of hands and folds 0.1% preflop when it owes chips; six copies
fall into re-raise wars in 30% of hands. gen9a (snapshot refresh every ~1M steps instead of ~10M, 10% bots) and gen9b
(gen8 fine-tuned against a frozen gen8 field) changed nothing, so the self-play lag is not the cause. The gen3 sweep
that set the hyperparameters was tuned at reward_scale 1 against bots with panel_worst as the score, which rewards
loose aggression (+117 BB/hand vs the station).

Recipe (config/poker.ini; previous config/poker.ini.gen8):
- 1024x2 PufferNet from scratch; 32,768 agents x horizon 32, minibatch 32,768, LR 0.003 with the EMAgnet,
  reward_scale 0.01, replay_ratio 1 (never swept).
- Live pool of the run's own snapshots (league_ramp 10), 20% bots, 20% panel tables, 20% of hands from a state set
  (leagues/poker/states_gen10.bin: 2M spots, gen_states with gen8 and gen7 at 15% of seats).
- Metric poker_score = panel_worst - 10 x war_rate + 2 x open_fold (logged in ocean/poker/poker.h).
- Throughput probe (RTX 5060 Ti 16 GB, 1024x2): 32,768 agents h16 430K SPS 9 GB; h32 459K SPS 11 GB; minibatch 65,536
  475K SPS 14.1 GB; 65,536 agents runs the league GPU recurrent state out of memory. (vec.total_agents is swept:
  trials so far used 2,048-16,384.)
- Lost trials: trial 0 (the high-throughput start) died at 05:19 with the league GPU out of memory because Gen 9 smoke
  runs shared the GPU; trials 1-3 died at 05:25 when Rust test builds briefly filled the disk. Nothing else may use
  this GPU while the sweep runs.
- Disk guard: prune.sh (copy of leagues/poker/gen10/prune.sh on the box).

Side results: gen9a (leagues/poker/gen9a) wars 29.9%, multi all-in 11.1%, web field +1.60, panel worst +2.18;
gen9b (leagues/poker/gen9b) wars 29.7%, panel worst +2.24, flat training score.

## Overnight (trials 0-17, 2026-10-30 03:06-14:10)

13 trials scored, all at or near the 200M-step floor (Protein drives toward cheap runs). Best poker_score: trial 9
-0.77 (panel_worst +1.51, war_rate 0.23, open_fold 0.003), trial 7 -0.85. Trials that fold preflop (open_fold 0.15-0.51:
trials 4, 5, 6, 10) lose to the bot panel (panel_worst -1.8 to +0.4); loose trials beat it. The bots still reward loose
play, so panel_worst and open_fold pull against each other. war_rate is measured in training (vs pool snapshots and
bots), not at a six-copies table, so it is not directly comparable with gen8's escalation.c 30%.
Trial 17 (minibatch 65,536) ran the league GPU out of memory; the sweep cap is now 32,768. Changing that range made
the sweep start a fresh history (old one: leagues/poker/gen10/sweep.history.old-1793369514); its trial 0 is the
high-throughput center config again (32,768 agents x 32, minibatch 32,768, 1B steps), the one lost on day one.
