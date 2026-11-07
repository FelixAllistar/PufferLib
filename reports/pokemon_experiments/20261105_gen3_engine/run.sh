#!/usr/bin/env bash
# Gen 3 differential test: N shards x POS positions x SAMPLES Showdown samples (local, low priority). Usage: ENGINE=frozen/diffcheck run.sh N POS SAMPLES
# Pass a copy of the diffcheck binary: cargo test rebuilds target/release/diffcheck, which would change the engine mid-run.
R=$(cd "$(dirname "$0")" && pwd); cd "$R/../../../build/pokemon/showdown_server"
export NODE_PATH=$PWD/node_modules PUFFERLIB=$(cd ../../.. && pwd)
export ENGINE=${ENGINE:-$PUFFERLIB/../forks/poke-engine-gen3/target/release/diffcheck}
[ -x "$ENGINE" ] || { echo "no engine at $ENGINE"; exit 1; }
for s in $(seq 0 $(($1 - 1))); do nice -n 15 node "$R/engine_diff.js" $2 $3 "$R/run_s$s.jsonl" $s > "$R/run_s$s.log" 2>&1 & done; wait
cat "$R"/run_s*.jsonl > "$R/run.jsonl"; echo RUN_DONE  # score with rescore.js + diff_report.py
