// Experimental native Transformer backbone and standalone benchmark core.
//
// Policy adapters supply tokenization, heads and the optimizer separately.
// Dense non-causal self-attention, pre-LN, GELU, learned affine normalization,
// residuals, and a final LN. No tokenizer, positions, action heads or optimizer.
// Attention is the explicit cuBLAS/softmax implementation, NOT FlashAttention.
// FP32 parameter gradients; FP32 or BF16 weights/activations, FP32 reductions.
#pragma once

#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace puf_transformer {

inline void check(cudaError_t result, const char* operation) {
    if (result != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(result));
}
inline void check(cublasStatus_t result, const char* operation) {
    if (result != CUBLAS_STATUS_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": cuBLAS status " + std::to_string(result));
}

template<class T> __host__ __device__ inline float fp32(T v) { return static_cast<float>(v); }
template<> __host__ __device__ inline float fp32(__nv_bfloat16 v) { return __bfloat162float(v); }
template<class T> __host__ __device__ inline T scalar(float v) { return static_cast<T>(v); }
template<> __host__ __device__ inline __nv_bfloat16 scalar(float v) { return __float2bfloat16(v); }
template<class T> constexpr cudaDataType_t dtype() { return CUDA_R_32F; }
template<> constexpr cudaDataType_t dtype<__nv_bfloat16>() { return CUDA_R_16BF; }

template<class T> struct Buffer {
    T* data = nullptr;
    size_t count = 0;
    bool owned = true;
    Buffer() = default;
    explicit Buffer(size_t n) { allocate(n); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept : data(other.data), count(other.count), owned(other.owned) {
        other.data = nullptr; other.count = 0;
    }
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            if (data && owned) cudaFree(data);
            data = other.data; count = other.count; owned = other.owned;
            other.data = nullptr; other.count = 0;
        }
        return *this;
    }
    ~Buffer() { if (data && owned) cudaFree(data); }
    // External policy weights/gradients stay in PuffeRL's contiguous allocators.
    void borrow(T* pointer, size_t n) {
        if (data && owned) throw std::runtime_error("cannot rebind an owned buffer");
        data = pointer; count = n; owned = false;
    }
    void allocate(size_t n) {
        if (data) throw std::runtime_error("Buffer allocated twice");
        count = n;
        owned = true;
        check(cudaMalloc(&data, n * sizeof(T)), "cudaMalloc");
    }
    void upload(const std::vector<float>& values) {
        if (values.size() != count) throw std::runtime_error("upload shape mismatch");
        std::vector<T> converted(count);
        for (size_t i = 0; i < count; ++i) converted[i] = scalar<T>(values[i]);
        check(cudaMemcpy(data, converted.data(), count * sizeof(T), cudaMemcpyHostToDevice), "upload");
        // Pageable H2D cudaMemcpy can return after staging, before device DMA.
        // Callers may immediately consume this buffer on a nonblocking stream.
        check(cudaStreamSynchronize(nullptr), "finish upload");
    }
    std::vector<float> download() const {
        std::vector<T> raw(count);
        check(cudaMemcpy(raw.data(), data, count * sizeof(T), cudaMemcpyDeviceToHost), "download");
        std::vector<float> values(count);
        for (size_t i = 0; i < count; ++i) values[i] = fp32(raw[i]);
        return values;
    }
};

struct Config {
    int batch = 1, tokens = 264, width = 256, layers = 12, heads = 8, ff = 1024;
    void validate() const {
        if (batch < 1 || tokens < 1 || width < 1 || layers < 1 || heads < 1 || ff < 1)
            throw std::runtime_error("all dimensions must be positive");
        if (width % heads) throw std::runtime_error("width must be divisible by heads");
        // Kernels use 32-bit indices. Reject impractical shapes before overflow.
        int64_t rows = int64_t(batch) * tokens;
        if (width > INT32_MAX / 3 ||
            rows > INT32_MAX / std::max(3 * int64_t(width), int64_t(ff)) ||
            rows > INT32_MAX / int64_t(heads) / tokens)
            throw std::runtime_error("shape exceeds the benchmark's 32-bit indexing limit");
    }
    size_t parameter_count() const {
        return size_t(layers) * (4ULL * width * width + 2ULL * width * ff + 9ULL * width + ff)
            + 2ULL * width;
    }
};

struct ParameterSpec { int rows, cols, initialization; };
// Order is also the policy checkpoint order. Vectors have rows=1; policy
// adapters register them as rank-1 so Muon does not orthogonalize LN/biases.
inline std::vector<ParameterSpec> parameter_specs(const Config& c) {
    std::vector<ParameterSpec> result;
    for (int i = 0; i < c.layers; ++i) {
        const ParameterSpec block[] = {{1,c.width,1}, {1,c.width,2},
                {3*c.width,c.width,0}, {1,3*c.width,2},
                {c.width,c.width,0}, {1,c.width,2},
                {1,c.width,1}, {1,c.width,2},
                {c.ff,c.width,0}, {1,c.ff,2},
                {c.width,c.ff,0}, {1,c.width,2}};
        for (auto s : block) result.push_back(s);
    }
    result.push_back({1,c.width,1}); result.push_back({1,c.width,2});
    return result;
}

__device__ inline float warp_sum(float x) {
    for (int offset = 16; offset; offset >>= 1) x += __shfl_down_sync(0xffffffff, x, offset);
    return x;
}
__device__ inline float warp_max(float x) {
    for (int offset = 16; offset; offset >>= 1) x = fmaxf(x, __shfl_down_sync(0xffffffff, x, offset));
    return x;
}
// Butterfly reductions: every lane receives the result.
__device__ inline float warp_all_sum(float x) {
    for (int offset = 16; offset; offset >>= 1) x += __shfl_xor_sync(0xffffffff, x, offset);
    return x;
}
__device__ inline float warp_all_max(float x) {
    for (int offset = 16; offset; offset >>= 1) x = fmaxf(x, __shfl_xor_sync(0xffffffff, x, offset));
    return x;
}
// Row kernels use one warp per row (lanes stride the row), which suits both short
// rows (21-token softmax, 128-wide LN) and long ones without idle threads or
// block-wide barriers. Launch with warp_rows_grid / kWarpRowThreads.
constexpr int kWarpRowThreads = 128;
inline unsigned warp_rows_grid(int rows) { return unsigned((int64_t(rows) * 32 + kWarpRowThreads - 1) / kWarpRowThreads); }
template<bool Maximum = false> __device__ inline float block_reduce(float x) {
    __shared__ float partial[32];
    int lane = threadIdx.x & 31, warp = threadIdx.x >> 5;
    x = Maximum ? warp_max(x) : warp_sum(x);
    if (lane == 0) partial[warp] = x;
    __syncthreads();
    x = threadIdx.x < (blockDim.x / 32) ? partial[threadIdx.x] : (Maximum ? -INFINITY : 0.f);
    if (warp == 0) x = Maximum ? warp_max(x) : warp_sum(x);
    if (threadIdx.x == 0) partial[0] = x;
    __syncthreads();
    float result = partial[0];
    __syncthreads(); // permit two consecutive reductions using this shared array
    return result;
}

template<class T> __global__ void layernorm_forward(
        const T* x, const T* gamma, const T* beta, T* y, float* stats, int d) {
    int row = blockIdx.x;
    float sum = 0.f, squared = 0.f;
    for (int j = threadIdx.x; j < d; j += blockDim.x) {
        float value = fp32(x[size_t(row) * d + j]);
        sum += value; squared += value * value;
    }
    float mean = block_reduce(sum) / d;
    float variance = fmaxf(block_reduce(squared) / d - mean * mean, 0.f);
    float inverse = rsqrtf(variance + 1e-5f);
    if (threadIdx.x == 0) { stats[2 * row] = mean; stats[2 * row + 1] = inverse; }
    for (int j = threadIdx.x; j < d; j += blockDim.x) {
        size_t i = size_t(row) * d + j;
        y[i] = scalar<T>((fp32(x[i]) - mean) * inverse * fp32(gamma[j]) + fp32(beta[j]));
    }
}

template<class T> __global__ void layernorm_forward_warp(
        const T* x, const T* gamma, const T* beta, T* y, float* stats, int rows, int d) {
    int row = int((size_t(blockIdx.x) * blockDim.x + threadIdx.x) >> 5), lane = threadIdx.x & 31;
    if (row >= rows) return;
    size_t base = size_t(row) * d;
    float sum = 0.f, squared = 0.f;
    for (int j = lane; j < d; j += 32) { float value = fp32(x[base + j]); sum += value; squared += value * value; }
    float mean = warp_all_sum(sum) / d;
    float variance = fmaxf(warp_all_sum(squared) / d - mean * mean, 0.f);
    float inverse = rsqrtf(variance + 1e-5f);
    if (lane == 0) { stats[2 * row] = mean; stats[2 * row + 1] = inverse; }
    for (int j = lane; j < d; j += 32)
        y[base + j] = scalar<T>((fp32(x[base + j]) - mean) * inverse * fp32(gamma[j]) + fp32(beta[j]));
}

template<class T> __global__ void layernorm_backward_input_warp(
        const T* dy, const T* x, const T* gamma, const float* stats, T* dx, int rows, int d) {
    int row = int((size_t(blockIdx.x) * blockDim.x + threadIdx.x) >> 5), lane = threadIdx.x & 31;
    if (row >= rows) return;
    size_t base = size_t(row) * d;
    float mean = stats[2 * row], inverse = stats[2 * row + 1], s1 = 0.f, s2 = 0.f;
    for (int j = lane; j < d; j += 32) {
        float g = fp32(dy[base + j]) * fp32(gamma[j]);
        s1 += g; s2 += g * (fp32(x[base + j]) - mean) * inverse;
    }
    s1 = warp_all_sum(s1) / d; s2 = warp_all_sum(s2) / d;
    for (int j = lane; j < d; j += 32) {
        float normalized = (fp32(x[base + j]) - mean) * inverse;
        dx[base + j] = scalar<T>(inverse * (fp32(dy[base + j]) * fp32(gamma[j]) - s1 - normalized * s2));
    }
}

template<class T> __global__ void layernorm_backward_input(
        const T* dy, const T* x, const T* gamma, const float* stats, T* dx, int d) {
    int row = blockIdx.x;
    float mean = stats[2 * row], inverse = stats[2 * row + 1], s1 = 0.f, s2 = 0.f;
    for (int j = threadIdx.x; j < d; j += blockDim.x) {
        size_t i = size_t(row) * d + j;
        float g = fp32(dy[i]) * fp32(gamma[j]);
        s1 += g; s2 += g * (fp32(x[i]) - mean) * inverse;
    }
    s1 = block_reduce(s1) / d; s2 = block_reduce(s2) / d;
    for (int j = threadIdx.x; j < d; j += blockDim.x) {
        size_t i = size_t(row) * d + j;
        float normalized = (fp32(x[i]) - mean) * inverse;
        dx[i] = scalar<T>(inverse * (fp32(dy[i]) * fp32(gamma[j]) - s1 - normalized * s2));
    }
}

// Each tile reads consecutive features; atomics combine independent row tiles.
// Tall tiles keep the atomic count low when there are many rows (tokens x batch).
constexpr int kAffineRows = 256;
template<class T> __global__ void affine_gradients(const T* dy, const T* x,
        const float* stats, float* dgamma, float* dbeta, int rows, int d) {
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (j >= d) return;
    float a = 0.f, b = 0.f;
    for (int row = blockIdx.y * kAffineRows; row < min(rows, int(blockIdx.y + 1) * kAffineRows); ++row) {
        size_t i = size_t(row) * d + j;
        float grad = fp32(dy[i]);
        b += grad;
        if (dgamma) a += grad * (fp32(x[i]) - stats[2 * row]) * stats[2 * row + 1];
    }
    atomicAdd(dbeta + j, b);
    if (dgamma) atomicAdd(dgamma + j, a);
}

__host__ __device__ inline float gelu(float x) {
    constexpr float c = 0.7978845608028654f;
    return .5f * x * (1.f + tanhf(c * (x + .044715f * x * x * x)));
}
__host__ __device__ inline float gelu_derivative(float x) {
    constexpr float c = 0.7978845608028654f;
    float t = tanhf(c * (x + .044715f * x * x * x));
    return .5f * (1.f + t) + .5f * x * (1.f - t * t) * c * (1.f + .134145f * x * x);
}

template<class T> __global__ void bias_add(T* x, const T* bias, int n, int d) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) x[i] = scalar<T>(fp32(x[i]) + fp32(bias[i % d]));
}
template<class T> __global__ void residual_bias(T* y, const T* residual, const T* bias, int n, int d) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) y[i] = scalar<T>(fp32(y[i]) + fp32(residual[i]) + fp32(bias[i % d]));
}
template<class T> __global__ void gelu_bias(T* pre, T* post, const T* bias, int n, int d) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        T rounded = scalar<T>(fp32(pre[i]) + fp32(bias[i % d]));
        pre[i] = rounded;
        post[i] = scalar<T>(gelu(fp32(rounded)));
    }
}
template<class T> __global__ void gelu_backward(T* grad, const T* pre, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) grad[i] = scalar<T>(fp32(grad[i]) * gelu_derivative(fp32(pre[i])));
}
template<class T> __global__ void add_inplace(T* x, const T* y, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) x[i] = scalar<T>(fp32(x[i]) + fp32(y[i]));
}

// QKV projection is [B,N,3D]; attention arrays are [B,H,N,D/H].
template<class T, bool Reverse = false> __global__ void qkv_layout(
        T* packed, T* q, T* k, T* v, int b, int n, int d, int h) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= b * n * d) return;
    int channel = i % d, row = i / d, token = row % n, batch = row / n, dk = d / h;
    size_t target = ((size_t(batch) * h + channel / dk) * n + token) * dk + channel % dk;
    size_t source = size_t(row) * 3 * d + channel;
    if (Reverse) {
        packed[source] = q[target]; packed[source + d] = k[target]; packed[source + 2 * d] = v[target];
    } else {
        q[target] = packed[source]; k[target] = packed[source + d]; v[target] = packed[source + 2 * d];
    }
}
template<class T, bool Reverse = false> __global__ void context_layout(
        T* merged, T* split, int b, int n, int d, int h) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= b * n * d) return;
    int channel = i % d, row = i / d, token = row % n, batch = row / n, dk = d / h;
    size_t target = ((size_t(batch) * h + channel / dk) * n + token) * dk + channel % dk;
    if (Reverse) split[target] = merged[i]; else merged[i] = split[target];
}

template<class T> __global__ void attention_softmax(T* values, int n) {
    size_t base = size_t(blockIdx.x) * n;
    float maximum = -INFINITY;
    for (int j = threadIdx.x; j < n; j += blockDim.x) maximum = fmaxf(maximum, fp32(values[base + j]));
    maximum = block_reduce<true>(maximum);
    float sum = 0.f;
    for (int j = threadIdx.x; j < n; j += blockDim.x) sum += expf(fp32(values[base + j]) - maximum);
    sum = block_reduce(sum);
    for (int j = threadIdx.x; j < n; j += blockDim.x)
        values[base + j] = scalar<T>(expf(fp32(values[base + j]) - maximum) / sum);
}
template<class T> __global__ void attention_softmax_warp(T* values, int rows, int n) {
    int row = int((size_t(blockIdx.x) * blockDim.x + threadIdx.x) >> 5), lane = threadIdx.x & 31;
    if (row >= rows) return;
    size_t base = size_t(row) * n;
    float maximum = -INFINITY;
    for (int j = lane; j < n; j += 32) maximum = fmaxf(maximum, fp32(values[base + j]));
    maximum = warp_all_max(maximum);
    float sum = 0.f;
    for (int j = lane; j < n; j += 32) sum += expf(fp32(values[base + j]) - maximum);
    sum = warp_all_sum(sum);
    for (int j = lane; j < n; j += 32)
        values[base + j] = scalar<T>(expf(fp32(values[base + j]) - maximum) / sum);
}
template<class T> __global__ void attention_softmax_backward_warp(
        T* grad, const T* probabilities, int rows, int n, float scale) {
    int row = int((size_t(blockIdx.x) * blockDim.x + threadIdx.x) >> 5), lane = threadIdx.x & 31;
    if (row >= rows) return;
    size_t base = size_t(row) * n;
    float dot = 0.f;
    for (int j = lane; j < n; j += 32) dot += fp32(grad[base + j]) * fp32(probabilities[base + j]);
    dot = warp_all_sum(dot);
    for (int j = lane; j < n; j += 32)
        grad[base + j] = scalar<T>(scale * fp32(probabilities[base + j]) * (fp32(grad[base + j]) - dot));
}
template<class T> __global__ void attention_softmax_backward(
        T* grad, const T* probabilities, int n, float scale) {
    size_t base = size_t(blockIdx.x) * n;
    float dot = 0.f;
    for (int j = threadIdx.x; j < n; j += blockDim.x)
        dot += fp32(grad[base + j]) * fp32(probabilities[base + j]);
    dot = block_reduce(dot);
    for (int j = threadIdx.x; j < n; j += blockDim.x)
        grad[base + j] = scalar<T>(scale * fp32(probabilities[base + j]) * (fp32(grad[base + j]) - dot));
}

inline int grid(int n) { return (n + 255) / 256; }

template<class T> struct Parameter {
    std::string name;
    int rows, cols;
    Buffer<T> value;
    Buffer<float> gradient;
    std::vector<float> initial;
    Parameter(std::string label, int r, int c, std::mt19937& rng, int initialization = 0,
              bool own_storage = true)
        : name(std::move(label)), rows(r), cols(c), initial(size_t(r) * c) {
        float bound = std::sqrt(6.f / (r + c));
        std::uniform_real_distribution<float> distribution(-bound, bound);
        for (float& v : initial) v = initialization == 1 ? 1.f : initialization == 2 ? 0.f : distribution(rng);
        if (own_storage) {
            value.allocate(initial.size()); gradient.allocate(initial.size());
            value.upload(initial);
        }
    }
};

template<class T> struct Block {
    Parameter<T> norm1_g, norm1_b, qkv_w, qkv_b, out_w, out_b;
    Parameter<T> norm2_g, norm2_b, ff1_w, ff1_b, ff2_w, ff2_b;
    Buffer<T> z1, qkv, q, k, v, probability, context, merged, residual, z2, pre, hidden, output;
    Buffer<float> stats1, stats2;
    Block(const Config& c, int index, std::mt19937& rng, bool own = true)
        : norm1_g(label(index, "ln1.gamma"), 1, c.width, rng, 1, own),
          norm1_b(label(index, "ln1.beta"), 1, c.width, rng, 2, own),
          qkv_w(label(index, "qkv.weight"), 3 * c.width, c.width, rng, 0, own),
          qkv_b(label(index, "qkv.bias"), 1, 3 * c.width, rng, 2, own),
          out_w(label(index, "out.weight"), c.width, c.width, rng, 0, own),
          out_b(label(index, "out.bias"), 1, c.width, rng, 2, own),
          norm2_g(label(index, "ln2.gamma"), 1, c.width, rng, 1, own),
          norm2_b(label(index, "ln2.beta"), 1, c.width, rng, 2, own),
          ff1_w(label(index, "ff1.weight"), c.ff, c.width, rng, 0, own),
          ff1_b(label(index, "ff1.bias"), 1, c.ff, rng, 2, own),
          ff2_w(label(index, "ff2.weight"), c.width, c.ff, rng, 0, own),
          ff2_b(label(index, "ff2.bias"), 1, c.width, rng, 2, own) {
        size_t m = size_t(c.batch) * c.tokens, md = m * c.width;
        z1.allocate(md); qkv.allocate(3 * md); q.allocate(md); k.allocate(md); v.allocate(md);
        probability.allocate(size_t(c.batch) * c.heads * c.tokens * c.tokens);
        context.allocate(md); merged.allocate(md); residual.allocate(md); z2.allocate(md);
        pre.allocate(m * c.ff); hidden.allocate(m * c.ff); output.allocate(md);
        stats1.allocate(2 * m); stats2.allocate(2 * m);
    }
    static std::string label(int i, const char* suffix) { return "block" + std::to_string(i) + "." + suffix; }
    std::vector<Parameter<T>*> parameters() {
        return {&norm1_g, &norm1_b, &qkv_w, &qkv_b, &out_w, &out_b,
                &norm2_g, &norm2_b, &ff1_w, &ff1_b, &ff2_w, &ff2_b};
    }
};

template<class T> class Model {
public:
    Config config;
    bool owns_parameters;
    cudaStream_t stream = nullptr;
    cudaStream_t owned_stream = nullptr;
    cublasHandle_t blas = nullptr;
    Buffer<unsigned char> workspace;
    std::mt19937 rng;
    std::vector<std::unique_ptr<Block<T>>> blocks;
    Parameter<T> final_g, final_b;
    Buffer<T> input, output, upstream, state_gradient, branch_gradient, norm_gradient;
    Buffer<T> merged_gradient, ff_gradient, qkv_gradient, context_gradient;
    Buffer<T> probability_gradient, q_gradient, k_gradient, v_gradient;
    Buffer<float> final_stats;
    size_t allocated_bytes = 0;

    explicit Model(Config dimensions, uint32_t seed = 17, bool own_parameters = true)
        : config(validated(dimensions)), owns_parameters(own_parameters), rng(seed),
          final_g("final.gamma", 1, config.width, rng, 1, own_parameters),
          final_b("final.beta", 1, config.width, rng, 2, own_parameters) {
        check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "create stream");
        owned_stream = stream;
        check(cublasCreate(&blas), "create cuBLAS");
        check(cublasSetStream(blas, stream), "set cuBLAS stream");
        check(cublasSetMathMode(blas, CUBLAS_DEFAULT_MATH), "set cuBLAS math mode");
        if (!own_parameters) {
            workspace.allocate(4 * 1024 * 1024);
            check(cublasSetWorkspace(blas, workspace.data, workspace.count), "set cuBLAS workspace");
        }
        size_t m = size_t(config.batch) * config.tokens, md = m * config.width;
        input.allocate(md); output.allocate(md); upstream.allocate(md); state_gradient.allocate(md);
        branch_gradient.allocate(md); norm_gradient.allocate(md); merged_gradient.allocate(md);
        ff_gradient.allocate(m * config.ff); qkv_gradient.allocate(3 * md); context_gradient.allocate(md);
        probability_gradient.allocate(size_t(config.batch) * config.heads * config.tokens * config.tokens);
        q_gradient.allocate(md); k_gradient.allocate(md); v_gradient.allocate(md); final_stats.allocate(2 * m);
        for (int i = 0; i < config.layers; ++i) blocks.emplace_back(new Block<T>(config, i, rng, own_parameters));
        if (own_parameters) {
        std::uniform_real_distribution<float> random(-.5f, .5f);
        std::vector<float> values(md);
        for (float& v : values) v = random(rng);
        input.upload(values);
        for (float& v : values) v = random(rng) * .1f;
        upstream.upload(values);
        }
        allocated_bytes = memory_bytes();
    }
    ~Model() {
        if (stream) cudaStreamSynchronize(stream);
        if (blas) cublasDestroy(blas);
        if (owned_stream) cudaStreamDestroy(owned_stream);
    }
    void set_stream(cudaStream_t external_stream) {
        stream = external_stream;
        check(cublasSetStream(blas, stream), "bind policy stream");
        if (workspace.data)
            check(cublasSetWorkspace(blas, workspace.data, workspace.count), "bind policy workspace");
    }
    static Config validated(Config c) { c.validate(); return c; }
    std::vector<Parameter<T>*> parameters() {
        std::vector<Parameter<T>*> p;
        for (auto& b : blocks) for (auto* q : b->parameters()) p.push_back(q);
        p.push_back(&final_g); p.push_back(&final_b);
        return p;
    }
    size_t memory_bytes() {
        size_t m = size_t(config.batch) * config.tokens, md = m * config.width;
        size_t attention = size_t(config.batch) * config.heads * config.tokens * config.tokens;
        // Owned buffers only, including the training cache and parameter gradients.
        size_t activations = (14 * md + m * config.ff + attention) * sizeof(T) + 2 * m * sizeof(float);
        activations += config.layers * ((12 * md + 2 * m * config.ff + attention) * sizeof(T)
                                      + 4 * m * sizeof(float));
        return activations + workspace.count + (owns_parameters
            ? config.parameter_count() * (sizeof(T) + sizeof(float)) : 0);
    }

    // Dense row-major y = x * w^T, w[outputs, inputs].
    void linear(const T* x, const Parameter<T>& w, T* y, int rows) {
        float one = 1.f, zero = 0.f;
        check(cublasGemmEx(blas, CUBLAS_OP_T, CUBLAS_OP_N, w.rows, rows, w.cols,
            &one, w.value.data, dtype<T>(), w.cols, x, dtype<T>(), w.cols,
            &zero, y, dtype<T>(), w.rows, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT), "linear forward");
    }
    void linear_backward(const T* dy, const T* x, Parameter<T>& w, Parameter<T>& bias,
                         T* dx, int rows) {
        float one = 1.f, zero = 0.f;
        check(cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_N, w.cols, rows, w.rows,
            &one, w.value.data, dtype<T>(), w.cols, dy, dtype<T>(), w.rows,
            &zero, dx, dtype<T>(), w.cols, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT), "linear input gradient");
        check(cublasGemmEx(blas, CUBLAS_OP_N, CUBLAS_OP_T, w.cols, w.rows, rows,
            &one, x, dtype<T>(), w.cols, dy, dtype<T>(), w.rows,
            &zero, w.gradient.data, CUDA_R_32F, w.cols, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT), "linear weight gradient");
        check(cudaMemsetAsync(bias.gradient.data, 0, bias.gradient.count * sizeof(float), stream), "zero bias gradient");
        affine_gradients<T><<<dim3((w.rows + 127) / 128, (rows + kAffineRows - 1) / kAffineRows), 128, 0, stream>>>(
            dy, nullptr, nullptr, nullptr, bias.gradient.data, rows, w.rows);
    }
    void norm(const T* x, Parameter<T>& gamma, Parameter<T>& beta, T* y, float* stats) {
        int rows = config.batch * config.tokens;
        layernorm_forward_warp<T><<<warp_rows_grid(rows), kWarpRowThreads, 0, stream>>>(
            x, gamma.value.data, beta.value.data, y, stats, rows, config.width);
    }
    void norm_backward(const T* dy, const T* x, Parameter<T>& gamma, Parameter<T>& beta,
                       const float* stats, T* dx) {
        int rows = config.batch * config.tokens, d = config.width;
        layernorm_backward_input_warp<T><<<warp_rows_grid(rows), kWarpRowThreads, 0, stream>>>(
            dy, x, gamma.value.data, stats, dx, rows, d);
        check(cudaMemsetAsync(gamma.gradient.data, 0, d * sizeof(float), stream), "zero LN scale gradient");
        check(cudaMemsetAsync(beta.gradient.data, 0, d * sizeof(float), stream), "zero LN bias gradient");
        affine_gradients<T><<<dim3((d + 127) / 128, (rows + kAffineRows - 1) / kAffineRows), 128, 0, stream>>>(
            dy, x, stats, gamma.gradient.data, beta.gradient.data, rows, d);
    }
    // Row-major batched C[M,N] = op(A)[M,K] * op(B)[K,N].
    void batched(const T* a, const T* b, T* c, int m, int n, int k,
                 bool transpose_a, bool transpose_b, float alpha = 1.f) {
        float zero = 0.f;
        check(cublasGemmStridedBatchedEx(blas,
            transpose_b ? CUBLAS_OP_T : CUBLAS_OP_N,
            transpose_a ? CUBLAS_OP_T : CUBLAS_OP_N,
            n, m, k, &alpha, b, dtype<T>(), transpose_b ? k : n, int64_t(k) * n,
            a, dtype<T>(), transpose_a ? m : k, int64_t(m) * k,
            &zero, c, dtype<T>(), n, int64_t(m) * n, config.batch * config.heads,
            CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT), "attention GEMM");
    }
    void forward() {
        int b = config.batch, n = config.tokens, d = config.width, h = config.heads;
        int m = b * n, md = m * d, dk = d / h;
        const T* x = input.data;
        for (auto& pointer : blocks) {
            auto& a = *pointer;
            norm(x, a.norm1_g, a.norm1_b, a.z1.data, a.stats1.data);
            linear(a.z1.data, a.qkv_w, a.qkv.data, m);
            bias_add<T><<<grid(3 * md), 256, 0, stream>>>(a.qkv.data, a.qkv_b.value.data, 3 * md, 3 * d);
            qkv_layout<T><<<grid(md), 256, 0, stream>>>(a.qkv.data, a.q.data, a.k.data, a.v.data, b, n, d, h);
            batched(a.q.data, a.k.data, a.probability.data, n, n, dk, false, true, 1.f / std::sqrt(float(dk)));
            attention_softmax_warp<T><<<warp_rows_grid(b * h * n), kWarpRowThreads, 0, stream>>>(
                a.probability.data, b * h * n, n);
            batched(a.probability.data, a.v.data, a.context.data, n, dk, n, false, false);
            context_layout<T><<<grid(md), 256, 0, stream>>>(a.merged.data, a.context.data, b, n, d, h);
            linear(a.merged.data, a.out_w, a.residual.data, m);
            residual_bias<T><<<grid(md), 256, 0, stream>>>(a.residual.data, x, a.out_b.value.data, md, d);
            norm(a.residual.data, a.norm2_g, a.norm2_b, a.z2.data, a.stats2.data);
            linear(a.z2.data, a.ff1_w, a.pre.data, m);
            gelu_bias<T><<<grid(m * config.ff), 256, 0, stream>>>(a.pre.data, a.hidden.data, a.ff1_b.value.data, m * config.ff, config.ff);
            linear(a.hidden.data, a.ff2_w, a.output.data, m);
            residual_bias<T><<<grid(md), 256, 0, stream>>>(a.output.data, a.residual.data, a.ff2_b.value.data, md, d);
            x = a.output.data;
        }
        norm(x, final_g, final_b, output.data, final_stats.data);
        check(cudaGetLastError(), "Transformer forward kernels");
    }
    void backward() {
        int b = config.batch, n = config.tokens, d = config.width, h = config.heads;
        int m = b * n, md = m * d, dk = d / h;
        norm_backward(upstream.data, blocks.back()->output.data, final_g, final_b,
                      final_stats.data, state_gradient.data);
        for (int layer = config.layers - 1; layer >= 0; --layer) {
            auto& a = *blocks[layer];
            const T* x = layer == 0 ? input.data : blocks[layer - 1]->output.data;
            linear_backward(state_gradient.data, a.hidden.data, a.ff2_w, a.ff2_b, ff_gradient.data, m);
            gelu_backward<T><<<grid(m * config.ff), 256, 0, stream>>>(ff_gradient.data, a.pre.data, m * config.ff);
            linear_backward(ff_gradient.data, a.z2.data, a.ff1_w, a.ff1_b, branch_gradient.data, m);
            norm_backward(branch_gradient.data, a.residual.data, a.norm2_g, a.norm2_b, a.stats2.data, norm_gradient.data);
            add_inplace<T><<<grid(md), 256, 0, stream>>>(state_gradient.data, norm_gradient.data, md);
            linear_backward(state_gradient.data, a.merged.data, a.out_w, a.out_b, merged_gradient.data, m);
            context_layout<T, true><<<grid(md), 256, 0, stream>>>(merged_gradient.data, context_gradient.data, b, n, d, h);
            batched(context_gradient.data, a.v.data, probability_gradient.data, n, n, dk, false, true);
            batched(a.probability.data, context_gradient.data, v_gradient.data, n, dk, n, true, false);
            attention_softmax_backward_warp<T><<<warp_rows_grid(b * h * n), kWarpRowThreads, 0, stream>>>(
                probability_gradient.data, a.probability.data, b * h * n, n, 1.f / std::sqrt(float(dk)));
            batched(probability_gradient.data, a.k.data, q_gradient.data, n, dk, n, false, false);
            batched(probability_gradient.data, a.q.data, k_gradient.data, n, dk, n, true, false);
            qkv_layout<T, true><<<grid(md), 256, 0, stream>>>(qkv_gradient.data, q_gradient.data, k_gradient.data, v_gradient.data, b, n, d, h);
            linear_backward(qkv_gradient.data, a.z1.data, a.qkv_w, a.qkv_b, branch_gradient.data, m);
            norm_backward(branch_gradient.data, x, a.norm1_g, a.norm1_b, a.stats1.data, norm_gradient.data);
            add_inplace<T><<<grid(md), 256, 0, stream>>>(state_gradient.data, norm_gradient.data, md);
        }
        check(cudaGetLastError(), "Transformer backward kernels");
    }
};

} // namespace puf_transformer
