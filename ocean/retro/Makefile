ROOT := ../..
BUILD := $(ROOT)/build/retro
CXX := clang++
CXXFLAGS ?= -O2 -g -march=native -std=c++17
CORE := $(wildcard nes_emu/*.cpp)
OBJECTS := $(patsubst nes_emu/%.cpp,$(BUILD)/%.o,$(CORE))
LIB := $(BUILD)/libquicknes.a
INCLUDES := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include -I. -Ines_emu
LDLIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -ldl -lm -pthread -fopenmp
.PHONY: library test reference-probe sweep-tools
library: $(LIB)
$(BUILD)/%.o: nes_emu/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -Ines_emu -c $< -o $@
$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_retro: tests/test_retro.cpp retro.h retro_obs.h $(LIB)
	$(CXX) $(CXXFLAGS) -mavx2 -mfma $(INCLUDES) $< $(LIB) $(LDLIBS) -o $@
$(BUILD)/test_sweep: tests/test_sweep.cpp retro.h retro_obs.h retro_sweep_config.h $(LIB)
	$(CXX) $(CXXFLAGS) -mavx2 -mfma $(INCLUDES) $< $(LIB) $(LDLIBS) -o $@
test: $(BUILD)/test_retro $(BUILD)/test_sweep
	cd $(ROOT) && $(abspath $(BUILD)/test_retro)
	cd $(ROOT) && $(abspath $(BUILD)/test_sweep)
$(BUILD)/reference_probe: tests/reference_probe.cpp retro.h retro_obs.h $(LIB)
	$(CXX) $(CXXFLAGS) -mavx2 -mfma $(INCLUDES) $< $(LIB) $(LDLIBS) -o $@
reference-probe: $(BUILD)/reference_probe
	cd $(ROOT) && $(abspath $(BUILD)/reference_probe)
-include $(OBJECTS:.o=.d)

$(BUILD)/sweep_eval: sweep_eval.cpp retro.h retro_obs.h retro_sweep_config.h $(ROOT)/src/puffercpu.h $(LIB)
	$(CXX) $(CXXFLAGS) -mavx2 -mfma $(INCLUDES) $< $(LIB) $(LDLIBS) -o $@
sweep-tools: $(BUILD)/sweep_eval
