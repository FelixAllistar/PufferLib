ROOT := ../..
CC ?= clang
BUILD := build
LIB := $(BUILD)/libkaggriculture.so
CFLAGS ?= -O3 -std=c17 -Wall -Wextra -Werror -fPIC

.PHONY: all lib parity top-bot-parity cuda-core cuda-adapter bc bench hotpath adapter eval metagame native-test test clean

all: lib

lib: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(LIB): kaggriculture_core.c kaggriculture_core.h | $(BUILD)
	$(CC) $(CFLAGS) -shared kaggriculture_core.c -lm -o $@

parity: $(LIB)
	$(ROOT)/../.venv/bin/python parity.py --lib $(LIB)

top-bot-parity: | $(BUILD)
	$(CC) $(CFLAGS) -shared \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include \
		tests/top_bot_oracle.c $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lGL -lm -lpthread -o $(BUILD)/libtop_bot_oracle.so
	$(ROOT)/../.venv/bin/python submission/test_top_bot_parity.py

cuda-core: | $(BUILD)
	$${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc \
		-O2 --threads 0 -arch=$${NVCC_ARCH:-native} -std=c++17 \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor \
		tests/test_cuda_core.cu -lm -o $(BUILD)/test_cuda_core
	$(BUILD)/test_cuda_core

cuda-adapter: | $(BUILD)
	$${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc \
		-O2 --threads 0 -arch=$${NVCC_ARCH:-native} -std=c++17 \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor \
		-I$(ROOT)/raylib-5.5_linux_amd64/include \
		tests/test_cuda_adapter.cu \
		$(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lGL -lm -lpthread -o $(BUILD)/test_cuda_adapter
	$(BUILD)/test_cuda_adapter

$(BUILD)/kag_bc: kag_bc.cu kaggriculture.h kaggriculture_core.h \
		kaggriculture_core.c kaggriculture_bots.h \
		kaggriculture_public_bots.h kaggriculture_tape_data.h \
		kaggriculture_public_extra_tapes.h \
		$(ROOT)/src/pufferenv.h $(ROOT)/src/ini.h \
		$(ROOT)/src/pufferl_preamble.h $(ROOT)/src/algo.cu | $(BUILD)
	$${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc \
		-O2 --threads 0 -arch=$${NVCC_ARCH:-native} -std=c++17 \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor \
		-I$(ROOT)/raylib-5.5_linux_amd64/include \
		kag_bc.cu $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lGL -lm -lpthread -lcudart -lcublas -lcurand -o $(BUILD)/kag_bc

bc: $(BUILD)/kag_bc
	$(BUILD)/kag_bc bc.steps=26 bc.epochs=200

adapter: | $(BUILD)
	$(CC) -O3 -std=c17 -Wall -Wextra -Werror \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include \
		tests/test_adapter.c $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lGL -lm -lpthread -o $(BUILD)/test_adapter
	$(BUILD)/test_adapter

metagame: | $(BUILD)
	$(CC) -O3 -std=c17 -Wall -Wextra -Werror metagame.c -lm \
		-o $(BUILD)/kaggriculture_metagame

native-test: adapter

test: native-test parity

clean:
	rm -rf $(BUILD)
