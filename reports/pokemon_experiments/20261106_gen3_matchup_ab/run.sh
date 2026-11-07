#!/usr/bin/env bash
# Gen 3 matchup-feature A/B (2026-11-06). Same binary (puffer_pk3m, obs 5343), same seed and recipe; the only
# difference is env.matchup: 1 = per-move effectiveness / damage / KO and per-Pokemon threat against the other active
# (lib.rs write_matchups), 0 = those features zeroed. 256x2 (test size), 500M steps each, run one after the other
# (one training run per box). Battle skill only: pool teams, no drafting. League as the fresh run: 25% of games vs 4
# frozen snapshots. Judged by watch.sh: win rate vs the in-engine max-damage bot (env.bot_policy=2) on held-out teams.
# Starts when the gen3omni run (tmux gen3omni:train) and its watcher are done.
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=11
D=reports/pokemon_experiments/20261106_gen3_matchup_ab
O=reports/pokemon_experiments/20261106_gen3_omni
T=build/pokemon3/ou3
while tmux list-panes -t gen3omni:train >/dev/null 2>&1 || ! grep -q RUN_DONE $O/evals.txt; do sleep 30; done
for arm in on off; do
  m=$([ $arm = on ] && echo 1 || echo 0)
  echo "$(date -u +%H:%M) start $arm" >> $D/status.txt
  ./puffer_pk3m train --base.env_name=pokemon3 --policy.hidden_size=256 --policy.num_layers=2 \
    --train.total_timesteps=500000000 --vec.num_threads=11 --vec.total_agents=4096 \
    --env.sets=$T/sets.txt --env.pool=$T/train.txt --env.free_fraction=0 --env.matchup=$m \
    --selfplay.enabled=1 --vec.num_policies=5 --vec.hist_policy_percent=0.25 --selfplay.max_size=16 \
    --selfplay.opp_timeout_steps=50000000 --selfplay.eval_bot_games=0 \
    --train.learning_rate=0.0005 --train.min_lr_ratio=0.25 --train.ent_coef=0.01 --train.anneal_ent_coef=1 \
    --train.min_ent_coef_ratio=0.1 --train.gamma=0.999 --train.magnet_coef=0.03 --train.magnet_tau=0.01 \
    --base.checkpoint_interval=200 --base.checkpoint_dir=$D/checkpoints_$arm > $D/train_$arm.log 2>&1
  echo "$(date -u +%H:%M) done $arm exit=$?" >> $D/status.txt
done
echo AB_DONE >> $D/status.txt
