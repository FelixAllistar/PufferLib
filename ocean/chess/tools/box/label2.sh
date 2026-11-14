#!/usr/bin/env bash
# Finish Stockfish labels for the 56,857 Oct games left when q8 was stopped (2026-11-14); 40 workers so Kimodo keeps CPUs.
D=/workspace/sfbc; P=/root/venv/bin/python
for i in $(seq 0 39); do SF_BIN=/usr/games/stockfish $P /workspace/loc/sf_label.py $D/human_remaining2.txt $D/hl5/hl_$i.txt --depth 8 --skip $i --every 40 > $D/hl5/w_$i.log 2>&1 & done
wait
cat $D/sf_labeled_so_far.txt $D/hl4/hl_*.txt $D/hl5/hl_*.txt > $D/sf_oct1m.txt
echo "$(date -u) labels done: $(wc -l < $D/sf_oct1m.txt) labeled games in sf_oct1m.txt (training paused; resume: bash q8_train.sh)" >> $D/done.txt
