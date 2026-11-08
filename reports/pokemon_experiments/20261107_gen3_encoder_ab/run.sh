#!/usr/bin/env bash
# Gen 3 structured-encoder A/B (2026-11-07, Vast 59640753; 59637366 had a faulty GPU, 6.6 GB/s memory). Same binary (puffer_pk3s), seed and recipe as
# ../20261106_gen3_matchup_ab arm "on" (matchup features on, pool teams, no drafting, 25% league vs 4 frozen
# snapshots, EMAgnet 0.03, 256x2, 500M steps). Only policy.pokemon3_net differs: 1 = structured encoder + pointer
# decoder (ocean/pokemon3/pokemon3_encoder.cu), 0 = flat default net. Arms run one after the other; struct first.
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=24
D=reports/pokemon_experiments/20261107_gen3_encoder_ab
T=build/pokemon3/ou3
for arm in struct flat; do
  n=$([ $arm = struct ] && echo 1 || echo 0)
  echo "$(date -u +%H:%M) start $arm" >> $D/status.txt
  ./puffer_pk3s train --base.env_name=pokemon3 --policy.hidden_size=256 --policy.num_layers=2 --policy.pokemon3_net=$n \
    --train.total_timesteps=500000000 --vec.num_threads=24 --vec.total_agents=4096 \
    --env.sets=$T/sets.txt --env.pool=$T/train.txt --env.free_fraction=0 --env.matchup=1 \
    --selfplay.enabled=1 --vec.num_policies=5 --vec.hist_policy_percent=0.25 --selfplay.max_size=16 \
    --selfplay.opp_timeout_steps=50000000 --selfplay.eval_bot_games=0 \
    --train.learning_rate=0.0005 --train.min_lr_ratio=0.25 --train.ent_coef=0.01 --train.anneal_ent_coef=1 \
    --train.min_ent_coef_ratio=0.1 --train.gamma=0.999 --train.magnet_coef=0.03 --train.magnet_tau=0.01 \
    --base.checkpoint_interval=400 --base.checkpoint_dir=$D/checkpoints_$arm > $D/train_$arm.log 2>&1
  echo "$(date -u +%H:%M) done $arm exit=$?" >> $D/status.txt
done
echo AB_DONE >> $D/status.txt
