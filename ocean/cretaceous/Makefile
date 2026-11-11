# Standalone builds for development tools. The player and trainer build via
# ./build.sh cretaceous [--cpu] from the repository root.
ROOT := ../..
BUILD ?= $(ROOT)/build/cretaceous
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC ?= clang
CFLAGS ?= -O2 -g -std=gnu11 -march=native -fopenmp -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/vendor/enet/include -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := terra/terra.c world.c sim.c abilities.c progress.c render/props_place.c
NET := net.c netclient.c netsim.c
VIEW := render/gfx.c render/view.c render/veg.c render/props.c render/csm.c render/gprof.c render/dino.c hud.c capture.c audio.c menu.c character.c
ENET_LIB := $(BUILD)/enet/libenet.a
LIBS := $(BOX3D)/build/src/libbox3d.a $(RAYLIB)/lib/libraylib.a -lGL -lm -lpthread -ldl

.PHONY: all bake player balance popprobe critprobe scnrun stuckprobe net-test net-test-build encoder-test netbench netfuzz
all: bake player balance
# Headless balance run with bots (tools/balance.c): no window, raylib or ENet.
balance: | $(BUILD)
	$(CC) $(CFLAGS) -I$(BOX3D)/include -I. tools/balance.c $(CORE) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/balance
# End-to-end RL throughput through the binding (tools/rlbench.c): no window, GPU, raylib library or ENet.
.PHONY: rlbench
rlbench: | $(BUILD)
	$(CC) $(CFLAGS) -I$(BOX3D)/include -I$(ROOT)/src -I$(RAYLIB)/include -I. tools/rlbench.c $(CORE) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/rlbench
# Headless population probe (tools/popprobe.c): distance from the player to the nearest animal over a session.
popprobe: | $(BUILD)
	$(CC) $(CFLAGS) -I$(BOX3D)/include -I. tools/popprobe.c population.c $(CORE) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/popprobe
# Early-game probe (tools/critprobe.c): a scripted hatchling's first meal, food and first quest, with and without critters.
critprobe: | $(BUILD)
	$(CC) $(CFLAGS) -I$(BOX3D)/include -I. tools/critprobe.c population.c $(CORE) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/critprobe
# Stuck probe (tools/stuckprobe.c): what holds animals that should be moving.
stuckprobe: | $(BUILD)
	$(CC) $(CFLAGS) -I$(BOX3D)/include -I. tools/stuckprobe.c scenario.c cheats.c population.c $(CORE) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/stuckprobe
# Headless scenario runner (tools/scnrun.c): plays config/scenarios/*.ini and prints the summaries.
scnrun: | $(BUILD)
	$(CC) $(CFLAGS) -I$(BOX3D)/include -I. tools/scnrun.c scenario.c cheats.c population.c $(CORE) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/scnrun
$(BUILD):
	mkdir -p $(BUILD)
$(ENET_LIB): | $(BUILD)
	cmake -S $(ROOT)/vendor/enet -B $(BUILD)/enet -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build $(BUILD)/enet --parallel 2 >/dev/null
bake: | $(BUILD)
	$(CC) $(CFLAGS) -I$(RAYLIB)/include terra/terra.c tools/bake.c $(RAYLIB)/lib/libraylib.a -lGL -lm -lpthread -ldl -o $(BUILD)/bake
player: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) cretaceous.c $(CORE) $(NET) $(VIEW) $(ENET_LIB) $(LIBS) -o $(BUILD)/cretaceous
# Headless network tools (no raylib): the benchmark and the codec fuzzer
# (AddressSanitizer + UndefinedBehaviorSanitizer). docs/net.md.
HEADLESS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
netbench: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tools/netbench.c $(CORE) $(NET) $(ENET_LIB) $(HEADLESS) -o $(BUILD)/netbench
netfuzz: $(ENET_LIB) | $(BUILD)
	$(CC) -O1 -g -std=gnu11 -fopenmp -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer $(CPPFLAGS) \
		tools/netfuzz.c $(CORE) $(NET) $(ENET_LIB) $(HEADLESS) -o $(BUILD)/netfuzz
# Headless loopback test of the respawn/species protocol (no window or GPU).
# Runs from the repository root, where the world cache lives.
# (test_net_respawn.c includes net.c, so $(NET) without it.)
net-test-build: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_net_respawn.c $(CORE) netclient.c netsim.c $(ENET_LIB) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/test_net_respawn
net-test: net-test-build
	cd $(ROOT) && $(abspath $(BUILD))/test_net_respawn

# Structured policy encoder (cretaceous_encoder.cu): fp32 checks with finite-difference gradients, a bf16 build,
# and the NumPy/PyTorch reference (tools/encoder_reference.py; skipped when python has no numpy).
NVCC ?= /usr/local/cuda/bin/nvcc
CUDA_ARCH ?= native
PYTHON ?= python3
ENC_FLAGS := -O2 -arch=$(CUDA_ARCH) -std=c++17 -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(RAYLIB)/include -I$(BOX3D)/include -I. \
	-Xcompiler=-fopenmp,-Wno-narrowing --diag-suppress 2361,111,128,550,177
ENC_LIBS := $(RAYLIB)/lib/libraylib.a -lGL -L/usr/local/cuda/lib64 -lcudart -lcublas -lcusolver -lcurand -lnccl -lnvidia-ml -lgomp -lm -ldl -lpthread
ENC_DEPS := test_encoder.cu cretaceous_encoder.cu sim.h $(ROOT)/src/ocean.cu $(ROOT)/src/algo.cu $(ROOT)/src/pufferl.cu
$(BUILD)/test_encoder: $(ENC_DEPS) | $(BUILD)
	$(NVCC) $(ENC_FLAGS) test_encoder.cu $(ENC_LIBS) -o $@
$(BUILD)/test_encoder_bf16: $(ENC_DEPS) | $(BUILD)
	$(NVCC) $(ENC_FLAGS) -DCRET_TEST_BF16 test_encoder.cu $(ENC_LIBS) -o $@
encoder-test: $(BUILD)/test_encoder $(BUILD)/test_encoder_bf16
	cd $(ROOT) && build/cretaceous/test_encoder && build/cretaceous/test_encoder_bf16
	cd $(ROOT) && if $(PYTHON) -c "import numpy" 2>/dev/null; then \
		$(PYTHON) ocean/cretaceous/tools/encoder_reference.py build/cretaceous/encoder_test_fp32.bin && \
		$(PYTHON) ocean/cretaceous/tools/encoder_reference.py build/cretaceous/encoder_test_bf16.bin; \
	else echo "skip reference: $(PYTHON) has no numpy (PYTHON=../../../venv/bin/python)"; fi

# Tests, sanitizers, fuzzing, bench and eval: make test | asan | fuzz | bench | eval (docs/testing.md).
include tests/tests.mk
