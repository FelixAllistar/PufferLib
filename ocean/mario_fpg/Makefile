ROOT := ../..
BUILD ?= $(ROOT)/build/mario_fpg
CC := clang
NVCC ?= /usr/local/cuda/bin/nvcc
INCLUDES := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include
CORE_DEPS := fpg_core.h fpg_enemies.h
ROM_DEPS := fpg_rom.h $(ROOT)/ocean/retro/smb1_tiles.h
LDLIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread
.PHONY: reference bank-builder test sanitize viewer policy-eval gpu-test transfer bench parity rom-gpu-test qualify
MODEL ?= checkpoints/mario_fpg/1790959626339/0000000134217728.bin
CONFIG ?= config/mario_fpg.ini
PARITY_OUT ?= build/mario_fpg/qualification
$(BUILD):
	mkdir -p $(BUILD)
$(ROOT)/build/retro/libquicknes.a:
	$(MAKE) -C ../retro library
$(BUILD)/reference: reference.cpp fpg_rom.h fpg_core.h search.h $(ROOT)/build/retro/libquicknes.a | $(BUILD)
	clang++ -O2 -g -std=c++17 $(INCLUDES) -I../retro/nes_emu $< $(ROOT)/build/retro/libquicknes.a $(LDLIBS) -fopenmp -o $@
reference: $(BUILD)/reference
$(BUILD)/build_bank: build_bank.cpp search.h fpg_task.h fpg_core.h | $(BUILD)
	clang++ -O3 -std=c++17 $< -o $@
bank-builder: $(BUILD)/build_bank
$(BUILD)/policy.o: policy.c policy.h fpg_task.h fpg_core.h $(ROOT)/src/puffercpu.c | $(BUILD)
	$(CC) -O2 -std=c11 $(INCLUDES) -c $< -o $@
$(BUILD)/eval: eval.c mario_fpg.h fpg_config.h fpg_task.h fpg_core.h $(BUILD)/policy.o | $(BUILD)
	$(CC) -O2 -std=c11 $(INCLUDES) $< $(BUILD)/policy.o -lm -o $@
policy-eval: $(BUILD)/eval
$(BUILD)/transfer: transfer.cpp fpg_rom.h fpg_task.h fpg_config.h fpg_core.h policy.h $(BUILD)/policy.o $(ROOT)/build/retro/libquicknes.a | $(BUILD)
	clang++ -O2 -g -std=c++17 $(INCLUDES) -I../retro/nes_emu $< $(BUILD)/policy.o $(ROOT)/build/retro/libquicknes.a $(LDLIBS) -fopenmp -o $@
transfer: $(BUILD)/transfer
$(BUILD)/parity: parity.cpp parity_format.h fpg_rom.h fpg_task.h fpg_config.h fpg_core.h policy.h $(BUILD)/policy.o $(ROOT)/build/retro/libquicknes.a | $(BUILD)
	clang++ -O2 -g -std=c++17 $(INCLUDES) -I../retro/nes_emu $< $(BUILD)/policy.o $(ROOT)/build/retro/libquicknes.a $(LDLIBS) -fopenmp -o $@
parity: $(BUILD)/parity
$(BUILD)/viewer: mario_fpg.c mario_fpg.h fpg_config.h fpg_task.h fpg_core.h | $(BUILD)
	$(CC) -O2 -std=c11 $(INCLUDES) $< $(LDLIBS) -o $@
viewer: $(BUILD)/viewer
$(BUILD)/test_task: tests/test_task.c mario_fpg.h fpg_config.h fpg_task.h fpg_core.h | $(BUILD)
	$(CC) -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter $(INCLUDES) $< -lm -o $@
$(BUILD)/test_contract: tests/test_contract.c fpg_task.h $(CORE_DEPS) $(ROM_DEPS) | $(BUILD)
	$(CC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -o $@
test: $(BUILD)/test_task $(BUILD)/test_contract
	cd $(ROOT) && build/mario_fpg/test_task
	$(BUILD)/test_contract
sanitize: | $(BUILD)
	$(CC) -O1 -g -std=c11 -fsanitize=address,undefined $(INCLUDES) tests/test_task.c -lm -o $(BUILD)/test_sanitize
	cd $(ROOT) && ASAN_OPTIONS=detect_leaks=0 build/mario_fpg/test_sanitize
$(BUILD)/test_cuda: tests/test_cuda.cu mario_fpg.cu mario_fpg.h fpg_config.h fpg_task.h fpg_core.h | $(BUILD)
	$(NVCC) -O2 -arch=native -std=c++17 $(INCLUDES) $< -o $@
gpu-test: $(BUILD)/test_cuda
	cd $(ROOT) && build/mario_fpg/test_cuda
$(BUILD)/test_rom_cuda: tests/test_rom_cuda.cu parity_format.h fpg_task.h fpg_core.h | $(BUILD)
	$(NVCC) -O2 -arch=native -std=c++17 $< -o $@
rom-gpu-test: $(BUILD)/test_rom_cuda
	@test -n "$(TRACE)" || (echo 'Set TRACE to a passing parity run rom_traces.bin path, relative to repo root'; exit 2)
	cd $(ROOT) && build/mario_fpg/test_rom_cuda "$(TRACE)"
qualify: test gpu-test $(BUILD)/parity $(BUILD)/test_rom_cuda
	cd $(ROOT) && env -u FPG_PARITY_SOURCE build/mario_fpg/parity "$(MODEL)" "$(CONFIG)" "$(PARITY_OUT)"
	cd $(ROOT) && build/mario_fpg/test_rom_cuda "$(PARITY_OUT)/rom_traces.bin"
$(BUILD)/bench_cuda: tests/bench_cuda.cu mario_fpg.cu mario_fpg.h fpg_config.h fpg_task.h fpg_core.h | $(BUILD)
	$(NVCC) -O3 -arch=native -std=c++17 $(INCLUDES) $< -o $@
bench: $(BUILD)/bench_cuda
	cd $(ROOT) && build/mario_fpg/bench_cuda

$(BUILD)/reference $(BUILD)/build_bank $(BUILD)/policy.o $(BUILD)/eval $(BUILD)/transfer $(BUILD)/parity $(BUILD)/viewer $(BUILD)/test_task $(BUILD)/test_cuda $(BUILD)/test_rom_cuda $(BUILD)/bench_cuda: $(CORE_DEPS)
$(BUILD)/reference $(BUILD)/transfer $(BUILD)/parity: $(ROM_DEPS)
