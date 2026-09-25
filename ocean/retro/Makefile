ROOT := ../..
BUILD := $(ROOT)/build/retro
BATCH := $(ROOT)/build/retro_batch
CXX := clang++
CXXFLAGS ?= -O2 -g -march=native -std=c++17
RETRO_OBS_SCALE ?= 4
CXXFLAGS += -DRETRO_OBS_SCALE=$(RETRO_OBS_SCALE)
CORE := $(wildcard nes_emu/*.cpp)
OBJECTS := $(patsubst nes_emu/%.cpp,$(BUILD)/%.o,$(CORE))
LIB := $(BUILD)/libquicknes.a
INCLUDES := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include -I. -Ines_emu
LDLIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -ldl -lm -pthread -fopenmp

.PHONY: library batch-library viewer test batch-test resolution policy-test policy-legacy-test encoder-test
viewer: $(BUILD)/viewer
$(BUILD)/retro_policy_api.o: retro_policy_api.c retro_policy_cpu.h retro_policy_shape.h $(ROOT)/src/puffercpu.c resolution
	@mkdir -p $(BUILD)
	clang -O2 -std=c11 -I$(ROOT)/src -DRETRO_OBS_SCALE=$(RETRO_OBS_SCALE) -c $< -o $@
$(BUILD)/viewer: retro.c retro_inspect.h retro.h retro_playback.h $(BUILD)/retro_policy_api.o $(BATCH)/libquicknes_batch.a resolution
	$(CXX) $(CXXFLAGS) $(INCLUDES) -x c++ $< -x none $(BUILD)/retro_policy_api.o \
		$(BATCH)/libquicknes_batch.a $(LDLIBS) -o $@

encoder-test: library
	clang -O2 -std=c11 -ffp-contract=off -I$(ROOT)/src -DRETRO_OBS_SCALE=$(RETRO_OBS_SCALE) \
		-DRETRO_CUDA_REFERENCE -c tests/test_policy_cpu.c -o $(BUILD)/cpu_reference.o
	$${CUDA_HOME:-/usr/local/cuda}/bin/nvcc -O2 --threads 2 -arch=$${NVCC_ARCH:-native} \
		-std=c++17 -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor $(INCLUDES) \
		-DRETRO_OBS_SCALE=$(RETRO_OBS_SCALE) -Xcompiler=-fopenmp,-Wno-narrowing,-Wno-multichar \
		--diag-suppress 550 --diag-suppress 68 --diag-suppress 2361 --diag-suppress 1422 \
		tests/test_encoder.cu $(BUILD)/cpu_reference.o $(LIB) $(filter-out -pthread -fopenmp,$(LDLIBS)) \
		-lpthread -lgomp \
		-lcudart -lcublas -lcusolver -lcurand -lnccl -lnvidia-ml -o $(BUILD)/test_encoder

policy-test:
	@mkdir -p $(BUILD)
	clang -O2 -std=c11 -ffp-contract=off -I$(ROOT)/src -DRETRO_OBS_SCALE=$(RETRO_OBS_SCALE) \
		tests/test_policy_cpu.c -lm -o $(BUILD)/test_policy_cpu
	$(BUILD)/test_policy_cpu

policy-legacy-test: policy-test
	test -n "$(LEGACY_POLICY)"
	$(CXX) -O2 -std=c++17 -ffp-contract=off -I$(dir $(LEGACY_POLICY))/../../src \
		-DRETRO_OBS_SCALE=$(RETRO_OBS_SCALE) -DRETRO_LEGACY_POLICY='"$(LEGACY_POLICY)"' \
		-x c++ tests/test_policy_cpu.c -lm -o $(BUILD)/test_policy_legacy
	test "$$($(BUILD)/test_policy_cpu)" = "$$($(BUILD)/test_policy_legacy)"

library: $(LIB)
batch-library: $(BATCH)/libquicknes_batch.a
$(BUILD)/%.o: nes_emu/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -Ines_emu -c $< -o $@
$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^

resolution:
$(BUILD)/test_retro: tests/test_retro.cpp tests/observation_reference.h retro.h retro_obs.h retro_playback.h retro_resolution.h retro_timing.h retro_rom_identity.h $(LIB) resolution
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(LIB) $(LDLIBS) -o $@
test: $(BUILD)/test_retro
	cd $(ROOT) && $(abspath $(BUILD)/test_retro)

$(BATCH)/rom_blocks.inc $(BATCH)/rom_blocks_identity.h $(BATCH)/rom_blocks_cases.inc &: batch/generate_blocks.py retro_rom_identity.h roms/smb1_ntsc.nes
	python3 batch/generate_blocks.py roms/smb1_ntsc.nes $(BATCH)
$(BATCH)/Nes_Cpu.o: nes_emu/Nes_Cpu.cpp nes_emu/Nes_Cpu.h $(BATCH)/rom_blocks.inc
	$(CXX) $(CXXFLAGS) -O1 -g0 -Ines_emu -I$(BATCH) -DRETRO_ROM_BLOCKS -MMD -MP -c $< -o $@
$(BATCH)/libquicknes_batch.a: $(filter-out $(BUILD)/Nes_Cpu.o,$(OBJECTS)) $(BATCH)/Nes_Cpu.o
	$(AR) rcs $@ $^
$(BATCH)/test_batch: batch/test_batch.cpp tests/test_retro.cpp tests/observation_reference.h retro.h retro_obs.h retro_playback.h retro_resolution.h retro_timing.h retro_rom_identity.h $(BATCH)/libquicknes_batch.a resolution
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I$(BATCH) $< $(BATCH)/libquicknes_batch.a $(LDLIBS) -o $@
$(BATCH)/test_observation: batch/test_observation.cpp retro.h retro_obs.h retro_resolution.h retro_timing.h retro_rom_identity.h $(BATCH)/libquicknes_batch.a resolution
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I$(BATCH) $< $(BATCH)/libquicknes_batch.a $(LDLIBS) -o $@
batch-test: $(BATCH)/test_batch $(BATCH)/test_observation
	cd $(ROOT) && $(abspath $(BATCH)/test_batch)
	cd $(ROOT) && $(abspath $(BATCH)/test_observation)

-include $(OBJECTS:.o=.d) $(BATCH)/Nes_Cpu.d
