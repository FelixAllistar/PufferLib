#!/usr/bin/env bash
# kp_h (2026-11-01): the 8-4 pipe routes searched again with REQUIRE_PIPE=1. kp_g's pipe-1 "route" was a lava death
# that respawns further on (the old search ignored deaths during non-playable frames), so its states start Mario
# walking into the lava. Base: kp_f's first 32 key points (all levels, 7-4 and 4-4 routes), then pipe 1 from key
# point 8 (x 640-720), the floating pipe and the water pipe each from the previous pipe's past-the-wall point.
# Then run 10 (fresh policy) restarts on kp_h.
cd /workspace/PufferLib
R=reports/retro/hard_levels_20261031
S=./build/retro/keypoint_search
export OMP_NUM_THREADS=6 REQUIRE_PIPE=1
$S $R/kp_f.archive.gz 8 1340 $R/kp_h1.archive.gz 30 | grep -v "^\[retro\]"
$S $R/kp_h1.archive.gz 33 2620 $R/kp_h2.archive.gz 30 | grep -v "^\[retro\]"
$S $R/kp_h2.archive.gz 36 3900 $R/kp_h.archive.gz 30 | grep -v "^\[retro\]"
ls -la $R/kp_h.archive.gz && sed -i "s|^KP=\$R/kp_g.archive.gz|KP=\$R/kp_h.archive.gz|" $R/run10.sh
tmux kill-session -t hard_run10; sleep 2; pkill -x puffer; sleep 3; rm -rf $R/run10 $R/eval_run10_* $R/keypoint_eval_run10_*
tmux new -d -s hard_run10 "bash $R/run10.sh"
echo KP_H_DONE
