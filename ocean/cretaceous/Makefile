# Standalone builds for development tools. The player and trainer build via
# ./build.sh cretaceous [--cpu] from the repository root.
ROOT := ../..
BUILD ?= $(ROOT)/build/cretaceous
BOX3D ?= $(ROOT)/../box3d
RAYLIB ?= $(ROOT)/raylib-5.5_linux_amd64
CC ?= clang
CFLAGS ?= -O2 -g -std=gnu11 -march=native -fopenmp -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/vendor/enet/include -I$(RAYLIB)/include -I$(BOX3D)/include -I.
CORE := terra/terra.c world.c sim.c render/props_place.c
NET := net.c
VIEW := render/gfx.c render/view.c render/veg.c render/props.c render/csm.c render/gprof.c render/dino.c hud.c capture.c audio.c menu.c
ENET_LIB := $(BUILD)/enet/libenet.a
LIBS := $(BOX3D)/build/src/libbox3d.a $(RAYLIB)/lib/libraylib.a -lGL -lm -lpthread -ldl

.PHONY: all bake player encoder-test
all: bake player
$(BUILD):
	mkdir -p $(BUILD)
$(ENET_LIB): | $(BUILD)
	cmake -S $(ROOT)/vendor/enet -B $(BUILD)/enet -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build $(BUILD)/enet --parallel 2 >/dev/null
bake: | $(BUILD)
	$(CC) $(CFLAGS) -I$(RAYLIB)/include terra/terra.c tools/bake.c $(RAYLIB)/lib/libraylib.a -lGL -lm -lpthread -ldl -o $(BUILD)/bake
player: $(ENET_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) cretaceous.c $(CORE) $(NET) $(VIEW) $(ENET_LIB) $(LIBS) -o $(BUILD)/cretaceous

# Structured policy encoder (cretaceous_encoder.cu): fp32 checks with finite-difference gradients, a bf16 build,
# and the NumPy/PyTorch reference (tools/encoder_reference.py; skipped when python has no numpy).
NVCC ?= /usr/local/cuda/bin/nvcc
CUDA_ARCH ?= native
PYTHON ?= python3
ENC_FLAGS := -O2 -arch=$(CUDA_ARCH) -std=c++17 -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(RAYLIB)/include -I$(BOX3D)/include -I. \
	-Xcompiler=-fopenmp,-Wno-narrowing --diag-suppress 2361,111,128,550,177
ENC_LIBS := $(RAYLIB)/lib/libraylib.a -lGL -L/usr/local/cuda/lib64 -lcudart -lcublas -lcusolver -lcurand -lnccl -lnvidia-ml -lgomp -lm -ldl -lpthread
ENC_DEPS := test_encoder.cu cretaceous_encoder.cu sim.h $(ROOT)/src/ocean.cu $(ROOT)/src/algo.cu $(ROOT)/src/pufferl.cu
$(BUILD)/test_encoder: $(ENC_DEPS) | $(BUILD)
	$(NVCC) $(ENC_FLAGS) test_encoder.cu $(ENC_LIBS) -o $@
$(BUILD)/test_encoder_bf16: $(ENC_DEPS) | $(BUILD)
	$(NVCC) $(ENC_FLAGS) -DCRET_TEST_BF16 test_encoder.cu $(ENC_LIBS) -o $@
encoder-test: $(BUILD)/test_encoder $(BUILD)/test_encoder_bf16
	cd $(ROOT) && build/cretaceous/test_encoder && build/cretaceous/test_encoder_bf16
	cd $(ROOT) && if $(PYTHON) -c "import numpy" 2>/dev/null; then \
		$(PYTHON) ocean/cretaceous/tools/encoder_reference.py build/cretaceous/encoder_test_fp32.bin && \
		$(PYTHON) ocean/cretaceous/tools/encoder_reference.py build/cretaceous/encoder_test_bf16.bin; \
	else echo "skip reference: $(PYTHON) has no numpy (PYTHON=../../../venv/bin/python)"; fi
