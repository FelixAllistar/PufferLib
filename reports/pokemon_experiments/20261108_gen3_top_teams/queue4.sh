#!/usr/bin/env bash
# Replaces queue3.sh (2026-11-08): both arms' final checkpoints share the name 0000000499908608.bin and kakuna_eval.sh
# names its results by basename, so the top arm plays through kakuna_ck/top_500m.bin(.magnet) links. Waits for the
# control policy's Kakuna games (started by queue3), then control magnet, top policy, top magnet; 30 games each.
cd /workspace/PufferLib
D=reports/pokemon_experiments/20261108_gen3_top_teams
A=reports/pokemon_experiments/20261107_gen3_encoder_ab
while [ "$(ps -eo args | grep -c '^bash .*kakuna_eval.sh')" != 0 ]; do sleep 20; done
C=$(ls $A/checkpoints_struct/*/*/*.bin | sort | tail -1)
for ck in $C.magnet $D/kakuna_ck/top_500m.bin $D/kakuna_ck/top_500m.bin.magnet; do
  echo "$(date -u +%H:%M) kakuna $ck" >> $D/status.txt
  bash $A/kakuna_eval.sh $ck 30 >> $D/status.txt 2>&1
done
echo QUEUE4_DONE >> $D/status.txt
