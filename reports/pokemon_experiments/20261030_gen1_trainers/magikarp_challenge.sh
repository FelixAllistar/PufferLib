#!/usr/bin/env bash
# How many Magikarp does it take (2026-11-01)? The Magikarp specialist pilots parties of 1..6 level 100 Magikarp
# against each trainer under the cartridge AI; GAMES per opponent per party size (default 5000). Also a Tackle-spam
# baseline is not needed: Splash does nothing, so the only decisions are switches.
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
C=ocean/pokemon/cartridge
G=${1:-5000}
LV=${LV:-}  # LV=50: level 50 Magikarp (party files magikarp50_N.tsv)
P=magikarp${LV:+${LV}_}
OPP=337,338,339,340,341,343,342,276,364,336,389,390,361,362,363
ck=$(ls $R/run_magikarp/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
for n in 1 2 3 4 5 6; do
  ./puffer_gen1tour eval "$ck" --headless --env.trainer_file=$C/trainers.tsv --env.trainer_ratings=$C/trainer_ratings.txt \
    --env.trainer_ai_player=$C/$P$n.tsv --env.trainer_ai_opponents=$OPP --env.trainer_tournament=$R/ai_$P$n.txt \
    --env.trainer_tournament_games=$G --base.eval_episodes=$((15 * G * 103 / 100)) > $R/ai_$P$n.log 2>&1
done
python3 - $R $P <<'PY'
import collections, json, sys
R, P = sys.argv[1], sys.argv[2]
T = json.load(open('ocean/pokemon/cartridge/trainers.json'))
rows = {}
for n in range(1, 7):
    c = collections.defaultdict(lambda: [0, 0])
    for line in open(f'{R}/ai_{P}{n}.txt'):
        _, opp, result = map(int, line.split())
        c[opp][0] += result == 1
        c[opp][1] += 1
    rows[n] = c
print(f"{'trainer':<14}" + ''.join(f'{str(n) + " Magikarp":>14}' for n in range(1, 7)))
for opp in rows[6]:
    t = T[opp]
    print(f"{t['name'] + ' #' + str(t['index']):<14}" + ''.join(f'{100 * rows[n][opp][0] / max(1, rows[n][opp][1]):>13.2f}%' for n in range(1, 7)))
PY
echo MAGIKARP_DONE
