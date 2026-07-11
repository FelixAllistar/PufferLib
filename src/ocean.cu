// NMMO3 CUDA encoder: multihot, cuDNN conv, embedding, concat, projection
// Included by pufferlib.cu — requires precision_t, PrecisionTensor, Allocator, puf_mm, etc.

#include "cudnn_conv2d.cu"

// ---- NMMO3 constants ----

static constexpr int N3_MAP_H = 11, N3_MAP_W = 15, N3_NFEAT = 10;
static constexpr int N3_MULTIHOT = 59;
static constexpr int N3_MAP_SIZE = N3_MAP_H * N3_MAP_W * N3_NFEAT;
static constexpr int N3_PLAYER = 47, N3_REWARD = 10;
static constexpr int N3_EMBED_DIM = 32, N3_EMBED_VOCAB = 128;
static constexpr int N3_PLAYER_EMBED = N3_PLAYER * N3_EMBED_DIM;
static constexpr int N3_C1_IC = 59, N3_C1_OC = 128, N3_C1_K = 5, N3_C1_S = 3;
static constexpr int N3_C1_OH = 3, N3_C1_OW = 4;
static constexpr int N3_C2_IC = 128, N3_C2_OC = 128, N3_C2_K = 3, N3_C2_S = 1;
static constexpr int N3_C2_OH = 1, N3_C2_OW = 2;
static constexpr int N3_CONV_FLAT = N3_C2_OC * N3_C2_OH * N3_C2_OW;
static constexpr int N3_CONCAT = N3_CONV_FLAT + N3_PLAYER_EMBED + N3_PLAYER + N3_REWARD;

__constant__ int N3_OFFSETS[10] = {0, 4, 8, 25, 30, 33, 38, 43, 48, 55};

static cudnnDataType_t n3_cudnn_dtype() {
    return (PRECISION_SIZE == 2) ? CUDNN_DATA_BFLOAT16 : CUDNN_DATA_FLOAT;
}

// ---- NMMO3 kernels ----

__global__ void n3_multihot_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ obs, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_MAP_H * N3_MAP_W) return;
    int b = idx / (N3_MAP_H * N3_MAP_W), rem = idx % (N3_MAP_H * N3_MAP_W);
    int h = rem / N3_MAP_W, w = rem % N3_MAP_W;
    const precision_t* src = obs + b * obs_size + (h * N3_MAP_W + w) * N3_NFEAT;
    precision_t* dst = out + b * N3_MULTIHOT * N3_MAP_H * N3_MAP_W;
    for (int f = 0; f < N3_NFEAT; f++)
        dst[(N3_OFFSETS[f] + (int)to_float(src[f])) * N3_MAP_H * N3_MAP_W + h * N3_MAP_W + w] = from_float(1.0f);
}

__global__ void n3_embedding_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ obs,
    const precision_t* __restrict__ embed_w, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_PLAYER) return;
    int b = idx / N3_PLAYER, f = idx % N3_PLAYER;
    int val = (int)to_float(obs[b * obs_size + N3_MAP_SIZE + f]);
    const precision_t* src = embed_w + val * N3_EMBED_DIM;
    precision_t* dst = out + b * N3_PLAYER_EMBED + f * N3_EMBED_DIM;
    for (int d = 0; d < N3_EMBED_DIM; d++) dst[d] = src[d];
}

__global__ void n3_concat_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ conv_flat,
    const precision_t* __restrict__ embed, const precision_t* __restrict__ obs,
    int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_CONCAT) return;
    int b = idx / N3_CONCAT, c = idx % N3_CONCAT;
    precision_t val;
    if (c < N3_CONV_FLAT) {
        int oc = c / (N3_C2_OH * N3_C2_OW), r = c % (N3_C2_OH * N3_C2_OW);
        int oh = r / N3_C2_OW, ow = r % N3_C2_OW;
        val = conv_flat[b * N3_CONV_FLAT + oc * N3_C2_OH * N3_C2_OW + oh * N3_C2_OW + ow];
    } else if (c < N3_CONV_FLAT + N3_PLAYER_EMBED)
        val = embed[b * N3_PLAYER_EMBED + (c - N3_CONV_FLAT)];
    else if (c < N3_CONV_FLAT + N3_PLAYER_EMBED + N3_PLAYER)
        val = obs[b * obs_size + N3_MAP_SIZE + (c - N3_CONV_FLAT - N3_PLAYER_EMBED)];
    else
        val = obs[b * obs_size + obs_size - N3_REWARD + (c - N3_CONV_FLAT - N3_PLAYER_EMBED - N3_PLAYER)];
    out[idx] = val;
}

__global__ void n3_bias_relu_kernel(
    precision_t* __restrict__ data, const precision_t* __restrict__ bias, int total, int dim) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx]) + to_float(bias[idx % dim])));
}

__global__ void n3_relu_backward_kernel(
    precision_t* __restrict__ grad, const precision_t* __restrict__ out, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    if (to_float(out[idx]) <= 0.0f) grad[idx] = from_float(0.0f);
}


__global__ void bias_grad_kernel(
    precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad, int N, int dim) {
    int d = blockIdx.x;
    if (d >= dim) return;
    float sum = 0.0f;
    for (int i = threadIdx.x; i < N; i += blockDim.x)
        sum += to_float(grad[i * dim + d]);
    for (int offset = 16; offset > 0; offset >>= 1)
        sum += __shfl_down_sync(0xffffffff, sum, offset);
    __shared__ float sdata[32];
    int lane = threadIdx.x % 32, warp = threadIdx.x / 32;
    if (lane == 0) sdata[warp] = sum;
    __syncthreads();
    if (warp == 0) {
        sum = (lane < (blockDim.x + 31) / 32) ? sdata[lane] : 0.0f;
        for (int offset = 16; offset > 0; offset >>= 1)
            sum += __shfl_down_sync(0xffffffff, sum, offset);
        if (lane == 0) bgrad[d] = from_float(sum);
    }
}

// NCHW bias grad: sum over (B, OH, OW) for each OC channel
__global__ void n3_conv_bias_grad_nchw(
    precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad,
    int B, int OC, int spatial) {
    int oc = blockIdx.x;
    if (oc >= OC) return;
    float sum = 0.0f;
    int total = B * spatial;
    for (int i = threadIdx.x; i < total; i += blockDim.x) {
        int b = i / spatial, s = i % spatial;
        sum += to_float(grad[b * OC * spatial + oc * spatial + s]);
    }
    for (int offset = 16; offset > 0; offset >>= 1)
        sum += __shfl_down_sync(0xffffffff, sum, offset);
    __shared__ float sdata[32];
    int lane = threadIdx.x % 32, warp = threadIdx.x / 32;
    if (lane == 0) sdata[warp] = sum;
    __syncthreads();
    if (warp == 0) {
        sum = (lane < (blockDim.x + 31) / 32) ? sdata[lane] : 0.0f;
        for (int offset = 16; offset > 0; offset >>= 1)
            sum += __shfl_down_sync(0xffffffff, sum, offset);
        if (lane == 0) bgrad[oc] = from_float(sum);
    }
}

__global__ void n3_concat_backward_conv_kernel(
    precision_t* __restrict__ conv_grad, const precision_t* __restrict__ concat_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_CONV_FLAT) return;
    int b = idx / N3_CONV_FLAT, c = idx % N3_CONV_FLAT;
    conv_grad[b * N3_CONV_FLAT + c] = concat_grad[b * N3_CONCAT + c];
}

// Embedding backward: scatter-add grad from concat_grad's player_embed region
// into embed_wgrad (float accumulation buffer).
// Each (b, f) looked up row obs[b, MAP_SIZE+f] from the table.
__global__ void n3_embedding_backward_kernel(
    float* __restrict__ embed_wgrad_f,
    const precision_t* __restrict__ concat_grad,
    const precision_t* __restrict__ obs,
    int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_PLAYER * N3_EMBED_DIM) return;
    int b = idx / (N3_PLAYER * N3_EMBED_DIM);
    int rem = idx % (N3_PLAYER * N3_EMBED_DIM);
    int f = rem / N3_EMBED_DIM;
    int d = rem % N3_EMBED_DIM;
    int val = (int)to_float(obs[b * obs_size + N3_MAP_SIZE + f]);
    float g = to_float(concat_grad[b * N3_CONCAT + N3_CONV_FLAT + f * N3_EMBED_DIM + d]);
    atomicAdd(&embed_wgrad_f[val * N3_EMBED_DIM + d], g);
}

// Cast float buffer to precision_t
__global__ void n3_float_to_precision_kernel(
    precision_t* __restrict__ dst, const float* __restrict__ src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) dst[idx] = from_float(src[idx]);
}

// ---- atomicAdd for precision_t ----
#ifdef PRECISION_FLOAT
__device__ __forceinline__ void atomicAdd_precision(precision_t* addr, precision_t val) {
    atomicAdd(addr, val);
}
#else
__device__ __forceinline__ void atomicAdd_precision(precision_t* addr, precision_t val) {
    // bf16 atomicAdd via CAS on enclosing 32-bit word
    unsigned int* addr_u32 = (unsigned int*)((size_t)addr & ~2ULL);
    bool is_high = ((size_t)addr & 2) != 0;
    unsigned int old_u32 = *addr_u32, assumed;
    do {
        assumed = old_u32;
        __nv_bfloat16* pair = (__nv_bfloat16*)&old_u32;
        float sum = __bfloat162float(pair[is_high]) + __bfloat162float(val);
        unsigned int new_u32 = assumed;
        ((__nv_bfloat16*)&new_u32)[is_high] = __float2bfloat16(sum);
        old_u32 = atomicCAS(addr_u32, assumed, new_u32);
    } while (old_u32 != assumed);
}
#endif

// ---- NCHW bias kernels for im2col conv path ----

__global__ void conv_bias_kernel(precision_t* __restrict__ data,
        const precision_t* __restrict__ bias, int B, int OC, int spatial) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int oc = (idx / spatial) % OC;
    data[idx] = from_float(to_float(data[idx]) + to_float(bias[oc]));
}

__global__ void conv_bias_relu_kernel(precision_t* __restrict__ data,
        const precision_t* __restrict__ bias, int B, int OC, int spatial) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int oc = (idx / spatial) % OC;
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx]) + to_float(bias[oc])));
}

// ---- im2col + cuBLAS conv (no cuDNN) ----
// NCHW layout throughout. Weight stored as (OC, IC*K*K).
// im2col produces (B*OH*OW, IC*K*K), matmul with W^T gives (B*OH*OW, OC),
// then reshape to NCHW (B, OC, OH, OW).

__global__ void im2col_kernel(
    const precision_t* __restrict__ input, precision_t* __restrict__ col,
    int B, int IC, int IH, int IW, int K, int S, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OH * OW * IC * K * K;
    if (idx >= total) return;
    int col_w = IC * K * K;
    int row = idx / col_w;
    int c = idx % col_w;
    int b = row / (OH * OW);
    int rem = row % (OH * OW);
    int oh = rem / OW, ow = rem % OW;
    int ic = c / (K * K), kk = c % (K * K);
    int kh = kk / K, kw = kk % K;
    int ih = oh * S + kh, iw = ow * S + kw;
    col[idx] = input[b * IC * IH * IW + ic * IH * IW + ih * IW + iw];
}

// Backward: col2im — input-centric gather to avoid atomics.
// Each thread owns one (b, ic, ih, iw) element and sums contributions from all
// (oh, ow, kh, kw) patches that map to it.
__global__ void col2im_kernel(
    const precision_t* __restrict__ col, precision_t* __restrict__ grad_input,
    int B, int IC, int IH, int IW, int K, int S, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * IC * IH * IW;
    if (idx >= total) return;
    int iw = idx % IW;
    int ih = (idx / IW) % IH;
    int ic = (idx / (IW * IH)) % IC;
    int b  = idx / (IW * IH * IC);
    float sum = 0.0f;
    for (int kh = 0; kh < K; kh++) {
        int ih_off = ih - kh;
        if (ih_off < 0 || ih_off % S != 0) continue;
        int oh = ih_off / S;
        if (oh >= OH) continue;
        for (int kw = 0; kw < K; kw++) {
            int iw_off = iw - kw;
            if (iw_off < 0 || iw_off % S != 0) continue;
            int ow = iw_off / S;
            if (ow >= OW) continue;
            int col_idx = (b * OH * OW + oh * OW + ow) * (IC * K * K) + ic * K * K + kh * K + kw;
            sum += to_float(col[col_idx]);
        }
    }
    grad_input[idx] = from_float(sum);
}

// Transpose (B, OC, OH, OW) -> (B*OH*OW, OC)  [NCHW to row-major spatial-first]
__global__ void nchw_to_rows_kernel(
    const precision_t* __restrict__ src, precision_t* __restrict__ dst,
    int B, int OC, int spatial
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    dst[(b * spatial + s) * OC + oc] = src[idx];
}

// Transpose (B*OH*OW, OC) -> (B, OC, OH, OW)  [row-major spatial-first to NCHW]
__global__ void rows_to_nchw_kernel(
    const precision_t* __restrict__ src, precision_t* __restrict__ dst,
    int B, int OC, int spatial
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    dst[idx] = src[(b * spatial + s) * OC + oc];
}

// Forward: im2col conv + bias + optional relu. All NCHW.
// col_buf: pre-allocated (max_B * OH * OW, IC * K * K)
// mm_buf:  pre-allocated (max_B * OH * OW, OC)  — row-major (spatial-first)
static void gemm_conv_forward(
    PrecisionTensor* weight, PrecisionTensor* bias,
    precision_t* input, precision_t* output,
    precision_t* col_buf, precision_t* mm_buf,
    int B, int IC, int IH, int IW, int OC, int K, int S, int OH, int OW,
    bool relu, cudaStream_t stream
) {
    int col_rows = B * OH * OW;
    int col_cols = IC * K * K;
    int total_col = col_rows * col_cols;
    int total_out = B * OC * OH * OW;

    // im2col: input NCHW -> col (B*OH*OW, IC*K*K)
    im2col_kernel<<<grid_size(total_col), BLOCK_SIZE, 0, stream>>>(
        input, col_buf, B, IC, IH, IW, K, S, OH, OW);

    // matmul: col (B*OH*OW, IC*K*K) @ W^T (IC*K*K, OC) = mm_buf (B*OH*OW, OC)
    PrecisionTensor col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    PrecisionTensor mm_t  = {.data = mm_buf,  .shape = {col_rows, OC}};
    puf_mm(&col_t, weight, &mm_t, stream);

    // transpose (B*OH*OW, OC) -> (B, OC, OH, OW) NCHW + bias + relu
    int spatial = OH * OW;
    rows_to_nchw_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
        mm_buf, output, B, OC, spatial);
    if (relu) {
        conv_bias_relu_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
            output, bias->data, B, OC, spatial);
    } else {
        conv_bias_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
            output, bias->data, B, OC, spatial);
    }
}

// Backward: weight grad + optional input grad via im2col/col2im + cuBLAS.
// grad_output is NCHW (B, OC, OH, OW). saved_input is NCHW.
// Caller handles relu backward and bias grad (same as cuDNN path).
static void gemm_conv_backward(
    PrecisionTensor* weight,
    precision_t* saved_input, precision_t* grad_output,
    precision_t* wgrad, precision_t* input_grad,
    precision_t* col_buf, precision_t* mm_buf,
    int B, int IC, int IH, int IW, int OC, int K, int S, int OH, int OW,
    cudaStream_t stream
) {
    int col_rows = B * OH * OW;
    int col_cols = IC * K * K;
    int total_col = col_rows * col_cols;
    int total_out = B * OC * OH * OW;
    int spatial = OH * OW;

    // Transpose grad_output NCHW -> (B*OH*OW, OC)
    nchw_to_rows_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
        grad_output, mm_buf, B, OC, spatial);

    // im2col of saved_input
    im2col_kernel<<<grid_size(total_col), BLOCK_SIZE, 0, stream>>>(
        saved_input, col_buf, B, IC, IH, IW, K, S, OH, OW);

    // Weight grad: mm_buf^T (OC, B*OH*OW) @ col_buf (B*OH*OW, IC*K*K) = wgrad (OC, IC*K*K)
    PrecisionTensor mm_t  = {.data = mm_buf,  .shape = {col_rows, OC}};
    PrecisionTensor col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    PrecisionTensor wg_t  = {.data = wgrad,   .shape = {OC, col_cols}};
    puf_mm_tn(&mm_t, &col_t, &wg_t, stream);

    // Input grad (optional): mm_buf (B*OH*OW, OC) @ weight (OC, IC*K*K) = col_grad (B*OH*OW, IC*K*K)
    if (input_grad) {
        puf_mm_nn(&mm_t, weight, &col_t, stream);  // reuse col_buf as col_grad
        col2im_kernel<<<grid_size(B * IC * IH * IW), BLOCK_SIZE, 0, stream>>>(
            col_buf, input_grad, B, IC, IH, IW, K, S, OH, OW);
    }
}

// ---- NMMO3 encoder structs ----

struct NMMO3EncoderWeights {
    ConvWeights conv1, conv2;
    PrecisionTensor embed_w, proj_w, proj_b;
    int obs_size, hidden;
};

struct NMMO3EncoderActivations {
    ConvActivations conv1, conv2;
    PrecisionTensor col1, mm1, col2, mm2;  // im2col + matmul scratch buffers
    PrecisionTensor multihot, embed_out, concat, out, saved_obs;
    PrecisionTensor embed_wgrad, proj_wgrad, proj_bgrad;
    FloatTensor embed_wgrad_f;  // float accumulation buffer for scatter-add
};

static NMMO3EncoderWeights* nmmo3_encoder_create(int obs_size, int hidden) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)calloc(1, sizeof(NMMO3EncoderWeights));
    ew->obs_size = obs_size; ew->hidden = hidden;
    conv_init(&ew->conv1, N3_C1_IC, N3_C1_OC, N3_C1_K, N3_C1_S, N3_MAP_H, N3_MAP_W, true);
    conv_init(&ew->conv2, N3_C2_IC, N3_C2_OC, N3_C2_K, N3_C2_S, N3_C1_OH, N3_C1_OW, false);
    return ew;
}

// ---- NMMO3 encoder interface ----

static PrecisionTensor nmmo3_encoder_forward(void* w, void* activations, PrecisionTensor input, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    int B = input.shape[0];

    if (a->saved_obs.data) puf_copy(&a->saved_obs, &input, stream);

    cudaMemsetAsync(a->multihot.data, 0, (int64_t)B * N3_MULTIHOT * N3_MAP_H * N3_MAP_W * sizeof(precision_t), stream);
    n3_multihot_kernel<<<grid_size(B * N3_MAP_H * N3_MAP_W), BLOCK_SIZE, 0, stream>>>(
        a->multihot.data, input.data, B, ew->obs_size);

    gemm_conv_forward(&ew->conv1.w, &ew->conv1.b, a->multihot.data, a->conv1.out.data,
        a->col1.data, a->mm1.data, B, N3_C1_IC, N3_MAP_H, N3_MAP_W,
        N3_C1_OC, N3_C1_K, N3_C1_S, N3_C1_OH, N3_C1_OW, true, stream);
    if (a->conv1.saved_input.data)
        cudaMemcpyAsync(a->conv1.saved_input.data, a->multihot.data,
            (int64_t)B * N3_C1_IC * N3_MAP_H * N3_MAP_W * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);
    gemm_conv_forward(&ew->conv2.w, &ew->conv2.b, a->conv1.out.data, a->conv2.out.data,
        a->col2.data, a->mm2.data, B, N3_C2_IC, N3_C1_OH, N3_C1_OW,
        N3_C2_OC, N3_C2_K, N3_C2_S, N3_C2_OH, N3_C2_OW, false, stream);
    if (a->conv2.saved_input.data)
        cudaMemcpyAsync(a->conv2.saved_input.data, a->conv1.out.data,
            (int64_t)B * N3_C2_IC * N3_C1_OH * N3_C1_OW * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);

    n3_embedding_kernel<<<grid_size(B * N3_PLAYER), BLOCK_SIZE, 0, stream>>>(
        a->embed_out.data, input.data, ew->embed_w.data, B, ew->obs_size);
    n3_concat_kernel<<<grid_size(B * N3_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->conv2.out.data, a->embed_out.data, input.data, B, ew->obs_size);

    puf_mm(&a->concat, &ew->proj_w, &a->out, stream);
    n3_bias_relu_kernel<<<grid_size(B * ew->hidden), BLOCK_SIZE, 0, stream>>>(
        a->out.data, ew->proj_b.data, B * ew->hidden, ew->hidden);
    return a->out;
}

static void nmmo3_encoder_backward(void* w, void* activations, PrecisionTensor grad, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    int B = grad.shape[0], H = ew->hidden;

    n3_relu_backward_kernel<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        grad.data, a->out.data, B * H);
    bias_grad_kernel<<<H, 256, 0, stream>>>(
        a->proj_bgrad.data, grad.data, B, H);
    puf_mm_tn(&grad, &a->concat, &a->proj_wgrad, stream);

    PrecisionTensor grad_concat = {.data = a->concat.data, .shape = {B, N3_CONCAT}};
    puf_mm_nn(&grad, &ew->proj_w, &grad_concat, stream);

    n3_concat_backward_conv_kernel<<<grid_size(B * N3_CONV_FLAT), BLOCK_SIZE, 0, stream>>>(
        a->conv2.grad.data, grad_concat.data, B);

    n3_conv_bias_grad_nchw<<<ew->conv2.OC, 256, 0, stream>>>(
        a->conv2.bgrad.data, a->conv2.grad.data,
        B, ew->conv2.OC, ew->conv2.OH * ew->conv2.OW);
    gemm_conv_backward(&ew->conv2.w, a->conv2.saved_input.data, a->conv2.grad.data,
        a->conv2.wgrad.data, a->conv1.grad.data,
        a->col2.data, a->mm2.data, B, N3_C2_IC, N3_C1_OH, N3_C1_OW,
        N3_C2_OC, N3_C2_K, N3_C2_S, N3_C2_OH, N3_C2_OW, stream);

    n3_relu_backward_kernel<<<grid_size(B * ew->conv1.OC * ew->conv1.OH * ew->conv1.OW), BLOCK_SIZE, 0, stream>>>(
        a->conv1.grad.data, a->conv1.out.data,
        B * ew->conv1.OC * ew->conv1.OH * ew->conv1.OW);
    n3_conv_bias_grad_nchw<<<ew->conv1.OC, 256, 0, stream>>>(
        a->conv1.bgrad.data, a->conv1.grad.data,
        B, ew->conv1.OC, ew->conv1.OH * ew->conv1.OW);
    gemm_conv_backward(&ew->conv1.w, a->conv1.saved_input.data, a->conv1.grad.data,
        a->conv1.wgrad.data, NULL,
        a->col1.data, a->mm1.data, B, N3_C1_IC, N3_MAP_H, N3_MAP_W,
        N3_C1_OC, N3_C1_K, N3_C1_S, N3_C1_OH, N3_C1_OW, stream);

    // Embedding backward: scatter-add from concat gradient into float buffer, then cast
    int embed_n = N3_EMBED_VOCAB * N3_EMBED_DIM;
    cudaMemsetAsync(a->embed_wgrad_f.data, 0, embed_n * sizeof(float), stream);
    n3_embedding_backward_kernel<<<grid_size(B * N3_PLAYER * N3_EMBED_DIM), BLOCK_SIZE, 0, stream>>>(
        a->embed_wgrad_f.data, grad_concat.data, a->saved_obs.data, B, ew->obs_size);
    n3_float_to_precision_kernel<<<grid_size(embed_n), BLOCK_SIZE, 0, stream>>>(
        a->embed_wgrad.data, a->embed_wgrad_f.data, embed_n);
}

static void nmmo3_encoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    conv_init_weights(&ew->conv1, seed, stream);
    conv_init_weights(&ew->conv2, seed, stream);
    auto init2d = [&](PrecisionTensor& t, int rows, int cols, float gain) {
        PrecisionTensor wt = {.data = t.data, .shape = {rows, cols}};
        puf_kaiming_init(&wt, gain, (*seed)++, stream);
    };
    puf_normal_init(&ew->embed_w, 1.0f, (*seed)++, stream);
    init2d(ew->proj_w, ew->hidden, N3_CONCAT, 1.0f);
    cudaMemsetAsync(ew->proj_b.data, 0, numel(ew->proj_b.shape) * sizeof(precision_t), stream);
}

static void nmmo3_encoder_reg_params(void* w, Allocator* alloc) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    conv_reg_params(&ew->conv1, alloc);
    conv_reg_params(&ew->conv2, alloc);
    ew->embed_w = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    ew->proj_w  = {.shape = {ew->hidden, N3_CONCAT}};
    ew->proj_b  = {.shape = {ew->hidden}};
    alloc_register(alloc,&ew->embed_w);
    alloc_register(alloc,&ew->proj_w);  alloc_register(alloc,&ew->proj_b);
}

static void nmmo3_encoder_reg_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    *a = {};
    a->multihot = {.shape = {B_TT, N3_MULTIHOT * N3_MAP_H * N3_MAP_W}};
    alloc_register(acts,&a->multihot);
    // Conv1 buffers
    a->conv1.out         = {.shape = {B_TT * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    a->conv1.grad        = {.shape = {B_TT * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    a->conv1.saved_input = {.shape = {B_TT * N3_C1_IC * N3_MAP_H * N3_MAP_W}};
    a->conv1.wgrad       = {.shape = {N3_C1_OC, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->conv1.bgrad       = {.shape = {N3_C1_OC}};
    alloc_register(acts,&a->conv1.out); alloc_register(acts,&a->conv1.grad); alloc_register(acts,&a->conv1.saved_input);
    alloc_register(grads,&a->conv1.wgrad); alloc_register(grads,&a->conv1.bgrad);
    a->col1 = {.shape = {B_TT * N3_C1_OH * N3_C1_OW, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->mm1  = {.shape = {B_TT * N3_C1_OH * N3_C1_OW, N3_C1_OC}};
    alloc_register(acts,&a->col1); alloc_register(acts,&a->mm1);
    // Conv2 buffers
    a->conv2.out         = {.shape = {B_TT * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    a->conv2.grad        = {.shape = {B_TT * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    a->conv2.saved_input = {.shape = {B_TT * N3_C2_IC * N3_C1_OH * N3_C1_OW}};
    a->conv2.wgrad       = {.shape = {N3_C2_OC, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->conv2.bgrad       = {.shape = {N3_C2_OC}};
    alloc_register(acts,&a->conv2.out); alloc_register(acts,&a->conv2.grad); alloc_register(acts,&a->conv2.saved_input);
    alloc_register(grads,&a->conv2.wgrad); alloc_register(grads,&a->conv2.bgrad);
    a->col2 = {.shape = {B_TT * N3_C2_OH * N3_C2_OW, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->mm2  = {.shape = {B_TT * N3_C2_OH * N3_C2_OW, N3_C2_OC}};
    alloc_register(acts,&a->col2); alloc_register(acts,&a->mm2);
    a->embed_out = {.shape = {B_TT, N3_PLAYER_EMBED}};
    a->concat    = {.shape = {B_TT, N3_CONCAT}};
    a->out       = {.shape = {B_TT, ew->hidden}};
    a->saved_obs = {.shape = {B_TT, ew->obs_size}};
    alloc_register(acts,&a->embed_out); alloc_register(acts,&a->concat);
    alloc_register(acts,&a->out);       alloc_register(acts,&a->saved_obs);
    a->embed_wgrad = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    a->embed_wgrad_f = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    a->proj_wgrad  = {.shape = {ew->hidden, N3_CONCAT}};
    a->proj_bgrad  = {.shape = {ew->hidden}};
    alloc_register(grads,&a->embed_wgrad);
    alloc_register(acts,&a->embed_wgrad_f);
    alloc_register(grads,&a->proj_wgrad);  alloc_register(grads,&a->proj_bgrad);
}

static void nmmo3_encoder_reg_rollout(void* w, void* activations, Allocator* alloc, int B) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    a->multihot = {.shape = {B, N3_MULTIHOT * N3_MAP_H * N3_MAP_W}};
    alloc_register(alloc,&a->multihot);
    a->conv1.out = {.shape = {B * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    alloc_register(alloc,&a->conv1.out);
    a->col1 = {.shape = {B * N3_C1_OH * N3_C1_OW, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->mm1  = {.shape = {B * N3_C1_OH * N3_C1_OW, N3_C1_OC}};
    alloc_register(alloc,&a->col1); alloc_register(alloc,&a->mm1);
    a->conv2.out = {.shape = {B * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    alloc_register(alloc,&a->conv2.out);
    a->col2 = {.shape = {B * N3_C2_OH * N3_C2_OW, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->mm2  = {.shape = {B * N3_C2_OH * N3_C2_OW, N3_C2_OC}};
    alloc_register(alloc,&a->col2); alloc_register(alloc,&a->mm2);
    a->embed_out = {.shape = {B, N3_PLAYER_EMBED}};
    a->concat    = {.shape = {B, N3_CONCAT}};
    a->out       = {.shape = {B, ew->hidden}};
    alloc_register(alloc,&a->embed_out); alloc_register(alloc,&a->concat); alloc_register(alloc,&a->out);
}

static void* nmmo3_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    return nmmo3_encoder_create(e->in_dim, e->out_dim);
}
static void nmmo3_encoder_free_weights(void* weights) { free(weights); }
static void nmmo3_encoder_free_activations(void* activations) { free(activations); }

// ---- PTCG native pointer model ------------------------------------------------

static constexpr int PTCG_MODEL_FLAT = 0;
static constexpr int PTCG_MODEL_POINTER_DOT_V1 = 1;
static constexpr int PTCG_STATE_DIM = 96;
static constexpr int PTCG_MAX_OPTIONS = 128;
static constexpr int PTCG_N_ACTIONS = PTCG_MAX_OPTIONS + 1;
static constexpr int PTCG_OPTION_DIM = 48;
static constexpr int PTCG_OBS_DIM = PTCG_STATE_DIM + PTCG_N_ACTIONS * PTCG_OPTION_DIM;
static constexpr float PTCG_BYTE_SCALE = 1.0f / 255.0f;
static constexpr float PTCG_INVALID_LOGIT = -1.0e30f;

struct PTCGStateEncoderWeights {
    PrecisionTensor weight;
    int obs_size, hidden;
};

struct PTCGStateEncoderActivations {
    PrecisionTensor state_input, out, wgrad_scratch;
};

__global__ void ptcg_extract_state_kernel(
        precision_t* __restrict__ dst,
        const precision_t* __restrict__ obs,
        int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * PTCG_STATE_DIM;
    if (idx >= total) {
        return;
    }
    int b = idx / PTCG_STATE_DIM;
    int c = idx % PTCG_STATE_DIM;
    dst[idx] = from_float(to_float(obs[b * obs_size + c]) * PTCG_BYTE_SCALE);
}

static PrecisionTensor ptcg_state_encoder_forward(
        void* w, void* activations, PrecisionTensor input, cudaStream_t stream) {
    PTCGStateEncoderWeights* ew = (PTCGStateEncoderWeights*)w;
    PTCGStateEncoderActivations* a = (PTCGStateEncoderActivations*)activations;
    int B = input.shape[0];
    ptcg_extract_state_kernel<<<grid_size(B * PTCG_STATE_DIM), BLOCK_SIZE, 0, stream>>>(
        a->state_input.data, input.data, B, ew->obs_size);
    puf_mm(&a->state_input, &ew->weight, &a->out, stream);
    return a->out;
}

static void ptcg_state_encoder_backward(
        void* w, void* activations, PrecisionTensor grad, cudaStream_t stream) {
    PTCGStateEncoderActivations* a = (PTCGStateEncoderActivations*)activations;
    puf_mm_tn(&grad, &a->state_input, &a->wgrad_scratch, stream);
}

static void ptcg_state_encoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    PTCGStateEncoderWeights* ew = (PTCGStateEncoderWeights*)w;
    PrecisionTensor wt = {
        .data = ew->weight.data,
        .shape = {ew->hidden, PTCG_STATE_DIM},
    };
    puf_kaiming_init(&wt, std::sqrt(2.0f), (*seed)++, stream);
}

static void ptcg_state_encoder_reg_params(void* w, Allocator* alloc) {
    PTCGStateEncoderWeights* ew = (PTCGStateEncoderWeights*)w;
    ew->weight = {.shape = {ew->hidden, PTCG_STATE_DIM}};
    alloc_register(alloc, &ew->weight);
}

static void ptcg_state_encoder_reg_train(
        void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    PTCGStateEncoderWeights* ew = (PTCGStateEncoderWeights*)w;
    PTCGStateEncoderActivations* a = (PTCGStateEncoderActivations*)activations;
    *a = {
        .state_input =   {.shape = {B_TT, PTCG_STATE_DIM}},
        .out =           {.shape = {B_TT, ew->hidden}},
        .wgrad_scratch = {.shape = {ew->hidden, PTCG_STATE_DIM}},
    };
    alloc_register(acts, &a->state_input);
    alloc_register(acts, &a->out);
    alloc_register(grads, &a->wgrad_scratch);
}

static void ptcg_state_encoder_reg_rollout(
        void* w, void* activations, Allocator* alloc, int B) {
    PTCGStateEncoderWeights* ew = (PTCGStateEncoderWeights*)w;
    PTCGStateEncoderActivations* a = (PTCGStateEncoderActivations*)activations;
    *a = {
        .state_input = {.shape = {B, PTCG_STATE_DIM}},
        .out =         {.shape = {B, ew->hidden}},
    };
    alloc_register(alloc, &a->state_input);
    alloc_register(alloc, &a->out);
}

static void* ptcg_state_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    PTCGStateEncoderWeights* ew = (PTCGStateEncoderWeights*)calloc(1, sizeof(PTCGStateEncoderWeights));
    ew->obs_size = e->in_dim;
    ew->hidden = e->out_dim;
    return ew;
}

static void ptcg_state_encoder_free_weights(void* weights) { free(weights); }
static void ptcg_state_encoder_free_activations(void* activations) { free(activations); }

struct PTCGPointerDecoderWeights {
    PrecisionTensor wq, wk, wb, wv;
    int hidden_dim, output_dim, option_embed_size;
    bool continuous;
};

struct PTCGPointerDecoderActivations {
    PrecisionTensor out, saved_hidden, option_input, q, k, bias, value;
    PrecisionTensor grad_hidden, qgrad, kgrad, dlogits_rows, value_grad, value_hidden_grad;
    PrecisionTensor wq_grad, wk_grad, wb_grad, wv_grad;
};

__global__ void ptcg_extract_options_kernel(
        precision_t* __restrict__ dst,
        const precision_t* __restrict__ obs,
        int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * PTCG_N_ACTIONS * PTCG_OPTION_DIM;
    if (idx >= total) {
        return;
    }
    int c = idx % PTCG_OPTION_DIM;
    int row = idx / PTCG_OPTION_DIM;
    int b = row / PTCG_N_ACTIONS;
    int option = row % PTCG_N_ACTIONS;
    int obs_idx = b * obs_size + PTCG_STATE_DIM + option * PTCG_OPTION_DIM + c;
    dst[idx] = from_float(to_float(obs[obs_idx]) * PTCG_BYTE_SCALE);
}

__global__ void ptcg_pointer_logits_kernel(
        precision_t* __restrict__ out,
        const precision_t* __restrict__ q,
        const precision_t* __restrict__ k,
        const precision_t* __restrict__ bias,
        const precision_t* __restrict__ value,
        const precision_t* __restrict__ option_input,
        int B, int E) {
    int logits_total = B * PTCG_N_ACTIONS;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < logits_total) {
        int b = idx / PTCG_N_ACTIONS;
        float dot = 0.0f;
        for (int e = 0; e < E; e++) {
            dot += to_float(q[b * E + e]) * to_float(k[idx * E + e]);
        }
        float logit = dot * rsqrtf((float)E) + to_float(bias[idx]);
        int option_base = idx * PTCG_OPTION_DIM;
        bool valid_row = to_float(option_input[option_base + 0]) > 0.0f;
        bool is_stop = to_float(option_input[option_base + 1]) > 0.0f;
        bool already_selected = to_float(option_input[option_base + 2]) > 0.0f;
        float select_min = to_float(option_input[option_base + 6]) * 255.0f;
        float select_max = to_float(option_input[option_base + 7]) * 255.0f;
        float selected_count = to_float(option_input[option_base + 8]) * 255.0f;
        bool can_choose_more = selected_count < select_max;
        bool legal_option = valid_row && !is_stop && !already_selected && can_choose_more;
        bool legal_stop = valid_row && is_stop && selected_count >= select_min;
        if (!(legal_option || legal_stop)) {
            logit = PTCG_INVALID_LOGIT;
        }
        out[b * (PTCG_N_ACTIONS + 1) + (idx % PTCG_N_ACTIONS)] = from_float(logit);
    }
    if (idx < B) {
        out[idx * (PTCG_N_ACTIONS + 1) + PTCG_N_ACTIONS] = value[idx];
    }
}

__global__ void ptcg_pointer_qgrad_kernel(
        precision_t* __restrict__ qgrad,
        const float* __restrict__ grad_logits,
        const precision_t* __restrict__ k,
        int B, int E) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * E;
    if (idx >= total) {
        return;
    }
    int b = idx / E;
    int e = idx % E;
    float acc = 0.0f;
    for (int i = 0; i < PTCG_N_ACTIONS; i++) {
        int row = b * PTCG_N_ACTIONS + i;
        acc += grad_logits[row] * to_float(k[row * E + e]);
    }
    qgrad[idx] = from_float(acc * rsqrtf((float)E));
}

__global__ void ptcg_pointer_kgrad_dlogits_kernel(
        precision_t* __restrict__ kgrad,
        precision_t* __restrict__ dlogits_rows,
        const float* __restrict__ grad_logits,
        const precision_t* __restrict__ q,
        int B, int E) {
    int total = B * PTCG_N_ACTIONS * E;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < total) {
        int e = idx % E;
        int row = idx / E;
        int b = row / PTCG_N_ACTIONS;
        float grad = grad_logits[row];
        kgrad[idx] = from_float(grad * to_float(q[b * E + e]) * rsqrtf((float)E));
    }
    int rows = B * PTCG_N_ACTIONS;
    if (idx < rows) {
        dlogits_rows[idx] = from_float(grad_logits[idx]);
    }
}

__global__ void ptcg_pointer_value_grad_kernel(
        precision_t* __restrict__ value_grad,
        const float* __restrict__ grad_value,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < B) {
        value_grad[idx] = from_float(grad_value[idx]);
    }
}

static PrecisionTensor ptcg_pointer_decoder_forward(
        void* w, void* activations, PrecisionTensor hidden, PrecisionTensor obs, cudaStream_t stream) {
    PTCGPointerDecoderWeights* dw = (PTCGPointerDecoderWeights*)w;
    PTCGPointerDecoderActivations* a = (PTCGPointerDecoderActivations*)activations;
    int B = hidden.shape[0];
    if (a->saved_hidden.data) {
        puf_copy(&a->saved_hidden, &hidden, stream);
    }
    ptcg_extract_options_kernel<<<grid_size(B * PTCG_N_ACTIONS * PTCG_OPTION_DIM), BLOCK_SIZE, 0, stream>>>(
        a->option_input.data, obs.data, B, obs.shape[1]);
    puf_mm(&hidden, &dw->wq, &a->q, stream);
    puf_mm(&a->option_input, &dw->wk, &a->k, stream);
    puf_mm(&a->option_input, &dw->wb, &a->bias, stream);
    puf_mm(&hidden, &dw->wv, &a->value, stream);
    ptcg_pointer_logits_kernel<<<grid_size(B * PTCG_N_ACTIONS), BLOCK_SIZE, 0, stream>>>(
        a->out.data, a->q.data, a->k.data, a->bias.data, a->value.data,
        a->option_input.data, B, dw->option_embed_size);
    return a->out;
}

static void ptcg_pointer_decoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    PTCGPointerDecoderWeights* dw = (PTCGPointerDecoderWeights*)w;
    puf_kaiming_init(&dw->wq, 1.0f, (*seed)++, stream);
    puf_kaiming_init(&dw->wk, 1.0f, (*seed)++, stream);
    puf_kaiming_init(&dw->wb, 0.01f, (*seed)++, stream);
    puf_kaiming_init(&dw->wv, 1.0f, (*seed)++, stream);
}

static void ptcg_pointer_decoder_reg_params(void* w, Allocator* alloc) {
    PTCGPointerDecoderWeights* dw = (PTCGPointerDecoderWeights*)w;
    int H = dw->hidden_dim, E = dw->option_embed_size;
    dw->wq = {.shape = {E, H}};
    dw->wk = {.shape = {E, PTCG_OPTION_DIM}};
    dw->wb = {.shape = {1, PTCG_OPTION_DIM}};
    dw->wv = {.shape = {1, H}};
    alloc_register(alloc, &dw->wq);
    alloc_register(alloc, &dw->wk);
    alloc_register(alloc, &dw->wb);
    alloc_register(alloc, &dw->wv);
}

static void ptcg_pointer_decoder_reg_train(
        void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    PTCGPointerDecoderWeights* dw = (PTCGPointerDecoderWeights*)w;
    PTCGPointerDecoderActivations* a = (PTCGPointerDecoderActivations*)activations;
    int H = dw->hidden_dim, E = dw->option_embed_size;
    *a = {
        .out =                {.shape = {B_TT, PTCG_N_ACTIONS + 1}},
        .saved_hidden =       {.shape = {B_TT, H}},
        .option_input =       {.shape = {B_TT * PTCG_N_ACTIONS, PTCG_OPTION_DIM}},
        .q =                  {.shape = {B_TT, E}},
        .k =                  {.shape = {B_TT * PTCG_N_ACTIONS, E}},
        .bias =               {.shape = {B_TT * PTCG_N_ACTIONS, 1}},
        .value =              {.shape = {B_TT, 1}},
        .grad_hidden =        {.shape = {B_TT, H}},
        .qgrad =              {.shape = {B_TT, E}},
        .kgrad =              {.shape = {B_TT * PTCG_N_ACTIONS, E}},
        .dlogits_rows =       {.shape = {B_TT * PTCG_N_ACTIONS, 1}},
        .value_grad =         {.shape = {B_TT, 1}},
        .value_hidden_grad =  {.shape = {B_TT, H}},
        .wq_grad =            {.shape = {E, H}},
        .wk_grad =            {.shape = {E, PTCG_OPTION_DIM}},
        .wb_grad =            {.shape = {1, PTCG_OPTION_DIM}},
        .wv_grad =            {.shape = {1, H}},
    };
    alloc_register(acts, &a->out);
    alloc_register(acts, &a->saved_hidden);
    alloc_register(acts, &a->option_input);
    alloc_register(acts, &a->q);
    alloc_register(acts, &a->k);
    alloc_register(acts, &a->bias);
    alloc_register(acts, &a->value);
    alloc_register(acts, &a->grad_hidden);
    alloc_register(acts, &a->qgrad);
    alloc_register(acts, &a->kgrad);
    alloc_register(acts, &a->dlogits_rows);
    alloc_register(acts, &a->value_grad);
    alloc_register(acts, &a->value_hidden_grad);
    alloc_register(grads, &a->wq_grad);
    alloc_register(grads, &a->wk_grad);
    alloc_register(grads, &a->wb_grad);
    alloc_register(grads, &a->wv_grad);
}

static void ptcg_pointer_decoder_reg_rollout(
        void* w, void* activations, Allocator* alloc, int B) {
    PTCGPointerDecoderWeights* dw = (PTCGPointerDecoderWeights*)w;
    PTCGPointerDecoderActivations* a = (PTCGPointerDecoderActivations*)activations;
    int E = dw->option_embed_size;
    *a = {
        .out =          {.shape = {B, PTCG_N_ACTIONS + 1}},
        .option_input = {.shape = {B * PTCG_N_ACTIONS, PTCG_OPTION_DIM}},
        .q =            {.shape = {B, E}},
        .k =            {.shape = {B * PTCG_N_ACTIONS, E}},
        .bias =         {.shape = {B * PTCG_N_ACTIONS, 1}},
        .value =        {.shape = {B, 1}},
    };
    alloc_register(alloc, &a->out);
    alloc_register(alloc, &a->option_input);
    alloc_register(alloc, &a->q);
    alloc_register(alloc, &a->k);
    alloc_register(alloc, &a->bias);
    alloc_register(alloc, &a->value);
}

static void* ptcg_pointer_decoder_create_weights(void* self) {
    Decoder* d = (Decoder*)self;
    PTCGPointerDecoderWeights* dw = (PTCGPointerDecoderWeights*)calloc(1, sizeof(PTCGPointerDecoderWeights));
    dw->hidden_dim = d->hidden_dim;
    dw->output_dim = d->output_dim;
    dw->continuous = d->continuous;
    dw->option_embed_size = d->option_embed_size;
    return dw;
}

static void ptcg_pointer_decoder_free_weights(void* weights) { free(weights); }
static void ptcg_pointer_decoder_free_activations(void* activations) { free(activations); }

static PrecisionTensor ptcg_pointer_decoder_backward(
        void* w, void* activations,
        FloatTensor grad_logits, FloatTensor grad_logstd, FloatTensor grad_value,
        cudaStream_t stream) {
    (void)grad_logstd;
    PTCGPointerDecoderWeights* dw = (PTCGPointerDecoderWeights*)w;
    PTCGPointerDecoderActivations* a = (PTCGPointerDecoderActivations*)activations;
    int B = a->saved_hidden.shape[0], H = dw->hidden_dim, E = dw->option_embed_size;

    ptcg_pointer_qgrad_kernel<<<grid_size(B * E), BLOCK_SIZE, 0, stream>>>(
        a->qgrad.data, grad_logits.data, a->k.data, B, E);
    ptcg_pointer_kgrad_dlogits_kernel<<<grid_size(B * PTCG_N_ACTIONS * E), BLOCK_SIZE, 0, stream>>>(
        a->kgrad.data, a->dlogits_rows.data, grad_logits.data, a->q.data, B, E);
    ptcg_pointer_value_grad_kernel<<<grid_size(B), BLOCK_SIZE, 0, stream>>>(
        a->value_grad.data, grad_value.data, B);

    puf_mm_tn(&a->qgrad, &a->saved_hidden, &a->wq_grad, stream);
    puf_mm_nn(&a->qgrad, &dw->wq, &a->grad_hidden, stream);
    puf_mm_tn(&a->kgrad, &a->option_input, &a->wk_grad, stream);
    puf_mm_tn(&a->dlogits_rows, &a->option_input, &a->wb_grad, stream);
    puf_mm_tn(&a->value_grad, &a->saved_hidden, &a->wv_grad, stream);
    puf_mm_nn(&a->value_grad, &dw->wv, &a->value_hidden_grad, stream);
    add_kernel<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        a->grad_hidden.data, a->value_hidden_grad.data, B * H);
    return a->grad_hidden;
}

// Override encoder/decoder vtables for known ocean environments. No-op for unknown envs.
static void create_custom_encoder(const std::string& env_name, int model_type, Encoder* enc) {
    if (env_name == "nmmo3") {
        *enc = Encoder{
            .forward = nmmo3_encoder_forward,
            .backward = nmmo3_encoder_backward,
            .init_weights = nmmo3_encoder_init_weights,
            .reg_params = nmmo3_encoder_reg_params,
            .reg_train = nmmo3_encoder_reg_train,
            .reg_rollout = nmmo3_encoder_reg_rollout,
            .create_weights = nmmo3_encoder_create_weights,
            .free_weights = nmmo3_encoder_free_weights,
            .free_activations = nmmo3_encoder_free_activations,
            .in_dim = enc->in_dim, .out_dim = enc->out_dim,
            .activation_size = sizeof(NMMO3EncoderActivations),
        };
    } else if (env_name == "ptcg" && model_type == PTCG_MODEL_POINTER_DOT_V1) {
        if (enc->in_dim != PTCG_OBS_DIM) {
            fprintf(stderr, "PTCG pointer encoder expected obs dim %d, got %d\n", PTCG_OBS_DIM, enc->in_dim);
        }
        *enc = Encoder{
            .forward = ptcg_state_encoder_forward,
            .backward = ptcg_state_encoder_backward,
            .init_weights = ptcg_state_encoder_init_weights,
            .reg_params = ptcg_state_encoder_reg_params,
            .reg_train = ptcg_state_encoder_reg_train,
            .reg_rollout = ptcg_state_encoder_reg_rollout,
            .create_weights = ptcg_state_encoder_create_weights,
            .free_weights = ptcg_state_encoder_free_weights,
            .free_activations = ptcg_state_encoder_free_activations,
            .in_dim = enc->in_dim, .out_dim = enc->out_dim,
            .activation_size = sizeof(PTCGStateEncoderActivations),
        };
    }
}

static void create_custom_decoder(
        const std::string& env_name, int model_type, int option_embed_size, Decoder* dec) {
    if (env_name == "ptcg" && model_type == PTCG_MODEL_POINTER_DOT_V1) {
        if (dec->continuous || dec->output_dim != PTCG_N_ACTIONS) {
            fprintf(stderr, "PTCG pointer decoder expected one discrete head of %d actions, got output_dim=%d continuous=%d\n",
                PTCG_N_ACTIONS, dec->output_dim, (int)dec->continuous);
        }
        if (option_embed_size <= 0) {
            fprintf(stderr, "PTCG pointer decoder requires positive option_embed_size, got %d\n", option_embed_size);
        }
        *dec = Decoder{
            .forward = ptcg_pointer_decoder_forward,
            .backward = ptcg_pointer_decoder_backward,
            .init_weights = ptcg_pointer_decoder_init_weights,
            .reg_params = ptcg_pointer_decoder_reg_params,
            .reg_train = ptcg_pointer_decoder_reg_train,
            .reg_rollout = ptcg_pointer_decoder_reg_rollout,
            .create_weights = ptcg_pointer_decoder_create_weights,
            .free_weights = ptcg_pointer_decoder_free_weights,
            .free_activations = ptcg_pointer_decoder_free_activations,
            .hidden_dim = dec->hidden_dim,
            .output_dim = dec->output_dim,
            .continuous = dec->continuous,
            .activation_size = sizeof(PTCGPointerDecoderActivations),
            .option_embed_size = option_embed_size,
        };
    }
}
