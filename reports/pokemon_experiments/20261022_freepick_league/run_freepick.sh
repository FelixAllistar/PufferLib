#!/usr/bin/env bash
# Free-pick PufferNet vs the upgraded frozen league (2026-10-22).
# B1's trial-0 recipe (magnet KL 0.1, same PPO settings) with:
#   - free-pick drafting: the learner team is None. 75% of games deal 3 required species from the
#     core deck (all 540,274 triples, without replacement), and the policy drafts the other 3 plus
#     all moves. 25% of games are fully free-pick.
#   - 93.75% of games (3,840 of 4,096) vs the 13-bank upgraded league (league/league.ini: per team
#     the better of B0 and B1), the rest current-policy self-play. The learner has one seat in a
#     league game and two in a self-play game, so 88% of its play is vs the league and 12% is
#     self-play. Banks play their own teams. Learner seats (8192 - 3840 = 4352) must divide into
#     the 128 minibatch sequences, which an exact 85% does not.
#   - 2B steps, a checkpoint about every 250M; drafted teams logged every 100th episode per env.
# The core deck needs one synchronous buffer. Eval is fully free-pick vs SyntheticRLV2 and TaurosV0
# (eval_freepick.sh). Usage: run_freepick.sh [RUN_DIR] [--extra.overrides]
cd /workspace/PufferLib
R=${1:-reports/pokemon_experiments/20261022_freepick_league/run}
mkdir -p $R
if [ ! -f $R/trained ]; then
  ./puffer train \
    --base.env_name=pokemon \
    --base.gpu_offset=0 \
    --base.profile=0 \
    --base.eval_episodes=0 \
    --base.eval_agents=-1 \
    --base.eval_greedy=0 \
    --base.load_model_path=None \
    --base.magnet_path=None \
    --base.load_enemy_model_path=None \
    --base.cudagraphs=1 \
    --base.seed=73 \
    --base.reset_every_horizon=0 \
    --base.async=0 \
    --base.eval_deterministic=0 \
    --vec.total_agents=8192 \
    --vec.num_buffers=1 \
    --vec.num_threads=4 \
    --vec.num_policies=14 \
    --vec.hist_policy_hidden_size=256 \
    --vec.hist_policy_num_layers=2 \
    --vec.hist_policy_percent=0.25 \
    --vec.action_mask_size=168 \
    --selfplay.enabled=1 \
    --selfplay.max_size=16 \
    --selfplay.seed=42 \
    --selfplay.initial_opponents=/workspace/PufferLib/reports/pokemon_experiments/20261022_freepick_league/league/opponents.txt \
    --selfplay.opp_timeout_steps=0 \
    --selfplay.eval_pool_size=0 \
    --selfplay.eval_games=0 \
    --selfplay.eval_bot_games=0 \
    --selfplay.eval_bots=0 \
    --selfplay.eval_bot_envs=8192 \
    --selfplay.eval_bot_threads=0 \
    --env.dr=0 \
    --env.num_agents=1 \
    --env.num_bots=0 \
    --env.force_core_combos=1 \
    --env.force_core_prob=0.75 \
    --env.core_pool=all \
    --env.core_seed=76102 \
    --env.core_start_assigned=0 \
    --env.core_start_drafted=0 \
    --env.core_next_assigned=0 \
    --env.core_next_drafted=0 \
    --env.core_observation_version=1 \
    --env.reset_state_bank=runs/pokemon_states_20260913/states.pks \
    --env.reset_state_prob=0 \
    --env.reset_observation_version=1 \
    --env.behavior_sleep=0 \
    --env.behavior_paralysis=0 \
    --env.expert_fraction=0.9375 \
    --env.native_league=reports/pokemon_experiments/20261022_freepick_league/league/league.ini \
    --env.seed=42 \
    --env.generation=1 \
    --env.abi_version=3 \
    --env.policy_version=3 \
    --env.rules_sha=3b71ba8b20aad4803f0238df5eb9a4a9f5285a119b00cd953b45901e9f3481d6 \
    --env.format=0 \
    --env.team_selection=1 \
    --env.max_updates=512 \
    --env.reward_win=1 \
    --env.reward_hp_scale=0 \
    --env.reward_ko_scale=0 \
    --env.personality_paralysis=0 \
    --env.personality_sleep=0 \
    --env.personality_offense=0 \
    --env.personality_defense=0 \
    --env.personality_reserve=0 \
    --env.reward_gamma=0.99989998300000005 \
    --env.team_log_interval=100 \
    --env.team_log_path=$R/teams.jsonl \
    --env.team_log_max_bytes=67108864 \
    --policy.architecture=puffernet \
    --policy.transformer_heads=2 \
    --policy.hidden_size=256 \
    --policy.num_layers=2 \
    --policy.pokemon_embed=32 \
    --policy.pokemon_mid=64 \
    --train.gpus=1 \
    --train.learning_rate=0.000500000082 \
    --train.anneal_lr=1 \
    --train.min_lr_ratio=0.100000001 \
    --train.gamma=0.999899983 \
    --train.gae_lambda=0.899999976 \
    --train.replay_ratio=2 \
    --train.clip_coef=0.12561436 \
    --train.vf_coef=0.52888006 \
    --train.vf_clip_coef=0.199999988 \
    --train.reward_clip=0 \
    --train.max_grad_norm=0.338345736 \
    --train.ent_coef=0.00100000005 \
    --train.anneal_ent_coef=1 \
    --train.min_ent_coef_ratio=0.100000001 \
    --train.magnet_coef=0.100000001 \
    --train.magnet_tau=0.00999999978 \
    --train.anneal_magnet_coef=0 \
    --train.min_magnet_coef_ratio=0.100000001 \
    --train.magnet_reverse=0 \
    --train.momentum=0.949999988 \
    --train.minibatch_size=4096 \
    --train.horizon=32 \
    --train.vtrace=0 \
    --train.vtrace_rho_clip=1 \
    --train.vtrace_c_clip=1 \
    --train.verb_eps=0.0 \
    --train.verb_eps_anneal_start=0.4 \
    --train.verb_eps_anneal_end=1.0 \
    --league.mode=matrix \
    --league.games=256 \
    --league.policy_manifest=None \
    --league.output=None \
    --metamon.interval_fraction=1 \
    --metamon.games=2 \
    --metamon.final_games=48 \
    --metamon.min_free_gb=2 \
    --metamon.python=build/pokemon/metamon-eval-env-cu128/bin/python \
    --metamon.device=cuda \
    --metamon.seed=20261025 \
    --metamon.timeout=3600 \
    --oak.enabled=0 \
    --oak.python=build/pokemon/oak-benchmark-env/bin/python \
    --oak.games=128 \
    --oak.budget=4096 \
    --oak.workers=6 \
    --oak.seed=20261015 \
    --base.checkpoint_dir=$R/checkpoints \
    --base.log_dir=$R/logs \
    --train.total_timesteps=2000000000 \
    --base.checkpoint_interval=954 \
    --metamon.enabled=0 \
    --env.learner_team=None \
    --env.learner_lead=None \
    --env.opponent_team=None \
    --env.opponent_lead=None \
    "${@:2}" > $R/train.log 2>&1 || exit 1
  touch $R/trained
fi
