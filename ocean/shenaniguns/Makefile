ROOT := ../..
BUILD ?= build
CC := clang
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
INCLUDES := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include
LIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lm -lGL -ldl -lpthread

.PHONY: test sanitize demo
$(BUILD):
	mkdir -p $(BUILD)
test: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) test_adapter.c $(LIBS) -o $(BUILD)/test_adapter
	cd $(ROOT) && $(abspath $(BUILD)/test_adapter)
sanitize:
	$(MAKE) test BUILD=build/sanitize CFLAGS="-O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer"
demo: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) shenaniguns.c $(LIBS) -o $(BUILD)/demo
