#!/usr/bin/env bash
# After rescore.sh: stop the policy-only sweep, rescore trials that finished meanwhile, set each history score to
# max(in-trial policy, magnet), switch to the max(policy, magnet) binary + config, resume from the history.
cd /workspace/chess_sweep
while ! grep -q "rescore done" rescore.txt 2>/dev/null; do sleep 30; done
P=$(pgrep -f "puffer_chess_teach2s sweep" | head -1); C=$(pgrep -P $P); kill $C $P; sleep 10
tmux kill-session -t sweep 2>/dev/null
sed -i "/rescore done/d" rescore.txt; bash rescore.sh
cp sweep.history sweep.history.policy_only; cp sweep.log sweep.log.policy_only
python3 - <<"PY"
import re
mag = {}
for l in open("rescore.txt"):
    m = re.match(r"(\d{4}) magnet perf=([0-9.]+)", l)
    if m: mag[int(m.group(1))] = float(m.group(2))
out = []
for l in open("sweep.history"):
    f = l.split(" ")
    try:
        r = int(f[0]); s = float(f[1])
    except Exception:
        out.append(l); continue
    if s == s and r in mag and mag[r] > s:
        f[1] = repr(mag[r]); l = " ".join(f)
    out.append(l)
open("sweep.history", "w").write("".join(out))
print("rewrote", len(mag), "magnet scores")
PY
cp /workspace/PufferLib/config/default.ini config/default.ini
cp /workspace/chess_sweep/chess1024_magnet.ini config/chess.ini
ln -sfn /workspace/PufferLib/puffer_chess_teach2sm puffer_chess_teach2s
tmux new -d -s sweep "bash sweep.sh"
echo "$(date -u) finalize done" >> rescore.txt
