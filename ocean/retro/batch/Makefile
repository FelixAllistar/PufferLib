ROOT := ../../..
BUILD := $(ROOT)/build/retro_batch
CORE_BUILD := $(ROOT)/build/retro
CORE := $(filter-out ../nes_emu/Nes_Cpu.cpp,$(wildcard ../nes_emu/*.cpp))
OBJECTS := $(patsubst ../nes_emu/%.cpp,$(CORE_BUILD)/%.o,$(CORE))
CXX := clang++
CXXFLAGS ?= -O2 -g -march=native -std=c++17
INCLUDES := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include -I.. -I../nes_emu -I$(BUILD)
LDLIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -ldl -lm -pthread -fopenmp
.PHONY: all common test
all: $(BUILD)/retro_batch $(BUILD)/test_batch $(BUILD)/sweep_eval
common:
	$(MAKE) -C .. library
$(BUILD)/rom_blocks.inc $(BUILD)/rom_blocks_identity.h $(BUILD)/rom_blocks_cases.inc &: generate_blocks.py ../roms/smb1.nes
	python3 generate_blocks.py ../roms/smb1.nes $(BUILD)
$(BUILD)/Nes_Cpu.o: ../nes_emu/Nes_Cpu.cpp ../nes_emu/Nes_Cpu.h $(BUILD)/rom_blocks.inc
	$(CXX) $(CXXFLAGS) -O1 -g0 $(INCLUDES) -DRETRO_ROM_BLOCKS -MMD -MP -c $< -o $@
$(BUILD)/libquicknes_batch.a: $(BUILD)/Nes_Cpu.o common
	$(AR) rcs $@ $(OBJECTS) $(BUILD)/Nes_Cpu.o
$(BUILD)/retro_batch: ../retro.c ../retro.h ../retro_obs.h ../retro_inspect.h ../retro_policy_cpu.h ../retro_policy_shape.h $(BUILD)/libquicknes_batch.a
	$(CXX) $(CXXFLAGS) -mavx2 -mfma $(INCLUDES) -x c++ $< -x none $(BUILD)/libquicknes_batch.a $(LDLIBS) -o $@
$(BUILD)/test_batch: test_batch.cpp ../retro.h ../retro_obs.h $(BUILD)/libquicknes_batch.a
	$(CXX) $(CXXFLAGS) -mavx2 -mfma $(INCLUDES) $< $(BUILD)/libquicknes_batch.a $(LDLIBS) -o $@
$(BUILD)/sweep_eval: ../sweep_eval.cpp ../retro.h ../retro_obs.h ../retro_sweep_config.h ../retro_policy_cpu.h ../retro_policy_shape.h $(BUILD)/libquicknes_batch.a
	$(CXX) $(CXXFLAGS) -mavx2 -mfma $(INCLUDES) $< $(BUILD)/libquicknes_batch.a $(LDLIBS) -o $@
$(BUILD)/test_screen_observation: test_observation.cpp ../retro.h ../retro_obs.h $(BUILD)/libquicknes_batch.a
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(BUILD)/libquicknes_batch.a $(LDLIBS) -o $@
test: $(BUILD)/test_batch $(BUILD)/test_screen_observation
	cd $(ROOT) && $(abspath $(BUILD)/test_batch)
	cd $(ROOT) && $(abspath $(BUILD)/test_screen_observation)
-include $(BUILD)/Nes_Cpu.d
