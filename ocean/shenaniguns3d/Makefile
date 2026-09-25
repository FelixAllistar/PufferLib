ROOT := ../..
BUILD := $(ROOT)/build/shenaniguns3d
BOX3D ?= $(ROOT)/../box3d
CC := clang
CFLAGS ?= -O2 -g -std=gnu11
CPPFLAGS := -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include -I$(BOX3D)/include -I.
LDLIBS := $(BOX3D)/build/src/libbox3d.a $(ROOT)/raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread

.PHONY: test sanitize encoder-test viewer gpu-test
gpu-test:
	@mkdir -p $(BUILD)
	$(CC) -O2 -std=gnu11 $(CPPFLAGS) -c character.c -o $(BUILD)/character.o
	$${CUDA_HOME:-/usr/local/cuda}/bin/nvcc -O2 --threads 2 -arch=$${NVCC_ARCH:-native} \
		-std=c++17 -I$(ROOT) $(CPPFLAGS) -Xcompiler=-Wno-narrowing \
		--diag-suppress 2361 tests/test_gpu.cu $(BUILD)/character.o $(LDLIBS) \
		-o $(BUILD)/test_gpu
	$(BUILD)/test_gpu
viewer:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) shenaniguns3d.c character.c $(LDLIBS) -o $(BUILD)/viewer
encoder-test:
	@mkdir -p $(BUILD)
	$(CC) -O2 -std=gnu11 $(CPPFLAGS) -c character.c -o $(BUILD)/character.o
	$${CUDA_HOME:-/usr/local/cuda}/bin/nvcc -O2 --threads 2 -arch=$${NVCC_ARCH:-native} \
		-std=c++17 -I$(ROOT) -I$(ROOT)/vendor $(CPPFLAGS) \
		-Xcompiler=-fopenmp,-Wno-narrowing --diag-suppress 2361 \
		tests/test_encoder.cu $(BUILD)/character.o $(LDLIBS) \
		-lgomp -lcudart -lcublas -lcusolver -lcurand -lnccl -lnvidia-ml \
		-o $(BUILD)/test_encoder
	$(BUILD)/test_encoder
test:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/test_cpu.c character.c $(LDLIBS) -o $(BUILD)/test_cpu
	$(BUILD)/test_cpu

sanitize:
	$(MAKE) test BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer'
