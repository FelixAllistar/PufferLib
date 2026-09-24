ROOT := ../..
CC ?= clang
NVCC ?= nvcc
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64
CPPFLAGS := -I. -I$(ROOT)/src -I$(RAYLIB)/include
CFLAGS ?= -O2 -std=c17 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter
CUDAFLAGS ?= -O3 --use_fast_math -std=c++17 -arch=native

.PHONY: verify test sanitize cuda-test bench-cpu bench-cuda clean

verify: test sanitize cuda-test

build:
	mkdir -p build

viewer: build
	$(CC) $(CPPFLAGS) -O2 -std=gnu11 puffer_survivors.c \
		$(RAYLIB)/lib/libraylib.a -lm -lGL -ldl -lpthread -o build/viewer

viewer-test: build
	$(CC) $(CPPFLAGS) -O2 -std=gnu11 tests/test_viewer.c \
		$(RAYLIB)/lib/libraylib.a -lm -lGL -ldl -lpthread -o build/test_viewer
	cd $(ROOT) && ./ocean/puffer_survivors/build/test_viewer $(CHECKPOINT)

test: tests/test_cpu
	cd $(ROOT) && ./ocean/puffer_survivors/tests/test_cpu

tests/test_cpu: tests/test_cpu.c $(wildcard tests/*.h ps_*.h) puffer_survivors.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DPS_HEADLESS_BINDING $< -lm -o $@

sanitize: tests/test_cpu_san
	cd $(ROOT) && ASAN_OPTIONS=detect_leaks=1 ./ocean/puffer_survivors/tests/test_cpu_san

tests/test_cpu_san: tests/test_cpu.c $(wildcard tests/*.h ps_*.h) puffer_survivors.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -DPS_HEADLESS_BINDING \
		-fsanitize=address,undefined -fno-omit-frame-pointer $< -lm -o $@

cuda-test: tests/test_cuda
	cd $(ROOT) && ./ocean/puffer_survivors/tests/test_cuda

tests/test_cuda: tests/test_cuda.cu $(wildcard tests/*.h ps_*.h) puffer_survivors.h \
		puffer_survivors.cu cuda/ps_cuda_sim.cu
	$(NVCC) $(CPPFLAGS) $(CUDAFLAGS) $< -o $@

bench-cpu: tests/bench_cpu

bench-cuda: tests/bench_cuda

tests/bench_cuda: tests/bench_cuda.cu $(wildcard ps_*.h) puffer_survivors.h \
		puffer_survivors.cu cuda/ps_cuda_sim.cu
	$(NVCC) $(CPPFLAGS) $(CUDAFLAGS) $< -o $@

tests/bench_cpu: tests/bench_cpu.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) $(CPPFLAGS) -O3 -march=native -std=c17 -DNDEBUG \
		-DPS_HEADLESS_BINDING $< -lm -o $@

clean:
	rm -f puffer_survivors tests/test_cpu tests/test_cpu_san tests/test_cuda tests/bench_cpu tests/bench_cuda
