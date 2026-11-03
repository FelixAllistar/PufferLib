// Transformer board encoder for chess (2026-11-03), built on src/native_transformer.cuh (read-only, as the Pokemon
// entity encoder uses it). Included by src/ocean.cu with PUFFER_CHESS and CHESS_XFMR.
// 65 tokens: the 64 squares (12 piece planes + legal-from + legal-to bits of chess.h's observation) and one global
// token (the 27 flags: side, castling, en passant, rule 50, repetition, checks, pass). Each token's input also has a
// one-hot token id, so the embedding learns square positions. Embedding W_tok [D, CX_IN] -> pre-LN Transformer
// (CHESS_XL layers, width CHESS_XD, CHESS_XH heads, FF 4D) -> the global token's output -> linear -> ReLU -> hidden.
#include "../../src/native_transformer.cuh"

#define CX_SQ 64
#define CX_TOKENS 65
#define CX_PLANES 14
#define CX_FLAGS (OBS_SIZE - CX_PLANES * CX_SQ)          // 27
#define CX_IN (CX_PLANES + CX_TOKENS + CX_FLAGS)          // 106
#ifndef CHESS_XD
#define CHESS_XD 64
#endif
#ifndef CHESS_XL
#define CHESS_XL 2
#endif
#ifndef CHESS_XH
#define CHESS_XH 4
#endif

__global__ static void cx_tokens(precision_t* __restrict__ dst, const precision_t* __restrict__ obs, int rows) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)rows * CX_TOKENS * CX_IN) return;
    int f = i % CX_IN; long r = i / CX_IN; int t = r % CX_TOKENS; long b = r / CX_TOKENS;
    const precision_t* o = obs + b * OBS_SIZE;
    float v = 0.0f;
    if (f < CX_PLANES) { if (t < CX_SQ) v = to_float(o[f * CX_SQ + t]); }
    else if (f < CX_PLANES + CX_TOKENS) v = (f - CX_PLANES) == t ? 1.0f : 0.0f;
    else if (t == CX_SQ) v = to_float(o[CX_PLANES * CX_SQ + (f - CX_PLANES - CX_TOKENS)]);
    dst[i] = from_float(v);
}
__global__ static void cx_pool(precision_t* pooled, const precision_t* tokens, int rows, int D) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < rows * D) pooled[i] = tokens[((long)(i / D) * CX_TOKENS + CX_SQ) * D + i % D];
}
__global__ static void cx_unpool(precision_t* tokens, const precision_t* pooled, int rows, int D) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)rows * CX_TOKENS * D) return;
    int t = (i / D) % CX_TOKENS;
    tokens[i] = t == CX_SQ ? pooled[(i / ((long)CX_TOKENS * D)) * D + i % D] : from_float(0.0f);
}
__global__ static void cx_relu(precision_t* x, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && to_float(x[i]) < 0.0f) x[i] = from_float(0.0f);
}
__global__ static void cx_relu_bwd(precision_t* g, const precision_t* y, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && to_float(y[i]) <= 0.0f) g[i] = from_float(0.0f);
}
static int cx_grid(long n) { return (int)((n + BLOCK_SIZE - 1) / BLOCK_SIZE); }

struct CXWeights {
    Prec tok, out;          // tok (D, CX_IN), out (hidden, D)
    puf_transformer::Config cfg;
    int count;              // 12 * layers + 2 Transformer parameters
    Prec* params;
    int hidden;
};
struct CXActs {
    Prec tokens_in, pooled, out;
    puf_transformer::Model<precision_t>* m;
    Prec dtok, dout, gpooled;
    Prec* grads;
    Float* fp32_grads;
};
static Prec cx_shape(puf_transformer::ParameterSpec s) {
    // Vectors stay rank-1 so Muon does not orthogonalize LN scales and biases (as the Pokemon encoder does).
    return s.rows == 1 ? Prec{.shape = {s.cols}} : Prec{.shape = {s.rows, s.cols}};
}
static void* cx_create(void* self) {
    Encoder* e = (Encoder*)self;
    CXWeights* w = (CXWeights*)calloc(1, sizeof(CXWeights));
    w->hidden = e->out_dim;
    w->cfg = {1, CX_TOKENS, CHESS_XD, CHESS_XL, CHESS_XH, 4 * CHESS_XD};
    w->cfg.validate();
    w->count = 12 * CHESS_XL + 2;
    w->params = new Prec[w->count]{};
    return w;
}
static void cx_params(void* weights, Allocator* alloc) {
    CXWeights* w = (CXWeights*)weights;
    w->tok = {.shape = {CHESS_XD, CX_IN}}; alloc_register(alloc, &w->tok);
    auto specs = puf_transformer::parameter_specs(w->cfg);
    for (int i = 0; i < w->count; i++) { w->params[i] = cx_shape(specs[i]); alloc_register(alloc, &w->params[i]); }
    w->out = {.shape = {w->hidden, CHESS_XD}}; alloc_register(alloc, &w->out);
}
static void cx_init(void* weights, ulong* seed, cudaStream_t st) {
    CXWeights* w = (CXWeights*)weights;
    puf_kaiming_init(&w->tok, 1.0f, (*seed)++, st);
    // As the Pokemon / Goofspiel Transformers: Xavier matrices, LN scale 1, zero biases.
    std::mt19937 rng((*seed)++);
    auto specs = puf_transformer::parameter_specs(w->cfg);
    for (int i = 0; i < w->count; i++) {
        auto s = specs[i];
        float bound = sqrtf(6.f / (s.rows + s.cols));
        std::uniform_real_distribution<float> dist(-bound, bound);
        std::vector<precision_t> values((size_t)s.rows * s.cols);
        for (auto& v : values) v = from_float(s.initialization == 1 ? 1.f : s.initialization == 2 ? 0.f : dist(rng));
        puf_transformer::check(cudaMemcpyAsync(w->params[i].data, values.data(), values.size() * sizeof(precision_t),
            cudaMemcpyHostToDevice, st), "initialize chess Transformer");
        puf_transformer::check(cudaStreamSynchronize(st), "finish chess Transformer initialization");
    }
    puf_kaiming_init(&w->out, 1.0f, (*seed)++, st);
}
static void cx_acts(void* weights, void* activations, Allocator* acts, Allocator* grads, int rows) {
    CXWeights* w = (CXWeights*)weights; CXActs* a = (CXActs*)activations;
    a->tokens_in = {.shape = {(long)rows * CX_TOKENS, CX_IN}}; alloc_register(acts, &a->tokens_in);
    auto cfg = w->cfg; cfg.batch = rows;
    a->m = new puf_transformer::Model<precision_t>(cfg, 17, false);
    a->pooled = {.shape = {rows, CHESS_XD}}; alloc_register(acts, &a->pooled);
    a->out = {.shape = {rows, w->hidden}}; alloc_register(acts, &a->out);
    if (grads) {
        a->gpooled = {.shape = {rows, CHESS_XD}}; alloc_register(acts, &a->gpooled);
        // gradients in parameter order: tok, Transformer, out
        a->dtok = {.shape = {CHESS_XD, CX_IN}}; alloc_register(grads, &a->dtok);
        auto specs = puf_transformer::parameter_specs(w->cfg);
        a->grads = new Prec[w->count]{};
        a->fp32_grads = new Float[w->count]{};
        for (int i = 0; i < w->count; i++) {
            a->grads[i] = cx_shape(specs[i]); alloc_register(grads, &a->grads[i]);
            // The Transformer writes FP32 parameter gradients; BF16 builds cast them after backward.
            if (USE_BF16) { a->fp32_grads[i] = {.shape = {specs[i].rows * specs[i].cols}}; alloc_register(acts, &a->fp32_grads[i]); }
        }
        a->dout = {.shape = {w->hidden, CHESS_XD}}; alloc_register(grads, &a->dout);
    }
}
static void cx_rollout(void* w, void* a, Allocator* alloc, int rows) { cx_acts(w, a, alloc, NULL, rows); }
static void cx_bind(CXWeights* w, CXActs* a, cudaStream_t st) {
    auto& m = *a->m;
    auto params = m.parameters();
    m.set_stream(st);
    for (int i = 0; i < w->count; i++) {
        size_t count = numel(w->params[i].shape);
        params[i]->value.borrow(w->params[i].data, count);
        if (a->grads) params[i]->gradient.borrow(USE_BF16 ? a->fp32_grads[i].data : (float*)a->grads[i].data, count);
    }
}
static Prec cx_forward(void* weights, void* activations, Prec obs, cudaStream_t st) {
    CXWeights* w = (CXWeights*)weights; CXActs* a = (CXActs*)activations;
    int rows = a->out.shape[0];
    cx_tokens<<<cx_grid((long)rows * CX_TOKENS * CX_IN), BLOCK_SIZE, 0, st>>>(a->tokens_in.data, obs.data, rows);
    cx_bind(w, a, st);
    auto& m = *a->m;
    Prec emb = {.data = m.input.data, .shape = {(long)rows * CX_TOKENS, CHESS_XD}};
    puf_mm(&a->tokens_in, &w->tok, &emb, st);
    m.forward();
    cx_pool<<<cx_grid((long)rows * CHESS_XD), BLOCK_SIZE, 0, st>>>(a->pooled.data, m.output.data, rows, CHESS_XD);
    puf_mm(&a->pooled, &w->out, &a->out, st);
    cx_relu<<<cx_grid((long)rows * w->hidden), BLOCK_SIZE, 0, st>>>(a->out.data, (long)rows * w->hidden);
    return a->out;
}
static void cx_backward(void* weights, void* activations, Prec grad, cudaStream_t st) {
    CXWeights* w = (CXWeights*)weights; CXActs* a = (CXActs*)activations;
    int rows = a->out.shape[0];
    auto& m = *a->m;
    cx_relu_bwd<<<cx_grid((long)rows * w->hidden), BLOCK_SIZE, 0, st>>>(grad.data, a->out.data, (long)rows * w->hidden);
    Prec g = {.data = grad.data, .shape = {rows, w->hidden}};
    puf_mm_tn(&g, &a->pooled, &a->dout, st);
    puf_mm_nn(&g, &w->out, &a->gpooled, st);
    m.set_stream(st);
    cx_unpool<<<cx_grid((long)rows * CX_TOKENS * CHESS_XD), BLOCK_SIZE, 0, st>>>(m.upstream.data, a->gpooled.data, rows, CHESS_XD);
    m.backward();
    if (USE_BF16) for (int i = 0; i < w->count; i++) {
        int count = numel(w->params[i].shape);
        cast<<<grid_size(count), BLOCK_SIZE, 0, st>>>(a->grads[i].data, a->fp32_grads[i].data, count);
    }
    Prec tg = {.data = m.state_gradient.data, .shape = {(long)rows * CX_TOKENS, CHESS_XD}};
    puf_mm_tn(&tg, &a->tokens_in, &a->dtok, st);
}
static void create_chess_transformer_encoder(Encoder* e) {
    e->forward = cx_forward; e->backward = cx_backward;
    e->reg_train = cx_acts; e->reg_rollout = cx_rollout;
    e->reg_params = cx_params; e->init_weights = cx_init;
    e->create_weights = cx_create; e->activation_size = sizeof(CXActs);
}
