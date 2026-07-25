ROOT := ../..
CC ?= clang
NVCC ?= nvcc
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64
CPPFLAGS := -I. -I$(ROOT)/src -I$(RAYLIB)/include
CFLAGS ?= -O2 -std=c17 -Wall -Wextra -Werror
CUDAFLAGS ?= -O3 --use_fast_math -std=c++17
# pufferenv.h uses AVX2 helpers; match build.sh standalone flags.
PLAY_CFLAGS := $(CFLAGS) -mavx2 -mfma -DPLATFORM_DESKTOP
PLAY_LIBS := $(RAYLIB)/lib/libraylib.a -lm -lpthread -ldl -lrt -lX11 -lGL

.PHONY: play verify test sanitize cuda-test bench-cpu clean

play: puffer_survivors
	cd $(ROOT) && ./ocean/puffer_survivors/puffer_survivors

puffer_survivors: puffer_survivors.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) $(CPPFLAGS) $(PLAY_CFLAGS) $< -o $@ $(PLAY_LIBS)

verify: test sanitize cuda-test

test: tests/test_cpu
	./tests/test_cpu

tests/test_cpu: tests/test_cpu.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -DPS_HEADLESS_BINDING $< -lm -o $@

sanitize: tests/test_cpu_san
	ASAN_OPTIONS=detect_leaks=1 ./tests/test_cpu_san

tests/test_cpu_san: tests/test_cpu.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -DPS_HEADLESS_BINDING \
		-fsanitize=address,undefined -fno-omit-frame-pointer $< -lm -o $@

cuda-test: tests/test_cuda
	cd $(ROOT) && ./ocean/puffer_survivors/tests/test_cuda

tests/test_cuda: tests/test_cuda.cu $(wildcard ps_*.h) puffer_survivors.h \
		puffer_survivors.cu cuda/ps_cuda_sim.cu
	$(NVCC) $(CPPFLAGS) $(CUDAFLAGS) -DPUFFER_GPU_ENV $< -o $@

bench-cpu: tests/bench_cpu

tests/bench_cpu: tests/bench_cpu.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) $(CPPFLAGS) -O3 -march=native -std=c17 -DNDEBUG \
		-DPS_HEADLESS_BINDING $< -lm -o $@

clean:
	rm -f puffer_survivors tests/test_cpu tests/test_cpu_san tests/test_cuda tests/bench_cpu
