ROOT := ../..
BUILD ?= $(ROOT)/build/swat
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC := clang
CFLAGS ?= -O2 -g -std=gnu11 -ffp-contract=off -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-unknown-pragmas
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/vendor/enet/include -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := body.c controller.c pose.c devices.c encounter.c locomotion.c weapons.c materials.c world.c motel.c storefront.c mission.c equipment.c tactical.c overwatch.c generation.c building.c acoustics.c audio_dsp.c sim.c
CHARACTER := character_runtime.c character_view.c character_asset.c
CLIENT := $(CHARACTER) render.c lighting.c weapon_art.c environment_art.c settings.c feedback.c frontend.c frontend_input.c sound_view.c spatial_audio.c
NET := protocol.c net.c replay.c
ENET_LIB := $(ROOT)/build/swat/enet/libenet.a
PHYSICS_LIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
VIEW_LIBS := $(RAYLIB)/lib/libraylib.a -lGL -ldl

.PHONY: all test sanitize viewer server net-test net-test-build net-motel-test net-probe frontend-test-build ui-assets movement-native-test training-benchmark lighting-bake impact-effects-test sound-bank-test sound-profiles-test breach-geometry-test door-support-test door-frame-test wall-support-test door-solid-test entry-interruptions-test squad-tasks-test squad-search-test squad-fire-test motel-squad-search-test motel-squad-fire-test navigation-escort-test motel-dumpster-test motel-dumpster-art-test-build motel-completion-test motel-completion-test-build motel-seeds-test
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
net-test-build: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_net.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_net
net-test: net-test-build
	ulimit -c 0; $(BUILD)/test_net
net-motel-test: net-test-build
	ulimit -c 0; $(BUILD)/test_net motel
.PHONY: motel-occupancy-test motel-occupancy-network-test motel-occupancy-completion-test
motel-occupancy-network-test: net-test-build
	ulimit -c 0; $(BUILD)/test_net occupancy
motel-occupancy-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter occupancy $(BUILD)/motel-occupancy.trace
	ulimit -c 0; $(BUILD)/test_encounter occupancy-corner
	ulimit -c 0; $(BUILD)/test_encounter occupancy-portal
	ulimit -c 0; $(BUILD)/test_encounter occupancy-portal-crowd $(BUILD)/motel-crowded-portal.trace
	ulimit -c 0; $(BUILD)/test_encounter occupancy-restraints $(BUILD)/motel-rear-restraints.trace
	ulimit -c 0; $(BUILD)/test_encounter occupancy-escort $(BUILD)/motel-rear-escort.trace
	ulimit -c 0; $(BUILD)/test_encounter occupancy-follow
motel-occupancy-completion-test: motel-completion-test-build
	ulimit -c 0; $(BUILD)/test_motel_completion $(BUILD)/motel-dispersed.sgrp squad search seed=82 occupancy=1
	ulimit -c 0; $(BUILD)/test_motel_completion $(BUILD)/motel-mixed.sgrp squad search seed=82 occupancy=2
	ulimit -c 0; $(BUILD)/test_motel_completion $(BUILD)/motel-busy.sgrp squad search seed=82 occupancy=3
	ulimit -c 0; $(BUILD)/test_motel_completion $(BUILD)/motel-mixed-hostile.sgrp combat interroom squad search seed=2026 occupancy=2 layout-seed=7
net-probe: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) net_probe.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/net_probe
test: breach-geometry-test door-support-test door-frame-test wall-support-test
wall-support-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_wall_support.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_wall_support
	$(BUILD)/test_wall_support $(BUILD)/wall-support-test.sgrp
door-support-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_door_support.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_door_support
	$(BUILD)/test_door_support $(BUILD)/door-support-test.sgrp
door-frame-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_door_frame.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_door_frame
	ulimit -c 0; $(BUILD)/test_door_frame
breach-geometry-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_breach_geometry.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_breach_geometry
	ulimit -c 0; $(BUILD)/test_breach_geometry
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
encounter-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	$(BUILD)/test_encounter
entry-interruptions-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter interruptions $(BUILD)/entry-interruptions.trace
squad-tasks-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter tasks $(BUILD)/squad-tasks.trace
squad-search-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter search $(BUILD)/squad-search.trace
squad-fire-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter fire $(BUILD)/squad-fire.trace
.PHONY: squad-fire-lanes-test squad-fire-lanes-network-test
squad-fire-lanes-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter fire-lanes $(BUILD)/squad-fire-lanes.trace
squad-fire-lanes-network-test: squad-fire-lanes-test
	ulimit -c 0; $(BUILD)/test_encounter fire-lanes-network
.PHONY: squad-threat-response-test squad-threat-posture-test squad-threat-network-test motel-moving-cover-test
squad-threat-response-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter threat-duel $(BUILD)/squad-threat-duel.trace
squad-threat-posture-test: squad-threat-response-test
	ulimit -c 0; $(BUILD)/test_encounter threat-posture $(BUILD)/squad-threat-posture.trace
squad-threat-network-test: squad-threat-response-test
	ulimit -c 0; $(BUILD)/test_encounter threat-network
motel-moving-cover-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-moving-cover.sgrp combat moving-cover seed=2026 occupancy=2 layout-seed=7
.PHONY: partial-body-navigation-test
.PHONY: mesh-contact-navigation-test squad-protected-people-test
mesh-contact-navigation-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter contact-nav $(BUILD)/mesh-contact-navigation.trace
squad-protected-people-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter protected-person $(BUILD)/protected-person.trace
partial-body-navigation-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter partial-nav $(BUILD)/partial-body-navigation.trace
.PHONY: motel-occupancy-deep-exit-test
motel-occupancy-deep-exit-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter occupancy-deep-exit $(BUILD)/motel-deep-exit.trace
navigation-search-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_navigation_search.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_navigation_search
	$(BUILD)/test_navigation_search

.PHONY: squad-equipment-test squad-equipment-network-test motel-squad-equipment-test
squad-equipment-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter equipment $(BUILD)/squad-equipment.trace
squad-equipment-network-test: squad-equipment-test
	ulimit -c 0; $(BUILD)/test_encounter equipment-network
.PHONY: squad-damaged-equipment-test motel-damaged-equipment-test
.PHONY: squad-initial-equipment-test squad-initial-equipment-network-test
.PHONY: squad-initial-remote-network-test
squad-initial-remote-network-test: squad-equipment-test
	ulimit -c 0; $(BUILD)/test_encounter initial-remote-network
squad-initial-equipment-test: squad-equipment-test
	ulimit -c 0; $(BUILD)/test_encounter initial-equipment $(BUILD)/squad-initial-equipment.trace
squad-initial-equipment-network-test: squad-equipment-test
	ulimit -c 0; $(BUILD)/test_encounter initial-equipment-network
squad-damaged-equipment-test: squad-equipment-test
	ulimit -c 0; $(BUILD)/test_encounter damaged-equipment $(BUILD)/squad-damaged-equipment.trace
motel-damaged-equipment-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-damaged-equipment.sgrp squad search damaged-equipment rooms seed=82
motel-squad-equipment-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-squad-equipment.sgrp squad search equipment seed=82
.PHONY: room-search-test room-search-network-test room-search-motel-test motel-room-search-test
room-search-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_encounter.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_encounter
	ulimit -c 0; $(BUILD)/test_encounter rooms $(BUILD)/room-search.trace
room-search-network-test: room-search-test
	ulimit -c 0; $(BUILD)/test_encounter rooms-network
room-search-motel-test: room-search-test
	ulimit -c 0; $(BUILD)/test_encounter rooms-motel $(BUILD)/room-search-motel.trace
motel-room-search-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-room-search.sgrp squad search equipment rooms seed=82
navigation-escort-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_navigation_search.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_navigation_search
	ulimit -c 0; $(BUILD)/test_navigation_search escort
motel-completion-test-build: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_completion.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_completion
motel-squad-search-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-squad-search.sgrp squad search seed=82
motel-squad-fire-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-squad-fire.sgrp combat interroom squad search seed=2026
motel-seeds-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-seed82-squad.sgrp squad seed=82
	$(BUILD)/test_motel_completion $(BUILD)/motel-seed2026-interroom.sgrp combat interroom seed=2026
motel-completion-test: motel-completion-test-build
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion.sgrp
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion-combat.sgrp combat
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion-exterior.sgrp combat exterior
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion-interroom.sgrp combat interroom
	$(BUILD)/test_motel_completion $(BUILD)/motel-completion-squad.sgrp squad
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
test: impact-effects-test sound-bank-test locomotion-test scenarios-test motel-test motel-nightstand-test motel-seating-test motel-bag-test motel-cabinet-test motel-service-test motel-screwdriver-test motel-dumpster-test motel-roadside-test motel-transformer-test motel-player-contacts-test motel-panes-test motel-windows-test motel-contacts-test mounted-props-test surroundings-test ground-test motel-completion-test storefront-test character-test environment-props-test environment-binding-test payloads-test devices-test encounter-test navigation-search-test foundation-test tactical-test overwatch-test generation-test | $(BUILD)
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
door-solid-test: environment-art-test-build
	ulimit -c 0; $(BUILD)/test_environment_art $(BUILD) door-solid
impact-effects-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_impact_effects.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_impact_effects
	ulimit -c 0; $(BUILD)/test_impact_effects
sound-bank-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_sound_bank.c spatial_audio.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_sound_bank
	ulimit -c 0; $(BUILD)/test_sound_bank assets/audio
sound-profiles-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_sound_profiles.c spatial_audio.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_sound_profiles
	ulimit -c 0; $(BUILD)/test_sound_profiles assets/audio
weapon-art-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_weapon_art.c weapon_art.c character_asset.c lighting.c environment_art.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_weapon_art
lighting-test-build: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_lighting.c lighting.c character_asset.c environment_art.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_lighting
frontend-test-build: ui-assets | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_frontend.c $(CLIENT) $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_frontend
.PHONY: frontend-input-test-build frontend-input-test
frontend-input-test-build: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_frontend_input.c frontend_input.c settings.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_frontend_input
frontend-input-test: frontend-input-test-build
	ulimit -c 0; $(BUILD)/test_frontend_input trace $(BUILD)/frontend-input.trace
	ulimit -c 0; $(BUILD)/test_frontend_input network
	bash tests/test_windows_network_wrapper.sh
test: frontend-input-test

motel-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel
	$(BUILD)/test_motel
motel-panes-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_panes.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_panes
	ulimit -c 0; $(BUILD)/test_motel_panes $(BUILD)/panes-test.sgrp
motel-seating-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_seating.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_seating
	ulimit -c 0; $(BUILD)/test_motel_seating
motel-bag-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_bag.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_bag
	ulimit -c 0; $(BUILD)/test_motel_bag
motel-cabinet-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_cabinet.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_cabinet
	ulimit -c 0; $(BUILD)/test_motel_cabinet
motel-service-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_service.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_service
	ulimit -c 0; $(BUILD)/test_motel_service
motel-screwdriver-test: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_screwdriver.c protocol.c $(CORE) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_screwdriver
	ulimit -c 0; $(BUILD)/test_motel_screwdriver
motel-dumpster-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_dumpster.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_dumpster
	ulimit -c 0; $(BUILD)/test_motel_dumpster $(BUILD)/dumpster-test.trace
motel-dumpster-art-test-build: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_dumpster_art.c character_asset.c lighting.c environment_art.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_motel_dumpster_art
.PHONY: motel-transformer-art-test-build
motel-transformer-art-test-build: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_transformer_art.c character_asset.c lighting.c environment_art.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_motel_transformer_art
.PHONY: motel-roadside-art-test-build
motel-roadside-art-test-build: | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_roadside_art.c character_asset.c lighting.c environment_art.c $(CORE) $(PHYSICS_LIBS) $(VIEW_LIBS) -o $(BUILD)/test_motel_roadside_art
.PHONY: motel-transformer-test
motel-transformer-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_transformer.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_transformer
	ulimit -c 0; $(BUILD)/test_motel_transformer $(BUILD)/transformer-test.trace
.PHONY: motel-roadside-test
motel-roadside-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_roadside.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_roadside
	ulimit -c 0; $(BUILD)/test_motel_roadside $(BUILD)/roadside-test.trace
.PHONY: motel-player-contacts-test
motel-player-contacts-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_player_contacts.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_player_contacts
	ulimit -c 0; $(BUILD)/test_motel_player_contacts $(BUILD)/player-contacts.trace $(BUILD)/player-contacts.sgrp
motel-nightstand-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_nightstand.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_nightstand
	ulimit -c 0; $(BUILD)/test_motel_nightstand $(BUILD)/nightstand-test.sgrp
motel-windows-test: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_motel_windows.c $(NET) $(CORE) $(ENET_LIB) $(PHYSICS_LIBS) -o $(BUILD)/test_motel_windows
	ulimit -c 0; $(BUILD)/test_motel_windows
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
