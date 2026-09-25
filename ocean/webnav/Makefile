ROOT := ../..
BUILD := $(ROOT)/build/webnav
CC := clang-19
CFLAGS := -O3 -std=c11 -D_POSIX_C_SOURCE=200809L
INCLUDES := -I. -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include

.PHONY: bridge test
bridge:
	bash build.sh

$(BUILD)/test_native: tests/test_native.c bridge.h $(BUILD)/libwebnav.a
	$(CC) $(CFLAGS) -I. $< $(BUILD)/libwebnav.a -lm -lpthread -o $@

$(BUILD)/test_env: tests/test_env.c webnav.h observation.h bridge.h $(BUILD)/libwebnav.a
	$(CC) $(CFLAGS) -mavx2 -mfma $(INCLUDES) $< $(BUILD)/libwebnav.a -lm -lpthread -o $@

test: $(BUILD)/test_native $(BUILD)/test_env
	$(BUILD)/test_native
	$(BUILD)/test_env
