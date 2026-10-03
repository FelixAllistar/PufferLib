ROOT := ../..
BUILD ?= $(ROOT)/build/swat
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC := clang
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/vendor/enet/include -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := body.c controller.c weapons.c world.c acoustics.c audio_dsp.c sim.c
CLIENT := render.c settings.c frontend.c sound_view.c
NET := protocol.c net.c
ENET_LIB := $(ROOT)/build/swat/enet/libenet.a
PHYSICS_LIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
VIEW_LIBS := $(RAYLIB)/lib/libraylib.a -lGL -ldl

.PHONY: all test sanitize viewer server net-test net-probe frontend-test-build
all: test viewer
$(BUILD):
	mkdir -p $(BUILD)
$(ENET_LIB): $(wildcard $(ROOT)/vendor/enet/*.c) $(wildcard $(ROOT)/vendor/enet/include/enet/*.h)
	cmake -S $(ROOT)/vendor/enet -B $(ROOT)/build/swat/enet -DCMAKE_BUILD_TYPE=Release
	cmake --build $(ROOT)/build/swat/enet --parallel 2
server: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) server.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/server
net-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_net.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_net
	ulimit -c 0; $(BUILD)/test_net
net-probe: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) net_probe.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/net_probe
test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_settings.c settings.c -lm -o $(BUILD)/test_settings
	$(BUILD)/test_settings $(BUILD)/settings-test.ini
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_audio_dsp.c audio_dsp.c -lm -o $(BUILD)/test_audio_dsp
	$(BUILD)/test_audio_dsp
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_acoustics.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_acoustics
	ulimit -c 0; $(BUILD)/test_acoustics
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_protocol.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_protocol
	ulimit -c 0; $(BUILD)/test_protocol
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_sim.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_sim
	ulimit -c 0; $(BUILD)/test_sim
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_adapter.c $(CORE) render.c $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_adapter
	ulimit -c 0; $(BUILD)/test_adapter
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas -fsanitize=address,undefined -fno-omit-frame-pointer'
viewer: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) swat.c $(CLIENT) $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/swat
	cp play.sh $(ROOT)/swat.launcher.tmp
	chmod +x $(ROOT)/swat.launcher.tmp
	mv $(ROOT)/swat.launcher.tmp $(ROOT)/swat
frontend-test-build: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_frontend.c $(CLIENT) $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_frontend
