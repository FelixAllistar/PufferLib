#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
bend_bin=${BEND_BIN:-$PWD/ocean/webnav/families/bend_cpu.sh}
[ "$(BEND_NO_TELEMETRY=1 "$bend_bin" --version)" = 'bend 2.0.6' ]
mkdir -p build/webnav/families
exec 9>build/webnav/families/resource.lock
if ! flock --nonblock 9; then
    echo 'WebNav build busy: another build holds the resource lock.' >&2
    exit 75
fi
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/PROOF.bend
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/Main.bend \
    -o build/webnav/miniwob_generated.next.c
mv build/webnav/miniwob_generated.next.c build/webnav/miniwob_generated.c
clang-19 -O3 -std=c11 -fPIC -Ibuild/webnav -Iocean/webnav \
    -DWEBNAV_GENERATED='"miniwob_generated.c"' -DWEBNAV_VALIDATE_MINIWOB \
    -c ocean/webnav/bridge.c -o build/webnav/miniwob_bridge.o
ar rcs build/webnav/libminiwob.a build/webnav/miniwob_bridge.o
for test in miniwob generated text_forms miniwob_boundary task_routing training_resets; do
    clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Iocean/webnav \
        "ocean/webnav/tests/test_${test}.c" build/webnav/libminiwob.a \
        -lpthread -lm -o "build/webnav/test_${test}"
    "build/webnav/test_${test}"
done
for task in sequence_select tree autocomplete; do
    source="ocean/webnav/miniwob/${task}/test_${task}.c"
    if [ "$task" = sequence_select ]; then
        source=ocean/webnav/miniwob/sequence_select/test_native.c
    fi
    clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Iocean/webnav \
        -Dwebnav_tree_batch=webnav_batch -Dautocomplete_batch=webnav_batch \
        "$source" build/webnav/libminiwob.a -lpthread -lm \
        -o "build/webnav/test_${task}"
    "build/webnav/test_${task}"
done
sha256sum ocean/webnav/miniwob/*.bend ocean/webnav/miniwob/*/*.bend \
    build/webnav/miniwob_generated.c > build/webnav/miniwob-sources.sha256
