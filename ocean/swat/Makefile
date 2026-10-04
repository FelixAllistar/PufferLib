ROOT := ../..
BUILD ?= $(ROOT)/build/swat
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC := clang
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/vendor/enet/include -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := body.c controller.c pose.c devices.c encounter.c weapons.c materials.c world.c mission.c equipment.c tactical.c overwatch.c generation.c acoustics.c audio_dsp.c sim.c
CLIENT := render.c environment_art.c settings.c feedback.c frontend.c sound_view.c spatial_audio.c
NET := protocol.c net.c replay.c
ENET_LIB := $(ROOT)/build/swat/enet/libenet.a
PHYSICS_LIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
VIEW_LIBS := $(RAYLIB)/lib/libraylib.a -lGL -ldl

.PHONY: all test sanitize viewer server net-test net-probe frontend-test-build ui-assets
layout-tool: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) layout_tool.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/layout_tool
replay-tool: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) replay_tool.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/replay_tool
foundation-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_foundation.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_foundation
	$(BUILD)/test_foundation $(BUILD)/foundation-test.sgrp
audio-lab: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) audio_lab.c spatial_audio.c $(CORE) $(PHYSICS_LIBS) -ldl -o $(BUILD)/audio_lab
spatial-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_spatial_audio.c spatial_audio.c -lm -ldl -o $(BUILD)/test_spatial_audio
	$(BUILD)/test_spatial_audio
all: test viewer
ui-assets: | $(BUILD)
	mkdir -p $(BUILD)/assets/ui
	cp $(ROOT)/resources/shared/Roboto-Regular.ttf $(BUILD)/assets/ui/
	mkdir -p $(BUILD)/assets/environment
	cp $(ROOT)/ocean/swat/assets/environment/*.png $(ROOT)/ocean/swat/assets/environment/*.glb $(ROOT)/ocean/swat/assets/environment/*.json $(ROOT)/ocean/swat/assets/environment/*.txt $(BUILD)/assets/environment/
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
tactical-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_tactical.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_tactical
	ulimit -c 0; $(BUILD)/test_tactical
overwatch-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_overwatch.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_overwatch
	ulimit -c 0; $(BUILD)/test_overwatch
generation-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_generation.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_generation
	ulimit -c 0; $(BUILD)/test_generation
payloads-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_payloads.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_payloads
	$(BUILD)/test_payloads
devices-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_devices.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_devices
	$(BUILD)/test_devices
encounter-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	$(BUILD)/test_encounter
environment-binding-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_environment_binding.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_environment_binding
	$(BUILD)/test_environment_binding
test: environment-binding-test payloads-test devices-test encounter-test foundation-test tactical-test overwatch-test generation-test | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_mission.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_mission
	ulimit -c 0; $(BUILD)/test_mission
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_settings.c settings.c -lm -o $(BUILD)/test_settings
	$(BUILD)/test_settings $(BUILD)/settings-test.ini
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_audio_dsp.c audio_dsp.c materials.c -lm -o $(BUILD)/test_audio_dsp
	$(BUILD)/test_audio_dsp
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_acoustics.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_acoustics
	ulimit -c 0; $(BUILD)/test_acoustics
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_protocol.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_protocol
	ulimit -c 0; $(BUILD)/test_protocol
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_sim.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_sim
	ulimit -c 0; $(BUILD)/test_sim
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_adapter.c $(CORE) render.c environment_art.c $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_adapter
	ulimit -c 0; $(BUILD)/test_adapter
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas -fsanitize=address,undefined -fno-omit-frame-pointer'
viewer: $(ENET_LIB) ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) swat.c $(CLIENT) $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/swat
	cp play.sh $(ROOT)/swat.launcher.tmp
	chmod +x $(ROOT)/swat.launcher.tmp
	mv $(ROOT)/swat.launcher.tmp $(ROOT)/swat
environment-art-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_environment_art.c render.c environment_art.c protocol.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_environment_art
frontend-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_frontend.c $(CLIENT) $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_frontend
