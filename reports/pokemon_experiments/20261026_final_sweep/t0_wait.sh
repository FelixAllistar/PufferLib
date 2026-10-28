# Run trial 0's Kakuna test once the side job has finished trials 14 and 12 and no evaluator is running (GPU memory).
cd /workspace/PufferLib
F=reports/pokemon_experiments/20261026_final_sweep
until grep -q "^trial   14 " $F/heldout.txt && grep -q "^trial   12 " $F/heldout.txt && ! pgrep -f '^build/pokemon/metamon-eval-env-cu128/bin/python ocean/pokemon/benchmarks/evaluate' > /dev/null; do sleep 60; done
python3 $F/t0_kakuna.py
