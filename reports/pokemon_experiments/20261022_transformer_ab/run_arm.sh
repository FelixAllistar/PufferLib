#!/usr/bin/env bash
# Transformer A/B on zam_egg_mie (2026-10-22). Both arms: B1's trial-0 recipe for 700M steps with
# current-policy self-play only (the frozen league banks are PufferNet-only, so neither arm uses them).
#   transformer: entity Transformer encoder (2 layers, width 128, 4 heads) + MinGRU
#   puffernet:   unchanged PufferNet encoder + MinGRU (control)
# Usage: run_arm.sh transformer|puffernet [--extra.overrides]   (from the PufferLib root; resumable via done file)
set -u
ARM=$1
D=reports/pokemon_experiments/20261022_transformer_ab/$ARM
case $ARM in
    transformer) EXTRA="--policy.pokemon_attention_layers=2 --policy.pokemon_attention_width=128 --policy.pokemon_attention_heads=4";;
    puffernet) EXTRA="--policy.pokemon_attention_layers=0";;
    *) echo "usage: $0 transformer|puffernet" >&2; exit 2;;
esac
mkdir -p $D
if [ ! -f $D/trained ]; then
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
    --vec.num_buffers=2 \
    --vec.num_threads=4 \
    --vec.hist_policy_hidden_size=256 \
    --vec.hist_policy_num_layers=2 \
    --vec.action_mask_size=168 \
    --selfplay.max_size=16 \
    --selfplay.seed=42 \
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
    --env.force_core_combos=0 \
    --env.force_core_prob=0 \
    --env.core_pool=all \
    --env.core_seed=76102 \
    --env.core_start_assigned=0 \
    --env.core_start_drafted=0 \
    --env.core_next_assigned=0 \
    --env.core_next_drafted=0 \
    --env.core_observation_version=1 \
    --env.reset_state_bank=None \
    --env.reset_state_prob=0 \
    --env.reset_observation_version=1 \
    --env.behavior_sleep=0 \
    --env.behavior_paralysis=0 \
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
    --env.team_log_interval=0 \
    --env.team_log_path=logs/pokemon/teams.jsonl \
    --env.team_log_max_bytes=8388608 \
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
    --train.total_timesteps=700186624 \
    --base.checkpoint_interval=1336 \
    --metamon.enabled=0 \
    --env.learner_team=species:65,103,121,113,143,128 \
    --env.learner_lead=65 \
    --env.opponent_team=species:65,103,121,113,143,128 \
    --env.opponent_lead=65 \
    --vec.num_policies=1 --vec.hist_policy_percent=0 --selfplay.enabled=0 --selfplay.initial_opponents=None \
    --env.expert_fraction=0 --env.native_league=None \
    --base.checkpoint_dir=$D/checkpoints --base.log_dir=$D/logs $EXTRA "${@:2}" > $D/train.log 2>&1 || exit 1
  touch $D/trained
fi
bash reports/pokemon_experiments/20261022_transformer_ab/eval_arm.sh $ARM
