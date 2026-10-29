# Poker training speed and the raise-size collapse (2026-10-29, box 57608917)

## Speed (speed_bench.sh, bench.txt; gen4 recipe, ~100 s per row, steady-state steps/s)
- Bots only: 815K (all five), 862K (rules/maniac/station), 784K (range), 815K (tight), 650K (trapper). The env is
  4-22% of the time, so the simulator and the bots are not the bottleneck. 2 buffers: 832K; 12 threads: 666K.
- Frozen league opponents (70% of seats, gen3/gen4 banks): 11.6K on GPU, 10.8K on CPU: 70x slower.
- Cause: OpenMP oversubscription. The league loops in poker.h had no num_threads clause, and the container reports 256
  CPUs against a 24.6-CPU quota. OMP_NUM_THREADS=24: 321K (8: 288K). Fixed in poker.h (num_threads(vec.num_threads /
  num_buffers), like the trainer's loops): 295K with no environment variable. Every earlier run slowed down as the
  league ramp handed seats from bots to snapshots; the old poker box very likely had the same problem.

## Raise-size collapse (ocean/poker/escalation.c; escalation_*.txt)
- gen4 control and gen3 almost never make a sized raise: unopened pots 91% check/call, 9% all in, 0.1% sized raises;
  facing a bet 37% fold, 59% call, 3% all in. Their only raise is all in, which is the user's "a little, then a lot".
- Likely cause: the trainer does not normalize advantages, so with rewards in raw BB the entropy bonus (0.002) is
  negligible; entropy reaches ~0.04 within 100M steps. New env metrics: raise_rate, allin_of_raises (control run:
  0.11 / 0.95 after one minute).
- Test (gen5_ab.sh): control vs env.reward_scale 0.01 vs train.ent_coef 0.02, 300M each.
