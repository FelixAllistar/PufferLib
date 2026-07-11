#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef __host__
#define __host__
#endif
#ifndef __device__
#define __device__
#endif
#ifndef __global__
#define __global__
#endif
#ifndef __constant__
#define __constant__
#endif
#ifndef __forceinline__
#define __forceinline__ inline
#endif

typedef int cudaError_t;
typedef void* cudaStream_t;
typedef void* cudaEvent_t;
static const cudaError_t cudaSuccess = 0;

typedef enum cudaMemcpyKind {
    cudaMemcpyHostToHost = 0,
    cudaMemcpyHostToDevice = 1,
    cudaMemcpyDeviceToHost = 2,
    cudaMemcpyDeviceToDevice = 3,
    cudaMemcpyDefault = 4
} cudaMemcpyKind;

struct dim3 {
    unsigned int x, y, z;
    constexpr dim3(unsigned int vx = 1, unsigned int vy = 1, unsigned int vz = 1) : x(vx), y(vy), z(vz) {}
};
struct uint3 { unsigned int x, y, z; };
static const uint3 blockIdx = {0,0,0};
static const uint3 blockDim = {1,1,1};
static const uint3 threadIdx = {0,0,0};
static const uint3 gridDim = {1,1,1};

inline cudaError_t cudaMalloc(void**, size_t) { return 0; }
inline cudaError_t cudaFree(void*) { return 0; }
inline cudaError_t cudaMemset(void*, int, size_t) { return 0; }
inline cudaError_t cudaMemsetAsync(void*, int, size_t, cudaStream_t) { return 0; }
inline cudaError_t cudaMemcpy(void*, const void*, size_t, cudaMemcpyKind) { return 0; }
inline cudaError_t cudaMemcpyAsync(void*, const void*, size_t, cudaMemcpyKind, cudaStream_t) { return 0; }
inline cudaError_t cudaDeviceSynchronize(void) { return 0; }
inline cudaError_t cudaStreamSynchronize(cudaStream_t) { return 0; }
inline cudaError_t cudaGetLastError(void) { return 0; }
inline const char* cudaGetErrorString(cudaError_t) { return "stub"; }
inline cudaError_t cudaEventCreate(cudaEvent_t*) { return 0; }
inline cudaError_t cudaEventDestroy(cudaEvent_t) { return 0; }
inline cudaError_t cudaEventRecord(cudaEvent_t, cudaStream_t = nullptr) { return 0; }
inline cudaError_t cudaEventSynchronize(cudaEvent_t) { return 0; }
inline cudaError_t cudaEventElapsedTime(float*, cudaEvent_t, cudaEvent_t) { return 0; }
inline float atomicAdd(float* p, float v) { float old = *p; *p += v; return old; }
