# Standalone Bomberman simulator checks. These do not require PufferLib, Raylib,
# CUDA, or a trained policy.

CC ?= clang
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function
FAST_CFLAGS ?= -O3 -DNDEBUG -std=c11 -Wall -Wextra -Wno-unused-function -march=native

.PHONY: all test sanitize bench clean

all: test

test: test_sim
	./test_sim

test_sim: test_sim.c bm_sim.h bm_constants.h
	$(CC) $(CFLAGS) -DBM_HEADLESS -I. test_sim.c -o $@ -lm


sanitize: clean
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-DBM_HEADLESS -I. test_sim.c -o test_sim_san -lm
	ASAN_OPTIONS=detect_leaks=1 ./test_sim_san

bench: bench_sim
	./bench_sim

bench_sim: bench_sim.c bm_sim.h bm_constants.h
	$(CC) $(FAST_CFLAGS) -I. bench_sim.c -o $@ -lm

clean:
	rm -f test_sim test_sim_san test_sim_gcc bench_sim
