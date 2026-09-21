ROOT := ../..
BUILD := $(ROOT)/build/pokemon
CC := clang
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include
CFLAGS ?= -O3 -g -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -mavx2 -mfma
LDLIBS := $(BUILD)/libpokemon.a -lm
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lrt
.PHONY: engine test benchmark sanitize validate audit sampler-test profile-test policy-test policy-input-test league-test league-eval native-league-test freepick-test freepick-check core-test state-test state-flag-test state-audit states behavior-test
freepick-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_freepick.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_freepick
	$(BUILD)/test_freepick
freepick-check:
	node generate_freepick.cjs --check --reuse-audit
states: $(BUILD)/collect_states
core-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) -fopenmp -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_core_deck.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_core_deck
	cd $(ROOT) && build/pokemon/test_core_deck
state-audit:
	$(CC) $(CFLAGS) audit_states.c $(LDLIBS) -o $(BUILD)/audit_states
state-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_state_bank.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_state_bank
	$(BUILD)/test_state_bank
state-benchmark:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_state_bank.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/benchmark_state_bank
state-flag-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_state_flag.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_state_flag
	$(BUILD)/test_state_flag
$(BUILD)/collect_states: collect_states.c $(wildcard *.h) $(wildcard *.inc) $(ROOT)/src/puffercpu.h $(BUILD)/libpokemon.a
	$(CC) $(CPPFLAGS) $(CFLAGS) collect_states.c $(LDLIBS) $(RAYLIB) -o $@
behavior-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_behavior_events.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_behavior_events
	$(BUILD)/test_behavior_events
native-league-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_native_league.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_native_league
	cd $(ROOT) && build/pokemon/test_native_league
league-eval: $(BUILD)/pokemon-league-eval
$(BUILD)/pokemon-league-eval: pokemon.c $(wildcard *.h) $(wildcard *.inc) $(ROOT)/src/puffercpu.h $(BUILD)/libpokemon.a
	$(CC) $(CPPFLAGS) $(CFLAGS) pokemon.c $(LDLIBS) $(RAYLIB) -o $@
league-test: league-eval
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_fixed_teams.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_fixed_teams
	$(BUILD)/test_fixed_teams
	cd $(ROOT) && node --test ocean/pokemon/tests/test_freeleague.mjs
policy-test: $(BUILD)/test_semantic_policy
	$(BUILD)/test_semantic_policy

$(BUILD)/test_semantic_policy: $(ROOT)/tests/test_pokemon_semantic_policy.cu $(wildcard *.h) pokemon_encoder.cu semantic_mlp.cuh $(ROOT)/src/algo.cu $(ROOT)/src/puffercpu.h $(BUILD)/libpokemon.a
	$${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc -O2 --threads 2 -arch=$${NVCC_ARCH:-native} -std=c++17 \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include \
		$(ROOT)/tests/test_pokemon_semantic_policy.cu $(LDLIBS) $(RAYLIB) -lcudart -lcublas -lcusolver -lcurand -o $@

policy-input-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_policy_input.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_policy_input
	$(BUILD)/test_policy_input
profile-test:
	cd $(ROOT) && python3 -m unittest tests.test_population_descriptors
	cd $(ROOT) && python3 ocean/pokemon/tests/test_profiles.py
sampler-test:
	cd $(ROOT) && node ocean/pokemon/tests/test_sampler_tail.cjs
audit: engine
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_contract.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_contract
	$(BUILD)/test_contract
engine:
	bash build_engine.sh
test: engine
	PK_OPT=ReleaseSafe bash build_engine.sh test
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_pokemon.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_pokemon
	$(BUILD)/test_pokemon
benchmark: engine
	$(CC) $(CFLAGS) benchmark.c $(LDLIBS) -o $(BUILD)/benchmark
	$(BUILD)/benchmark 10000 0
	$(BUILD)/benchmark 10000 1
sanitize:
	PK_OPT=ReleaseSafe $(MAKE) test CFLAGS="-O1 -g -std=c11 -mavx2 -mfma -fsanitize=address,undefined -fno-omit-frame-pointer"
validate:
	npm install --prefix $(BUILD)/validation --ignore-scripts --no-audit --no-fund pokemon-showdown@0.11.11
	node import_sets.cjs --check
