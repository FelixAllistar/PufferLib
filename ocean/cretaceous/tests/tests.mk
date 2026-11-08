# Included by ocean/cretaceous/Makefile. See docs/testing.md.
#   make test            fast suite: determinism, binding contract, physics, net codec (fake ENet),
#                        respawn/profile protocol over loopback ENet, tick bench
#   make asan            10k-tick bot soak, binding run, codec fuzz (test_net_codec) and the
#                        codec's own fuzzer (tools/netfuzz.c) under ASan + UBSan
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
TEST_DEPS := $(CORE) $(wildcard *.h terra/*.h render/props_place.h tests/*.h tests/fake_enet/enet/*.h) net.c netclient.c netsim.c cretaceous.h
# test_net_codec includes net.c; the rest of the net module builds against the same fake ENet.
NET_REST := netclient.c netsim.c
NETFUZZ_ITERS ?= 300000
TB := $(abspath $(TEST_BUILD))
RUN := cd $(ROOT) &&
FUZZ_ITERS ?= 2000000
EVAL_ARGS ?= --baselines
# BINDING_ARGS= (empty) prints the full obs-stats table.
BINDING_ARGS ?= --quiet

.PHONY: test asan fuzz bench bench-baseline eval test-bins
$(TEST_BUILD):
	mkdir -p $(TEST_BUILD)
$(TEST_BUILD)/test_sim: tests/test_sim.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
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
$(TEST_BUILD)/bench: tests/bench.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/cret_eval: tools/cret_eval.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_CPP) $(BINDING_CPP) $< $(CORE) $(TEST_LIBS) -o $@
$(TEST_BUILD)/%_asan: tests/%.c $(TEST_DEPS) | $(TEST_BUILD)
	$(TEST_CC) $(SAN_CFLAGS) $(NET_CPP) $(TEST_CPP) $(BINDING_CPP) $< $(CORE) $(TEST_LIBS) -o $@

test-bins: $(TEST_BUILD)/test_sim $(TEST_BUILD)/test_binding $(TEST_BUILD)/test_net_codec $(TEST_BUILD)/test_net_respawn $(TEST_BUILD)/bench $(TEST_BUILD)/cret_eval
test: test-bins
	@$(RUN) OMP_NUM_THREADS=$${OMP_NUM_THREADS:-4} OMP_WAIT_POLICY=$${OMP_WAIT_POLICY:-PASSIVE} $(TB)/test_sim
	@$(RUN) $(TB)/test_binding $(BINDING_ARGS)
	@$(RUN) $(TB)/test_net_codec
	@$(RUN) $(TB)/test_net_respawn
	@$(RUN) $(TB)/cret_eval --smoke --quiet
	@$(RUN) $(TB)/bench --check ocean/cretaceous/tests/bench_baseline.txt
	@echo "cretaceous: all tests passed"
asan: $(TEST_BUILD)/test_sim_asan $(TEST_BUILD)/test_binding_asan $(TEST_BUILD)/test_net_codec_asan $(TEST_BUILD)/netfuzz_asan
	@$(RUN) $(TB)/test_sim_asan --soak 10000
	@$(RUN) $(TB)/test_binding_asan --steps 5000
	@$(RUN) $(TB)/test_net_codec_asan --fuzz 200000
	@$(RUN) $(TB)/netfuzz_asan --packets $(NETFUZZ_ITERS)
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
