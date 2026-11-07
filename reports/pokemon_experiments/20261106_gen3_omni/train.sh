#!/usr/bin/env bash
# Gen 3 OU, fresh model with the Gen 1 recipe (2026-11-06). 1024x2 from random weights, 4B steps.
# - OMNI: every seat plays a random team from 172,594 Metamon gen3ou teams (exact sets; 1,000 held out, plus the
#   20-team competitive set for Kakuna), built in the env (pk3_new_teams, 1998/2000 turn-1 states equal to Showdown's).
# - Free-pick: 30% of seats draft their own team (species from the 128 most used, then one of its 6 most used sets).
# - Forced cores: half of the learner's drafts start with a common species pair, annealed to 0 at 2B steps.
# - League: 25% of games against 4 frozen snapshots of this run (rotated every 200M from its checkpoints).
# - EMAgnet 0.03, entropy 0.01 annealed to 0.001, lr 5e-4 annealed to 1.25e-4, gamma 0.999, replay_ratio 1.
. /workspace/env.sh
cd /workspace/PufferLib
export OMP_NUM_THREADS=11
D=reports/pokemon_experiments/20261106_gen3_omni
T=build/pokemon3/ou3
./puffer_pk3 train --base.env_name=pokemon3 --policy.hidden_size=1024 --policy.num_layers=2 \
    --train.total_timesteps=4000000000 --vec.num_threads=11 --vec.total_agents=4096 \
    --env.sets=$T/sets.txt --env.pool=$T/train.txt --env.draft=$T/draft.txt --env.cores=$T/cores.txt \
    --env.free_fraction=0.3 --env.force_core_prob=0.5 --env.force_core_steps=488281 \
    --selfplay.enabled=1 --vec.num_policies=5 --vec.hist_policy_percent=0.25 --selfplay.max_size=16 \
    --selfplay.opp_timeout_steps=200000000 --selfplay.eval_bot_games=0 \
    --train.learning_rate=0.0005 --train.min_lr_ratio=0.25 --train.ent_coef=0.01 --train.anneal_ent_coef=1 \
    --train.min_ent_coef_ratio=0.1 --train.gamma=0.999 --train.magnet_coef=0.03 --train.magnet_tau=0.01 \
    --base.checkpoint_interval=500 --base.checkpoint_dir=$D/checkpoints 2>&1 | tee -a $D/train.log
