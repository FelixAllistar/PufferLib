#!/usr/bin/env bash
# DAgger round 1 on the 2048 Stockfish-BC feature net (2026-11-14). The net plays itself (sampled, temp 0.7 / 1.0,
# from 6-16 ply prefixes of Sep-2025 2000+ games), Stockfish 16 depth 8 labels every ply, and training continues
# from sfbc1m_mf_2048 on the 1M Oct games + 3 copies of the self-play games (held-out games unchanged).
# Why: the BC nets are weakest at plies 80-127 and convert badly (51/120 won games not won vs Maia); the recurrent
# state is not the cause (full history beats 32/64-ply windows), so the fix is labels on the positions we reach.
D=/workspace/sfbc; G=$D/dg; P=/root/venv/bin/python
export OMP_NUM_THREADS=1 CHESS_TOOLS=/workspace/PufferLib/ocean/chess/tools
echo "$(date -u) dagger: self-play start" >> $D/done.txt
for i in $(seq 0 7); do  # batched GPU self-play (selfplay_gpu.py): 8 processes x 4000 games
  T=$([ $((i % 2)) -eq 0 ] && echo 0.7 || echo 1.0)
  $P /workspace/loc/selfplay_gpu.py $D/sfbc1m_mf_2048.bin $D/human_sep.txt $G/sp_$i.txt --games 4000 --batch 512 --seed $((100 + i)) \
    --hidden 2048 --move-feats --temp $T > $G/sp_$i.log 2>&1 &
done; wait
cat $G/sp_*.txt > $G/dagger_games.txt; echo "$(date -u) dagger: $(wc -l < $G/dagger_games.txt) self-play games" >> $D/done.txt
for i in $(seq 0 55); do SF_BIN=/usr/games/stockfish $P /workspace/loc/sf_label.py $G/dagger_games.txt $G/hl_$i.txt --depth 8 --skip $i --every 56 > $G/hl_$i.log 2>&1 & done; wait
cat $G/hl_*.txt > $G/dagger_labeled.txt; echo "$(date -u) dagger: $(wc -l < $G/dagger_labeled.txt) labeled" >> $D/done.txt
cat $D/sf_oct1m.txt $G/dagger_labeled.txt $G/dagger_labeled.txt $G/dagger_labeled.txt > $G/mix1.txt
export BC_ENCODE_LIB=$D/libbc_encode_ct.so
$P /workspace/PufferLib/ocean/chess/tools/bc_train.py $G/mix1.txt $D/sfbc_dagger1_2048.bin --init $D/sfbc1m_mf_2048.bin --teacher --target sf \
  --move-feats --hidden 2048 --val 2000 --batch 256 --window 128 --workers 52 --steps 6000 --lr 3e-4 --save-every 2000 > $D/sfbc_dagger1_2048.log 2>&1
echo "$(date -u) dagger: trained exit $? $(grep held-out $D/sfbc_dagger1_2048.log | tail -1)" >> $D/done.txt
EXTRA=--move-feats bash $D/evalm.sh $D/sfbc_dagger1_2048.bin sfbc_dagger1_2048 2048 2 50
echo "$(date -u) dagger: scored" >> $D/done.txt
