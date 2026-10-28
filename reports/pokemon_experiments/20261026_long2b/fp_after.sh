#!/usr/bin/env bash
# Finish the 1250M side test (SyntheticRLV2 leg) only once training is done and the checkpoint tester is idle,
# so it never shares the GPU with them (it ran out of memory alongside both).
cd /workspace/PufferLib
while pgrep -f "^[.]/puffer train" >/dev/null || pgrep -f "evaluate_[b]atched" >/dev/null; do sleep 300; done
python3 reports/pokemon_experiments/20261026_long2b/freepick_test.py $(ls reports/pokemon_experiments/20261026_long2b/checkpoints/pokemon/*/0000001250426880.bin)
