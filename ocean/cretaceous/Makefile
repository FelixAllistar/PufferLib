# Standalone builds for development tools. The player and trainer build via
# ./build.sh cretaceous [--cpu] from the repository root.
ROOT := ../..
BUILD ?= $(ROOT)/build/cretaceous
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC ?= clang
CFLAGS ?= -O2 -g -std=gnu11 -march=native -fopenmp -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/vendor/enet/include -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := terra/terra.c world.c sim.c abilities.c progress.c render/props_place.c
NET := net.c
VIEW := render/gfx.c render/view.c render/veg.c render/props.c render/csm.c render/gprof.c render/dino.c hud.c capture.c audio.c menu.c character.c
ENET_LIB := $(BUILD)/enet/libenet.a
LIBS := $(BOX3D)/build/src/libbox3d.a $(RAYLIB)/lib/libraylib.a -lGL -lm -lpthread -ldl

.PHONY: all bake player balance
all: bake player balance
# Headless balance run with bots (tools/balance.c): no window, raylib or ENet.
balance: | $(BUILD)
	$(CC) $(CFLAGS) -I$(BOX3D)/include -I. tools/balance.c $(CORE) $(BOX3D)/build/src/libbox3d.a -lm -lpthread -o $(BUILD)/balance
$(BUILD):
	mkdir -p $(BUILD)
$(ENET_LIB): | $(BUILD)
	cmake -S $(ROOT)/vendor/enet -B $(BUILD)/enet -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build $(BUILD)/enet --parallel 2 >/dev/null
bake: | $(BUILD)
	$(CC) $(CFLAGS) -I$(RAYLIB)/include terra/terra.c tools/bake.c $(RAYLIB)/lib/libraylib.a -lGL -lm -lpthread -ldl -o $(BUILD)/bake
player: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) cretaceous.c $(CORE) $(NET) $(VIEW) $(ENET_LIB) $(LIBS) -o $(BUILD)/cretaceous
