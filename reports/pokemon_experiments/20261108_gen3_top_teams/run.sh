#!/usr/bin/env bash
# Gen 3 top-teams A/B arm (2026-11-08): the encoder A/B's structured arm (../20261107_gen3_encoder_ab, struct = control,
# every seat a uniform pool team) with 75% of pool seats drawn from Smogon's ADV teams instead (build/pokemon3/ou3s/
# smogon_top.txt: current samples twice, older samples and Teams Through The Ages once; see smogon/). Same seed,
# recipe and 500M steps; ou3s is ou3 with the Smogon sets appended (old indices unchanged).
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=24
D=reports/pokemon_experiments/20261108_gen3_top_teams
T=build/pokemon3/ou3s
for arm in top; do
  n=1
  echo "$(date -u +%H:%M) start $arm" >> $D/status.txt
  ./puffer_pk3t train --base.env_name=pokemon3 --policy.hidden_size=256 --policy.num_layers=2 --policy.pokemon3_net=$n \
    --train.total_timesteps=500000000 --vec.num_threads=24 --vec.total_agents=4096 \
    --env.sets=$T/sets.txt --env.pool=$T/train.txt --env.free_fraction=0 --env.matchup=1 --env.top_pool=$T/smogon_top.txt --env.top_fraction=0.75 \
    --selfplay.enabled=1 --vec.num_policies=5 --vec.hist_policy_percent=0.25 --selfplay.max_size=16 \
    --selfplay.opp_timeout_steps=50000000 --selfplay.eval_bot_games=0 \
    --train.learning_rate=0.0005 --train.min_lr_ratio=0.25 --train.ent_coef=0.01 --train.anneal_ent_coef=1 \
    --train.min_ent_coef_ratio=0.1 --train.gamma=0.999 --train.magnet_coef=0.03 --train.magnet_tau=0.01 \
    --base.checkpoint_interval=400 --base.checkpoint_dir=$D/checkpoints_$arm > $D/train_$arm.log 2>&1
  echo "$(date -u +%H:%M) done $arm exit=$?" >> $D/status.txt
done
echo AB_DONE >> $D/status.txt
