#!/usr/bin/env bash
# Pick fix fine-tune (2026-10-31): the first run's pick actions had no score (semantic_contract.h had no phase-3 case),
# so every pick was uniform random. From run/'s final checkpoint, 500M more steps with picks scored like switch-ins,
# then the tournament (tournament.sh run2) and the pick-regret check against the random-pick tournament.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
ck=$(ls $R/run/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
python3 $R/make_config.py | sed -e "s|^load_model_path = .*|load_model_path = $ck|" \
  -e "s|/run/checkpoints|/run2/checkpoints|; s|/run/logs|/run2/logs|" \
  -e "s|^total_timesteps = .*|total_timesteps = 500000000|" > config/pokemon.ini
mkdir -p $R/run2
./puffer_gen1cart train > $R/run2/train.log 2>&1
echo "$(date) exit $?" >> $R/run2/train.log
bash $R/tournament.sh run2
python3 $R/pick_regret.py $R/tournament_games_randompick.txt $R/tournament_games_run2.txt \
  ocean/pokemon/cartridge/trainers_l100.json > $R/pick_regret_run2.txt 2>&1
echo RUN2_DONE >> $R/run2/train.log
