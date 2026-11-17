#!/bin/bash
# evalrun.sh RUN_ID [--env overrides the run was trained with...]: probe the final checkpoint.
cd /home/felix/puffertank/pufferlib || exit 1
RID=$1; shift
C=checkpoints/puffer_survivors/$RID/0000000499908608.bin
R=reports/puffer_survivors/walkaway_20261116; mkdir -p $R
./ocean/puffer_survivors/build/probe $C 48 1 "$@" > $R/${RID}_greedy.txt 2>/dev/null
./ocean/puffer_survivors/build/probe $C 24 0 "$@" > $R/${RID}_sampled.txt 2>/dev/null
for m in greedy sampled; do
    awk -v m=$m '/^ep/ {n++; k=0; for (j=1;j<=NF;j++) if ($j=="kills") k=$(j+1); s=($5=="survived"); if (k>=4000) {f++; fs+=s} else {w++; ws+=s; wl+=$4}}
        /^mean/ {sub(/mean length /,""); sub(/ of 24000/,""); gsub(/mean /,""); line=$0}
        END {printf "%s: fighters %d/%d (survived %d), walkers %d (survived %d, mean len %d) | %s\n", m, f, n, fs, w, ws, w?wl/w:0, line}' $R/${RID}_$m.txt
done
tr '\r' '\n' < logs/puffer_survivors/$RID.out | sed 's/\x1b\[[0-9;]*[A-Za-z]//g' | grep -E 'entropy' | tail -1 | grep -o 'entropy *[0-9.]*'
