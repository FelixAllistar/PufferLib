# Simulator checks are dependency-free. Adapter checks use the repository's
# PufferLib/Raylib headers. Native trainer builds use the root build.sh.

CC ?= clang
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-function
FAST_CFLAGS ?= -O3 -DNDEBUG -std=c11 -Wall -Wextra -Wno-unused-function -march=native
ROOT := ../..
BUILD ?= build
ADAPTER_INCLUDES := -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include

.PHONY: all test sanitize bench clean adapter

all: test

viewer: | $(BUILD)
	$(CC) $(ADAPTER_INCLUDES) -O2 -std=gnu11 bomberman.c \
		$(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lm -lGL -ldl -lpthread -o $(BUILD)/viewer

viewer-test: | $(BUILD)
	$(CC) $(ADAPTER_INCLUDES) -O2 -std=gnu11 test_viewer.c \
		$(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lm -lGL -ldl -lpthread -o $(BUILD)/test_viewer
	cd $(ROOT) && ./ocean/bomberman/$(BUILD)/test_viewer $(CHECKPOINT)

test: test_sim adapter
	./test_sim

$(BUILD):
	mkdir -p $(BUILD)

adapter: | $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-parameter $(ADAPTER_INCLUDES) test_adapter.c -lm -o $(BUILD)/test_adapter
	$(BUILD)/test_adapter $(ROOT)/config/bomberman.ini

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
