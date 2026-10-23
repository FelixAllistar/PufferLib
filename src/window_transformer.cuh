// Causal window Transformer network: an alternative to MinGRU (policy.network = window).
//
// Memory is the agent's last K encoder outputs, kept in the ordinary recurrent state tensor
// (shape {K, agents, hidden}; policy.num_layers is set to K). Each step attends over
// K + 1 tokens, the window plus the current input, and the current token's output feeds the
// decoder through a residual. Rows the trainer zeroed (episode resets) and rows from an
// earlier episode are empty slots. Training rebuilds exactly the window each step saw in
// rollout, from the segment's saved starting state plus earlier steps of the segment, so
// rollout and training compute the same function. The saved state is a constant: no
// gradient flows into it.
//
// Token input: [embedding (H) | valid (1) | age one-hot (K+1) | bias (1) | zero pad], one
// shared projection to width D, then src/native_transformer.cuh (pre-LN, dense attention).
#pragma once
#include "native_transformer.cuh"

__host__ __device__ static inline int wt_aug(int n) { return (n + 8) & ~7; }
static inline int wt_token_in(int H, int K) { return H + 1 + (K + 1); }
static inline void wt_check(const char* what) { puf_transformer::check(cudaGetLastError(), what); }

struct WindowWeights {
    int H, K, D, layers, heads;
    puf_transformer::Config config;
    int count;            // Transformer parameter tensors
    Prec in_w;            // (D, AUG(H + K + 2)): token projection with bias column
    Prec* params;         // [count]
    Prec out_w;           // (H, AUG(D)): output projection with bias column
};

struct WindowActivations {
    puf_transformer::Model<precision_t>* model;
    int rows, steps;                      // samples per call, horizon (1 in rollout)
    Prec tokens_in;                       // (rows*(K+1), AUG(H+K+2))
    Prec valid;                           // (rows*(K+1)) 0/1, kept for backward
    Prec pooled, pooled_aug, out;         // (rows, D), (rows, AUG(D)), (rows, H)
    int* last_reset;                      // (rows) latest in-segment reset step, -1 if none
    // Training only
    Prec* grads; Float* grads32;          // Transformer parameter gradients
    Prec d_in_w, d_out_w;
    Prec g_pooled_aug, g_tokens_in, g_x;
};

static Prec wt_shape(puf_transformer::ParameterSpec s) {
    return s.rows == 1 ? Prec{.shape = {s.cols}} : Prec{.shape = {s.rows, s.cols}};
}

static void* window_create_weights(void* self) {
    Network* n = (Network*)self;
    auto* w = new WindowWeights{};
    w->H = n->hidden; w->K = n->num_layers; w->D = n->window_width;
    w->layers = n->window_layers; w->heads = n->heads;
    if (w->K < 1 || w->K > 256 || w->D < 8 || w->D % 8 || w->layers < 1 || w->heads < 1 || w->D % w->heads) {
        fprintf(stderr, "policy.network=window requires window length (num_layers) 1-256, "
            "window_width a multiple of 8 divisible by window_heads, window_layers >= 1\n");
        exit(1);
    }
    w->config = {1, w->K + 1, w->D, w->layers, w->heads, 4 * w->D};
    w->config.validate();
    w->count = 12 * w->layers + 2;
    w->params = new Prec[w->count]{};
    return w;
}

static void window_reg_params(void* weights, Allocator* alloc) {
    auto* w = (WindowWeights*)weights;
    w->in_w = {.shape = {w->D, wt_aug(wt_token_in(w->H, w->K))}};
    alloc_register(alloc, &w->in_w);
    auto specs = puf_transformer::parameter_specs(w->config);
    for (int i = 0; i < w->count; ++i) {
        w->params[i] = wt_shape(specs[i]);
        alloc_register(alloc, &w->params[i]);
    }
    w->out_w = {.shape = {w->H, wt_aug(w->D)}};
    alloc_register(alloc, &w->out_w);
}

__global__ static void wt_zero_columns(precision_t* w, int rows, int cols, int from) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < rows * cols && i % cols > from) w[i] = from_float(0.f);
}

static void window_init_weights(void* weights, ulong* seed, cudaStream_t stream) {
    auto* w = (WindowWeights*)weights;
    int in_cols = wt_aug(wt_token_in(w->H, w->K)), out_cols = wt_aug(w->D);
    puf_kaiming_init(&w->in_w, 1.0f, (*seed)++, stream);
    wt_zero_columns<<<grid_size(w->D * in_cols), BLOCK_SIZE, 0, stream>>>(
        w->in_w.data, w->D, in_cols, wt_token_in(w->H, w->K));
        wt_check("wt_zero_columns");
    std::mt19937 rng((*seed)++);
    auto specs = puf_transformer::parameter_specs(w->config);
    for (int i = 0; i < w->count; ++i) {
        auto s = specs[i];
        float bound = sqrtf(6.f / (s.rows + s.cols));
        std::uniform_real_distribution<float> distribution(-bound, bound);
        std::vector<precision_t> values((size_t)s.rows * s.cols);
        for (auto& v : values) v = from_float(s.initialization == 1 ? 1.f : s.initialization == 2 ? 0.f : distribution(rng));
        puf_transformer::check(cudaMemcpyAsync(w->params[i].data, values.data(),
            values.size() * sizeof(precision_t), cudaMemcpyHostToDevice, stream), "initialize window Transformer");
        puf_transformer::check(cudaStreamSynchronize(stream), "finish window Transformer initialization");
    }
    // Small output projection: training starts close to the feed-forward residual.
    puf_kaiming_init(&w->out_w, 0.1f, (*seed)++, stream);
    wt_zero_columns<<<grid_size(w->H * out_cols), BLOCK_SIZE, 0, stream>>>(
        w->out_w.data, w->H, out_cols, w->D);
        wt_check("wt_zero_columns");
}

static void window_register(WindowWeights* w, WindowActivations* a, Allocator* acts, int rows, int steps) {
    int T = w->K + 1, in_cols = wt_aug(wt_token_in(w->H, w->K));
    auto config = w->config; config.batch = rows;
    a->model = new puf_transformer::Model<precision_t>(config, 17, false);
    a->rows = rows; a->steps = steps;
    a->tokens_in = {.shape = {rows * T, in_cols}};
    a->valid = {.shape = {rows * T}};
    a->pooled = {.shape = {rows, w->D}};
    a->pooled_aug = {.shape = {rows, wt_aug(w->D)}};
    a->out = {.shape = {rows, w->H}};
    alloc_register(acts, &a->tokens_in); alloc_register(acts, &a->valid);
    alloc_register(acts, &a->pooled); alloc_register(acts, &a->pooled_aug); alloc_register(acts, &a->out);
    puf_transformer::check(cudaMalloc(&a->last_reset, rows * sizeof(int)), "window reset buffer");
}

static void window_reg_rollout(void* weights, void* activations, Allocator* acts, int batch) {
    window_register((WindowWeights*)weights, (WindowActivations*)activations, acts, batch, 1);
}

static void window_reg_train(void* weights, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    auto* w = (WindowWeights*)weights;
    auto* a = (WindowActivations*)activations;
    window_register(w, a, acts, B_TT, 0);   // steps (horizon) is read from x at forward_train
    // Registration order matches window_reg_params: in_w, Transformer, out_w.
    a->d_in_w = {.shape = {w->D, wt_aug(wt_token_in(w->H, w->K))}};
    alloc_register(grads, &a->d_in_w);
    auto specs = puf_transformer::parameter_specs(w->config);
    a->grads = new Prec[w->count]{};
    a->grads32 = new Float[w->count]{};
    for (int i = 0; i < w->count; ++i) {
        a->grads[i] = wt_shape(specs[i]);
        alloc_register(grads, &a->grads[i]);
        if (USE_BF16) { a->grads32[i] = {.shape = {specs[i].rows * specs[i].cols}}; alloc_register(acts, &a->grads32[i]); }
    }
    a->d_out_w = {.shape = {w->H, wt_aug(w->D)}};
    alloc_register(grads, &a->d_out_w);
    int T = w->K + 1;
    a->g_pooled_aug = {.shape = {B_TT, wt_aug(w->D)}};
    a->g_tokens_in = {.shape = {B_TT * T, wt_aug(wt_token_in(w->H, w->K))}};
    a->g_x = {.shape = {B_TT, w->H}};
    alloc_register(acts, &a->g_pooled_aug); alloc_register(acts, &a->g_tokens_in); alloc_register(acts, &a->g_x);
}

// One thread per segment: the latest step <= t whose terminal starts a new episode.
__global__ static void wt_last_reset(int* last_reset, const precision_t* terminals, int B, int TT) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= B) return;
    int reset = -1;
    for (int t = 0; t < TT; ++t) {
        if (terminals && to_float(terminals[b * TT + t]) != 0.f) reset = t;
        last_reset[b * TT + t] = reset;
    }
}

// One warp per token. Sample n = (b, t). Token k < K holds the input of age K - k (step
// t - (K - k)): an earlier step of this segment, or a row of the saved window when that
// step precedes the segment. Token K is the current input. A token is valid when it is the
// current input, or it belongs to the current episode and its row is not all zero.
__global__ static void wt_build_tokens(precision_t* dst, precision_t* valid_out, const precision_t* x,
        const precision_t* state, const int* last_reset, int N, int TT, int H, int K,
        long state_agents, int agent_off, int in_cols) {
    int token = int((size_t(blockIdx.x) * blockDim.x + threadIdx.x) >> 5), lane = threadIdx.x & 31;
    int T = K + 1;
    if (token >= N * T) return;
    int n = token / T, k = token % T, b = n / TT, t = n % TT;
    const precision_t* src = nullptr;
    bool episode = true;
    if (k == K) src = x + (size_t)n * H;
    else {
        int s = t - (K - k), reset = last_reset[n];
        if (s >= 0) { src = x + (size_t)(b * TT + s) * H; episode = s >= reset; }
        else { src = state + ((size_t)(K + s) * state_agents + agent_off + b) * H; episode = reset < 0; }
    }
    bool nonzero = false;
    if (episode) for (int j = lane; j < H; j += 32) nonzero |= to_float(src[j]) != 0.f;
    bool ok = k == K || (episode && __any_sync(0xffffffff, nonzero));
    precision_t* row = dst + (size_t)token * in_cols;
    for (int j = lane; j < in_cols; j += 32) {
        float v = 0.f;
        if (j < H) v = ok ? to_float(src[j]) : 0.f;
        else if (j == H) v = ok;
        else if (j == H + 1 + k) v = 1.f;
        else if (j == H + 1 + T) v = 1.f;   // bias column
        row[j] = from_float(v);
    }
    if (lane == 0) valid_out[token] = from_float(ok ? 1.f : 0.f);
}

__global__ static void wt_pool_aug(precision_t* pooled, precision_t* pooled_aug, const precision_t* tokens,
        int N, int T, int D) {
    int i = blockIdx.x * blockDim.x + threadIdx.x, cols = wt_aug(D);
    if (i >= N * cols) return;
    int n = i / cols, d = i % cols;
    float v = d < D ? to_float(tokens[((size_t)n * T + T - 1) * D + d]) : d == D ? 1.f : 0.f;
    pooled_aug[i] = from_float(v);
    if (d < D) pooled[n * D + d] = from_float(v);
}

__global__ static void wt_add_residual(precision_t* out, const precision_t* x, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = from_float(to_float(out[i]) + to_float(x[i]));
}

// Rollout memory update: drop the oldest row, append the current input as the newest.
__global__ static void wt_shift_state(precision_t* state, const precision_t* x, int B, int H, int K) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= B * H) return;
    int b = i / H, h = i % H;
    for (int k = 0; k + 1 < K; ++k)
        state[((size_t)k * B + b) * H + h] = state[((size_t)(k + 1) * B + b) * H + h];
    state[((size_t)(K - 1) * B + b) * H + h] = x[i];
}

static Prec window_run(WindowWeights* w, WindowActivations* a, Prec x, Prec state, const precision_t* terminals,
        int agent_off, int TT, cudaStream_t stream) {
    int N = a->rows, T = w->K + 1, in_cols = wt_aug(wt_token_in(w->H, w->K));
    auto& m = *a->model;
    auto params = m.parameters();
    m.set_stream(stream);
    for (int i = 0; i < w->count; ++i) {
        size_t count = numel(w->params[i].shape);
        params[i]->value.borrow(w->params[i].data, count);
        if (a->grads) params[i]->gradient.borrow(USE_BF16 ? a->grads32[i].data : (float*)a->grads[i].data, count);
    }
    wt_last_reset<<<grid_size(N / TT), BLOCK_SIZE, 0, stream>>>(a->last_reset, terminals, N / TT, TT);
    wt_check("wt_last_reset");
    wt_build_tokens<<<puf_transformer::warp_rows_grid(N * T), puf_transformer::kWarpRowThreads, 0, stream>>>(
        a->tokens_in.data, a->valid.data, x.data, state.data, a->last_reset, N, TT, w->H, w->K,
        state.shape[1], agent_off, in_cols);
    wt_check("wt_build_tokens");
    Prec tokens = {.data = m.input.data, .shape = {N * T, w->D}};
    puf_mm(&a->tokens_in, &w->in_w, &tokens, stream);
    m.forward();
    wt_pool_aug<<<grid_size(N * wt_aug(w->D)), BLOCK_SIZE, 0, stream>>>(
        a->pooled.data, a->pooled_aug.data, m.output.data, N, T, w->D);
    wt_check("wt_pool_aug");
    puf_mm(&a->pooled_aug, &w->out_w, &a->out, stream);
    wt_add_residual<<<grid_size(N * w->H), BLOCK_SIZE, 0, stream>>>(a->out.data, x.data, N * w->H);
    wt_check("wt_add_residual");
    return a->out;
}

static Prec window_forward(void* weights, Prec x, Prec state, void* activations, cudaStream_t stream) {
    auto* w = (WindowWeights*)weights;
    auto* a = (WindowActivations*)activations;
    Prec out = window_run(w, a, x, state, nullptr, 0, 1, stream);
    int B = (int)state.shape[1];
    wt_shift_state<<<grid_size(B * w->H), BLOCK_SIZE, 0, stream>>>(state.data, x.data, B, w->H, w->K);
    wt_check("wt_shift_state");
    return out;
}

static Prec window_forward_train(void* weights, Prec x, Prec state, Prec terminals,
        void* activations, int agent_off, cudaStream_t stream) {
    auto* w = (WindowWeights*)weights;
    auto* a = (WindowActivations*)activations;
    int B = (int)x.shape[0], TT = a->rows / B;
    a->steps = TT;
    Prec out = window_run(w, a, x, state, terminals.data, agent_off, TT, stream);
    return {.data = out.data, .shape = {B, TT, w->H}};
}

__global__ static void wt_unpool(precision_t* upstream, const precision_t* g_pooled_aug, int N, int T, int D) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N * T * D) return;
    int d = i % D, k = (i / D) % T, n = i / (D * T);
    upstream[i] = k == T - 1 ? g_pooled_aug[n * wt_aug(D) + d] : from_float(0.f);
}

// Gradient into input (b, t): the residual, plus every valid token that held it, the
// current token at step t and age-a tokens of later steps t + a in this segment.
__global__ static void wt_gather_input_grad(precision_t* g_x, const precision_t* g_out,
        const precision_t* g_tokens_in, const precision_t* valid, int N, int TT, int H, int K, int in_cols) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N * H) return;
    int n = i / H, h = i % H, t = n % TT, T = K + 1;
    float g = to_float(g_out[i]);
    for (int age = 0; age <= K && t + age < TT; ++age) {
        size_t token = (size_t)(n + age) * T + (K - age);
        if (to_float(valid[token]) != 0.f) g += to_float(g_tokens_in[token * in_cols + h]);
    }
    g_x[i] = from_float(g);
}

static Prec window_backward(void* weights, Prec grad, void* activations, cudaStream_t stream) {
    auto* w = (WindowWeights*)weights;
    auto* a = (WindowActivations*)activations;
    int N = a->rows, T = w->K + 1, in_cols = wt_aug(wt_token_in(w->H, w->K));
    auto& m = *a->model;
    m.set_stream(stream);
    Prec g = {.data = grad.data, .shape = {N, w->H}};
    // dW on the side stream like MinGRU (puf_dw_join waits on it before the optimizer;
    // inside a captured train graph that join needs work forked onto the side stream).
    puf_mm_tn_async_after(&g, &a->pooled_aug, &a->d_out_w, stream);
    puf_mm_nn(&g, &w->out_w, &a->g_pooled_aug, stream);
    wt_unpool<<<grid_size(N * T * w->D), BLOCK_SIZE, 0, stream>>>(m.upstream.data, a->g_pooled_aug.data, N, T, w->D);
    wt_check("wt_unpool");
    m.backward();
    if (USE_BF16) for (int i = 0; i < w->count; ++i) {
        int count = numel(w->params[i].shape);
        cast<<<grid_size(count), BLOCK_SIZE, 0, stream>>>(a->grads[i].data, a->grads32[i].data, count);
        wt_check("cast");
    }
    Prec g_tokens = {.data = m.state_gradient.data, .shape = {N * T, w->D}};
    puf_mm_tn_async_after(&g_tokens, &a->tokens_in, &a->d_in_w, stream);
    puf_mm_nn(&g_tokens, &w->in_w, &a->g_tokens_in, stream);
    wt_gather_input_grad<<<grid_size(N * w->H), BLOCK_SIZE, 0, stream>>>(
        a->g_x.data, g.data, a->g_tokens_in.data, a->valid.data, N, a->steps, w->H, w->K, in_cols);
        wt_check("wt_gather_input_grad");
    return a->g_x;
}
