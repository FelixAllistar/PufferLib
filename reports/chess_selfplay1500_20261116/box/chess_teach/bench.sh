#!/usr/bin/env bash
# Speed / short-run launcher: H=hidden R=run id STEPS TP=teacher share THREADS START (optional checkpoint, with .magnet) LR
source /workspace/env.sh; cd /workspace/chess_teach
BIN=${BIN:-puffer_chess_teach}; H=${H:-256}; R=${R:-bench$H}; STEPS=${STEPS:-8000000}; TP=${TP:-0.3}; THREADS=${THREADS:-11}; LR=${LR:-0.002}
START=${START:-None}; MAG=None; [ "$START" != None ] && MAG=$START.magnet
A=/workspace/runs; mkdir -p $A/$R
CHESS_TEACHER_STATE=$A/$R/teacher_state.txt OMP_NUM_THREADS=$THREADS /workspace/PufferLib/$BIN train --headless \
--base.env_name=chess --base.gpu_offset=0 --base.profile=0 --base.checkpoint_dir=$A/$R/checkpoints --base.log_dir=$A/$R/logs \
--base.checkpoint_interval=${CKI:-1000} --base.eval_episodes=0 --base.eval_agents=-1 --base.eval_greedy=0 --base.enemy_greedy=-1 \
--base.load_model_path=$START --base.magnet_path=$MAG --base.load_enemy_model_path=None --base.run_id=$R \
--base.cudagraphs=1 --base.seed=${SEED:-73} --base.reset_every_horizon=0 --base.async=1 --base.resume=${RES:-0} --vec.total_agents=2048 \
--vec.num_buffers=2 --vec.num_threads=$THREADS --vec.num_policies=2 --vec.hist_policy_hidden_size=$H --vec.hist_policy_num_layers=2 \
--vec.hist_policy_percent=0.0625 --selfplay.enabled=1 --selfplay.max_size=500 --selfplay.seed=42 \
--selfplay.initial_opponents=None --selfplay.opp_timeout_steps=100000000 --selfplay.eval_pool_size=8 --selfplay.eval_games=0 \
--selfplay.eval_bot_games=0 --selfplay.eval_bots=0 --bot_eval.mode=4 --bot_eval.fen_curric_pct=0 --bot_eval.perf_book_weight=0 \
--env.dr=0 --env.num_agents=1 --env.num_bots=0 --env.max_moves=5000 --env.reward_draw=0 --env.reward_invalid_piece=0 \
--env.reward_invalid_move=0 --env.reward_repetition=0 --env.book_reward=0 --env.book_penalty=0 --env.perf_book_weight=0 \
--env.enable_50_move_rule=1 --env.enable_threefold_repetition=1 --env.mode=1 --env.random_fen=0 --env.render_fps=30 \
--env.fen_curric_pct=0.9 --env.teacher_pct=$TP --policy.hidden_size=$H --policy.num_layers=2 --train.gpus=1 --train.budget=agents \
--train.total_timesteps=$STEPS --train.learning_rate=$LR --train.anneal_lr=1 --train.min_lr_ratio=0.4 \
--train.gamma=0.994711041 --train.gae_lambda=0.871022522 --train.replay_ratio=1 --train.clip_coef=0.489849299 \
--train.vf_coef=4.80991316 --train.vf_clip_coef=1.57298505 --train.reward_clip=1.0 --train.max_grad_norm=1.17061818 \
--train.ent_coef=${ENT:-0.0058005} --train.anneal_ent_coef=${ENTA:-1} --train.min_ent_coef_ratio=0.1 --train.magnet_coef=0.00300000072 \
--train.magnet_tau=0.00501546497 --train.anneal_magnet_coef=0 --train.min_magnet_coef_ratio=0.1 --train.magnet_reverse=0 \
--train.momentum=0.9164415 --train.minibatch_size=8192 --train.horizon=128 --train.vtrace=0 --train.vtrace_rho_clip=1.47163081 \
--train.vtrace_c_clip=1 --train.verb_eps=0.0 --train.verb_eps_anneal_start=0.4 --train.verb_eps_anneal_end=1.0 "$@" >> $A/$R/train.log 2>&1
echo "$R exit $?" >> $A/done.txt
