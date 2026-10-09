ROOT := ../..
BUILD ?= $(ROOT)/build/swat
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC := clang
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/vendor/enet/include -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := body.c controller.c pose.c devices.c encounter.c locomotion.c weapons.c materials.c world.c motel.c storefront.c mission.c equipment.c tactical.c overwatch.c generation.c building.c acoustics.c audio_dsp.c sim.c
CHARACTER := character_runtime.c character_view.c character_asset.c
CLIENT := $(CHARACTER) render.c lighting.c weapon_art.c environment_art.c settings.c feedback.c frontend.c sound_view.c spatial_audio.c
NET := protocol.c net.c replay.c
ENET_LIB := $(ROOT)/build/swat/enet/libenet.a
PHYSICS_LIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
VIEW_LIBS := $(RAYLIB)/lib/libraylib.a -lGL -ldl

.PHONY: all test sanitize viewer server net-test net-probe frontend-test-build ui-assets movement-native-test training-benchmark lighting-bake
training-benchmark: | $(BUILD)
	$(CC) $(CFLAGS) -fopenmp $(CPPFLAGS) training_benchmark.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/training_benchmark
lighting-bake: | $(BUILD)
	$(CC) -O2 -std=c11 -I$(RAYLIB)/include lighting_bake.c -lm -o $(BUILD)/lighting_bake
	$(BUILD)/lighting_bake assets/environment/lighting_v1/kloofendal_48d_partly_cloudy_puresky_1k.hdr assets/environment/lighting_v1/daylight.bin
character-probe: | $(BUILD)
	$(CC) $(CFLAGS) -I. tests/character_probe.c character_asset.c -lm -o $(BUILD)/character_probe
character-test: character-probe
	python3 tests/test_character_contract.py --probe $(BUILD)/character_probe
character-lab: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) character_lab.c character_view.c character_asset.c lighting.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/character_lab
character-render-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_character_render.c $(CHARACTER) lighting.c weapon_art.c environment_art.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_character_render
performance-tool: $(ENET_LIB) ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) performance_tool.c $(CHARACTER) render.c lighting.c weapon_art.c environment_art.c sound_view.c spatial_audio.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/performance_tool
layout-tool: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) layout_tool.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/layout_tool
replay-tool: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) replay_tool.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/replay_tool
foundation-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_foundation.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_foundation
	$(BUILD)/test_foundation $(BUILD)/foundation-test.sgrp
audio-import: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) audio_import.c -lm -lpthread $(VIEW_LIBS) -o $(BUILD)/audio_import
audio-lab: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) audio_lab.c spatial_audio.c $(CORE) $(PHYSICS_LIBS) -ldl -o $(BUILD)/audio_lab
spatial-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_spatial_audio.c spatial_audio.c -lm -ldl -o $(BUILD)/test_spatial_audio
	$(BUILD)/test_spatial_audio
all: test viewer
ui-assets: | $(BUILD)
	mkdir -p $(BUILD)/assets
	cp -R $(ROOT)/ocean/swat/assets/. $(BUILD)/assets/
	cp $(ROOT)/resources/shared/Roboto-Regular.ttf $(BUILD)/assets/ui/
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
navigation-search-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_navigation_search.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_navigation_search
	$(BUILD)/test_navigation_search
motel-completion-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_completion.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_completion
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion.sgrp
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion-combat.sgrp combat
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion-exterior.sgrp combat exterior
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion-interroom.sgrp combat interroom
environment-binding-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_environment_binding.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_environment_binding
	$(BUILD)/test_environment_binding
environment-props-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_environment_props.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_environment_props
	$(BUILD)/test_environment_props
movement-native-test: | $(BUILD)
	$(CC) $(CFLAGS) -fopenmp $(CPPFLAGS) tests/test_movement_native.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_movement_native
	$(BUILD)/test_movement_native $(BUILD)/movement-native-test.bin
locomotion-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_locomotion.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_locomotion
	$(BUILD)/test_locomotion $(BUILD)/locomotion-test.weights
scenarios-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_scenarios.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_scenarios
	ulimit -c 0; $(BUILD)/test_scenarios
test: locomotion-test scenarios-test motel-test motel-contacts-test mounted-props-test surroundings-test ground-test motel-completion-test storefront-test character-test environment-props-test environment-binding-test payloads-test devices-test encounter-test navigation-search-test foundation-test tactical-test overwatch-test generation-test | $(BUILD)
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
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_adapter.c $(CORE) $(CHARACTER) render.c lighting.c weapon_art.c environment_art.c $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_adapter
	ulimit -c 0; $(BUILD)/test_adapter
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas -fsanitize=address,undefined -fno-omit-frame-pointer'
viewer: $(ENET_LIB) ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) swat.c $(CLIENT) $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/swat
	cp play.sh $(ROOT)/swat.launcher.tmp
	chmod +x $(ROOT)/swat.launcher.tmp
	mv $(ROOT)/swat.launcher.tmp $(ROOT)/swat
environment-art-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_environment_art.c $(CHARACTER) render.c lighting.c weapon_art.c environment_art.c protocol.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_environment_art
weapon-art-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_weapon_art.c weapon_art.c character_asset.c lighting.c environment_art.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_weapon_art
lighting-test-build: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_lighting.c lighting.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_lighting
frontend-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_frontend.c $(CLIENT) $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_frontend

motel-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel
	$(BUILD)/test_motel
motel-contacts-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_contacts.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_contacts
	ulimit -c 0; $(BUILD)/test_motel_contacts
mounted-props-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_mounted_props.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_mounted_props
	$(BUILD)/test_mounted_props $(BUILD)/mounted-props.sgrp
ground-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_ground.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_ground
	$(BUILD)/test_ground
surroundings-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_surroundings.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_surroundings
	ulimit -c 0; $(BUILD)/test_surroundings
storefront-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_storefront.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_storefront
	$(BUILD)/test_storefront
