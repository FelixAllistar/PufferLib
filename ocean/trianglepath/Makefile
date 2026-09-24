ROOT := ../..
NVCC ?= /usr/local/cuda/bin/nvcc

.PHONY: test cuda-test
test:
	cd $(ROOT) && uv run --no-project --with pytest python -m pytest -q \
		ocean/trianglepath/tests/test_trianglepath.py

build:
	mkdir -p build

cuda-test: build
	$(NVCC) -O2 -arch=native -std=c++17 -I$(ROOT)/src \
		-I$(ROOT)/raylib-5.5_linux_amd64/include tests/test_cuda.cu -o build/test_cuda
	build/test_cuda
