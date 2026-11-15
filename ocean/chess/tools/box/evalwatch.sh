#!/usr/bin/env bash
# Scores mfrl checkpoints about every 1B steps vs top-move Maia 1500 (evalm.sh: greedy + search, 300 games each,
# --move-feats --no-repeat as the start net sfbc1m_mf_2048_norep: 1713 / 1854). 20 CPU jobs beside training.
CK=/workspace/pmf/runs/mfrl/checkpoints/chess/mfrl; E=/workspace/pmf/evck; mkdir -p $E; next=250000000
while true; do
  for f in $(ls $CK/0*.bin 2>/dev/null | grep -v magnet | sort); do
    s=$((10#$(basename $f .bin)))
    if [ $s -ge $next ]; then
      sleep 30; /root/venv/bin/python /workspace/pmf/mf_to_native.py --reverse $f $E/mfrl_$s.bin > /dev/null
      EXTRA="--move-feats --no-repeat" bash /workspace/sfbc/evalm.sh $E/mfrl_$s.bin mfrl_$s 2048 2 20
      echo "$(date -u) scored $s" >> /workspace/pmf/evalwatch.log; next=$((s + 250000000))
    fi
  done
  pgrep -f "puffer_chess_mf train" > /dev/null || ls $CK/0*.bin | grep -v magnet | sort | tail -1 | grep -q . && [ -f /workspace/pmf/runs/mfrl/DONE ] && break
  sleep 300
done
