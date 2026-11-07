#!/usr/bin/env bash
# Randbat quick check (2026-11-05): ladder 2B model vs itself and vs a max-damage bot, Gen 1 random battles, local server.
# Usage: run.sh N_SELF N_MAXDMG
D=/home/felix/puffertank/pufferlib/reports/pokemon_experiments/20261105_randbat_check; L=/tmp/claude-1000/-home-felix-puffertank/f82c804a-c431-4116-b8d2-86b9f32d1eef/scratchpad/ladder; PS=/home/felix/puffertank/pufferlib/build/pokemon/showdown_server
cd $PS && node node_modules/pokemon-showdown/pokemon-showdown start 8123 --no-security > $D/server.log 2>&1 &
SRV=$!; sleep 12
cd $L
S=ws://localhost:8123/showdown/websocket; CK=${CK:-model/20261028_ladder_2B.bin}; T=teams/egg1_std_zapdos.paste.txt
if [ ${1:-0} -gt 0 ]; then
  node client.js --server $S --name puffa --team $T --checkpoint $CK --accept --format gen1randombattle --logs $D/self_logs > $D/self_a.log 2>&1 &
  A=$!; sleep 3
  node client.js --server $S --name puffb --team $T --checkpoint $CK --challenge puffa --format gen1randombattle --battles $1 --results $D/self.txt --logs $D/self_logs > $D/self_b.log 2>&1
  kill $A
fi
if [ ${2:-0} -gt 0 ]; then
  PS_DIR=$PS node $D/maxdmg.mjs --server $S --name pufmaxdmg7 > $D/maxdmg_bot.log 2>&1 &
  M=$!; sleep 3
  node client.js --server $S --name puffer --team $T --checkpoint $CK --challenge pufmaxdmg7 --format gen1randombattle --battles $2 --results $D/${OUT:-vs_maxdmg}.txt --logs $D/maxdmg_logs > $D/vs_maxdmg.log 2>&1
  kill $M
fi
kill $SRV; echo DONE
