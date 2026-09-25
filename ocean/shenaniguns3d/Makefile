ROOT := ../..
BUILD := $(ROOT)/build/shenaniguns3d
BOX3D ?= $(ROOT)/../box3d
CC := clang
CFLAGS ?= -O2 -g -std=gnu11
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include -I$(BOX3D)/include -I.
LDLIBS := $(BOX3D)/build/src/libbox3d.a $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread

.PHONY: test sanitize
test:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_cpu.c character.c $(LDLIBS) -o $(BUILD)/test_cpu
	$(BUILD)/test_cpu

sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer'
