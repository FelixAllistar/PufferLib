ROOT := ../..
BUILD ?= $(ROOT)/build/mario_lab
CC := clang
NVCC ?= /usr/local/cuda/bin/nvcc
CUDA_ARCH ?= native
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function
INCLUDES := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include
LDLIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread

.PHONY: test gpu-test bench sanitize viewer policy-eval rom-bridge sim-probe policy-test
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/test_sim: tests/test_sim.c ml_sim.h | $(BUILD)
	$(CC) $(CFLAGS) $< -lm -o $@
$(BUILD)/test_rom_obs: tests/test_rom_obs.c ml_rom_obs.h ml_sim.h ../retro/smb1_tiles.h | $(BUILD)
	$(CC) $(CFLAGS) $< -lm -o $@
test: $(BUILD)/test_sim $(BUILD)/test_rom_obs
	$(BUILD)/test_sim
	$(BUILD)/test_rom_obs
$(BUILD)/test_policy_api: tests/test_policy_api.c ml_policy_api.c ml_policy_api.h ml_sim.h $(ROOT)/src/puffercpu.c | $(BUILD)
	$(CC) -O2 -std=c11 $< -lm -o $@
policy-test: $(BUILD)/test_policy_api
	$(BUILD)/test_policy_api $(MODEL)
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-omit-frame-pointer'
$(BUILD)/test_cuda: tests/test_cuda.cu mario_lab.cu mario_lab.h ml_config.h ml_sim.h | $(BUILD)
	$(NVCC) -O2 -arch=$(CUDA_ARCH) -std=c++17 $(INCLUDES) $< -o $@
gpu-test: $(BUILD)/test_cuda
	$(BUILD)/test_cuda
$(BUILD)/bench_cuda: tests/bench_cuda.cu mario_lab.cu mario_lab.h ml_config.h ml_sim.h | $(BUILD)
	$(NVCC) -O3 -arch=$(CUDA_ARCH) -std=c++17 $(INCLUDES) $< -o $@
bench: $(BUILD)/bench_cuda
	$(BUILD)/bench_cuda 4096 512
$(BUILD)/viewer: mario_lab.c mario_lab.h ml_config.h ml_sim.h | $(BUILD)
	$(CC) -O2 -std=c11 $(INCLUDES) $< $(LDLIBS) -o $@
viewer: $(BUILD)/viewer
$(BUILD)/eval_policy: eval.c mario_lab.h ml_config.h ml_sim.h $(ROOT)/src/puffercpu.c | $(BUILD)
	$(CC) -O2 -std=c11 $(INCLUDES) $< -lm -o $@
policy-eval: $(BUILD)/eval_policy
$(BUILD)/ml_policy_api.o: ml_policy_api.c ml_policy_api.h ml_sim.h $(ROOT)/src/puffercpu.c | $(BUILD)
	$(CC) -O2 -std=c11 $(INCLUDES) -c $< -o $@
$(ROOT)/build/retro/libquicknes.a:
	$(MAKE) -C ../retro library
$(BUILD)/rom_bridge: rom_bridge.cpp ml_rom_obs.h ml_config.h ml_sim.h ml_policy_api.h $(BUILD)/ml_policy_api.o $(ROOT)/build/retro/libquicknes.a | $(BUILD)
	clang++ -O2 -g -std=c++17 $(INCLUDES) -I../retro/nes_emu $< $(BUILD)/ml_policy_api.o \
		$(ROOT)/build/retro/libquicknes.a $(LDLIBS) -fopenmp -o $@
rom-bridge: $(BUILD)/rom_bridge
$(BUILD)/sim_probe: sim_probe.c ml_sim.h ml_config.h | $(BUILD)
	$(CC) -O2 -std=c11 $(INCLUDES) $< -lm -o $@
sim-probe: $(BUILD)/sim_probe
