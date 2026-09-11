ROOT := ../..
BUILD := build
CC := clang
CPPFLAGS := -D_POSIX_C_SOURCE=200809L -I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
LDLIBS := $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -lpthread -ldl -lrt

.PHONY: cuda-adapter test sanitize clean

$(BUILD):
	mkdir -p $(BUILD)

cuda-adapter: | $(BUILD)
	$${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc \
		-O2 --threads 0 -arch=$${NVCC_ARCH:-native} -std=c++17 \
		-DGS_NUM_CARDS=$${GS_NUM_CARDS:-4} \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor \
		-I$(ROOT)/raylib-5.5_linux_amd64/include \
		tests/test_cuda.cu \
		$(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lGL -lm -lpthread -ldl -lrt -o $(BUILD)/test_cuda_adapter
	$(BUILD)/test_cuda_adapter

$(BUILD)/test_%: tests/test_%.c goofspiel.h goofspiel_core.h goofspiel_obs.h goofspiel_exploit.c goofspiel_exploit.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDLIBS) -o $@

test: $(BUILD)/test_goofspiel $(BUILD)/test_adapter $(BUILD)/test_exploit
	$(BUILD)/test_goofspiel
	$(BUILD)/test_adapter
	$(BUILD)/test_exploit

sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS="-O1 -g -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer"

clean:
	rm -rf $(BUILD)
