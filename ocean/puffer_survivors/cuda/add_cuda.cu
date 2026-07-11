#include <iostream>
#include <math.h>

__global__
void add(int n, float *x, float *y)
{
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  int stride = blockDim.x * gridDim.x;
  for (int i = index; i < n; i += stride)
    y[i] = x[i] + y[i];
}

#define CUDA_CHECK(val) { \
    cudaError_t err = val; \
    if (err != cudaSuccess) { \
        std::cerr << "CUDA Error at line " << __LINE__ << ": " << cudaGetErrorString(err) << std::endl; \
        exit(1); \
    } \
}

int main(void) {
    int N = 1<<20;
    float *x, *y;

    CUDA_CHECK(cudaMallocManaged(&x, N*sizeof(float)));
    CUDA_CHECK(cudaMallocManaged(&y, N*sizeof(float)));

    for (int i = 0; i < N; i++) {
        x[i] = 1.0f;
        y[i] = 2.0f;
    }
    cudaMemPrefetchAsync(x, N*sizeof(float), 0, 0);
    cudaMemPrefetchAsync(y, N*sizeof(float), 0, 0);
    int blockSize = 256;
    int numBlocks = (N + blockSize - 1) / blockSize;
    add<<<numBlocks, blockSize>>>(N, x, y);
    
    // Check if the kernel launch itself had a syntax/parameter error
    CUDA_CHECK(cudaGetLastError()); 
    
    // Check if the kernel execution failed on the GPU
    CUDA_CHECK(cudaDeviceSynchronize());

    float maxError = 0.0f;
    for (int i = 0; i < N; i++) {
        maxError = fmax(maxError, fabs(y[i]-3.0f));
    }
    std::cout << "Max error: " << maxError << std::endl;

    cudaFree(x);
    cudaFree(y);
    return 0;
}