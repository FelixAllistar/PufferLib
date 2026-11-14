#!/usr/bin/env bash
# Rest of q8 (paused 2026-11-14 for Kimodo on the GPU): waits for labeling, then trains + scores 1024 and 2048.
D=/workspace/sfbc; T=/workspace/PufferLib/ocean/chess/tools/bc_train.py; P=/root/venv/bin/python
export BC_ENCODE_LIB=$D/libbc_encode_ct.so


for H in 1024 2048; do
  $P $T $D/sf_oct1m.txt $D/sfbc1m_mf_$H.bin --teacher --target sf --move-feats --hidden $H --val 2000 --batch 256 --window 128 \
    --workers 52 --steps 20000 --lr 1e-3 --save-every 2000 > $D/sfbc1m_mf_$H.log 2>&1
  echo "$(date -u) trained sfbc1m_mf_$H exit $? $(grep held-out $D/sfbc1m_mf_$H.log | tail -1)" >> $D/done.txt
  EXTRA=--move-feats bash $D/evalm.sh $D/sfbc1m_mf_$H.bin sfbc1m_mf_$H $H 2 50; echo "$(date -u) scored sfbc1m_mf_$H" >> $D/done.txt
done
echo "$(date -u) q8 done" >> $D/done.txt
