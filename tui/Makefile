ROOT := ..
BUILD := build
CC ?= cc
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64
CPPFLAGS := -I. -I$(ROOT) -I$(ROOT)/src -I$(RAYLIB)/include
CFLAGS ?= -O2 -std=gnu11

.PHONY: all breakout test
all: $(BUILD)/tui_viewer

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tui_viewer: tui_viewer.c | $(BUILD)
	$(CC) $(CFLAGS) $< -ldl -o $@

breakout: all | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DPUFFER_TUI_CAPTURE -DPUFFERCPU_EVAL_MAIN \
		-DENV_HEADER='"ocean/breakout/breakout.h"' -DPUFFER_ENV_NAME='"breakout"' \
		$(ROOT)/src/puffercpu.c $(RAYLIB)/lib/libraylib.a \
		-lm -lGL -ldl -lpthread -o $(BUILD)/breakout

test: all | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_capture.c -o $(BUILD)/test_capture
	$(BUILD)/test_capture
