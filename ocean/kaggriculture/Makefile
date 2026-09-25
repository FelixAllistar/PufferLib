CC ?= cc
NVCC ?= nvcc
CUDA_ARCH ?= native
BUILD ?= build
REPLAY_SOURCES := core.h policy.h kaggriculture.h kag_bc_replay.c ../../src/ini.h ../../src/pufferenv.h
REPLAY_HASH := $(shell sha256sum $(REPLAY_SOURCES) | sha256sum | cut -c1-16)
INCLUDES := -I../../src -I../../raylib-5.5_linux_amd64/include
RENDER_LIBS := ../../raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl

.PHONY: replay-bridge replay-test submission-bridge render-test render-gpu-test
replay-bridge: $(BUILD)/kag_bc_replay.so

submission-bridge: $(BUILD)/entity_bridge.so

$(BUILD)/entity_bridge.so: submission/entity_bridge.c policy.h core.h | $(BUILD)
	$(CC) -O2 -shared -fPIC submission/entity_bridge.c -lm -o $@

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kag_bc_replay.so: $(REPLAY_SOURCES) | $(BUILD)
	$(CC) -O2 -shared -fPIC $(INCLUDES) -DKAG_BC_SOURCE_HASH=0x$(REPLAY_HASH)ULL \
		kag_bc_replay.c $(RENDER_LIBS) -lm -o $@

replay-test: | $(BUILD)
	$(CC) -O2 $(INCLUDES) tests/test_bc_replay.c $(RENDER_LIBS) -lm -o $(BUILD)/test_bc_replay
	cd ../.. && $(abspath $(BUILD)/test_bc_replay)

# Explicit display tests; normal headless builds never open a window.
render-test: | $(BUILD)
	$(CC) -O2 $(INCLUDES) tests/test_render.c $(RENDER_LIBS) -lm -o $(BUILD)/test_render
	$(BUILD)/test_render $(BUILD)/render_cpu.png

render-gpu-test: | $(BUILD)
	$(NVCC) -x cu -c -O2 -arch=$(CUDA_ARCH) -DKAG_RENDER_GPU $(INCLUDES) \
		-Xcompiler=-Wno-narrowing --diag-suppress=2361 tests/test_render.c \
		-o $(BUILD)/test_render_gpu.o
	$(NVCC) $(BUILD)/test_render_gpu.o $(RENDER_LIBS) -lm -o $(BUILD)/test_render_gpu
	$(BUILD)/test_render_gpu $(BUILD)/render_gpu.png
