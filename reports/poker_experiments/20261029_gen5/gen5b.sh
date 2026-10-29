#!/usr/bin/env bash
# gen5b (2026-10-29): reward_scale 0.01 kept raise sizes alive but stayed too random at 300M; middle scales and a 1B run. From gen5_ab: does the raise-size collapse (fold / call / all in only) come from the reward scale? gen4 recipe
# (config/poker.ini, states off), 300M steps each, one run at a time; then each final checkpoint gets the escalation
# probe, the offline panel, the exploit probe and the all-in rate. Results: leagues/poker/gen5/results_b.txt.
cd /workspace/PufferLib
G=leagues/poker/gen5; mkdir -p $G
FH=leagues/poker/gen4/fullhouse_weights.h
clang -O2 -march=native -std=c11 -DNDEBUG -DPOKER_FULLHOUSE_MODEL="\"$PWD/$FH\"" -Iocean/poker ocean/poker/escalation.c -lm -o build/poker/escalation
clang -O2 -march=native -ffp-contract=off -std=c11 -DNDEBUG ocean/poker/panel.c -lm -o build/poker/panel
clang -O2 -march=native -ffp-contract=off -std=c11 -DNDEBUG -Iocean/poker ocean/poker/exploit.c -lm -o build/poker/exploit
clang -O2 -march=native -std=c11 -DNDEBUG -Iocean/poker ocean/poker/allin.c -lm -o build/poker/allin
run() {
  name=$1; shift
  ./puffer train --base.env_name=poker "$@" > $G/$name.log 2>&1
  dir=$(grep -a "live pool from" $G/$name.log | head -1 | sed 's/.*live pool from //')
  ck=$(ls $dir/*.bin | grep -v magnet | sort | tail -1); cp $ck $G/$name.bin
  tail=$(sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' $G/$name.log | tr -s ' ' | grep -a 'SPS\|entropy\|panel_worst\|raise_rate' | tail -4 | tr '\n' ' ' | tr -s '│ ' ' ')
  echo "== $name ($ck) $tail" >> $G/results_b.txt
  build/poker/escalation $G/$name.bin 256 3 8000 >> $G/results_b.txt
  build/poker/panel $G/$name.bin 256 3 --hands 20000 | grep "PANEL\|vs 5" >> $G/results_b.txt
  build/poker/exploit 3000 100 $G/$name.bin 256 3 | grep "^policy" >> $G/results_b.txt
  build/poker/allin $G/$name.bin >> $G/results_b.txt
}



run scale01 --env.reward_scale=0.1
run scale003 --env.reward_scale=0.03
run scale001_1b --env.reward_scale=0.01 --train.total_timesteps=1000000000
echo GEN5B_DONE >> $G/results_b.txt
