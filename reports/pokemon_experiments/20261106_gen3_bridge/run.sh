#!/usr/bin/env bash
# Gen 3 bridge check (2026-11-06): our pokemon3 checkpoint through ocean/pokemon3/showdown (client.js + policy_server)
# on a local Showdown server, vs a max-damage bot (BOT=--random: random moves), gen3randombattle. Usage: CK=... run.sh N [--sample]
D=$(cd "$(dirname "$0")" && pwd); P=$(cd $D/../../.. && pwd); PS=$P/build/pokemon/showdown_server
export NODE_PATH=$PS/node_modules
cd $PS && node node_modules/pokemon-showdown/pokemon-showdown start 8124 --no-security > $D/server.log 2>&1 &
SRV=$!; for i in $(seq 60); do grep -q "now listening" $D/server.log && break; sleep 1; done
S=ws://localhost:8124/showdown/websocket
PS_DIR=$PS node $D/maxdmg.mjs --server $S --name pufmaxdmg3 --gen 3 --format gen3randombattle $BOT > $D/maxdmg_bot.log 2>&1 &
M=$!; sleep 4
cd $P && node ocean/pokemon3/showdown/client.js --server $S --name pufgen3 --checkpoint $CK --challenge pufmaxdmg3 \
  --format gen3randombattle --battles $1 $2 --results $D/${OUT:-vs_maxdmg}.txt --logs $D/logs > $D/client.log 2>&1
kill $M $SRV; echo DONE
