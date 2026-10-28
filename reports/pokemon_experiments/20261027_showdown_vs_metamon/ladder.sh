#!/usr/bin/env bash
# Ladder runner: plays until TOTAL battles are in ladder.txt; if the client exits early (e.g. Showdown login server
# outage, which it refuses rather than sending a bad token), waits 15 seconds and resumes (login outages come in short bursts). Usage: ladder.sh [TOTAL]
cd /workspace/PufferLib
export PATH=/workspace/node-v24.18.0-linux-x64/bin:$PATH
D=reports/pokemon_experiments/20261027_showdown_vs_metamon; TOTAL=${1:-20}
# Model and team: LADDER_CK / LADDER_TEAM, default the giant run 4B checkpoint with the team it drafts (2026-10-27).
CK=${LADDER_CK:-$(ls reports/pokemon_experiments/20261027_giant/checkpoints/pokemon/*/0000003999793152.bin)}
TEAM=${LADDER_TEAM:-$D/giant4b_freepick.paste.txt}
while :; do
  done_n=$(cat $D/ladder.txt 2>/dev/null | wc -l); left=$((TOTAL - done_n))
  [ $left -le 0 ] && break
  node build/pokemon/showdown_parity/client.js --server wss://sim3.psim.us/showdown/websocket --name felix_pufferlib \
    --status "AI bot (PufferLib RL)" --team $TEAM --checkpoint $CK --ladder --battles $left \
    --results $D/ladder.txt --logs $D/ladder_battles 2>>$D/ladder_debug.log | tee -a $D/ladder.log
  [ $(cat $D/ladder.txt | wc -l) -ge $TOTAL ] && break
  echo "$(date -u) client exited early; retrying in 15 seconds" | tee -a $D/ladder.log; sleep 15
done
echo LADDER_DONE | tee -a $D/ladder.log
