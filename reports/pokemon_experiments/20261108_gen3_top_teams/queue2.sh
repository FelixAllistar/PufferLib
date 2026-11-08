#!/usr/bin/env bash
# Box queue v2 (2026-11-08): the Smogon top-teams arm (run.sh + watch.sh), then Kakuna vs the encoder A/B's structured
# 500M policy and magnet (30 games each). Replaces queue.sh (its own-policy team scoring is parked; see README).
cd /workspace/PufferLib
D=reports/pokemon_experiments/20261108_gen3_top_teams
A=reports/pokemon_experiments/20261107_gen3_encoder_ab
tmux new-window -t encab -n topwatch "bash $D/watch.sh; sleep 86400"
bash $D/run.sh
CK=$(ls $A/checkpoints_struct/*/*/*.bin | sort | tail -1)
bash $A/kakuna_eval.sh $CK 30 >> $D/status.txt 2>&1
bash $A/kakuna_eval.sh $CK.magnet 30 >> $D/status.txt 2>&1
echo QUEUE2_DONE >> $D/status.txt
