#!/usr/bin/env bash
# Size study queue: trains each size in turn (smallest first), installing its config as config/pokemon.ini.
# Skips sizes whose final checkpoint exists. Usage (box): queue.sh
cd /workspace/PufferLib
Z=reports/pokemon_experiments/20261027_size
for s in h512x2 h512x3 h1024x2 h1024x3 h2048x2 h2048x3; do
  ls $Z/$s/checkpoints/pokemon/*/000000199* >/dev/null 2>&1 && continue
  cp $Z/$s/pokemon.ini config/pokemon.ini
  echo "$(date -u) start $s" | tee -a $Z/queue.log
  ./puffer train --base.env_name=pokemon 2>&1 | tee $Z/$s/train.log
  echo "$(date -u) end $s" | tee -a $Z/queue.log
  ls $Z/$s/checkpoints/pokemon/*/000000199* >/dev/null 2>&1 || { echo "$(date -u) $s did not finish; stopping" | tee -a $Z/queue.log; exit 1; }
done
echo SIZE_QUEUE_DONE | tee -a $Z/queue.log
