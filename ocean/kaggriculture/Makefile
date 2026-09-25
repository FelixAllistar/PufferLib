CC ?= cc
BUILD ?= build
REPLAY_SOURCES := core.h policy.h kaggriculture.h kag_bc_replay.c ../../src/ini.h ../../src/pufferenv.h
REPLAY_HASH := $(shell sha256sum $(REPLAY_SOURCES) | sha256sum | cut -c1-16)
INCLUDES := -I../../src -I../../raylib-5.5_linux_amd64/include

.PHONY: replay-bridge replay-test
replay-bridge: $(BUILD)/kag_bc_replay.so

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kag_bc_replay.so: $(REPLAY_SOURCES) | $(BUILD)
	$(CC) -O2 -shared -fPIC $(INCLUDES) -DKAG_BC_SOURCE_HASH=0x$(REPLAY_HASH)ULL \
		kag_bc_replay.c -lm -o $@

replay-test: | $(BUILD)
	$(CC) -O2 $(INCLUDES) tests/test_bc_replay.c -lm -o $(BUILD)/test_bc_replay
	cd ../.. && $(abspath $(BUILD)/test_bc_replay)
