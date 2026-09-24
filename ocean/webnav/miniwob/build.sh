#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
bend_bin=${BEND_BIN:-$HOME/.bend/bin/bend}
[ "$(BEND_NO_TELEMETRY=1 "$bend_bin" --version)" = 'bend 2.0.6' ]
mkdir -p build/webnav
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/PROOF.bend
BEND_NO_TELEMETRY=1 "$bend_bin" ocean/webnav/miniwob/Main.bend -o build/webnav/miniwob_generated.c
clang-19 -O3 -std=c11 -fPIC -Ibuild/webnav -Iocean/webnav \
    -DWEBNAV_GENERATED='"miniwob_generated.c"' -DWEBNAV_VALIDATE_MINIWOB \
    -c ocean/webnav/bridge.c -o build/webnav/miniwob_bridge.o
ar rcs build/webnav/libminiwob.a build/webnav/miniwob_bridge.o
clang-19 -O3 -std=c11 -Ivendor -Iocean/webnav ocean/webnav/miniwob/oracle.c \
    ocean/webnav/dom.c ocean/webnav/cdp.c vendor/cJSON.c build/webnav/libminiwob.a \
    -lpthread -lm -o build/webnav/miniwob_oracle
clang-19 -O3 -std=c11 -Iocean/webnav ocean/webnav/tests/test_miniwob.c \
    build/webnav/libminiwob.a -lpthread -lm -o build/webnav/test_miniwob
clang-19 -O3 -std=c11 -Iocean/webnav ocean/webnav/tests/test_generated.c \
    build/webnav/libminiwob.a -lpthread -lm -o build/webnav/test_generated
clang-19 -O3 -std=c11 -Ivendor -Iocean/webnav ocean/webnav/miniwob/forms.c \
    vendor/cJSON.c build/webnav/libminiwob.a -lpthread -lm -o build/webnav/generate_forms
clang-19 -O3 -std=c11 -Iocean/webnav ocean/webnav/tests/test_text_forms.c \
    build/webnav/libminiwob.a -lpthread -lm -o build/webnav/test_text_forms
clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Iocean/webnav ocean/webnav/tests/test_miniwob_boundary.c \
    build/webnav/libminiwob.a -lpthread -lm -o build/webnav/test_miniwob_boundary
clang-19 -O3 -std=c11 -Ivendor -Iocean/webnav ocean/webnav/miniwob/text_oracle.c \
    ocean/webnav/dom.c ocean/webnav/cdp.c vendor/cJSON.c build/webnav/libminiwob.a \
    -lpthread -lm -o build/webnav/text_oracle
clang-19 -O2 -std=c11 -Ivendor -Iocean/webnav ocean/webnav/miniwob/tree_oracle.c \
    ocean/webnav/cdp.c vendor/cJSON.c build/webnav/libminiwob.a \
    -lpthread -lm -o build/webnav/tree_oracle
clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Dwebnav_tree_batch=webnav_batch \
    ocean/webnav/miniwob/tree/test_tree.c build/webnav/libminiwob.a \
    -lpthread -lm -o build/webnav/test_tree
clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Iocean/webnav \
    ocean/webnav/tests/test_task_routing.c build/webnav/libminiwob.a \
    -lpthread -lm -o build/webnav/test_task_routing
clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Iocean/webnav \
    ocean/webnav/miniwob/sequence_select/test_native.c build/webnav/libminiwob.a \
    -lpthread -lm -o build/webnav/test_sequence_select
clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Dautocomplete_batch=webnav_batch \
    ocean/webnav/miniwob/autocomplete/test_autocomplete.c build/webnav/libminiwob.a \
    -lpthread -lm -o build/webnav/test_autocomplete
for task in sequence_select autocomplete; do
    clang-19 -O2 -std=c11 -Wall -Wextra -Werror -Ivendor -Iocean/webnav \
        "ocean/webnav/miniwob/${task}_oracle.c" ocean/webnav/cdp.c vendor/cJSON.c \
        build/webnav/libminiwob.a -lpthread -lm -o "build/webnav/${task}_oracle"
done
sha256sum ocean/webnav/miniwob/*.bend ocean/webnav/miniwob/*/*.bend build/webnav/miniwob_generated.c > build/webnav/miniwob-sources.sha256
