#!/usr/bin/env bash
# One continuous 503M-step run from old-league master r26 (H64x2), same binary,
# opponents and PPO settings as its r26 stint, except: cosine LR over the whole
# run (0.00298 -> 0.02x), entropy cosine-annealed 0.02 -> 0.0002, and periodic
# checkpoints for eval_watch.sh. Native Metamon hook stays off (v1 projection).
set -euo pipefail
cd /workspace/PufferLib
OLD=leagues/pokemon/rr50m_bf16_20261017
EXP=reports/pokemon_experiments/20261020_r26_anneal
RUN=r26_anneal_503m
mkdir -p $EXP/work/checkpoints $EXP/work/logs
exec $OLD/bin/puffer train --base.env_name=pokemon --base.gpu_offset=0 --base.profile=0 \
  --base.checkpoint_dir=/workspace/PufferLib/$EXP/work/checkpoints --base.log_dir=/workspace/PufferLib/$EXP/work/logs \
  --base.checkpoint_interval=96 --base.eval_episodes=0 --base.eval_agents=-1 --base.eval_greedy=0 \
  --base.load_model_path=/workspace/PufferLib/$OLD/members/master/r000026-a001/weights.bin --base.load_enemy_model_path=None \
  --base.run_id=$RUN --base.result_fd=0 --base.cudagraphs=1 --base.seed=73 --base.reset_every_horizon=0 --base.async=0 \
  --vec.total_agents=16384 --vec.num_buffers=1 --vec.num_threads=4 --vec.num_policies=1 \
  --vec.hist_policy_hidden_size=64 --vec.hist_policy_num_layers=2 --vec.hist_policy_percent=0 \
  --selfplay.enabled=0 --selfplay.max_size=16 --selfplay.seed=42 --selfplay.initial_opponents=None \
  --selfplay.opp_timeout_steps=0 --selfplay.eval_pool_size=0 --selfplay.eval_games=0 --selfplay.eval_bot_games=0 \
  --selfplay.eval_bots=0 --selfplay.eval_bot_envs=8192 --selfplay.eval_bot_threads=0 \
  --env.dr=0 --env.num_agents=1 --env.num_bots=0 --policy.architecture=puffernet --policy.transformer_heads=2 \
  --policy.hidden_size=64 --policy.num_layers=2 --train.gpus=1 --train.total_timesteps=503316480 \
  --train.learning_rate=0.00297837891 --train.anneal_lr=1 --train.min_lr_ratio=0.02 \
  --train.gamma=0.999899983 --train.gae_lambda=0.899999976 --train.replay_ratio=3.71047497 --train.clip_coef=0.12561436 \
  --train.vf_coef=0.52888006 --train.vf_clip_coef=0.2 --train.reward_clip=0 --train.max_grad_norm=0.338345766 \
  --train.ent_coef=0.020000007 --train.anneal_ent_coef=1 --train.min_ent_coef_ratio=0.01 \
  --train.momentum=0.95 --train.minibatch_size=8192 --train.horizon=32 --train.vtrace=0 --train.vtrace_rho_clip=1.0 \
  --train.vtrace_c_clip=1.0 --train.verb_eps=0.0 --train.verb_eps_anneal_start=0.4 --train.verb_eps_anneal_end=1.0 \
  --base.eval_deterministic=0 --vec.action_mask_size=168 --env.force_core_combos=0 --env.force_core_prob=0 \
  --env.core_pool=all --env.core_seed=76102 --env.core_start_assigned=0 --env.core_start_drafted=0 \
  --env.core_next_assigned=0 --env.core_next_drafted=0 --env.core_observation_version=1 \
  --env.reset_state_bank=runs/pokemon_states_20260913/states.pks --env.reset_state_prob=0 --env.reset_observation_version=1 \
  --env.behavior_sleep=0 --env.behavior_paralysis=0 --env.expert_fraction=0.5 \
  --env.native_league=/workspace/PufferLib/$OLD/rounds/r000026/master-opponents.ini \
  --env.seed=42 --env.generation=1 --env.abi_version=3 --env.policy_version=3 \
  --env.rules_sha=3b71ba8b20aad4803f0238df5eb9a4a9f5285a119b00cd953b45901e9f3481d6 \
  --env.format=0 --env.team_selection=1 --env.max_updates=512
