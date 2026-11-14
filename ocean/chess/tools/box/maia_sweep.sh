#!/usr/bin/env bash
# Search settings on the feature net vs top-move Maia 1500 (300 games each, 6 jobs beside the labeling).
cd /workspace/loc; export OMP_NUM_THREADS=1 CHESS_TOOLS=/workspace/PufferLib/ocean/chess/tools
CK=/workspace/sfbc/sfbc200k_1024_mf.bin; O=/workspace/sfbc/ev/maia_sweep; mkdir -p $O
for cfg in "2 5 50" "3 5 50" "2 3 50" "2 8 50" "2 5 0" "3 8 25"; do
  set -- $cfg; name=d$1_k$2_m$3
  for j in 0 1 2 3 4 5; do /root/venv/bin/python play.py $CK $O/$name.jsonl --games 50 --first $((j*50)) --maia 1500 --move-feats --search $1 --k $2 --margin $3 > /dev/null 2>> $O/err.log & done; wait
  /root/venv/bin/python -c "
import json, math
r = [json.loads(l)[\"result\"] for l in open(\"$O/$name.jsonl\")]; s = sum(r) / len(r); c = min(max(s, .001), .999)
print(f\"feature net + search depth $1 top-$2 margin $3: n={len(r)} maia1500 {s:.3f} elo~{1500 + 400 * math.log10(c / (1 - c)):.0f}\")" >> $O/summary.txt
done
