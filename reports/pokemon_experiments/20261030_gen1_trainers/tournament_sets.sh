#!/usr/bin/env bash
# Every-set tournament (2026-10-31): the trainer fine-tune's checkpoint pilots every set each trainer could bring
# (min(party, 3) Pokemon and the lead) against every set of every other trainer, GAMES per cell (default 20), sides
# alternating; nash_sets.py solves each pair's pick game and rank.py ranks the equilibrium scores.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
G=${1:-20}
ck=$(ls $R/run/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
echo "checkpoint $ck games $G" > $R/sets.log
./puffer_gen1tour eval "$ck" --headless --env.trainer_tournament=$R/sets_cells.txt --env.trainer_tournament_sets=1 \
  --env.trainer_tournament_games=$G --base.eval_episodes=$((2749885 * G * 103 / 100)) >> $R/sets.log 2>&1
python3 ocean/pokemon/cartridge/nash_sets.py $R/sets_cells.txt ocean/pokemon/cartridge/trainers_l100.json $G \
  $R/sets_pairs.tsv $R/sets_picks.json >> $R/sets.log 2>&1
python3 ocean/pokemon/cartridge/rank.py ocean/pokemon/cartridge/trainers_l100.json $R/sets_pairs.tsv \
  $R/rank_sets.json 40 >> $R/sets.log 2>&1
echo SETS_DONE >> $R/sets.log
