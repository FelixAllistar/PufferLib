#!/usr/bin/env bash
# Box eval of one checkpoint (2026-11-13): greedy and greedy + search (play.py --search 2), vs Stockfish 16 at
# UCI_Elo 1320 (1 s/move, 200 games) and top-move Maia 1500 (1 node, 300 games), from the 1000 eval openings,
# colors alternating. Usage: evalm.sh CKPT TAG [HIDDEN] [LAYERS] [JOBS]. Appends to /workspace/sfbc/results.txt.
CK=$1; TAG=$2; H=${3:-1024}; L=${4:-2}; J=${5:-40}
cd /workspace/loc; export OMP_NUM_THREADS=1 CHESS_TOOLS=/workspace/PufferLib/ocean/chess/tools SF_BIN=/usr/games/stockfish
O=/workspace/sfbc/ev/$TAG; mkdir -p $O
run() {  # name games extra-args...
  local name=$1 n=$2; shift 2; local per=$(( (n + J - 1) / J ))
  for j in $(seq 0 $((J - 1))); do
    local first=$((j * per)); [ $first -ge $n ] && break
    local g=$(( first + per > n ? n - first : per ))
    /root/venv/bin/python play.py $CK $O/$name.jsonl --games $g --first $first --hidden $H --layers $L $EXTRA "$@" > /dev/null 2>> $O/err.log &
  done; wait
  /root/venv/bin/python -c "
import json, math
r = [json.loads(l)['result'] for l in open('$O/$name.jsonl')]; s = sum(r) / len(r); c = min(max(s, .001), .999)
anchor = 1500 if 'maia' in '$name' else 1320
print(f'$TAG $name n={len(r)} score={s:.3f} elo~{anchor + 400 * math.log10(c / (1 - c)):.0f}')" >> /workspace/sfbc/results.txt
}
# RUNS (optional) picks a subset, e.g. RUNS="maia1500_greedy maia1500_search"
: ${RUNS:=maia1500_greedy maia1500_search}  # 2026-11-14: Stockfish evals skipped (too slow), Maia only
want() { [ -z "$RUNS" ] || [[ " $RUNS " == *" $1 "* ]]; }
want sf_greedy && run sf_greedy 200 --ms 1000
want sf_search && run sf_search 200 --ms 1000 --search 2
want maia1500_greedy && run maia1500_greedy 300 --maia 1500
want maia1500_search && run maia1500_search 300 --maia 1500 --search 2
