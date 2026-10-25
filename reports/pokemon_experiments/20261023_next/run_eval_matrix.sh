#!/usr/bin/env bash
# OMNI eval matrix (2026-10-25): each candidate pilots every team in pilot_teams.txt (13 league teams, free-pick
# v1's team and the three FP-discovered teams; species fixed, it drafts its own moves) vs SyntheticRLV2 and
# TaurosV0. Two streams, two candidates each, sharing the GPU with training. Pick the best OMNI by mean over
# teams, worst team as tiebreak.
cd /workspace/PufferLib
N=reports/pokemon_experiments/20261023_next
ck() { ls $N/$1/checkpoints/pokemon/*/$2.bin; }
stream() { while [ $# -gt 0 ]; do bash $N/eval_teams.sh "$(ck $1 $2)" $N/eval_matrix/$1_$2; shift 2; done; }
stream omni_master_1024 0000001999896576 omni_master_256 0000001999896576 > $N/eval_matrix_a.log 2>&1 &
stream omni_master_1024 0000001000341504 omni_master_256 0000001500512256 > $N/eval_matrix_b.log 2>&1 &
wait
echo MATRIX_DONE
