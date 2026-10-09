ROOT := ../..
BUILD ?= $(ROOT)/build/mario_sim
OUT ?= $(BUILD)/qualification
CC := clang
CXX := clang++
NVCC ?= /usr/local/cuda/bin/nvcc
PTXAS ?= /usr/local/cuda/bin/ptxas
CUDA_ARCH ?= sm_61
INCLUDES := -I$(BUILD) -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include -I../retro/nes_emu
LDLIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread -fopenmp
ROM_LIB := $(ROOT)/build/retro/libquicknes.a
ROM_HEADERS := $(wildcard ../retro/nes_emu/*.h)
.PHONY: catalog logic-test qualify
$(BUILD):
	mkdir -p $(BUILD)
$(ROM_LIB):
	$(MAKE) -C ../retro library
$(BUILD)/catalog: catalog.cpp ../retro/retro.h ../retro/retro_rom_identity.h $(ROM_LIB) | $(BUILD)
	$(CXX) -O2 -std=c++17 $(INCLUDES) $< $(ROM_LIB) $(LDLIBS) -o $@
catalog: $(BUILD)/catalog
	cd $(ROOT) && "$(abspath $(BUILD))/catalog" "$(abspath $(BUILD))/catalog.json"
$(BUILD)/generated_logic.h: generate_logic.py ../retro/nes_emu/Nes_Cpu.cpp $(ROOT)/build/mario_lab/reference/smbdis_complete.asm $(ROOT)/ocean/retro/roms/smb1_ntsc.nes | $(BUILD)
	cd $(ROOT) && python3 ocean/mario_sim/generate_logic.py ocean/retro/roms/smb1_ntsc.nes build/mario_lab/reference/smbdis_complete.asm "$(abspath $@)"
$(BUILD)/logic_cpu.o: logic_cpu.cpp logic_cpu.h logic.h $(BUILD)/generated_logic.h | $(BUILD)
	$(CXX) -O1 -std=c++17 $(INCLUDES) -c $< -o $@
$(BUILD)/verify_logic: verify_logic.cpp logic.h logic_cpu.h rom_import.h $(ROM_HEADERS) $(BUILD)/logic_cpu.o $(ROM_LIB) | $(BUILD)
	$(CXX) -O1 -std=c++17 $(INCLUDES) $< $(BUILD)/logic_cpu.o $(ROM_LIB) $(LDLIBS) -o $@
$(BUILD)/reconstruct: reconstruct.cpp reference_sources.h trace.h scene.h parameters.h world.h logic.h logic_cpu.h rom_import.h $(ROM_HEADERS) $(BUILD)/logic_cpu.o $(ROM_LIB) | $(BUILD)
	$(CXX) -O1 -std=c++17 $(INCLUDES) $< $(BUILD)/logic_cpu.o $(ROM_LIB) $(LDLIBS) -o $@
$(BUILD)/find_clip: find_clip.cpp logic_cpu.h rom_import.h $(ROM_HEADERS) $(BUILD)/logic_cpu.o $(ROM_LIB) | $(BUILD)
	$(CXX) -O1 -std=c++17 $(INCLUDES) $< $(BUILD)/logic_cpu.o $(ROM_LIB) $(LDLIBS) -o $@
$(BUILD)/debug_clip: debug_clip.cpp trace.h scene.h logic_cpu.h $(BUILD)/logic_cpu.o | $(BUILD)
	$(CXX) -O1 -std=c++17 $(INCLUDES) $< $(BUILD)/logic_cpu.o -o $@
$(BUILD)/cuda_replay.ptx: cuda_replay.cu cuda_replay.h trace.h scene.h logic.h $(BUILD)/generated_logic.h | $(BUILD)
	$(NVCC) -G -O0 -arch=$(CUDA_ARCH) -std=c++17 $(INCLUDES) -ptx $< -o $@
$(BUILD)/cuda_replay.cubin: $(BUILD)/cuda_replay.ptx | $(BUILD)
	$(PTXAS) -arch=$(CUDA_ARCH) -O0 $< -o $@
$(BUILD)/verify_cuda: verify_cuda.cu cuda_module.cpp cuda_module.h cuda_replay.h trace.h scene.h logic.h $(BUILD)/cuda_replay.cubin | $(BUILD)
	$(NVCC) -O1 -std=c++17 $(INCLUDES) verify_cuda.cu cuda_module.cpp -L/usr/local/cuda/lib64/stubs -lcuda -o $@
logic-test: $(BUILD)/verify_logic
	cd $(ROOT) && "$(abspath $(BUILD))/verify_logic" 32 1200

# This target verifies fidelity only. It never starts a trainer.
qualify: $(BUILD)/catalog $(BUILD)/reconstruct $(BUILD)/verify_cuda
	mkdir -p "$(abspath $(OUT))"
	cd $(ROOT) && "$(abspath $(BUILD))/catalog" "$(abspath $(OUT))/catalog.json"
	cd $(ROOT) && env -u SMB_RECONSTRUCT_CASE "$(abspath $(BUILD))/reconstruct" "$(abspath $(OUT))/main"
	cd $(ROOT) && env -u SMB_RECONSTRUCT_CASE "$(abspath $(BUILD))/reconstruct" "$(abspath $(OUT))/supplement" supplement
	cd $(ROOT) && env -u SMB_RECONSTRUCT_CASE "$(abspath $(BUILD))/reconstruct" "$(abspath $(OUT))/world" world
	cd $(ROOT) && env -u SMB_RECONSTRUCT_CASE "$(abspath $(BUILD))/reconstruct" "$(abspath $(OUT))/boundaries" boundaries
	cd $(ROOT) && "$(abspath $(BUILD))/verify_cuda" "$(abspath $(OUT))/main/clips.bin" > "$(abspath $(OUT))/main/cuda.json"
	cd $(ROOT) && "$(abspath $(BUILD))/verify_cuda" "$(abspath $(OUT))/supplement/clips.bin" > "$(abspath $(OUT))/supplement/cuda.json"
	cd $(ROOT) && "$(abspath $(BUILD))/verify_cuda" "$(abspath $(OUT))/world/clips.bin" > "$(abspath $(OUT))/world/cuda.json"
	cd $(ROOT) && "$(abspath $(BUILD))/verify_cuda" "$(abspath $(OUT))/boundaries/clips.bin" > "$(abspath $(OUT))/boundaries/cuda.json"
	cd $(ROOT) && python3 ocean/mario_sim/check_trace_guards.py "$(abspath $(BUILD))/verify_cuda" "$(abspath $(OUT))/main/clips.bin" "$(abspath $(OUT))/guards"
	cd $(ROOT) && python3 ocean/mario_sim/summarize_trace.py "$(abspath $(OUT))/main" "$(abspath $(OUT))/supplement" "$(abspath $(OUT))/world" "$(abspath $(OUT))/boundaries"
