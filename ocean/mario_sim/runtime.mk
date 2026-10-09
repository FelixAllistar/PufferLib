# Run from the repository root with: make -f ocean/mario_sim/runtime.mk -j2
RUNTIME_DIR ?= build/mario_sim/runtime
RUNTIME_SRC := ocean/mario_sim
NVCC ?= /usr/local/cuda/bin/nvcc
CUDA_ARCH ?= sm_61
CPU_OPT ?= -O3
CUDA_OPT ?= -O3
INCLUDES := -I$(RUNTIME_DIR) -I$(RUNTIME_SRC)
GENERATOR := $(RUNTIME_SRC)/generate_runtime.py $(RUNTIME_SRC)/generate_logic.py
GEN_INPUTS := ocean/retro/roms/smb1_ntsc.nes build/mario_lab/reference/smbdis_complete.asm
$(RUNTIME_DIR)/sources.mk: $(GENERATOR) $(GEN_INPUTS)
	mkdir -p $(RUNTIME_DIR)
	python3 $(RUNTIME_SRC)/generate_runtime.py $(GEN_INPUTS) $(RUNTIME_DIR)
include $(RUNTIME_DIR)/sources.mk

CPU_OBJECTS := $(addprefix $(RUNTIME_DIR)/cpu/,$(RUNTIME_GENERATED:.cpp=.o))
GPU_OBJECTS := $(addprefix $(RUNTIME_DIR)/gpu/,$(RUNTIME_GENERATED:.cpp=.o))
.PHONY: all cpu gpu bank-builder environment
.DEFAULT_GOAL := all
all: cpu gpu
cpu: $(RUNTIME_DIR)/logic_cpu.a $(RUNTIME_DIR)/verify_cpu
gpu: $(RUNTIME_DIR)/cuda_replay.cubin $(RUNTIME_DIR)/verify_cuda $(RUNTIME_DIR)/bench_gpu
bank-builder: $(RUNTIME_DIR)/build_bank
environment: $(RUNTIME_DIR)/test_runtime $(RUNTIME_DIR)/bench_env $(RUNTIME_DIR)/test_bank

$(RUNTIME_DIR)/cpu/%.o: $(RUNTIME_DIR)/%.cpp $(RUNTIME_DIR)/generated_logic.h $(RUNTIME_SRC)/logic.h $(RUNTIME_SRC)/runtime_registers.h $(RUNTIME_SRC)/runtime_poll.h
	mkdir -p $(@D)
	clang++ $(CPU_OPT) -std=c++17 $(INCLUDES) -c $< -o $@
$(RUNTIME_DIR)/cpu/logic_cpu.o: $(RUNTIME_SRC)/logic_cpu.cpp $(RUNTIME_DIR)/generated_logic.h $(RUNTIME_SRC)/logic.h
	mkdir -p $(@D)
	clang++ $(CPU_OPT) -std=c++17 $(INCLUDES) -c $< -o $@
$(RUNTIME_DIR)/logic_cpu.a: $(CPU_OBJECTS) $(RUNTIME_DIR)/cpu/logic_cpu.o
	ar rcs $@ $^
$(RUNTIME_DIR)/verify_cpu: $(RUNTIME_SRC)/verify_runtime_cpu.cpp $(RUNTIME_SRC)/trace.h $(RUNTIME_DIR)/logic_cpu.a
	clang++ $(CPU_OPT) -std=c++17 $(INCLUDES) $< $(RUNTIME_DIR)/logic_cpu.a -o $@
$(RUNTIME_DIR)/build_bank: $(RUNTIME_SRC)/build_runtime_bank.cpp $(RUNTIME_SRC)/bank.h $(RUNTIME_SRC)/generator.h $(RUNTIME_SRC)/reference_sources.h $(RUNTIME_DIR)/logic_cpu.a build/retro/libquicknes.a
	clang++ -O2 -std=c++17 $(INCLUDES) -Isrc -Iraylib-5.5_linux_amd64/include -Iocean/retro/nes_emu $< $(RUNTIME_DIR)/logic_cpu.a build/retro/libquicknes.a raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread -fopenmp -o $@
build/retro/libquicknes.a:
	$(MAKE) -C ocean/retro library

$(RUNTIME_DIR)/gpu/%.o: $(RUNTIME_DIR)/%.cpp $(RUNTIME_DIR)/generated_logic.h $(RUNTIME_SRC)/logic.h $(RUNTIME_SRC)/runtime_registers.h $(RUNTIME_SRC)/runtime_poll.h
	mkdir -p $(@D)
	$(NVCC) $(CUDA_OPT) -arch=$(CUDA_ARCH) -std=c++17 $(INCLUDES) -x cu -dc $< -o $@
$(RUNTIME_DIR)/gpu/kernel.o: $(RUNTIME_SRC)/runtime_kernel.cu $(RUNTIME_SRC)/cuda_replay.cu $(RUNTIME_SRC)/cuda_replay.h $(RUNTIME_SRC)/trace.h $(RUNTIME_SRC)/scene.h $(RUNTIME_DIR)/generated_logic.h
	mkdir -p $(@D)
	$(NVCC) $(CUDA_OPT) -arch=$(CUDA_ARCH) -std=c++17 $(INCLUDES) -dc $< -o $@
$(RUNTIME_DIR)/cuda_replay.cubin: $(GPU_OBJECTS) $(RUNTIME_DIR)/gpu/kernel.o
	$(NVCC) -arch=$(CUDA_ARCH) -dlink -cubin $^ -o $@
$(RUNTIME_DIR)/verify_cuda: $(RUNTIME_SRC)/verify_cuda.cu $(RUNTIME_SRC)/cuda_module.cpp $(RUNTIME_SRC)/cuda_module.h $(RUNTIME_SRC)/trace.h
	$(NVCC) -O2 -std=c++17 $(INCLUDES) $(RUNTIME_SRC)/verify_cuda.cu $(RUNTIME_SRC)/cuda_module.cpp -L/usr/local/cuda/lib64/stubs -lcuda -o $@
$(RUNTIME_DIR)/bench_gpu: $(RUNTIME_SRC)/bench_runtime_gpu.cu $(RUNTIME_SRC)/trace.h $(RUNTIME_DIR)/cuda_replay.cubin
	$(NVCC) -O2 -std=c++17 $(INCLUDES) $< -L/usr/local/cuda/lib64/stubs -lcuda -o $@
ENV_DEPS := $(RUNTIME_SRC)/mario_sim.cu $(wildcard $(RUNTIME_SRC)/*.h)
$(RUNTIME_DIR)/test_runtime: $(RUNTIME_SRC)/test_runtime.cu $(ENV_DEPS) $(RUNTIME_DIR)/logic_cpu.a $(RUNTIME_DIR)/cuda_replay.cubin
	$(NVCC) -O2 -arch=$(CUDA_ARCH) -std=c++17 $(INCLUDES) -Isrc -Iraylib-5.5_linux_amd64/include $< $(RUNTIME_DIR)/logic_cpu.a -L/usr/local/cuda/lib64/stubs -lcuda -o $@
$(RUNTIME_DIR)/bench_env: $(RUNTIME_SRC)/bench_env.cu $(ENV_DEPS) $(RUNTIME_DIR)/cuda_replay.cubin
	$(NVCC) -O2 -arch=$(CUDA_ARCH) -std=c++17 $(INCLUDES) -Isrc -Iraylib-5.5_linux_amd64/include $< -L/usr/local/cuda/lib64/stubs -lcuda -o $@
$(RUNTIME_DIR)/test_bank: $(RUNTIME_SRC)/test_bank.cpp $(RUNTIME_SRC)/bank_io.h $(RUNTIME_SRC)/bank.h
	clang++ -O2 -std=c++17 $(INCLUDES) $< -o $@
