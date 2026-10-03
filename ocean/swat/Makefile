ROOT := ../..
BUILD ?= $(ROOT)/build/swat
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC := clang
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := body.c controller.c weapons.c world.c sim.c
PHYSICS_LIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
VIEW_LIBS := $(RAYLIB)/lib/libraylib.a -lGL -ldl

.PHONY: all test sanitize viewer
all: test viewer
$(BUILD):
	mkdir -p $(BUILD)
test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_sim.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_sim
	ulimit -c 0; $(BUILD)/test_sim
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_adapter.c $(CORE) render.c $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_adapter
	ulimit -c 0; $(BUILD)/test_adapter
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas -fsanitize=address,undefined -fno-omit-frame-pointer'
viewer: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) swat.c render.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/swat
