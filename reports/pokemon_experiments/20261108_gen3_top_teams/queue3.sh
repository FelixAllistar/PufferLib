#!/usr/bin/env bash
# Kakuna (2026-11-08, after the superkazam.gin attention fix): control (encoder A/B structured 500M, all teams) and the
# Smogon top-teams arm 500M, policy and magnet, 30 games each. Results in ../20261107_gen3_encoder_ab/kakuna/summary.txt.
cd /workspace/PufferLib
D=reports/pokemon_experiments/20261108_gen3_top_teams
A=reports/pokemon_experiments/20261107_gen3_encoder_ab
C=$(ls $A/checkpoints_struct/*/*/*.bin | sort | tail -1)
T=$(ls $D/checkpoints_top/*/*/*.bin | sort | tail -1)
for ck in $C $C.magnet $T $T.magnet; do
  echo "$(date -u +%H:%M) kakuna $ck" >> $D/status.txt
  bash $A/kakuna_eval.sh $ck 30 >> $D/status.txt 2>&1
done
echo QUEUE3_DONE >> $D/status.txt
