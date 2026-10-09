CC ?= cc
BUILD ?= build
INCLUDES := -I../../src -I../../raylib-5.5_linux_amd64/include
LIBS := ../../raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lpthread -ldl -lm
SOURCES := replay.c policy.h kaggriculture_direct.h ../kaggriculture/kag_bc_replay.c ../kaggriculture/policy.h ../kaggriculture/core.h ../kaggriculture/kaggriculture.h ../kaggriculture/critic.h
SOURCE_HASH := $(shell sha256sum $(SOURCES) | sha256sum | cut -c1-16)

.PHONY: test
test: $(BUILD)/test_policy
	$(BUILD)/test_policy

$(BUILD)/test_policy: tests/test_policy.c policy.h ../kaggriculture/policy.h ../kaggriculture/core.h ../kaggriculture/critic.h
	mkdir -p $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Wno-unused-function $< -lm -o $@

.PHONY: replay-bridge
replay-bridge: $(BUILD)/replay.so

.PHONY: test-replay
test-replay: $(BUILD)/test_projection
	$(BUILD)/test_projection

$(BUILD)/test_projection: tests/test_projection.c $(SOURCES)
	mkdir -p $(BUILD)
	$(CC) -std=c11 -O1 $(INCLUDES) -DKAG_WITH_PAIRED_CRITIC $< $(LIBS) -o $@

$(BUILD)/replay.so: $(SOURCES)
	mkdir -p $(BUILD)
	$(CC) -std=c11 -O1 -shared -fPIC $(INCLUDES) -DKAG_WITH_PAIRED_CRITIC \
		-DKAG_BC_SOURCE_HASH=0x$(SOURCE_HASH)ULL replay.c $(LIBS) -o $@
