ROOT := ../..
BUILD := $(ROOT)/build/pokemon
CC := clang
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-function
LDLIBS := $(BUILD)/libpokemon.a -lm
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lrt

.PHONY: engine test sanitize benchmark semantic-test semantic-legacy-test viewer adapter-test
engine:
	bash build_engine.sh

viewer: engine
	$(CC) $(CPPFLAGS) $(CFLAGS) pokemon.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/viewer

adapter-test: engine
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_pokemon.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_adapter
	$(BUILD)/test_adapter
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_policy_input.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_policy_input
	cd $(ROOT) && ./build/pokemon/test_policy_input
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_fixed_teams.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_fixed_teams
	$(BUILD)/test_fixed_teams

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
