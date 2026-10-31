#!/usr/bin/env bash
# Model tournament (2026-10-30): the fine-tune's final checkpoint (RUN dir, default run) picks and pilots both sides of
# every trainer pair (level 100, Showdown-standard stats, pick three), 20 games per pair, sides alternating.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
RUN=${1:-run}
S=$([ "$RUN" = run ] && echo "" || echo "_$RUN")
ck=$(ls $R/$RUN/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
echo "checkpoint $ck" > $R/tournament$S.log
./puffer_gen1tour eval "$ck" --headless --env.trainer_tournament=$R/tournament_games$S.txt \
  --env.trainer_tournament_games=20 --base.eval_episodes=1900000 >> $R/tournament$S.log 2>&1
python3 ocean/pokemon/cartridge/tournament_rank.py $R/tournament_games$S.txt ocean/pokemon/cartridge/trainers_l100.json \
  $R/model_pairs$S.tsv $R/rank_model$S.json >> $R/tournament$S.log 2>&1
echo TOURNAMENT_DONE >> $R/tournament$S.log
