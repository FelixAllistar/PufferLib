// To investigate: 32f compute? Need to check bf16
#ifdef PRECISION_FLOAT
typedef float precision_t;
constexpr bool USE_BF16 = false;
constexpr cudaDataType_t CUBLAS_PRECISION = CUDA_R_32F;
constexpr cublasComputeType_t CUBLAS_COMPUTE_PRECISION = CUBLAS_COMPUTE_32F;
#define NCCL_PRECISION ncclFloat
#define to_float(x) (x)
#define from_float(x) (x)
#else
typedef __nv_bfloat16 precision_t;
constexpr bool USE_BF16 = true;
constexpr cudaDataType_t CUBLAS_PRECISION = CUDA_R_16BF;
constexpr cublasComputeType_t CUBLAS_COMPUTE_PRECISION = CUBLAS_COMPUTE_32F;
#define NCCL_PRECISION ncclBfloat16
#define to_float(x) __bfloat162float(x)
#define from_float(x) __float2bfloat16(x)
#endif

#define PUF_MAX_DIMS 8
#define BLOCK_SIZE 256
int grid_size(int N) {
    return (N + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

// Compile-time env: build.sh passes -DENV_HEADER="ocean/<env>/<env>.h".
// GPU mode: -DPUFFER_GPU_ENV and -DGPU_ENV_HEADER=...cu (exclusive, not dual runtime).
#ifdef ENV_HEADER
#include ENV_HEADER
#endif
#ifdef PUFFER_GPU_ENV
#ifndef GPU_ENV_HEADER
#error "PUFFER_GPU_ENV requires GPU_ENV_HEADER (build with --gpu)"
#endif
#include GPU_ENV_HEADER
#endif

typedef struct {
    float* data;
    int64_t shape[PUF_MAX_DIMS];
} FloatTensor;

typedef struct {
    unsigned char* data;
    int64_t shape[PUF_MAX_DIMS];
} ByteTensor;

typedef struct {
    long* data;
    int64_t shape[PUF_MAX_DIMS];
} LongTensor;

typedef struct {
    int* data;
    int64_t shape[PUF_MAX_DIMS];
} IntTensor;

typedef struct {
    precision_t* data;
    int64_t shape[PUF_MAX_DIMS];
} PrecisionTensor;

__host__ __device__ int ndim(int64_t* shape) {
    int n = 0;
    while (n < PUF_MAX_DIMS && shape[n] != 0) {
        n++;
    }
    return n;
}

__host__ __device__ int64_t numel(int64_t* shape) {
    int64_t n = 1;
    for (int i = 0; i < PUF_MAX_DIMS && shape[i] != 0; i++) {
        n *= shape[i];
    }
    return n;
}

int64_t batch_size(int64_t* shape) {
    int n = ndim(shape);
    int64_t b = 1;
    for (int i = 0; i < n - 2; i++) {
        b *= shape[i];
    }
    return b;
}

void puf_squeeze_shape(int64_t* shape, int dim) {
    int n = ndim(shape);
    shape[dim] *= shape[dim + 1];
    for (int i = dim + 1; i < n - 1; i++) {
        shape[i] = shape[i + 1];
    }
    shape[n - 1] = 0;
}

PrecisionTensor* puf_squeeze(PrecisionTensor* t, int dim) {
    puf_squeeze_shape(t->shape, dim);
    return t;
}

FloatTensor* puf_squeeze(FloatTensor* t, int dim) {
    puf_squeeze_shape(t->shape, dim);
    return t;
}

PrecisionTensor* puf_unsqueeze(PrecisionTensor* t, int dim, int64_t d0, int64_t d1) {
    int n = ndim(t->shape);
    assert(n + 1 <= PUF_MAX_DIMS);
    assert(t->shape[dim] == d0 * d1);
    for (int i = n; i > dim; i--) {
        t->shape[i] = t->shape[i - 1];
    }
    t->shape[dim] = d0;
    t->shape[dim + 1] = d1;
    return t;
}

void puf_copy(PrecisionTensor* dst, PrecisionTensor* src, cudaStream_t stream) {
    assert(numel(dst->shape) == numel(src->shape) && "puf_copy: size mismatch");
    cudaMemcpyAsync(dst->data, src->data,
        numel(dst->shape) * sizeof(precision_t),
        cudaMemcpyDeviceToDevice, stream);
}

__global__ void cast(precision_t* dst,
        float* src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        dst[idx] = from_float(src[idx]);
    }
}

__global__ void ema_weights(float* magnet, const float* weights,
        float tau, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) magnet[idx] += tau * (weights[idx] - magnet[idx]);
}

#ifndef PRECISION_FLOAT
// Identity overload so obs→rollout cast arm typechecks when obs_t is bf16.
__global__ void cast(precision_t* dst,
        precision_t* src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        dst[idx] = src[idx];
    }
}

__global__ void cast(float* dst,
        precision_t* src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        dst[idx] = to_float(src[idx]);
    }
}
#endif

__global__ void cast(precision_t* dst,
        unsigned char* src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
#ifdef PUFFERLIB_OBS_U8_NORMALIZED
        dst[idx] = from_float((float)src[idx] * (1.0f / 255.0f));
#else
        dst[idx] = from_float((float)src[idx]);
#endif
    }
}

__global__ void cast(unsigned char* dst,
        precision_t* src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        dst[idx] = to_float(src[idx]);
    }
}

// Fused rew+term cast (two tiny launches were launch-bound).
__global__ void cast_rew_term(
        precision_t* __restrict__ rew_dst, const float* __restrict__ rew_src,
        precision_t* __restrict__ term_dst, const float* __restrict__ term_src,
        int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        rew_dst[idx] = from_float(rew_src[idx]);
        term_dst[idx] = from_float(term_src[idx]);
    }
}

struct AllocEntry {
    void** data_ptr;    // address of the tensor's data field
    int64_t* shape;     // pointer to the tensor's shape array
    int elem_size;      // sizeof element type
};

struct Allocator {
    AllocEntry* regs = nullptr;
    int num_regs = 0;
    void* mem = nullptr;
    long total_elems = 0;
    long total_bytes = 0;
};

void alloc_register_impl(Allocator* alloc, void** data_ptr, int64_t* shape, int elem_size) {
    alloc->regs = (AllocEntry*)realloc(alloc->regs, (alloc->num_regs + 1) * sizeof(AllocEntry));
    alloc->regs[alloc->num_regs++] = {data_ptr, shape, elem_size};
    int64_t n = numel(shape);
    alloc->total_elems += n;
    alloc->total_bytes = (alloc->total_bytes + 15) & ~15;
    alloc->total_bytes += n * elem_size;
}
void alloc_register(Allocator* a, PrecisionTensor* t) {
    alloc_register_impl(a, (void**)&t->data, t->shape, sizeof(precision_t));
}
void alloc_register(Allocator* a, FloatTensor* t) {
    alloc_register_impl(a, (void**)&t->data, t->shape, sizeof(float));
}
void alloc_register(Allocator* a, LongTensor* t) {
    alloc_register_impl(a, (void**)&t->data, t->shape, sizeof(long));
}
void alloc_register(Allocator* a, IntTensor* t) {
    alloc_register_impl(a, (void**)&t->data, t->shape, sizeof(int));
}
void alloc_register(Allocator* a, ByteTensor* t) {
    alloc_register_impl(a, (void**)&t->data, t->shape, sizeof(unsigned char));
}

cudaError_t alloc_create(Allocator* alloc) {
    if (alloc->total_bytes == 0) {
        return cudaSuccess;
    }
    cudaError_t err = cudaMalloc(&alloc->mem, alloc->total_bytes);
    if (err != cudaSuccess) {
        return err;
    }
    cudaMemset(alloc->mem, 0, alloc->total_bytes);
    long offset = 0;
    for (int i = 0; i < alloc->num_regs; i++) {
        offset = (offset + 15) & ~15;
        *alloc->regs[i].data_ptr = (char*)alloc->mem + offset;
        offset += numel(alloc->regs[i].shape) * alloc->regs[i].elem_size;
    }
    return cudaSuccess;
}

// Process-lifetime allocs: no free on exit (OS reclaims). OOM → assert.
static void* xcalloc(size_t n) {
    void* p = calloc(1, n);
    assert(p && "oom");
    return p;
}
static void* xcuda(size_t n) {
    void* p = nullptr;
    assert(cudaMalloc(&p, n) == cudaSuccess && "cudaMalloc failed");
    cudaMemset(p, 0, n);
    return p;
}
static void* xpin(size_t n) {
    void* p = nullptr;
    assert(cudaHostAlloc(&p, n, cudaHostAllocPortable) == cudaSuccess
        && "cudaHostAlloc failed");
    return p;
}

// Policy / optim / GEMM / encoders / weight init.
// Needs tensors, Allocator, batch_size/ndim, grid_size, cast, CUBLAS_PRECISION*.
