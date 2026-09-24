ROOT := ../..
BUILD ?= build
CC := clang
BOX3D ?= $(ROOT)/../box3d
INCLUDES := -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include -I$(BOX3D)/include
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
LDLIBS := $(BOX3D)/build/src/libbox3d.a -lm -lpthread
NVCC := $${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc

.PHONY: test sanitize native-cpu-test viewer viewer-test viewer-cpp-test frontier-test reach-test
$(BUILD):
	mkdir -p $(BUILD)
viewer: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) arpg.c $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		$(LDLIBS) -lGL -ldl -o $(BUILD)/viewer
test: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_sim.c $(LDLIBS) -o $(BUILD)/test_sim
	cd $(ROOT) && $(abspath $(BUILD)/test_sim)
sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS="-O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer"
	$(MAKE) frontier-test BUILD=$(BUILD)/sanitize CFLAGS="-O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer"
	$(MAKE) reach-test BUILD=$(BUILD)/sanitize CFLAGS="-O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer"
reach-test: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_reach.c $(LDLIBS) -o $(BUILD)/test_reach
	cd $(ROOT) && $(abspath $(BUILD)/test_reach)
frontier-test: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_frontier.c $(LDLIBS) -o $(BUILD)/test_frontier
	cd $(ROOT) && $(abspath $(BUILD)/test_frontier)
viewer-test: | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_viewer.c $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		$(LDLIBS) -lGL -ldl -o $(BUILD)/test_viewer
	cd $(ROOT) && $(abspath $(BUILD)/test_viewer) $(SHOT)
viewer-cpp-test: | $(BUILD)
	$(NVCC) -O0 -std=c++17 -arch=$${NVCC_ARCH:-native} $(INCLUDES) \
		-c tests/test_render_cpp.cu -o $(BUILD)/test_render_cpp.o
native-cpu-test: | $(BUILD)
	$(NVCC) -x cu -O1 -std=c++17 -arch=$${NVCC_ARCH:-native} $(INCLUDES) \
		-c tests/test_sim.c -o $(BUILD)/test_native_cpu.o
	$(NVCC) $(BUILD)/test_native_cpu.o $(LDLIBS) -o $(BUILD)/test_native_cpu
	cd $(ROOT) && $(abspath $(BUILD)/test_native_cpu)
