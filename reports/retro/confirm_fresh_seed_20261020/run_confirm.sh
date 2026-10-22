#!/usr/bin/env bash
# Fresh-seed confirmation of the Mario archive-sweep leaders, CPU only, off the sweep box.
set -uo pipefail
cd /workspace/mario_eval
C=checkpoints/retro_archive_20261019/retro
declare -A CK=([parent]=saved/retro/pixel_197_20261018.bin [t2]=$C/sweep_1792431845100_0002/0000000200015872.bin [t31]=$C/sweep_1792483827634_0031/0000000200015872.bin)
mkdir -p confirm
log(){ echo "$(date -u +%FT%TZ) $*" | tee -a confirm/progress.log; }
E(){ ./build/retro/sweep_eval "$1" --config "$1.ini" "${@:2}" --frameskip 4 --workers 12; }
log "repro t31 game seed 20260907 x16 (must match sweep panel)"
E ${CK[t31]} --game --metric perf --repeats 16 --seed 20260907 --frames 108000 --output confirm/repro_t31.game.tsv > confirm/repro_t31.log 2>&1
for n in parent t2 t31; do log "game $n seed 20261020 x64"
  E ${CK[$n]} --game --metric perf --repeats 64 --seed 20261020 --frames 108000 --output confirm/$n.game.tsv > confirm/$n.game.log 2>&1; done
for n in parent t2 t31; do log "levels $n seed 20261020 x8"
  E ${CK[$n]} --full-run --levels all --metric perf --repeats 8 --seed 20261020 --frames 30000 --output confirm/$n.panel.tsv > confirm/$n.panel.log 2>&1; done
log "done"
