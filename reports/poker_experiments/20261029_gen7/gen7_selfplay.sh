#!/usr/bin/env bash
# gen7 self-play (2026-10-29): gen5 plays calmly vs bots but tables of six copies fall into re-raise wars (escalation.c
# self field: 82.7% of hands have a street with 3+ raises, 71.7% end with 2+ players all in), because its opponents were
# mostly bots and old snapshots. From gen6's final checkpoint, 95% of non-panel seats are copies of itself
# (rules_fraction 0.05; 20% panel tables keep the bots), reward_scale 0.01, 1B steps each:
#   A: league_active 1, opponents are only the newest snapshot (near-pure self-play, <= ~10M steps behind);
#   B: league_active 4, the most recent four snapshots (more variety, against cycling).
# Then each gets the escalation probe (incl. six-copies tables), the panel, the exploit probe and the all-in rate.
cd /workspace/PufferLib
while pgrep -f '^[.]/puffer train' > /dev/null; do sleep 30; done   # wait for gen6
G=leagues/poker/gen7; mkdir -p $G
dir6=$(grep -a "live pool from" leagues/poker/gen6/train.log | head -1 | sed 's/.*live pool from //')
cp $(ls $dir6/*.bin | grep -v magnet | sort | tail -1) leagues/poker/gen6/gen6.bin
printf 'poker_v1 6 256 3 1\n%s\n' leagues/poker/gen6/gen6.bin > $G/start.txt
echo "== gen6 (start)" >> $G/results.txt
build/poker/escalation leagues/poker/gen6/gen6.bin 256 3 6000 >> $G/results.txt
build/poker/panel leagues/poker/gen6/gen6.bin 256 3 --hands 20000 | grep "PANEL\|vs 5" >> $G/results.txt
run() {
  name=$1; shift
  ./puffer train --base.env_name=poker --base.load_model_path=leagues/poker/gen6/gen6.bin --env.reward_scale=0.01 \
    --env.native_league=$G/start.txt --env.league_pool_dir=auto --env.league_ramp=0 --env.rules_fraction=0.05 \
    --train.total_timesteps=1000000000 "$@" > $G/$name.log 2>&1
  dir=$(grep -a "live pool from" $G/$name.log | head -1 | sed 's/.*live pool from //')
  ck=$(ls $dir/*.bin | grep -v magnet | sort | tail -1); cp $ck $G/$name.bin
  tail=$(sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' $G/$name.log | tr -s ' ' | grep -a 'entropy\|panel_worst\|raise_rate' | tail -3 | tr '\n' ' ' | tr -s '│ ' ' ')
  echo "== $name ($ck) $tail" >> $G/results.txt
  build/poker/escalation $G/$name.bin 256 3 6000 >> $G/results.txt
  build/poker/panel $G/$name.bin 256 3 --hands 20000 | grep "PANEL\|vs 5" >> $G/results.txt
  build/poker/exploit 3000 100 $G/$name.bin 256 3 | grep "^policy" >> $G/results.txt
  build/poker/allin $G/$name.bin >> $G/results.txt
}
run sp_newest --env.league_active=1
run sp_recent4 --env.league_active=4
echo GEN7_DONE >> $G/results.txt
