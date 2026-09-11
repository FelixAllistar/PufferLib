# Simulator checks are dependency-free. Adapter checks use the repository's
# PufferLib/Raylib headers; only cuda-adapter requires a CUDA toolkit and GPU.

CC ?= clang
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function
FAST_CFLAGS ?= -O3 -DNDEBUG -std=c11 -Wall -Wextra -Wno-unused-function -march=native
ROOT := ../..
BUILD ?= build
ADAPTER_INCLUDES := -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include

.PHONY: all test sanitize bench clean adapter cuda-adapter

all: test

test: test_sim adapter
	./test_sim

$(BUILD):
	mkdir -p $(BUILD)

adapter: | $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-parameter $(ADAPTER_INCLUDES) test_adapter.c -lm -o $(BUILD)/test_adapter
	$(BUILD)/test_adapter

cuda-adapter: | $(BUILD)
	$${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc \
		-O2 -arch=$${NVCC_ARCH:-native} -std=c++17 $(ADAPTER_INCLUDES) \
		test_cuda.cu -o $(BUILD)/test_cuda
	cd $(ROOT) && $(abspath $(BUILD)/test_cuda)

test_sim: test_sim.c bm_sim.h bm_constants.h
	$(CC) $(CFLAGS) -DBM_HEADLESS -I. test_sim.c -o $@ -lm


sanitize: clean
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-DBM_HEADLESS -I. test_sim.c -o test_sim_san -lm
	ASAN_OPTIONS=detect_leaks=1 ./test_sim_san
	$(MAKE) adapter BUILD=$(BUILD)/sanitize CFLAGS="-O1 -g -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer"

bench: bench_sim
	./bench_sim

bench_sim: bench_sim.c bm_sim.h bm_constants.h
	$(CC) $(FAST_CFLAGS) -I. bench_sim.c -o $@ -lm

clean:
	rm -f test_sim test_sim_san test_sim_gcc bench_sim
