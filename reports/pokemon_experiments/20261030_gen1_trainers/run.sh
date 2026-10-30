#!/usr/bin/env bash
# Gen 1 trainer fine-tune (2026-10-30). Waits for the poker sweep to pause (pause_after_trial.sh), installs the trainer
# config as config/pokemon.ini (the previous one saved as config/pokemon.ini.before_trainers), and trains with the
# cartridge-mechanics build (puffer_gen1cart).
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
until [ -s leagues/poker/gen10/pause.log ]; do sleep 30; done
while pgrep -x puffer > /dev/null; do sleep 5; done
[ -f config/pokemon.ini.before_trainers ] || cp config/pokemon.ini config/pokemon.ini.before_trainers
python3 $R/make_config.py > config/pokemon.ini
mkdir -p $R/run
./puffer_gen1cart train > $R/run/train.log 2>&1
echo "$(date) done" >> $R/run/train.log
