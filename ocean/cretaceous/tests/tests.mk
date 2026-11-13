# Included by ocean/cretaceous/Makefile. See docs/testing.md.
#   make test            fast suite: determinism, ecosystem arithmetic, critters, quests, binding contract, physics, net codec (fake ENet),
#                        respawn/profile protocol over loopback ENet, startup caches, developer
#                        tooling (console parser, cheats, scenarios, population director), small-animal
#                        meshes, the third-person camera on hard ground (tools/camprobe.c --quick
#                        against tests/camprobe_budget.txt), tick bench
#   make asan            10k-tick bot soak, binding run, codec fuzz (test_net_codec), the
#                        codec's own fuzzer (tools/netfuzz.c) and the camera probe under ASan + UBSan
#   make fuzz            long codec fuzz under ASan + UBSan (FUZZ_ITERS=...)
#   make bench           tick microbenchmark only; make bench-baseline rewrites the stored baseline
#   make eval            policy baselines and the reward audit (tools/cret_eval.c)
# Binaries run from the repository root (world cache and props are relative
# to it). The test world is tiny (512^2); see tests/test_util.h.
TEST_BUILD := $(BUILD)/test
TEST_CC ?= clang
TEST_CFLAGS ?= -O2 -g -std=gnu11 -march=native -fopenmp -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
SAN_CFLAGS ?= -O1 -g -std=gnu11 -fopenmp -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined \
              -Wall -Wno-unused-parameter -Wno-unused-function
TEST_CPP := -I$(BOX3D)/include -I. -Itests
TEST_LIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
BINDING_CPP := -I$(ROOT)/src -I$(RAYLIB)/include
NET_CPP := -Itests/fake_enet
TEST_DEPS := $(CORE) dynprops.c critters.c scent.c eco.c bots.c netcritters.c $(wildcard *.h terra/*.h render/props_place.h render/veg_place.h tests/*.h tests/fake_enet/enet/*.h) net.c netprops.c netclient.c netsim.c cretaceous.h
# test_net_codec includes net.c; the rest of the net module builds against the same fake ENet.
NET_REST := netclient.c netsim.c
NETFUZZ_ITERS ?= 300000
TB := $(abspath $(TEST_BUILD))
RUN := cd $(ROOT) &&
FUZZ_ITERS ?= 2000000
EVAL_ARGS ?= --baselines
# BINDING_ARGS= (empty) prints the full obs-stats table.
BINDING_ARGS ?= --quiet

.PHONY: test asan fuzz bench bench-baseline eval test-bins nest-smoke
$(TEST_BUILD):
	mkdir -p $(TEST_BUILD)
$(TEST_BUILD)/test_sim: tests/test_sim.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# Ecosystem arithmetic and the scripted animals' choices (tests/test_eco.c, docs/ecosystem.md).
$(TEST_BUILD)/test_eco: tests/test_eco.c population.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< population.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_eco_asan: tests/test_eco.c population.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< population.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_critters: tests/test_critters.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# Quests and the character save (tests/test_quests.c).
$(TEST_BUILD)/test_quests: tests/test_quests.c character.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< character.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_quests_asan: tests/test_quests.c character.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< character.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_binding: tests/test_binding.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $(BINDING_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_net_codec: tests/test_net_codec.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(NET_CPP) $(TEST_CPP) $< $(CORE) $(NET_REST) $(TEST_LIBS) -o $@
# Loopback over the real ENet (includes net.c too).
$(TEST_BUILD)/test_net_respawn: tests/test_net_respawn.c $(TEST_DEPS) $(ENET_LIB) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(CPPFLAGS) $< $(CORE) $(NET_REST) $(ENET_LIB) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_net_codec_asan: tests/test_net_codec.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(NET_CPP) $(TEST_CPP) $< $(CORE) $(NET_REST) $(TEST_LIBS) -o $@
$(TEST_BUILD)/netfuzz_asan: tools/netfuzz.c $(TEST_DEPS) $(ENET_LIB) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(CPPFLAGS) $< $(CORE) $(NET) $(ENET_LIB) $(TEST_LIBS) -o $@
# Startup caches (render/cache.c) and CPU texture payloads (render/texbuild.c): no GL, no world.
$(TEST_BUILD)/test_cache: tests/test_cache.c render/cache.c render/cache.h render/texbuild.c render/texbuild.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< render/cache.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_cache_asan: tests/test_cache.c render/cache.c render/cache.h render/texbuild.c render/texbuild.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< render/cache.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/bench: tests/bench.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/cret_eval: tools/cret_eval.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $(BINDING_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# Small-animal meshes (render/critter_mesh.h): no GL, no world.
$(TEST_BUILD)/test_critter_mesh: tests/test_critter_mesh.c render/critter_mesh.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_critter_mesh_asan: tests/test_critter_mesh.c render/critter_mesh.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# The third-person camera (camera.h) on hard ground: tools/camprobe.c, checked against a budget.
$(TEST_BUILD)/camprobe: tools/camprobe.c camera.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/camprobe_asan: tools/camprobe.c camera.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# Developer tooling: console parser, cheats, scenarios, population director (tests/test_debug.c).
DEV_SRC := cheats.c scenario.c population.c
$(TEST_BUILD)/test_debug: tests/test_debug.c $(DEV_SRC) $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(DEV_SRC) $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_debug_asan: tests/test_debug.c $(DEV_SRC) $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< $(DEV_SRC) $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/%_asan: tests/%.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(NET_CPP) $(TEST_CPP) $(BINDING_CPP) $< $(CORE) $(TEST_LIBS) -o $@

test-bins: $(TEST_BUILD)/test_sim $(TEST_BUILD)/test_eco $(TEST_BUILD)/test_critters $(TEST_BUILD)/test_quests $(TEST_BUILD)/test_binding $(TEST_BUILD)/test_net_codec $(TEST_BUILD)/test_net_respawn $(TEST_BUILD)/test_cache $(TEST_BUILD)/test_critter_mesh $(TEST_BUILD)/bench $(TEST_BUILD)/cret_eval $(TEST_BUILD)/test_debug $(TEST_BUILD)/camprobe
test: test-bins
	@$(RUN) OMP_NUM_THREADS=$${OMP_NUM_THREADS:-4} OMP_WAIT_POLICY=$${OMP_WAIT_POLICY:-PASSIVE} $(TB)/test_sim
	@$(RUN) $(TB)/test_eco
	@$(RUN) $(TB)/test_critters
	@$(RUN) $(TB)/test_quests
	@$(RUN) $(TB)/test_binding $(BINDING_ARGS)
	@$(RUN) $(TB)/test_net_codec
	@$(RUN) $(TB)/test_net_respawn
	@$(RUN) $(TB)/test_cache
	@$(RUN) $(TB)/test_critter_mesh
	@$(RUN) $(TB)/test_debug
	@$(RUN) $(TB)/cret_eval --smoke --quiet
	@$(RUN) $(TB)/camprobe --quick --check ocean/cretaceous/tests/camprobe_budget.txt
	@$(RUN) $(TB)/bench --check ocean/cretaceous/tests/bench_baseline.txt
	@echo "cretaceous: all tests passed"
asan: $(TEST_BUILD)/test_sim_asan $(TEST_BUILD)/test_eco_asan $(TEST_BUILD)/test_critters_asan $(TEST_BUILD)/test_quests_asan $(TEST_BUILD)/test_binding_asan $(TEST_BUILD)/test_net_codec_asan $(TEST_BUILD)/netfuzz_asan $(TEST_BUILD)/test_cache_asan $(TEST_BUILD)/test_critter_mesh_asan $(TEST_BUILD)/test_debug_asan $(TEST_BUILD)/camprobe_asan
	@$(RUN) $(TB)/test_sim_asan --soak 10000
	@$(RUN) $(TB)/test_eco_asan
	@$(RUN) $(TB)/test_critters_asan
	@$(RUN) $(TB)/test_quests_asan
	@$(RUN) $(TB)/test_binding_asan --steps 5000
	@$(RUN) $(TB)/test_net_codec_asan --fuzz 200000
	@$(RUN) $(TB)/netfuzz_asan --packets $(NETFUZZ_ITERS)
	@$(RUN) $(TB)/test_cache_asan
	@$(RUN) $(TB)/test_critter_mesh_asan
	@$(RUN) $(TB)/test_debug_asan
	@$(RUN) $(TB)/camprobe_asan --quick --check ocean/cretaceous/tests/camprobe_budget.txt
	@echo "cretaceous: sanitizer runs clean"
fuzz: $(TEST_BUILD)/test_net_codec_asan $(TEST_BUILD)/netfuzz_asan
	@$(RUN) $(TB)/test_net_codec_asan --fuzz $(FUZZ_ITERS)
	@$(RUN) $(TB)/netfuzz_asan --packets $(FUZZ_ITERS)
bench: $(TEST_BUILD)/bench
	@$(RUN) $(TB)/bench
bench-baseline: $(TEST_BUILD)/bench
	@$(RUN) $(TB)/bench --write ocean/cretaceous/tests/bench_baseline.txt
eval: $(TEST_BUILD)/cret_eval
	@$(RUN) $(TB)/cret_eval $(EVAL_ARGS)
# Nest drawing smoke test: the player renders the gallery's raptor nest with and
# without the nests, headless (under xvfb-run when there is no display; Mesa
# llvmpipe is fine), and checks that the nest and its eggs were drawn (render/nest.c).
NEST_SMOKE_X := $(if $(DISPLAY),,$(shell command -v xvfb-run >/dev/null && echo "xvfb-run -a -s '-screen 0 640x360x24'"))
nest-smoke: player
	@$(RUN) $(NEST_SMOKE_X) $(abspath $(BUILD))/cretaceous capture build/cretaceous/nest_smoke --nest-smoke --size 640 360
