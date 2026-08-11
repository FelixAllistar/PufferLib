ROOT := ../..
BUILD := build

.PHONY: all lib test cuda-adapter parity clean

all: lib

lib:
	mkdir -p $(BUILD)

cuda-adapter: | $(BUILD)
	$${CUDA_HOME:-$${CUDA_PATH:-/usr/local/cuda}}/bin/nvcc \
		-O2 --threads 0 -arch=$${NVCC_ARCH:-native} -std=c++17 \
		-I$(ROOT) -I$(ROOT)/src -I$(ROOT)/vendor \
		-I$(ROOT)/raylib-5.5_linux_amd64/include \
		tests/test_cuda.cu \
		$(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a \
		-lGL -lm -lpthread -ldl -lrt -o $(BUILD)/test_cuda
	$(BUILD)/test_cuda

test: cuda-adapter

clean:
	rm -rf $(BUILD)
