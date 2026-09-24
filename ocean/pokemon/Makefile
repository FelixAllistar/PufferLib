ROOT := ../..
BUILD := $(ROOT)/build/pokemon
CC := clang
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-function
LDLIBS := $(BUILD)/libpokemon.a -lm

.PHONY: engine test sanitize benchmark semantic-test semantic-legacy-test
engine:
	bash build_engine.sh

test: engine
	PK_OPT=ReleaseSafe bash build_engine.sh test
	$(CC) $(CFLAGS) tests/test_engine_core.c $(LDLIBS) -o $(BUILD)/test_engine_core
	$(BUILD)/test_engine_core

sanitize:
	PK_OPT=ReleaseSafe $(MAKE) test CFLAGS="-O1 -g -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer"

benchmark: engine
	$(CC) $(CFLAGS) benchmark.c $(LDLIBS) -o $(BUILD)/benchmark
	$(BUILD)/benchmark 1000 0
	$(BUILD)/benchmark 1000 1

semantic-test: engine
	$(CC) $(CFLAGS) -I$(ROOT)/src tests/test_semantic_cpu.c $(LDLIBS) -o $(BUILD)/test_semantic_cpu
	$(BUILD)/test_semantic_cpu

semantic-legacy-test: semantic-test
	test -n "$(LEGACY_CPU)"
	$(CC) $(CFLAGS) -I$(ROOT)/src -DPK_LEGACY_CPU='"$(LEGACY_CPU)"' \
		tests/test_semantic_cpu.c $(LDLIBS) -o $(BUILD)/test_semantic_legacy
	test "$$($(BUILD)/test_semantic_cpu)" = "$$($(BUILD)/test_semantic_legacy)"
