# Included by ocean/cretaceous/Makefile. See docs/testing.md.
#   make test            fast suite: determinism, far groups, ecosystem arithmetic, critters, quests, binding contract, physics, net codec (fake ENet),
#                        foot IK on synthetic ground (tests/test_dino_ik.c), the animation check on the test dinosaurs (tools/animcheck.c),
#                        respawn/profile protocol over loopback ENet, startup caches, developer
#                        tooling (console parser, cheats, scenarios, population director), small-animal
#                        meshes, the dinosaurs' procedural animation math (tests/test_dino_proc.c), the third-person camera on hard ground (tools/camprobe.c --quick
#                        against tests/camprobe_budget.txt), combat feel (tools/combatprobe.c --quick
#                        against tests/combatprobe_budget.txt), the ecosystem (tools/ecoprobe.c --quick
#                        against tests/ecoprobe_budget.txt), tick bench, netbench replay (a short recording over
#                        loopback ENet at 150 ms with jitter, replayed twice: both must reproduce its hashes; its clean
#                        clients must draw no flag from the server checks), the area of interest, server checks and
#                        telemetry (tests/test_net_aoi.c, and tools/cret_telemetry.py reading the log it writes)
#   make asan            10k-tick bot soak, binding run, codec fuzz (test_net_codec), the
#                        codec's own fuzzer (tools/netfuzz.c), the CPU policy path, the camera probe and the combat probe under ASan + UBSan
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
TEST_DEPS := $(CORE) dynprops.c critters.c scent.c eco.c wounds.c bots.c fargroup.c netcritters.c netaoi.c netcheck.c nettelem.c $(wildcard *.h terra/*.h render/props_place.h render/veg_place.h tests/*.h tests/fake_enet/enet/*.h) net.c netprops.c netjoin.c netclient.c netsim.c cretaceous.h
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
# Far groups (fargroup.h): fold and unfold, placement, determinism with folding on.
$(TEST_BUILD)/test_far: tests/test_far.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
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
# Area of interest, server checks and telemetry (protocol v23, docs/anticheat.md), against the same fake ENet.
$(TEST_BUILD)/test_net_aoi: tests/test_net_aoi.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(NET_CPP) $(TEST_CPP) $< $(CORE) $(NET_REST) $(TEST_LIBS) -o $@
# Loopback over the real ENet (includes net.c too).
$(TEST_BUILD)/test_net_respawn: tests/test_net_respawn.c $(TEST_DEPS) $(ENET_LIB) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(CPPFLAGS) $< $(CORE) $(NET_REST) $(ENET_LIB) $(TEST_LIBS) -o $@
# Joining while the world loads (protocol v20): a 30 Hz server, two clients and a slow-loading one, over the real ENet.
$(TEST_BUILD)/test_net_join: tests/test_net_join.c $(TEST_DEPS) $(ENET_LIB) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(CPPFLAGS) $< $(CORE) $(NET_REST) $(ENET_LIB) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_net_codec_asan: tests/test_net_codec.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(NET_CPP) $(TEST_CPP) $< $(CORE) $(NET_REST) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_net_aoi_asan: tests/test_net_aoi.c $(TEST_DEPS) | $(TEST_BUILD)
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
# CPU policy path (policy_cpu.c) against the NumPy reference's fixtures (tools/policy_reference.py), refusals, a sim run.
$(TEST_BUILD)/test_policy: tests/test_policy.c policy_cpu.c policy_cpu.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< policy_cpu.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_policy_asan: tests/test_policy.c policy_cpu.c policy_cpu.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< policy_cpu.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/cret_eval: tools/cret_eval.c policy_cpu.c policy_cpu.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $(BINDING_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# Small-animal meshes (render/critter_mesh.h): no GL, no world.
$(TEST_BUILD)/test_critter_mesh: tests/test_critter_mesh.c render/critter_mesh.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_critter_mesh_asan: tests/test_critter_mesh.c render/critter_mesh.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# The dinosaurs' procedural animation layers (render/dino_proc.c): no GL, no world, no models.
$(TEST_BUILD)/test_dino_proc: tests/test_dino_proc.c render/dino_proc.c render/dino_proc.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< render/dino_proc.c $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_dino_proc_asan: tests/test_dino_proc.c render/dino_proc.c render/dino_proc.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< render/dino_proc.c $(CORE) $(TEST_LIBS) -o $@
# Foot IK on synthetic ground (render/dino_ik.c, tests/test_dino_ik.c): no GL, no world; the sim is a stub cret_ground_at.
DINO_IK_DEPS := tests/test_dino_ik.c render/dino_ik.c render/dino_ik.h tools/dino_file.h tests/test_util.h $(wildcard tests/fixtures/*.dino)
$(TEST_BUILD)/test_dino_ik: $(DINO_IK_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< render/dino_ik.c -lm -o $@
$(TEST_BUILD)/test_dino_ik_asan: $(DINO_IK_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< render/dino_ik.c -lm -o $@
# The animation check (tools/animcheck.c) on the test dinosaurs: the clean ones pass, the sliding ones are flagged.
$(TEST_BUILD)/animcheck: tools/animcheck.c render/dino_ik.c render/dino_ik.h tools/dino_file.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< render/dino_ik.c $(CORE) $(TEST_LIBS) -o $@
FIXTURES := ocean/cretaceous/tests/fixtures
# The ecosystem (tools/ecoprobe.c --quick: the test world, 24 animals), checked against a budget.
$(TEST_BUILD)/ecoprobe: tools/ecoprobe.c population.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< population.c $(CORE) $(TEST_LIBS) -o $@
# The third-person camera (camera.h) on hard ground: tools/camprobe.c, checked against a budget.
$(TEST_BUILD)/camprobe: tools/camprobe.c camera.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/camprobe_asan: tools/camprobe.c camera.h $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# The combat probe (tools/combatprobe.c --quick), checked against tests/combatprobe_budget.txt.
$(TEST_BUILD)/combatprobe: tools/combatprobe.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/combatprobe_asan: tools/combatprobe.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# Plant meshes and card shading math (tests/test_veg.c, render/veg_gen.h, render/veg_look.h): no GL.
VEG_TEST_DEPS := render/veg_gen.h render/veg_look.h render/veg_layers.h render/veg_place.h render/flora.h
$(TEST_BUILD)/test_veg: tests/test_veg.c $(VEG_TEST_DEPS) $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
# Developer tooling: console parser, cheats, scenarios, population director (tests/test_debug.c).
DEV_SRC := cheats.c scenario.c population.c
$(TEST_BUILD)/test_debug: tests/test_debug.c $(DEV_SRC) $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(DEV_SRC) $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/test_debug_asan: tests/test_debug.c $(DEV_SRC) $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(TEST_CPP) $< $(DEV_SRC) $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/%_asan: tests/%.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(NET_CPP) $(TEST_CPP) $(BINDING_CPP) $< $(CORE) $(TEST_LIBS) -o $@

# netbench (tools/netbench.c) with ENet's clock on its virtual one, for the replay check (docs/net.md, Replay).
$(TEST_BUILD)/netbench: tools/netbench.c $(TEST_DEPS) $(ENET_LIB) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(CPPFLAGS) $< $(CORE) $(NET) $(ENET_LIB) $(TEST_LIBS) $(NETBENCH_LD) -o $@
NETREPLAY := $(TB)/netbench --port 27650
NETREPLAY_REC := --scenario 150j20 --agents 32 --clients 4 --seconds 4 --warmup 1 --checks log --record $(TB)/netreplay.rec
test-bins: $(TEST_BUILD)/netbench $(TEST_BUILD)/test_sim $(TEST_BUILD)/test_far $(TEST_BUILD)/test_eco $(TEST_BUILD)/ecoprobe $(TEST_BUILD)/test_critters $(TEST_BUILD)/test_quests $(TEST_BUILD)/test_binding $(TEST_BUILD)/test_net_codec $(TEST_BUILD)/test_net_aoi $(TEST_BUILD)/test_net_respawn $(TEST_BUILD)/test_net_join $(TEST_BUILD)/test_cache $(TEST_BUILD)/test_critter_mesh $(TEST_BUILD)/test_dino_proc $(TEST_BUILD)/bench $(TEST_BUILD)/cret_eval $(TEST_BUILD)/test_debug $(TEST_BUILD)/camprobe $(TEST_BUILD)/combatprobe $(TEST_BUILD)/test_policy $(TEST_BUILD)/test_dino_ik $(TEST_BUILD)/animcheck $(TEST_BUILD)/test_veg
test: test-bins
	@$(RUN) OMP_NUM_THREADS=$${OMP_NUM_THREADS:-4} OMP_WAIT_POLICY=$${OMP_WAIT_POLICY:-PASSIVE} $(TB)/test_sim
	@$(RUN) $(TB)/test_far
	@$(RUN) $(TB)/test_eco
	@$(RUN) $(TB)/test_critters
	@$(RUN) $(TB)/test_quests
	@$(RUN) $(TB)/test_binding $(BINDING_ARGS)
	@$(RUN) $(TB)/test_net_codec
	@$(RUN) $(TB)/test_net_aoi
	@$(RUN) python3 -I ocean/cretaceous/tools/cret_telemetry.py $(TB)/telemetry_test.ctl --check
	@$(RUN) $(TB)/test_net_respawn
	@$(RUN) $(TB)/test_net_join
	@$(RUN) $(TB)/test_cache
	@$(RUN) $(TB)/test_critter_mesh
	@$(RUN) $(TB)/test_dino_proc
	@$(RUN) $(TB)/test_debug
	@$(RUN) $(TB)/test_veg
	@$(RUN) $(TB)/test_dino_ik > /dev/null
	@$(RUN) $(TB)/animcheck --check --species raptor $(FIXTURES)/fixture_biped.dino > /dev/null && $(TB)/animcheck --check --species sauropelta $(FIXTURES)/fixture_quad.dino > /dev/null \
		&& ! $(TB)/animcheck --check --species raptor $(FIXTURES)/fixture_biped_slide.dino > /dev/null && ! $(TB)/animcheck --check --species sauropelta $(FIXTURES)/fixture_quad_slide.dino > /dev/null \
		&& echo "animcheck: ok (clean fixtures pass, sliding fixtures flagged)" || { echo "animcheck: FAILED on the fixtures (run it on tests/fixtures/*.dino)"; false; }
	@$(RUN) $(TB)/cret_eval --smoke --quiet
	@$(RUN) $(TB)/test_policy
	@$(RUN) $(TB)/camprobe --quick --check ocean/cretaceous/tests/camprobe_budget.txt
	@$(RUN) $(TB)/combatprobe --quick --check ocean/cretaceous/tests/combatprobe_budget.txt > /dev/null
	@$(RUN) $(TB)/ecoprobe --quick --quiet --check ocean/cretaceous/tests/ecoprobe_budget.txt
	@$(RUN) $(TB)/bench --check ocean/cretaceous/tests/bench_baseline.txt
	@$(RUN) $(NETREPLAY) $(NETREPLAY_REC) > $(TB)/netreplay.log 2>&1 || { cat $(TB)/netreplay.log; exit 1; }
	@grep -q "; flags 0$$" $(TB)/netreplay.log || { grep "interest" $(TB)/netreplay.log; echo "netbench: the server checks flagged clean clients"; exit 1; }
	@$(RUN) for k in 1 2; do $(NETREPLAY) --replay $(TB)/netreplay.rec --check 2>&1 > /dev/null | grep "the replay matches" || exit 1; done
	@echo "cretaceous: all tests passed"
asan: $(TEST_BUILD)/test_sim_asan $(TEST_BUILD)/test_eco_asan $(TEST_BUILD)/test_critters_asan $(TEST_BUILD)/test_quests_asan $(TEST_BUILD)/test_binding_asan $(TEST_BUILD)/test_net_codec_asan $(TEST_BUILD)/test_net_aoi_asan $(TEST_BUILD)/netfuzz_asan $(TEST_BUILD)/test_cache_asan $(TEST_BUILD)/test_critter_mesh_asan $(TEST_BUILD)/test_dino_proc_asan $(TEST_BUILD)/test_debug_asan $(TEST_BUILD)/camprobe_asan $(TEST_BUILD)/combatprobe_asan $(TEST_BUILD)/test_policy_asan $(TEST_BUILD)/test_dino_ik_asan
	@$(RUN) $(TB)/test_sim_asan --soak 10000
	@$(RUN) $(TB)/test_eco_asan
	@$(RUN) $(TB)/test_critters_asan
	@$(RUN) $(TB)/test_quests_asan
	@$(RUN) $(TB)/test_binding_asan --steps 5000
	@$(RUN) $(TB)/test_net_codec_asan --fuzz 200000
	@$(RUN) $(TB)/test_net_aoi_asan
	@$(RUN) $(TB)/netfuzz_asan --packets $(NETFUZZ_ITERS)
	@$(RUN) $(TB)/test_cache_asan
	@$(RUN) $(TB)/test_critter_mesh_asan
	@$(RUN) $(TB)/test_dino_proc_asan
	@$(RUN) $(TB)/test_debug_asan
	@$(RUN) $(TB)/test_policy_asan
	@$(RUN) $(TB)/test_dino_ik_asan > /dev/null
	@$(RUN) $(TB)/camprobe_asan --quick --check ocean/cretaceous/tests/camprobe_budget.txt
	@$(RUN) $(TB)/combatprobe_asan --quick --check ocean/cretaceous/tests/combatprobe_budget.txt > /dev/null
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
