// Attention policy head + zero-sum paired critic for the chess Transformer (2026-11-03). Included by src/ocean.cu with
// PUFFER_CHESS, CHESS_XFMR and CHESS_APOL (after chess_transformer.cu, which it reads). As in Leela's transformer nets:
// the logit of move from-square f -> to-square t is q_f . k_t / sqrt(dk), with q = Wq x and k = Wk x on the encoder's
// output tokens x of those two squares, so every move is scored from the two squares it connects (4096 logits, the
// same from*64+to order as chess.h). The pass logit (the other side is to move) comes from the recurrent state.
// Value: the zero-sum paired critic of chess_decoder.cu, tanh(0.5 * (f(own view) - f(opponent view))).
// The gradient on the square tokens goes back to the encoder (CXActs::tok_grad, added before its Transformer backward).

#define CAP_DK 64
#define CAP_CF (12 * 64 + 1 + 4 + 9 + 8 + 2 + 2)  // critic features, as chess_decoder.cu
#ifndef CHESS_CH
#define CHESS_CH 256
#endif

__global__ static void cap_critic_features(const precision_t* __restrict__ obs, precision_t* __restrict__ feats, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= 2L * B * CAP_CF) return;
    int k = i % CAP_CF; long row = i / CAP_CF; int opp = row >= B; long b = opp ? row - B : row;
    const precision_t* o = obs + b * OBS_SIZE;
    int src;
    if (k < 768) {
        int plane = k / 64, sq = k % 64;
        src = opp ? ((plane + 6) % 12) * 64 + (sq ^ 56) : k;
    } else {
        int j = k - 768;
        if (j == 0) { float side = to_float(o[O_SIDE]); feats[i] = from_float(opp ? 1.0f - side : side); return; }
        if (j <= 4) { int c = j - 1; src = O_CASTLE + (opp ? (c + 2) % 4 : c); }
        else if (j <= 13) src = O_EP + (j - 5);
        else if (j <= 21) src = O_RULE50 + (j - 14);
        else if (j <= 23) src = O_REPETITION + (j - 22);
        else src = (j == 24) == !opp ? O_SELF_CHECK : O_OPP_CHECK;
    }
    feats[i] = o[src];
}
__global__ static void cap_relu_copy(const precision_t* z, precision_t* a, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) a[i] = from_float(fmaxf(0.0f, to_float(z[i])));
}
__global__ static void cap_relu_mask(precision_t* g, const precision_t* z, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && to_float(z[i]) <= 0.0f) g[i] = from_float(0.0f);
}
// out[b, :4096] = qk, out[b, 4096] = pass, out[b, 4097] = tanh(0.5 (f_own - f_opp))
__global__ static void cap_assemble(precision_t* out, const precision_t* qk, const precision_t* pass,
        const precision_t* f, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * 4098) return;
    int a = i % 4098; long b = i / 4098;
    float v = a < 4096 ? to_float(qk[b * 4096 + a]) : a == 4096 ? to_float(pass[b])
        : tanhf(0.5f * (to_float(f[b]) - to_float(f[B + b])));
    out[i] = from_float(v);
}
// gqk = grad_logits[:, :4096] (precision), gpass = grad_logits[:, 4096], gf = paired critic gradient
__global__ static void cap_grads(const float* gl, const float* gv, const precision_t* out, precision_t* gqk,
        precision_t* gpass, precision_t* gf, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * 4097) return;
    int a = i % 4097; long b = i / 4097;
    if (a < 4096) gqk[b * 4096 + a] = from_float(gl[i]);
    else {
        gpass[b] = from_float(gl[i]);
        float v = to_float(out[b * 4098 + 4097]);
        float g = gv[b] * 0.5f * (1.0f - v * v);
        gf[b] = from_float(g); gf[B + b] = from_float(-g);
    }
}
static int cap_grid(long n) { return (int)((n + BLOCK_SIZE - 1) / BLOCK_SIZE); }

// Strided-batched row-major GEMMs over the per-board 64 x 64 move matrix (tokens are 65 per board; the global one,
// last, is skipped by using 64 rows per batch with a 65-row stride).
static void cap_gemm(cublasHandle_t h, cublasOperation_t ta, cublasOperation_t tb, int m, int n, int k, float alpha,
        const precision_t* A, int lda, long sa, const precision_t* Bm, int ldb, long sb, float beta,
        precision_t* C, int ldc, long sc, int batch) {
    auto t = puf_transformer::dtype<precision_t>();
    puf_transformer::check(cublasGemmStridedBatchedEx(h, ta, tb, m, n, k, &alpha, A, t, lda, sa, Bm, t, ldb, sb, &beta,
        C, t, ldc, sc, batch, CUBLAS_COMPUTE_32F, CUBLAS_GEMM_DEFAULT), "chess attention policy GEMM");
}

// Same leading fields as DecoderWeights: the trainer reads ->continuous and ->logstd through that type.
struct CAPWeights {
    Prec weight, logstd;    // weight = pass row (1, hidden)
    int hidden_dim, output_dim;
    bool continuous;
    Prec wq, wk, w1, w2;    // (dk, D), (dk, D), critic (CH, CF), (1, CH)
};
struct CAPActs {
    Prec out, saved_h, obs;
    Prec q, k, qk, pass, feats, z, a, f;
    Prec gqk, gpass, gq, gk, gf, ga, grad_h;
    Prec dpass, dwq, dwk, dw1, dw2;
    CXActs* enc;
};
static void cap_bind(void* activations, Prec observation) { ((CAPActs*)activations)->obs = observation; }

static Prec cap_forward(void* w, void* activations, Prec h, cudaStream_t st) {
    CAPWeights* dw = (CAPWeights*)w; CAPActs* a = (CAPActs*)activations;
    int B = h.shape[0];
    a->enc = g_cx_last;
    auto& m = *a->enc->m;
    if (a->saved_h.data) puf_copy(&a->saved_h, &h, st);
    Prec x = {.data = m.output.data, .shape = {(long)B * CX_TOKENS, CHESS_XD}};
    Prec q = {.data = a->q.data, .shape = {(long)B * CX_TOKENS, CAP_DK}}, k = {.data = a->k.data, .shape = {(long)B * CX_TOKENS, CAP_DK}};
    puf_mm(&x, &dw->wq, &q, st);
    puf_mm(&x, &dw->wk, &k, st);
    m.set_stream(st);
    // qk_rm[b] (64 x 64) = Q_b K_b^T / sqrt(dk): column-major C = op(K)^T Q
    cap_gemm(m.blas, CUBLAS_OP_T, CUBLAS_OP_N, 64, 64, CAP_DK, 1.0f / sqrtf((float)CAP_DK), k.data, CAP_DK,
        (long)CX_TOKENS * CAP_DK, q.data, CAP_DK, (long)CX_TOKENS * CAP_DK, 0.0f, a->qk.data, 64, 4096, B);
    Prec pass = {.data = a->pass.data, .shape = {B, 1}};
    puf_mm(&h, &dw->weight, &pass, st);
    // paired critic
    cap_critic_features<<<cap_grid(2L * B * CAP_CF), BLOCK_SIZE, 0, st>>>(a->obs.data, a->feats.data, B);
    Prec feats = {.data = a->feats.data, .shape = {2 * B, CAP_CF}};
    Prec z = {.data = a->z.data, .shape = {2 * B, CHESS_CH}}, act = {.data = a->a.data, .shape = {2 * B, CHESS_CH}};
    Prec f = {.data = a->f.data, .shape = {2 * B, 1}};
    puf_mm(&feats, &dw->w1, &z, st);
    cap_relu_copy<<<cap_grid(2L * B * CHESS_CH), BLOCK_SIZE, 0, st>>>(z.data, act.data, 2L * B * CHESS_CH);
    puf_mm(&act, &dw->w2, &f, st);
    Prec out = {.data = a->out.data, .shape = {B, 4098}};
    cap_assemble<<<cap_grid((long)B * 4098), BLOCK_SIZE, 0, st>>>(out.data, a->qk.data, pass.data, f.data, B);
    return out;
}
static Prec cap_backward(void* w, void* activations, Float grad_logits, Float grad_logstd, Float grad_value,
        cudaStream_t st) {
    (void)grad_logstd;
    CAPWeights* dw = (CAPWeights*)w; CAPActs* a = (CAPActs*)activations;
    int B = a->saved_h.shape[0];
    CXActs* e = a->enc;
    auto& m = *e->m;
    m.set_stream(st);
    cap_grads<<<cap_grid((long)B * 4097), BLOCK_SIZE, 0, st>>>(grad_logits.data, grad_value.data, a->out.data,
        a->gqk.data, a->gpass.data, a->gf.data, B);
    // pass row
    Prec gpass = {.data = a->gpass.data, .shape = {B, 1}};
    puf_mm_tn(&gpass, &a->saved_h, &a->dpass, st);
    puf_mm_nn(&gpass, &dw->weight, &a->grad_h, st);
    // move logits: dQ_b = G_b K_b / sqrt(dk), dK_b = G_b^T Q_b / sqrt(dk); the global token rows stay zero
    float s = 1.0f / sqrtf((float)CAP_DK);
    long tok = (long)B * CX_TOKENS * CAP_DK;
    cudaMemsetAsync(a->gq.data, 0, tok * sizeof(precision_t), st);
    cudaMemsetAsync(a->gk.data, 0, tok * sizeof(precision_t), st);
    cap_gemm(m.blas, CUBLAS_OP_N, CUBLAS_OP_N, CAP_DK, 64, 64, s, a->k.data, CAP_DK, (long)CX_TOKENS * CAP_DK,
        a->gqk.data, 64, 4096, 0.0f, a->gq.data, CAP_DK, (long)CX_TOKENS * CAP_DK, B);
    cap_gemm(m.blas, CUBLAS_OP_N, CUBLAS_OP_T, CAP_DK, 64, 64, s, a->q.data, CAP_DK, (long)CX_TOKENS * CAP_DK,
        a->gqk.data, 64, 4096, 0.0f, a->gk.data, CAP_DK, (long)CX_TOKENS * CAP_DK, B);
    Prec x = {.data = m.output.data, .shape = {(long)B * CX_TOKENS, CHESS_XD}};
    Prec gq = {.data = a->gq.data, .shape = {(long)B * CX_TOKENS, CAP_DK}}, gk = {.data = a->gk.data, .shape = {(long)B * CX_TOKENS, CAP_DK}};
    puf_mm_tn(&gq, &x, &a->dwq, st);
    puf_mm_tn(&gk, &x, &a->dwk, st);
    Prec tg = {.data = e->tok_grad.data, .shape = {(long)B * CX_TOKENS, CHESS_XD}};
    puf_mm_nn(&gq, &dw->wq, &tg, st);
    puf_mm_nn(&gk, &dw->wk, &tg, st, 1.0f, 1.0f);
    e->tok_grad_ready = true;
    // critic
    Prec gf = {.data = a->gf.data, .shape = {2 * B, 1}}, act = {.data = a->a.data, .shape = {2 * B, CHESS_CH}};
    Prec ga = {.data = a->ga.data, .shape = {2 * B, CHESS_CH}};
    Prec feats = {.data = a->feats.data, .shape = {2 * B, CAP_CF}};
    puf_mm_tn(&gf, &act, &a->dw2, st);
    puf_mm_nn(&gf, &dw->w2, &ga, st);
    cap_relu_mask<<<cap_grid(2L * B * CHESS_CH), BLOCK_SIZE, 0, st>>>(ga.data, a->z.data, 2L * B * CHESS_CH);
    puf_mm_tn(&ga, &feats, &a->dw1, st);
    return a->grad_h;
}
static void cap_init(void* w, ulong* seed, cudaStream_t st) {
    CAPWeights* dw = (CAPWeights*)w;
    puf_kaiming_init(&dw->weight, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->wq, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->wk, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->w1, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->w2, 1.0f, (*seed)++, st);
}
static void cap_params(void* w, Allocator* alloc) {
    CAPWeights* dw = (CAPWeights*)w;
    dw->weight = {.shape = {1, dw->hidden_dim}};
    dw->wq = {.shape = {CAP_DK, CHESS_XD}}; dw->wk = {.shape = {CAP_DK, CHESS_XD}};
    dw->w1 = {.shape = {CHESS_CH, CAP_CF}}; dw->w2 = {.shape = {1, CHESS_CH}};
    alloc_register(alloc, &dw->weight); alloc_register(alloc, &dw->wq); alloc_register(alloc, &dw->wk);
    alloc_register(alloc, &dw->w1); alloc_register(alloc, &dw->w2);
}
static void cap_buffers(CAPWeights* dw, CAPActs* a, Allocator* acts, int B) {
    long tok = (long)B * CX_TOKENS;
    a->out = {.shape = {B, 4098}};
    a->q = {.shape = {tok, CAP_DK}}; a->k = {.shape = {tok, CAP_DK}}; a->qk = {.shape = {B, 4096}};
    a->pass = {.shape = {B, 1}};
    a->feats = {.shape = {2 * B, CAP_CF}}; a->z = {.shape = {2 * B, CHESS_CH}}; a->a = {.shape = {2 * B, CHESS_CH}};
    a->f = {.shape = {2 * B, 1}};
    Prec* all[] = {&a->out, &a->q, &a->k, &a->qk, &a->pass, &a->feats, &a->z, &a->a, &a->f};
    for (Prec* p : all) alloc_register(acts, p);
    (void)dw;
}
static void cap_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B) {
    CAPWeights* dw = (CAPWeights*)w; CAPActs* a = (CAPActs*)activations;
    cap_buffers(dw, a, acts, B);
    long tok = (long)B * CX_TOKENS;
    a->saved_h = {.shape = {B, dw->hidden_dim}}; a->grad_h = {.shape = {B, dw->hidden_dim}};
    a->gqk = {.shape = {B, 4096}}; a->gpass = {.shape = {B, 1}};
    a->gq = {.shape = {tok, CAP_DK}}; a->gk = {.shape = {tok, CAP_DK}};
    a->gf = {.shape = {2 * B, 1}}; a->ga = {.shape = {2 * B, CHESS_CH}};
    Prec* all[] = {&a->saved_h, &a->grad_h, &a->gqk, &a->gpass, &a->gq, &a->gk, &a->gf, &a->ga};
    for (Prec* p : all) alloc_register(acts, p);
    // gradients in parameter order
    a->dpass = {.shape = {1, dw->hidden_dim}};
    a->dwq = {.shape = {CAP_DK, CHESS_XD}}; a->dwk = {.shape = {CAP_DK, CHESS_XD}};
    a->dw1 = {.shape = {CHESS_CH, CAP_CF}}; a->dw2 = {.shape = {1, CHESS_CH}};
    Prec* gs[] = {&a->dpass, &a->dwq, &a->dwk, &a->dw1, &a->dw2};
    for (Prec* p : gs) alloc_register(grads, p);
}
static void cap_rollout(void* w, void* activations, Allocator* alloc, int B) {
    cap_buffers((CAPWeights*)w, (CAPActs*)activations, alloc, B);
}
static void* cap_create(void* self) {
    Decoder* d = (Decoder*)self;
    if (d->output_dim != 4097) { fprintf(stderr, "chess attention policy needs the 4097-action chess env\n"); exit(1); }
    CAPWeights* dw = (CAPWeights*)calloc(1, sizeof(CAPWeights));
    dw->hidden_dim = d->hidden_dim; dw->output_dim = d->output_dim; dw->continuous = false;
    return dw;
}
static void create_chess_apolicy_decoder(Decoder* d) {
    d->forward = cap_forward; d->bind_observation = cap_bind; d->backward = cap_backward;
    d->init_weights = cap_init; d->reg_params = cap_params; d->reg_train = cap_train; d->reg_rollout = cap_rollout;
    d->create_weights = cap_create; d->activation_size = sizeof(CAPActs);
}
