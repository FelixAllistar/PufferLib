#!/usr/bin/env bash
# Local scoring (2026-11-16): greedy policy, no search, vs Stockfish 16 UCI_Elo 1320 at 500 ms/move on the local
# 4-core machine, eval openings (1000 FENs, colors alternate), 100 games per model in two rounds of 3 processes.
# Only compare numbers from this script with each other: Stockfish's limited strength depends on time and CPU.
cd "$(dirname "$0")"
PY=~/puffertank/.venv/bin/python; P=~/tools/puffer_chess_engine/play.py; H=/mnt/d/puffer_media/chess_local_harness
F=/mnt/d/puffer_media/box_backups/rtx5080_box_60549588/loc/chess_eval/resources/chess/fens.txt
SP=/mnt/d/puffer_media/box_backups/chess_box_60081973/chess_20261101/states_cont/states_cont/checkpoints/chess/states_cont/0000005999951872.bin.magnet
SFBC=/mnt/d/puffer_media/box_backups/rtx5080_box_60549588/sfbc/sfbc1m_mf_2048.bin
for first in 0 50; do
  OMP_NUM_THREADS=1 $PY $P $SP local_sf/sp_best_magnet.jsonl --games 50 --first $first --fens $F > local_sf/sp_best_magnet.$first.log 2>&1 &
  OMP_NUM_THREADS=1 $PY $P $H/mfrl_1084M_bcorder.bin local_sf/mfrl_1084M.jsonl --games 50 --first $first --fens $F --hidden 2048 --move-feats > local_sf/mfrl.$first.log 2>&1 &
  OMP_NUM_THREADS=1 $PY $P $SFBC local_sf/sfbc1m_mf_2048.jsonl --games 50 --first $first --fens $F --hidden 2048 --move-feats > local_sf/sfbc.$first.log 2>&1 &
  wait
done
$PY - <<'PYEOF'
import json, glob, math
for f in sorted(glob.glob('local_sf/*.jsonl')):
    r = [json.loads(l) for l in open(f) if l.strip()]
    s = sum(float(x['result']) for x in r) / len(r)
    w = sum(x['result'] == 1 for x in r); d = sum(x['result'] == 0.5 for x in r)
    print(f"{f} n={len(r)} score={s:.3f} W/D/L {w}/{d}/{len(r)-w-d} elo~{1320 + 400 * math.log10(s / (1 - s)):.0f}" if 0 < s < 1 else (f, len(r), s))
PYEOF
