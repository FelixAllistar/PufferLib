#!/usr/bin/env bash
# Teacher test 2026-11-18: trial 25 hypers (1024x2, from the 1024 sweep), 400M steps. R=run id TP=teacher share
# SWING=1 turns on the drop ladder (CHESS_TEACH_SWING). BIN=puffer_chess_drop.
source /workspace/env.sh; cd /workspace/chess_teach
BIN=${BIN:-puffer_chess_drop}; H=1024; R=$R; STEPS=${STEPS:-400000000}; TP=${TP:-0.3}; THREADS=26
A=/workspace/runs; mkdir -p $A/$R
unset CHESS_TEACH_SWING; [ "${SWING:-0}" = 1 ] && export CHESS_TEACH_SWING=1
CHESS_TEACHER_STATE=$A/$R/teacher_state.txt OMP_NUM_THREADS=$THREADS /workspace/PufferLib/$BIN train --headless \
--base.env_name=chess --base.gpu_offset=0 --base.profile=0 --base.checkpoint_dir=$A/$R/checkpoints --base.log_dir=$A/$R/logs \
--base.checkpoint_interval=${CKI:-381} --base.eval_episodes=0 --base.eval_agents=-1 --base.eval_greedy=0 --base.enemy_greedy=-1 \
--base.load_model_path=None --base.magnet_path=None --base.load_enemy_model_path=None --base.run_id=$R \
--base.cudagraphs=1 --base.seed=73 --base.reset_every_horizon=0 --base.async=1 --base.resume=0 --vec.total_agents=2048 \
--vec.num_buffers=2 --vec.num_threads=$THREADS --vec.num_policies=2 --vec.hist_policy_hidden_size=$H --vec.hist_policy_num_layers=2 \
--vec.hist_policy_percent=0.0331672989 --selfplay.enabled=1 --selfplay.max_size=500 --selfplay.seed=42 \
--selfplay.initial_opponents=None --selfplay.opp_timeout_steps=100000000 --selfplay.eval_pool_size=8 --selfplay.eval_games=0 \
--selfplay.eval_bot_games=0 --selfplay.eval_bots=0 --bot_eval.mode=4 --bot_eval.fen_curric_pct=0 --bot_eval.perf_book_weight=0 \
--env.dr=0 --env.num_agents=1 --env.num_bots=0 --env.max_moves=5000 --env.reward_draw=0 --env.reward_invalid_piece=0 \
--env.reward_invalid_move=0 --env.reward_repetition=0 --env.book_reward=0 --env.book_penalty=0 --env.perf_book_weight=0 \
--env.enable_50_move_rule=1 --env.enable_threefold_repetition=1 --env.mode=1 --env.random_fen=0 --env.render_fps=30 \
--env.fen_curric_pct=0.9 --env.teacher_pct=$TP --policy.hidden_size=$H --policy.num_layers=2 --train.gpus=1 --train.budget=agents \
--train.total_timesteps=$STEPS --train.learning_rate=0.00564299198 --train.anneal_lr=1 --train.min_lr_ratio=0.284700155 \
--train.gamma=0.995710135 --train.gae_lambda=0.926995635 --train.replay_ratio=1 --train.clip_coef=0.553786278 \
--train.vf_coef=5.43505192 --train.vf_clip_coef=1.60018265 --train.reward_clip=1.0 --train.max_grad_norm=2.04927731 \
--train.ent_coef=0.00452633994 --train.anneal_ent_coef=1 --train.min_ent_coef_ratio=0.205719382 --train.magnet_coef=0.00924349111 \
--train.magnet_tau=0.0354384109 --train.anneal_magnet_coef=0 --train.min_magnet_coef_ratio=0.1 --train.magnet_reverse=0 \
--train.momentum=0.934445381 --train.minibatch_size=4096 --train.horizon=128 --train.vtrace=0 --train.vtrace_rho_clip=1.47163081 \
--train.vtrace_c_clip=1 --train.verb_eps=0.0 --train.verb_eps_anneal_start=0.4 --train.verb_eps_anneal_end=1.0 >> $A/$R/train.log 2>&1
echo "$(date -u) $R exit $?" >> $A/arms_done.txt
