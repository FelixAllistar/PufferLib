#!/usr/bin/env bash
# Poker training speed bench (2026-10-29): gen4 recipe, one change per row, ~100 s each; prints the last dashboard SPS and
# the Evaluate/Env/Train time shares. Fixed league: gen3 + gen4 256x3 checkpoints (like late training, 70% league seats).
cd /workspace/PufferLib
O=reports/poker_experiments/20261029_speed; mkdir -p $O
G=leagues/poker/gen4
printf 'poker_v1 6 256 3 3\n%s\n%s\n%s\n' $G/gen3.bin $G/gen4_control.bin $G/gen4_states.bin > $G/league_fixed.txt
run() {
  name=$1; shift
  timeout 110 ./puffer train --base.env_name=poker --train.total_timesteps=2000000000 "$@" > $O/$name.log 2>&1
  t=$(sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' $O/$name.log | tr -s ' ' | grep -a 'SPS\|Evaluate\|Env \|Train \|Steps' | tail -5 | tr '\n' ' ')
  echo "$name | $t" | tee -a $O/bench.txt
}
NOL="--env.native_league=None"
FIX="--env.native_league=$G/league_fixed.txt --env.league_pool_dir=None --env.league_ramp=0"
run bots_all       $NOL
run bots_cheap     $NOL --env.native_baseline_mask=770
run bots_range     $NOL --env.native_baseline_mask=128
run bots_tight     $NOL --env.native_baseline_mask=1024
run bots_trapper   $NOL --env.native_baseline_mask=2048
run league70_gpu   $FIX
run league70_cpu   $FIX --env.league_gpu=0
run bots_all_buf2  $NOL --vec.num_buffers=2
run bots_all_thr12 $NOL --vec.num_threads=12
echo BENCH_DONE | tee -a $O/bench.txt
