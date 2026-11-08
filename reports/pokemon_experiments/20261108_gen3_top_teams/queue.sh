#!/usr/bin/env bash
# Box queue (2026-11-08), one job at a time after the encoder A/B: build puffer_pk3t (env.top_pool), score the teams
# with the A/B's structured 500M magnet, Kakuna vs the structured 500M policy and magnet (30 games each), then the
# top-teams 256x2 arm (run.sh) with its eval watcher (watch.sh).
. /workspace/env.sh
cd /workspace/PufferLib
D=reports/pokemon_experiments/20261108_gen3_top_teams
A=reports/pokemon_experiments/20261107_gen3_encoder_ab
until grep -q WATCH_DONE $A/evals.txt 2>/dev/null; do sleep 60; done
echo "$(date -u +%H:%M) build" >> $D/status.txt
./build.sh pokemon3 puffer_pk3t > $D/build.log 2>&1 || { echo "build failed" >> $D/status.txt; exit 1; }
CK=$(ls $A/checkpoints_struct/*/*/*.bin | sort | tail -1)
bash $D/score.sh $CK.magnet 3500000 2000000
until grep -q KAKUNA_SETUP_DONE /workspace/kakuna_setup.log 2>/dev/null; do sleep 60; done
bash $A/kakuna_eval.sh $CK 30 >> $D/status.txt 2>&1
bash $A/kakuna_eval.sh $CK.magnet 30 >> $D/status.txt 2>&1
[ -s $D/top1000.txt ] || { echo "no top1000.txt" >> $D/status.txt; exit 1; }
tmux new-window -t encab -n topwatch "bash $D/watch.sh; sleep 86400"
bash $D/run.sh
