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

.PHONY: library test batch-test resolution
library: $(LIB)
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
