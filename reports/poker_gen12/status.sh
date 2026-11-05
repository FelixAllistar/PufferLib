#!/usr/bin/env bash
# gen12 sweep status (tmux poker_gen12_sweep, window status). A sweep sends trial dashboards to /dev/null, so this
# shows what is on disk every 30 s: the running trial's steps and speed from its checkpoints, the PFSP line from its
# stderr log (bank order: leagues/poker/gen12/league.txt, then the run's own snapshots), and every scored trial.
cd /workspace/PufferLib
H=leagues/poker/gen12/sweep.history
while true; do
  clear
  echo "gen12 poker sweep  $(date -u '+%H:%M UTC')"
  d=$(ls -dt checkpoints/poker/sweep_* 2>/dev/null | head -1)
  if [ -n "$d" ] && pgrep -f "puffer_poker_zs train" >/dev/null; then
    id=$(basename "$d")
    cmd=$(pgrep -af "puffer_poker_zs train" | head -1)
    total=$(grep -o -- "--train.total_timesteps=[^ ]*" <<< "$cmd" | cut -d= -f2)
    mapfile -t ck < <(ls "$d"/*.bin 2>/dev/null | grep -v magnet | sort | tail -2)
    if [ ${#ck[@]} -ge 1 ]; then
      step=$((10#$(basename "${ck[-1]}" .bin)))
      echo "running $id: $((step / 1000000))M / $(python3 -c "print(int(float('$total')/1e6))")M steps"
      if [ ${#ck[@]} -ge 2 ]; then
        s0=$((10#$(basename "${ck[0]}" .bin))); t0=$(stat -c %Y "${ck[0]}"); t1=$(stat -c %Y "${ck[-1]}")
        [ $t1 -gt $t0 ] && echo "speed ~$(( (step - s0) / (t1 - t0) / 1000 ))K steps/s"
      fi
    else
      echo "running $id: no checkpoint yet (first at ~21M steps)"
    fi
    echo "$(grep -o -- "--vec.total_agents=[^ ]*\|--train.learning_rate=[^ ]*\|--train.ent_coef=[^ ]*\|--train.gamma=[^ ]*" <<< "$cmd" | sed 's/--//' | tr '\n' ' ')"
    p=$(grep -a "^PFSP" logs/poker/$id.err 2>/dev/null | tail -1)
    [ -n "$p" ] && echo "$p" | fold -w 90
  else
    echo "no trial running (final eval or between trials)"
  fi
  echo
  echo "scored trials (id score cost_s steps):"
  awk '$1 ~ /^[0-9]+$/ && NF > 5 {printf "  %3d  %8.3f  %6.0f  %5.0fM\n", $1, $2, $3, $5/1e6}' "$H" | tail -20
  sleep 30
done
