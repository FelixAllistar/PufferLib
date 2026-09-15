// Shared two-layer MLP primitive (derived from Kaggriculture's tested implementation).
#pragma once
struct PKMLPWeights {
    int in, mid, out;
    bool relu_out;
    float output_gain;
    PrecisionTensor w1, w2;
};
struct PKMLPActs {
    PrecisionTensor input_aug, mid, mid_aug, out;
    PrecisionTensor grad_out, grad_mid_aug, grad_mid, grad_input_aug, grad_input;
    PrecisionTensor dw1, dw2;
};

__global__ static void pk_nn_zero_padding(precision_t* w, int rows, int features) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row < rows) for (int c = features; c < PK_AUG(features); c++)
        w[row * PK_AUG(features) + c] = from_float(0.0f);
}

static void pk_nn_mlp_params(PKMLPWeights* w, Allocator* alloc) {
    w->w1 = {.shape = {w->mid, PK_AUG(w->in)}};
    w->w2 = {.shape = {w->out, PK_AUG(w->mid)}};
    alloc_register(alloc, &w->w1);
    alloc_register(alloc, &w->w2);
}

static void pk_nn_mlp_init(PKMLPWeights* w, ulong* seed, cudaStream_t stream) {
    puf_kaiming_init(&w->w1, sqrtf(2.0f * PK_AUG(w->in) / w->in), (*seed)++, stream);
    puf_kaiming_init(&w->w2, w->output_gain * sqrtf((float)PK_AUG(w->mid) / w->mid), (*seed)++, stream);
    pk_nn_zero_padding<<<grid_size(w->mid), BLOCK_SIZE, 0, stream>>>(w->w1.data, w->mid, w->in);
    pk_nn_zero_padding<<<grid_size(w->out), BLOCK_SIZE, 0, stream>>>(w->w2.data, w->out, w->mid);
}

static void pk_nn_mlp_acts(PKMLPWeights* w, PKMLPActs* a, Allocator* acts,
        Allocator* grads, int rows, bool input_grad) {
    *a = {};
    a->input_aug = {.shape = {rows, PK_AUG(w->in)}};
    a->mid = {.shape = {rows, w->mid}};
    a->mid_aug = {.shape = {rows, PK_AUG(w->mid)}};
    a->out = {.shape = {rows, w->out}};
    alloc_register(acts, &a->input_aug);
    alloc_register(acts, &a->mid);
    alloc_register(acts, &a->mid_aug);
    alloc_register(acts, &a->out);
    if (!grads) return;
    a->grad_out = {.shape = {rows, w->out}};
    a->grad_mid_aug = {.shape = {rows, PK_AUG(w->mid)}};
    a->grad_mid = {.shape = {rows, w->mid}};
    alloc_register(acts, &a->grad_out);
    alloc_register(acts, &a->grad_mid_aug);
    alloc_register(acts, &a->grad_mid);
    if (input_grad) {
        a->grad_input_aug = {.shape = {rows, PK_AUG(w->in)}};
        a->grad_input = {.shape = {rows, w->in}};
        alloc_register(acts, &a->grad_input_aug);
        alloc_register(acts, &a->grad_input);
    }
    // Parameter and gradient registration order MUST match.
    a->dw1 = {.shape = {w->mid, PK_AUG(w->in)}};
    a->dw2 = {.shape = {w->out, PK_AUG(w->mid)}};
    alloc_register(grads, &a->dw1);
    alloc_register(grads, &a->dw2);
}

__global__ static void pk_nn_pack_aug(precision_t* dst, const precision_t* src,
        int rows, int cols, bool relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * PK_AUG(cols)) return;
    int r = idx / PK_AUG(cols), c = idx % PK_AUG(cols);
    float x = c == cols ? 1.0f : c > cols ? 0.0f : to_float(src[r * cols + c]);
    dst[idx] = from_float(relu && x < 0.0f ? 0.0f : x);
}

__global__ static void pk_nn_relu(precision_t* values, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n && to_float(values[idx]) < 0.0f) values[idx] = from_float(0.0f);
}

static PrecisionTensor pk_nn_mlp_forward(PKMLPWeights* w, PKMLPActs* a, cudaStream_t stream) {
    int rows = a->out.shape[0];
    puf_mm(&a->input_aug, &w->w1, &a->mid, stream);
    pk_nn_pack_aug<<<grid_size(rows * PK_AUG(w->mid)), BLOCK_SIZE, 0, stream>>>(
        a->mid_aug.data, a->mid.data, rows, w->mid, true);
    puf_mm(&a->mid_aug, &w->w2, &a->out, stream);
    if (w->relu_out) pk_nn_relu<<<grid_size(rows * w->out), BLOCK_SIZE, 0, stream>>>(a->out.data, rows * w->out);
    return a->out;
}

__global__ static void pk_nn_relu_backward(precision_t* dst, const precision_t* grad,
        const precision_t* output, int n, bool relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) dst[idx] = from_float(relu && to_float(output[idx]) <= 0.0f ? 0.0f : to_float(grad[idx]));
}

__global__ static void pk_nn_strip_aug(precision_t* dst, const precision_t* grad,
        const precision_t* output_aug, int rows, int cols, bool relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= rows * cols) return;
    int source = (idx / cols) * PK_AUG(cols) + idx % cols;
    dst[idx] = from_float(relu && to_float(output_aug[source]) <= 0.0f ? 0.0f : to_float(grad[source]));
}

static PrecisionTensor pk_nn_mlp_backward(PKMLPWeights* w, PKMLPActs* a,
        PrecisionTensor grad, cudaStream_t stream) {
    int rows = a->out.shape[0];
    pk_nn_relu_backward<<<grid_size(rows * w->out), BLOCK_SIZE, 0, stream>>>(
        a->grad_out.data, grad.data, a->out.data, rows * w->out, w->relu_out);
    // These GEMMs stay on the caller's stream: no cross-stream cache reuse.
    puf_mm_tn(&a->grad_out, &a->mid_aug, &a->dw2, stream);
    puf_mm_nn(&a->grad_out, &w->w2, &a->grad_mid_aug, stream);
    pk_nn_strip_aug<<<grid_size(rows * w->mid), BLOCK_SIZE, 0, stream>>>(
        a->grad_mid.data, a->grad_mid_aug.data, a->mid_aug.data, rows, w->mid, true);
    puf_mm_tn(&a->grad_mid, &a->input_aug, &a->dw1, stream);
    if (a->grad_input.data) {
        puf_mm_nn(&a->grad_mid, &w->w1, &a->grad_input_aug, stream);
        pk_nn_strip_aug<<<grid_size(rows * w->in), BLOCK_SIZE, 0, stream>>>(
            a->grad_input.data, a->grad_input_aug.data, nullptr, rows, w->in, false);
    }
    return a->grad_input;
}
