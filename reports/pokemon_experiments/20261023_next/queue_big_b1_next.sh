#!/usr/bin/env bash
# After big_b1_1024 is trained and fully scored: warm-restart it to 3B if its scores were still climbing
# (mean of SyntheticRLV2/TaurosV0 rose from 250M to 500M and from 500M to 750M); otherwise run the
# learning-rate scan (1.5e-4, then 2.5e-4, 1B each).
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261023_next/big_b1_1024
until grep -q EVALS_DONE $R/evals.log 2>/dev/null; do sleep 60; done
decision=$(python3 - $R/evals.log <<'PY'
import sys, collections
scores = collections.defaultdict(dict)
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) == 3 and parts[2] != 'FAILED':
        scores[int(parts[0].split('.')[0])][parts[1]] = float(parts[2])
mean = {s: sum(v.values()) / len(v) for s, v in scores.items() if len(v) == 2}
steps = sorted(mean)
pick = lambda target: mean[min(steps, key=lambda s: abs(s - target))]
m250, m500, m750 = pick(250e6), pick(500e6), pick(750e6)
print('continue' if m500 > m250 and m750 > m500 else 'lrscan', f'means 250M={m250:.3f} 500M={m500:.3f} 750M={m750:.3f}')
PY
)
echo "DECISION $decision"
case $decision in
  continue*) bash reports/pokemon_experiments/20261023_next/run_big_b1_cont.sh ;;
  *) bash reports/pokemon_experiments/20261023_next/run_big_b1_lr.sh 0.00015
     bash reports/pokemon_experiments/20261023_next/run_big_b1_lr.sh 0.00025 ;;
esac
echo BIG_B1_NEXT_DONE
