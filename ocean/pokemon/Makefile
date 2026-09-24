ROOT := ../..
BUILD := $(ROOT)/build/pokemon
CC := clang
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-function
LDLIBS := $(BUILD)/libpokemon.a -lm
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lrt -lm

.PHONY: engine test sanitize benchmark semantic-test semantic-legacy-test viewer adapter-test state-tools state-test core-test validate
engine:
	bash build_engine.sh

validate: engine
	npm install --prefix $(BUILD)/validation --ignore-scripts --no-audit --no-fund pokemon-showdown@0.11.11
	node import_sets.cjs --check
	node generate_freepick.cjs --check --reuse-audit

core-test: engine
	$(CC) $(CPPFLAGS) $(CFLAGS) -fopenmp tests/test_core_deck.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_core_deck
	cd $(ROOT) && ./build/pokemon/test_core_deck

state-tools: engine
	$(CC) $(CPPFLAGS) $(CFLAGS) collect_states.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/collect_states
	$(CC) $(CPPFLAGS) $(CFLAGS) audit_states.c $(LDLIBS) -o $(BUILD)/audit_states

state-test: state-tools
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_state_bank.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_state_bank
	$(BUILD)/test_state_bank
	@set -e; bank_dir=$$(mktemp -d /tmp/pokemon-bank-test-XXXXXX); \
		trap 'rm -f "$$bank_dir/first.bin" "$$bank_dir/repeat.bin"; rmdir "$$bank_dir"' EXIT; \
		cd $(ROOT); \
		./build/pokemon/collect_states "$$bank_dir/first.bin" 128 871 random random; \
		./build/pokemon/audit_states "$$bank_dir/first.bin" 871 128 2; \
		./build/pokemon/collect_states "$$bank_dir/repeat.bin" 128 871 random random; \
		cmp "$$bank_dir/first.bin" "$$bank_dir/repeat.bin"; \
		status=0; ASAN_OPTIONS=exitcode=99 ./build/pokemon/audit_states \
			"$$bank_dir/first.bin" 872 128 2 || status=$$?; test "$$status" -eq 1; \
		status=0; ASAN_OPTIONS=exitcode=99 ./build/pokemon/collect_states \
			"$$bank_dir/first.bin" 128 871 random random || status=$$?; test "$$status" -eq 1

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
