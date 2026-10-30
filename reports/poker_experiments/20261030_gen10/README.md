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
  475K SPS 14.1 GB; 65,536 agents runs the league GPU recurrent state out of memory. The sweep history does not list
  vec.total_agents, so every trial runs at 32,768.
- Disk guard: prune.sh (copy of leagues/poker/gen10/prune.sh on the box).

Side results: gen9a (leagues/poker/gen9a) wars 29.9%, multi all-in 11.1%, web field +1.60, panel worst +2.18;
gen9b (leagues/poker/gen9b) wars 29.7%, panel worst +2.24, flat training score.
