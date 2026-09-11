ROOT := ../..
BUILD ?= build
CC := clang
BOX3D ?= $(ROOT)/../box3d
INCLUDES := -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include -I$(BOX3D)/include
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
LDLIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
NVCC := $${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc

.PHONY: test sanitize cuda-test native-cpu-test viewer-test
$(BUILD):
	mkdir -p $(BUILD)
test: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_sim.c $(LDLIBS) -o $(BUILD)/test_sim
	cd $(ROOT) && $(abspath $(BUILD)/test_sim)
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS="-O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer"
viewer-test: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_viewer.c $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		$(LDLIBS) -lGL -ldl -o $(BUILD)/test_viewer
	cd $(ROOT) && $(abspath $(BUILD)/test_viewer) $(SHOT)
cuda-test: | $(BUILD)
	$(NVCC) -O2 -std=c++17 \
		-arch=$${NVCC_ARCH:-native} $(INCLUDES) tests/test_cuda.cu -o $(BUILD)/test_cuda
	cd $(ROOT) && $(abspath $(BUILD)/test_cuda)
native-cpu-test: | $(BUILD)
	$(NVCC) -x cu -O1 -std=c++17 -arch=$${NVCC_ARCH:-native} $(INCLUDES) \
		-c tests/test_sim.c -o $(BUILD)/test_native_cpu.o
	$(NVCC) $(BUILD)/test_native_cpu.o $(LDLIBS) -o $(BUILD)/test_native_cpu
	cd $(ROOT) && $(abspath $(BUILD)/test_native_cpu)
