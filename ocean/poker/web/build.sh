#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
web_src="$PWD/ocean/poker/web"
web_build="$PWD/build/poker/web"
web_out="${1:-$web_build/dist}"
mkdir -p "$web_out" "$web_build/source" build/webnav/families
web_emcc="${EMCC:-$HOME/emsdk/upstream/emscripten/emcc}"
if ! test -x "$web_emcc"; then web_emcc=emcc; fi
if ! command -v "$web_emcc" >/dev/null; then
    echo 'Install Emscripten or set EMCC to emcc.' >&2; exit 1
fi
web_weights="${POKER_FULLHOUSE_WEIGHTS:-$PWD/build/poker/fullhouse_weights.h}"
if ! test -f "$web_weights"; then
    echo 'Fullhouse weights missing. See ocean/poker/README.md to export the pinned model.' >&2
    exit 1
fi
# Stock Bend checks and browser bundling share the existing kernel resource
# guard and compiler lock. No unconfined fallback.
flock --nonblock build/webnav/families/resource.lock \
  systemd-run --user --scope -p MemoryMax=2147483648 -p MemorySwapMax=0 \
  -p TasksMax=128 -p CPUQuota=100% \
  timeout --signal=TERM --kill-after=5s 300s \
  bash -c 'bash ocean/webnav/families/bend_cpu.sh "$1/PROOF.bend" && bash ocean/webnav/families/bend_cpu.sh "$1/index.html" -o "$2"' _ "$web_src" "$web_out"
for web_variant in 2 6 9 2pro; do
  web_players="${web_variant/pro/}"
  web_engine="$web_build/source/$web_variant"
  mkdir -p "$web_engine"
  cp ocean/poker/{core.h,rules.h,equity.h,fullhouse.h} "$web_engine/"
  if [[ "$web_variant" == 2pro ]]; then cp "$web_src"/experimental/*.h "$web_engine/"; fi
  cp "$web_src/bridge.c" "$web_engine/bridge.c"
  cp "$web_src/learning.h" "$web_engine/learning.h"
  web_model=()
  if [[ "$web_players" != 9 ]]; then web_model=("-DPOKER_FULLHOUSE_MODEL=\"$web_weights\""); fi
  "$web_emcc" -O3 -std=c11 -ffp-contract=off -Wall -Wextra \
    -DPOKER_PLAYERS="$web_players" -DPOKER_BATCH=1 -DPOKER_WIDE_CHIPS=1 \
    "${web_model[@]}" \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node \
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 \
    '-sEXPORTED_FUNCTIONS=["_web_init","_web_next","_web_act","_web_bot","_web_state","_web_stack","_web_open","_web_learning"]' \
    '-sEXPORTED_RUNTIME_METHODS=["HEAP32"]' \
    "$web_engine/bridge.c" -lm -o "$web_out/poker$web_variant.mjs"
done
cp "$web_src/worker.js" "$web_out/worker.js"
cp ocean/poker/third_party/fullhouse-LICENSE.txt "$web_out/fullhouse-LICENSE.txt"
python3 - "$web_out" "$web_weights" <<'PY'
import hashlib,json,pathlib,subprocess,sys
out=pathlib.Path(sys.argv[1]); repo=pathlib.Path.cwd()
files=[*repo.glob('ocean/poker/*.h'),*repo.glob('ocean/poker/web/*.*'),*repo.glob('ocean/poker/web/experimental/*'),*repo.glob('ocean/poker/web/assets/*')]
data={'engine_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
      'bend':'stock 2.0.6; browser controller compiled to JavaScript',
      'experimental':json.loads((repo/'ocean/poker/web/experimental/provenance.json').read_text()),
      'weights_sha256':hashlib.sha256(pathlib.Path(sys.argv[2]).read_bytes()).hexdigest(),
      'sources':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files) if p.is_file()},
      'artifacts':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.is_file() and p.name!='build-manifest.json'}}
(out/'build-manifest.json').write_text(json.dumps(data,indent=2)+'\n')
PY
echo "Built playable poker at $web_out"
