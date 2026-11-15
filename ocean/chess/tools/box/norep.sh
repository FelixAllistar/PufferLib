#!/usr/bin/env bash
# Maia evals with the no-repeat guard (play.py --no-repeat, 2026-11-14): ~80% of draws vs Maia were threefold repetitions.
EXTRA="--move-feats --no-repeat" bash /workspace/sfbc/evalm.sh /workspace/sfbc/sfbc1m_mf_2048.bin sfbc1m_mf_2048_norep 2048 2 50

echo "$(date -u) norep evals done" >> /workspace/sfbc/done.txt
