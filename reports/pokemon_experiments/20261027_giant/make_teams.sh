#!/usr/bin/env bash
# Giant run teams (2026-10-27). Held-out C: 28 teams from Metamon's gl_05_26 (general ladder May 2026), a set never used
# before. Training ladder teams with exact sets, sampled from all seven Metamon gen1ou team sets, each disjoint from held-out
# A (hl_05_26 sample), B (modern_replays_v2 sample), C and our turf. Only teams our engine plays exactly are kept.
set -e
cd /workspace/PufferLib
G=reports/pokemon_experiments/20261027_giant; B=reports/pokemon_benchmarks; T=build/pokemon/metamon-cache/teams
CK=$(ls reports/pokemon_experiments/20261026_long2b/checkpoints/pokemon/*/0000001875640320.bin)
PY="env PYTHONPATH=ocean/pokemon/benchmarks build/pokemon/metamon-eval-env-cu128/bin/python reports/pokemon_benchmarks/sample_teams.py"
HELD="$B/metamon_hl_05_26_sample28 $B/metamon_modern_replays_v2_sample28 $B/our_turf30"
[ -d $B/metamon_gl_05_26_sample28 ] || $PY $CK $T/gl_05_26/gen1ou $B/metamon_gl_05_26_sample28 28 20261030 $HELD
HELD="$HELD $B/metamon_gl_05_26_sample28"
for spec in hl_05_26:700 gl_05_26:700 modern_replays_v2:600 modern_replays:500 paper_replays:500 paper_variety:300; do
  set_name=${spec%%:*}; n=${spec##*:}
  [ -d $G/ladder_$set_name ] || $PY $CK $T/$set_name/gen1ou $G/ladder_$set_name $n 20261031 $HELD
done
echo TEAMS_DONE
