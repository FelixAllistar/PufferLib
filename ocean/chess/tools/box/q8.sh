#!/usr/bin/env bash
# 5080 box queue (2026-11-13): Stockfish-BC on lots of data, per-move lookahead features on every run.
# 1) Stockfish 16 depth-8 labels for the 758K Lichess Oct-2025 games not yet labeled (52 workers); meanwhile stream
#    Lichess Sep 2025 and keep 1M more games with both players 2000+ (next data batch).
# 2) Stockfish-BC with features on the full 1M Oct games, the human BC's recipe (20K steps), 1024 then 2048.
D=/workspace/sfbc; T=/workspace/PufferLib/ocean/chess/tools/bc_train.py; P=/root/venv/bin/python
export BC_ENCODE_LIB=$D/libbc_encode_ct.so
mkdir -p $D/hl4
for i in $(seq 0 51); do SF_BIN=/usr/games/stockfish $P /workspace/loc/sf_label.py $D/human_remaining.txt $D/hl4/hl_$i.txt --depth 8 --skip $i --every 52 > $D/hl4/w_$i.log 2>&1 & done
$P /workspace/PufferLib/ocean/chess/tools/lichess_games.py $D/human_sep.txt --url https://database.lichess.org/standard/lichess_db_standard_rated_2025-09.pgn.zst --games 1000000 --min-elo 2000 --min-base 180 --workers 4 > $D/sep_download.log 2>&1 &
echo "$(date -u) q8: labeling 758K games, downloading Sep 2025" >> $D/done.txt
while ps -eo args= | grep -q "^$P /workspace/loc/sf_label.py"; do sleep 60; done
cat $D/sf_labeled_so_far.txt $D/hl4/hl_*.txt > $D/sf_oct1m.txt; echo "$(date -u) $(wc -l < $D/sf_oct1m.txt) labeled games" >> $D/done.txt
for H in 1024 2048; do
  $P $T $D/sf_oct1m.txt $D/sfbc1m_mf_$H.bin --teacher --target sf --move-feats --hidden $H --val 2000 --batch 256 --window 128 \
    --workers 52 --steps 20000 --lr 1e-3 --save-every 2000 > $D/sfbc1m_mf_$H.log 2>&1
  echo "$(date -u) trained sfbc1m_mf_$H exit $? $(grep held-out $D/sfbc1m_mf_$H.log | tail -1)" >> $D/done.txt
  EXTRA=--move-feats bash $D/evalm.sh $D/sfbc1m_mf_$H.bin sfbc1m_mf_$H $H 2 50; echo "$(date -u) scored sfbc1m_mf_$H" >> $D/done.txt
done
echo "$(date -u) q8 done" >> $D/done.txt
