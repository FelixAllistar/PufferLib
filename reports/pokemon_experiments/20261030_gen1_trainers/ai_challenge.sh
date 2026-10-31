#!/usr/bin/env bash
# Weedle vs the game (2026-10-31), Gym Leader Matt's challenge: a level 100 Weedle (perfect DVs, maximum stat
# experience, all eight badges) against the gym leaders, Giovanni, the Elite Four and the Champion at their original
# levels, under the cartridge trainer AI and single-player rules. Our trainer fine-tune pilots the Weedle, once with
# Poison Sting only (Matt's rule) and once with String Shot too. GAMES per opponent (default 20000). RUN picks the
# checkpoint folder (default run, the trainer fine-tune; run_weedle for the Weedle specialist).
cd /workspace/PufferLib
R=reports/pokemon_experiments/20261030_gen1_trainers
C=ocean/pokemon/cartridge
G=${1:-20000}
OPP=337,338,339,340,341,343,342,276,364,336,389,390,361,362,363
RUN=${RUN:-run}
S=$([ "$RUN" = run ] && echo "" || echo "_$RUN")
ck=$(ls $R/$RUN/checkpoints/pokemon/*/*.bin | grep -v magnet | sort | tail -1)
for party in weedle_poison_sting weedle; do
  ./puffer_gen1tour eval "$ck" --headless --env.trainer_file=$C/trainers.tsv --env.trainer_ratings=$C/trainer_ratings.txt \
    --env.trainer_ai_player=$C/$party.tsv --env.trainer_ai_opponents=$OPP --env.trainer_tournament=$R/ai_$party$S.txt \
    --env.trainer_tournament_games=$G --base.eval_episodes=$((15 * G * 103 / 100)) > $R/ai_$party$S.log 2>&1
done
python3 - $R "$S" <<'PY'
import collections, json, sys
R, S = sys.argv[1], sys.argv[2]
T = json.load(open('ocean/pokemon/cartridge/trainers.json'))
matt = {'BRUNO': '1 in 5.5', 'LANCE': '1 in 31', 'GIOVANNI': '1 in 66', 'LORELEI': '1 in 2,633', 'RIVAL3': '1 in 4,770',
        'AGATHA': 'below 1 in 500M'}
rows = {}
for party in ('weedle_poison_sting', 'weedle'):
    c = collections.defaultdict(lambda: [0, 0])
    for line in open(f'{R}/ai_{party}{S}.txt'):
        _, opp, result = map(int, line.split())
        c[opp][0] += result == 1
        c[opp][1] += 1
    rows[party] = c
print(f"{'trainer':<14} {'Poison Sting only':>20} {'+ String Shot':>20}   Matt (Poison Sting spam)")
for opp in rows['weedle']:
    t = T[opp]
    cells = []
    for party in ('weedle_poison_sting', 'weedle'):
        w, n = rows[party][opp]
        cells.append(f"{w}/{n}" + (f" (1 in {n / w:.0f})" if w else ""))
    print(f"{t['name'] + ' #' + str(t['index']):<14} {cells[0]:>20} {cells[1]:>20}   {matt.get(t['class'], '')}")
PY
echo AI_DONE
