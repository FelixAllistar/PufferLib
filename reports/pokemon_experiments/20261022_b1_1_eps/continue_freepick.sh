#!/usr/bin/env bash
# Continue the curriculum free-pick run (2026-10-23): load its 2B checkpoint and train 2B more steps
# against the full-strength league (eps 0), same recipe otherwise. The LR schedule restarts, so the
# peak LR is halved (2.5e-4, cosine to 0.1x) to avoid knocking the trained policy off course.
# Waits for the B1.1 queue to finish. One watcher scores checkpoints as they appear.
cd /workspace/PufferLib
D=reports/pokemon_experiments/20261022_b1_1_eps
F=reports/pokemon_experiments/20261022_freepick_league
R=$D/freepick_eps0.9_continue_2b
until grep -q QUEUE_DONE $D/queue.log 2>/dev/null; do sleep 60; done
CK=$(ls $D/freepick_eps0.9_until0.5/checkpoints/pokemon/*/0000001999896576.bin)
echo "continuing from $CK"
mkdir -p $R
tmux new -d -s fp2_eval "until [ -f $R/trained ]; do sleep 300; bash $F/eval_freepick.sh $R; done; bash $F/eval_freepick.sh $R; echo EVALS_DONE; exec bash"
bash $F/run_freepick.sh $R --base.load_model_path=$CK --env.expert_random_start=0 --train.learning_rate=0.00025
echo CONTINUE_DONE
