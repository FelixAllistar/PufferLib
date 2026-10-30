#!/usr/bin/env bash
# Model tournament (2026-10-30): after the trainer fine-tune finishes, its final checkpoint picks and pilots both sides
# of every trainer pair (level 100, Showdown-standard stats, pick three), 20 games per pair, sides alternating.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
until grep -a -q " exit " $R/run/train.log; do sleep 60; done
ck=$(ls $R/run/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
echo "checkpoint $ck" > $R/tournament.log
./puffer_gen1tour eval "$ck" --headless --env.trainer_tournament=$R/tournament_games.txt \
  --env.trainer_tournament_games=20 --base.eval_episodes=1900000 >> $R/tournament.log 2>&1
python3 ocean/pokemon/cartridge/tournament_rank.py $R/tournament_games.txt ocean/pokemon/cartridge/trainers_l100.json \
  $R/model_pairs.tsv $R/rank_model.json >> $R/tournament.log 2>&1
echo TOURNAMENT_DONE >> $R/tournament.log
