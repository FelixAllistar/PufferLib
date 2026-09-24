#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/webnav/training
# Incremental rebuild; generated C and runtime must follow every Bend/bridge edit.
if [ ! -f build/webnav/libminiwob.a ] || find ocean/webnav/miniwob -name '*.bend' -newer build/webnav/libminiwob.a -print -quit | read -r newer || [ ocean/webnav/bridge.c -nt build/webnav/libminiwob.a ]; then
    bash ocean/webnav/miniwob/build.sh
fi
node <<'JS'
const fs=require('fs'),crypto=require('crypto');
const files=['ocean/webnav/training.h','ocean/webnav/training.c','ocean/webnav/training_contract.c','ocean/webnav/miniwob/training/countries.h','ocean/webnav/text_encoder.c','ocean/webnav/text_encoder.h','build/webnav/miniwob_generated.c'];
const h=crypto.createHash('sha256');for(const f of files.sort()){h.update(f);h.update(fs.readFileSync(f));}fs.writeFileSync('build/webnav/training/source_hash.h','#define WT_SOURCE_HASH "'+h.digest('hex')+'"\n');
JS
for source in training training_contract text_encoder; do
    clang-19 -O3 -std=c11 -Ivendor -Iocean/webnav -include build/webnav/training/source_hash.h \
        -c "ocean/webnav/$source.c" -o "build/webnav/training/$source.o"
done
clang-19 -O3 -std=c11 -Ivendor -c vendor/cJSON.c -o build/webnav/training/cJSON.o
ar rcs build/webnav/libtraining.a build/webnav/training/{training,training_contract,text_encoder,cJSON}.o
clang-19 -O3 -std=c11 -mavx2 -mfma -Isrc -Ivendor -Iocean/webnav \
    -c ocean/webnav/training_policy.c -o build/webnav/training/training_policy.o
clang-19 -O3 -std=c11 -Isrc -Ivendor -Iocean/webnav ocean/webnav/training_eval.c \
    build/webnav/training/training_policy.o build/webnav/libtraining.a build/webnav/libminiwob.a \
    -licuuc -lpthread -lm -o build/webnav/training_eval
if [ -f ocean/webnav/training_browser.c ]; then
    clang-19 -O3 -std=c11 -Isrc -Ivendor -Iocean/webnav ocean/webnav/training_browser.c \
        ocean/webnav/cdp.c ocean/webnav/dom.c build/webnav/training/training_policy.o \
        build/webnav/libtraining.a build/webnav/libminiwob.a -licuuc -lpthread -lm \
        -o build/webnav/training_browser
fi
