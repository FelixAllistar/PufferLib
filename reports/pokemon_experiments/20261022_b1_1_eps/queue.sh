#!/usr/bin/env bash
# B1.1 queue on the B1 box (2026-10-22): gar eps 0.9, free-pick from scratch eps 0.9, gar eps 0.5.
cd /workspace/PufferLib
D=reports/pokemon_experiments/20261022_b1_1_eps
F=reports/pokemon_experiments/20261022_freepick_league
bash $D/run_gar_eps.sh 0.9 0.5
bash $F/run_freepick.sh $D/freepick_eps0.9_until0.5 --env.expert_random_start=0.9 --env.expert_random_until=0.5 \
  && bash $F/eval_freepick.sh $D/freepick_eps0.9_until0.5
bash $D/run_gar_eps.sh 0.5 0.5
echo QUEUE_DONE
