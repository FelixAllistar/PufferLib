ROOT := ../..
BUILD := $(ROOT)/build/pokemon
CC := clang
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include
CFLAGS ?= -O3 -g -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -mavx2 -mfma
LDLIBS := $(BUILD)/libpokemon.a -lm
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lrt
.PHONY: engine test benchmark sanitize validate audit sampler-test profile-test policy-test league-test league-eval native-league-test
native-league-test:
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_native_league.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_native_league
	cd $(ROOT) && build/pokemon/test_native_league
league-eval: $(BUILD)/pokemon-league-eval
$(BUILD)/pokemon-league-eval: pokemon.c pokemon.h pokemon_core.h policy_view.h profile.h bridge.h catalog_meta.h species_labels.h $(ROOT)/src/puffercpu.h $(BUILD)/libpokemon.a
	$(CC) $(CPPFLAGS) $(CFLAGS) pokemon.c $(LDLIBS) $(RAYLIB) -o $@
league-test: league-eval
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_fixed_teams.c $(LDLIBS) $(RAYLIB) -o $(BUILD)/test_fixed_teams
	$(BUILD)/test_fixed_teams
	cd $(ROOT) && python3 ocean/pokemon/tests/test_league.py
policy-test:
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
