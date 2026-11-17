#!/bin/bash
# supervise.sh RUN_ID [trainer overrides...]
# Runs one training run to the end. A watchdog pauses it (state saved) when Windows or WSL
# memory is low; this script resumes it exactly once memory is back. Writes DONE or FAILED
# in the run's checkpoint directory.
cd /home/felix/puffertank/pufferlib || exit 1
RID=$1; shift
DIR=checkpoints/puffer_survivors/$RID
OUT=logs/puffer_survivors/$RID.out
MEM=logs/puffer_survivors/${RID}_mem.log
winfree() { timeout 20 powershell.exe -NoProfile -Command "[int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024)" 2>/dev/null | tr -d '\r'; }
resume=""
[ -d "$DIR/state" ] && resume="--base.resume=1"
while true; do
    rm -f "$DIR/PAUSE"
    echo "$(date +%H:%M:%S) start $resume" >> "$MEM"
    ./puffer_ps train --vec.num_threads=4 --base.run_id=$RID "$@" $resume >> "$OUT" 2>&1 &
    pid=$!
    while kill -0 $pid 2>/dev/null; do
        win=$(winfree); wsl=$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)
        echo "$(date +%H:%M:%S) win_free_mb=$win wsl_avail_mb=$wsl" >> "$MEM"
        if [ -d "$DIR" ] && { { [ -n "$win" ] && [ "$win" -lt 700 ]; } || [ "$wsl" -lt 1500 ]; }; then
            touch "$DIR/PAUSE"; echo "$(date +%H:%M:%S) low memory, asked the run to pause" >> "$MEM"
            wait $pid; break
        fi
        sleep 20
    done
    wait $pid 2>/dev/null
    if tail -c 400 "$OUT" | grep -q "Paused at"; then
        resume="--base.resume=1"
        ok=0
        while [ $ok -lt 3 ]; do
            sleep 30; win=$(winfree)
            if [ -n "$win" ] && [ "$win" -gt 2000 ]; then ok=$((ok+1)); else ok=0; fi
        done
        echo "$(date +%H:%M:%S) memory back ($win MB), resuming" >> "$MEM"
        continue
    fi
    if ls "$DIR" | grep -q '^0000000499'; then touch "$DIR/DONE"; echo "$(date +%H:%M:%S) done" >> "$MEM"
    else touch "$DIR/FAILED"; echo "$(date +%H:%M:%S) FAILED" >> "$MEM"; fi
    break
done
