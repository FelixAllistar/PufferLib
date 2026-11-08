#!/usr/bin/env bash
# Gen 3 team strength (2026-11-08, "team tests vs own policy"), two stages, run alone on the box (whole GPU).
# Our best structured checkpoint plays both seats (sampled actions, as in training); env.draft_log records each game's
# teams and result; rank.py gives each team a Laplace win rate against the field.
#   Stage 1: every seat a random training-pool team, GAMES1 games (~2 x GAMES1 / 172,594 games per team) -> top5000.
#   Stage 2: half the seats from those 5,000 (env.top_pool, top_fraction 0.5), GAMES2 games (~GAMES2 / 5,000 per
#   candidate), ranked among the candidates only -> stage2/top1000.txt (the training top_pool).
# Usage: score.sh CHECKPOINT GAMES1 GAMES2
. /workspace/env.sh
cd /workspace/PufferLib
D=reports/pokemon_experiments/20261108_gen3_top_teams
T=build/pokemon3/ou3
export OMP_NUM_THREADS=24
E="--base.env_name=pokemon3 --policy.hidden_size=256 --policy.num_layers=2 --policy.pokemon3_net=1 --vec.total_agents=8192
  --vec.num_threads=24 --train.minibatch_size=8192 --headless --env.sets=$T/sets.txt --env.pool=$T/train.txt
  --env.free_fraction=0 --env.matchup=1 --env.bot_policy=0 --base.eval_greedy=0 --base.load_model_path=$1"
mkdir -p $D/stage1 $D/stage2; rm -f $D/stage1/games.log $D/stage2/games.log
echo "$(date -u +%H:%M) stage1 start" >> $D/status.txt
./puffer_pk3t eval $E --env.draft_log=$D/stage1/games.log --base.eval_episodes=$2 > $D/stage1/score.log 2>&1
echo "$(date -u +%H:%M) stage1 exit=$? games=$(wc -l < $D/stage1/games.log)" >> $D/status.txt
python3 $D/rank.py $T/train.txt $D/stage1/games.log $D/stage1 >> $D/status.txt
./puffer_pk3t eval $E --env.top_pool=$D/stage1/top5000.txt --env.top_fraction=0.5 --env.draft_log=$D/stage2/games.log \
  --base.eval_episodes=$3 > $D/stage2/score.log 2>&1
echo "$(date -u +%H:%M) stage2 exit=$? games=$(wc -l < $D/stage2/games.log)" >> $D/status.txt
python3 $D/rank.py $T/train.txt $D/stage2/games.log $D/stage2 $D/stage1/top5000.txt >> $D/status.txt
cp $D/stage2/top1000.txt $D/top1000.txt
echo SCORE_DONE >> $D/status.txt
