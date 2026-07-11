CC ?= gcc
CXX ?= clang++
NVCC ?= nvcc
CFLAGS ?= -O2 -std=c17 -Wall -Wextra -Werror
CUDAFLAGS ?= -O3 --use_fast_math -std=c++17 -I.

.PHONY: verify test sanitize syntax cuda-syntax bench-cpu bench-cuda clean

verify: test sanitize syntax cuda-syntax

test: tests/test_cpu
	./tests/test_cpu

tests/test_cpu: tests/test_cpu.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) $(CFLAGS) -I. $< -lm -o $@

sanitize: tests/test_cpu_san
	ASAN_OPTIONS=detect_leaks=1 ./tests/test_cpu_san

tests/test_cpu_san: tests/test_cpu.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) -O1 -g -std=c17 -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I. $< -lm -o $@

syntax:
	$(CC) $(CFLAGS) -Itests -I. -c tests/test_render_syntax.c -o tests/test_render_syntax.o
	$(CC) $(CFLAGS) -DPS_HEADLESS_BINDING -Itests -I. -c binding.c -o tests/binding_cpu.o
	$(CC) $(CFLAGS) -DPS_HEADLESS_BINDING -DPS_ENABLE_CUDA_VEC -Itests -I. \
		-c binding.c -o tests/binding_cuda.o

cuda-syntax:
	python3 tests/verify_cuda_syntax.py

bench-cpu: tests/bench_cpu

tests/bench_cpu: tests/bench_cpu.c $(wildcard ps_*.h) puffer_survivors.h
	$(CC) -O3 -march=native -std=c17 -DNDEBUG -I. $< -lm -o $@

bench-cuda: tests/bench_cuda

tests/bench_cuda: tests/bench_cuda.cu cuda/ps_cuda_sim.cu cuda/ps_cuda_sim.cuh
	$(NVCC) $(CUDAFLAGS) tests/bench_cuda.cu cuda/ps_cuda_sim.cu -o $@

clean:
	rm -f tests/test_cpu tests/test_cpu_san tests/bench_cpu tests/bench_cuda \
		tests/test_render_syntax.o tests/binding_cpu.o tests/binding_cuda.o
