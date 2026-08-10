ROOT := ../..
BUILD := build

.PHONY: cuda-adapter test clean

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

test: cuda-adapter

clean:
	rm -rf $(BUILD)
