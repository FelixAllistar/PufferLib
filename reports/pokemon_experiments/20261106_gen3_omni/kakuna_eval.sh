#!/usr/bin/env bash
# Gen 3 OU vs Metamon Kakuna (2026-11-06; for the fresh Gen 1-recipe model): our pokemon3 checkpoint through the Gen 3 Showdown bridge
# (ocean/pokemon3/showdown: client.js + policy_server, CPU) against Kakuna (metamon.rl.evaluate --eval_type challenge,
# GPU) on Metamon's own local Showdown server. Both sides draw teams from Metamon's gen3ou "competitive" set (20 teams),
# as in Metamon's own evaluations. Same teams and protocol as the earlier Gen 3 runs.
# Usage: kakuna_eval.sh CHECKPOINT N [--sample] ; results in kakuna/<tag>.txt (one line per battle), logs in kakuna/
set -u
CK=$1; N=$2; MODE=${3:-}
D=/workspace/PufferLib/reports/pokemon_experiments/20261106_gen3_omni
TAG=$(basename $CK .bin)${MODE:+_sample}
OUT=$D/kakuna; mkdir -p $OUT
export PATH=/workspace/node/bin:$PATH METAMON_CACHE_DIR=/workspace/metamon_cache
cd /workspace/metamon/server/pokemon-showdown
[ -f config/config.js ] || cp config/config-example.js config/config.js
node pokemon-showdown start 8000 --no-security > $OUT/server_$TAG.log 2>&1 &
SRV=$!
for i in $(seq 90); do grep -q "now listening" $OUT/server_$TAG.log && break; sleep 1; done
. /workspace/metamon_venv/bin/activate
cd /workspace/metamon
python -m metamon.rl.evaluate --eval_type challenge --agent Kakuna --username MetaKakuna3 --opponent_username PufferGen3 \
  --role acceptor --gens 3 --formats ou --total_battles $N --team_set competitive --save_results_to $OUT/metamon_$TAG \
  > $OUT/kakuna_$TAG.log 2>&1 &
K=$!
# The client re-challenges every 15 s until Kakuna (still loading) is online; if Kakuna's process dies, stop the client.
cd /workspace/PufferLib
( while kill -0 $K 2>/dev/null; do sleep 10; done; sleep 5; pkill -P $$ -f client.js 2>/dev/null; echo "Kakuna exited" >> $OUT/kakuna_$TAG.log ) &
NODE_PATH=build/pokemon/showdown_server/node_modules node ocean/pokemon3/showdown/client.js --server ws://localhost:8000/showdown/websocket \
  --name PufferGen3 --checkpoint $CK --challenge MetaKakuna3 --format gen3ou --battles $N $MODE \
  --team-dir $METAMON_CACHE_DIR/teams/competitive/gen3ou --results $OUT/$TAG.txt --logs $OUT/logs_$TAG > $OUT/client_$TAG.log 2>&1
kill $K $SRV 2>/dev/null
W=$(grep -c " win " $OUT/$TAG.txt); T=$(grep -c " tie " $OUT/$TAG.txt); G=$(wc -l < $OUT/$TAG.txt)
echo "$(date -u +%H:%M) $TAG vs Kakuna gen3ou: $W wins, $T ties, $G games" | tee -a $OUT/summary.txt
