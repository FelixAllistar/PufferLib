#!/usr/bin/env bash
# Goofspiel mega sweep: PufferNet x1000, then Transformer x500. Same GPU, same ranges,
# EMAgnet dimensions included; trial 0 starts with the magnet on (coef 1, tau 0.01).
set -uo pipefail
cd /workspace/PufferLib-mega
M="--train.magnet_coef=1 --train.magnet_tau=0.01 --sweep.max_train_graph_work=0"
bash ocean/goofspiel/sweep.sh puffernet reports/goofspiel_sweeps/mega_puffernet_01 $M --sweep.max_runs=1000
bash ocean/goofspiel/sweep.sh transformer reports/goofspiel_sweeps/mega_transformer_01 $M --sweep.max_runs=500
