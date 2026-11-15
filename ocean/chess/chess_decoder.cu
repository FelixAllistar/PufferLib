// Zero-sum paired critic for chess self-play (2026-11-02), as kaggriculture's critic_mode 2 does it. Included by
// src/ocean.cu with PUFFER_CHESS and CHESS_PAIRED. Logits: the usual linear head on the recurrent state. Value:
// tanh(0.5 * (f(own view) - f(opponent view))), one critic MLP f (CHESS_CF features -> CHESS_CH -> 1) on the board
// planes and flags of the bound observation, the opponent's view being the same position with own/enemy planes
// swapped, ranks flipped and the side/castling/check flags swapped. V(me) = -V(opponent) exactly. The value
// gradient trains only the critic MLP; the logit gradient flows to the recurrent state as before.

#define CHESS_CF (12 * 64 + 1 + 4 + 9 + 8 + 2 + 2)  // 794: planes, side, castling, en passant, rule 50, rep, checks
#ifndef CHESS_CH
#define CHESS_CH 256
#endif

// feats rows [0, B): own view; rows [B, 2B): opponent view
__global__ void chess_critic_features(const precision_t* __restrict__ obs, precision_t* __restrict__ feats, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= 2L * B * CHESS_CF) return;
    int k = i % CHESS_CF; long row = i / CHESS_CF; int opp = row >= B; long b = opp ? row - B : row;
    const precision_t* o = obs + b * OBS_SIZE;
    int src;
    if (k < 768) {
        int plane = k / 64, sq = k % 64;
        src = opp ? ((plane + 6) % 12) * 64 + (sq ^ 56) : k;
    } else {
        int j = k - 768;  // 0 side, 1-4 castle, 5-13 ep, 14-21 rule50, 22-23 rep, 24-25 checks
        if (j == 0) {
            float side = to_float(o[O_SIDE]);
            feats[i] = from_float(opp ? 1.0f - side : side);
            return;
        }
        if (j <= 4) { int c = j - 1; src = O_CASTLE + (opp ? (c + 2) % 4 : c); }
        else if (j <= 13) src = O_EP + (j - 5);
        else if (j <= 21) src = O_RULE50 + (j - 14);
        else if (j <= 23) src = O_REPETITION + (j - 22);
        else src = (j == 24) == !opp ? O_SELF_CHECK : O_OPP_CHECK;
    }
    feats[i] = o[src];
}
__global__ void chess_relu_copy(const precision_t* __restrict__ z, precision_t* __restrict__ a, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) a[i] = from_float(fmaxf(0.0f, to_float(z[i])));
}
// out[b, A] = tanh(0.5 * (f[b] - f[B + b]))
__global__ void chess_paired_value(const precision_t* __restrict__ f, precision_t* __restrict__ out, int B, int A) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= B) return;
    out[(long)b * (A + 1) + A] = from_float(tanhf(0.5f * (to_float(f[b]) - to_float(f[B + b]))));
}
// grad_out[b, :A] = grad_logits, grad_out[b, A] = 0 (the value is the critic's)
__global__ void chess_logit_grad(precision_t* __restrict__ grad_out, const float* __restrict__ gl,
        const float* __restrict__ gv, int B, int A) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * (A + 1)) return;
    int a = i % (A + 1); long b = i / (A + 1);
#ifdef CHESS_PAIRED_OFF
    grad_out[i] = from_float(a < A ? gl[b * A + a] : gv[b]);
#else
    (void)gv;
    grad_out[i] = from_float(a < A ? gl[b * A + a] : 0.0f);
#endif
}
// gf[b] = gv * 0.5 * (1 - v^2), gf[B + b] = -gf[b]
__global__ void chess_paired_grad(const float* __restrict__ gv, const precision_t* __restrict__ out,
        precision_t* __restrict__ gf, int B, int A) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= B) return;
    float v = to_float(out[(long)b * (A + 1) + A]);
    float g = gv[b] * 0.5f * (1.0f - v * v);
    gf[b] = from_float(g);
    gf[B + b] = from_float(-g);
}
__global__ void chess_relu_mask(precision_t* __restrict__ g, const precision_t* __restrict__ z, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && to_float(z[i]) <= 0.0f) g[i] = from_float(0.0f);
}
static int chess_dgrid(long n) { return (int)((n + BLOCK_SIZE - 1) / BLOCK_SIZE); }

#ifdef CHESS_MOVE_FEATS
// Per-move lookahead features (2026-11-14, -DCHESS_MOVE_FEATS), as bc_train.py --move-feats: for each legal move m of
// the side to move, logit[a_m] += sum_f feat_m[f] * (mf_w[f] + (h @ mf_u^T)[f]), feat_m the 7 one-hot bits of the
// slot code that chess.h writes after CHESS_OBS_END (swing bucket | check << 3 | mate << 4; 255 = empty).
#define CHESS_MF 7
__device__ __forceinline__ float chess_mf_bit(int code, int f) {
    return f < 5 ? (float)((code & 7) == f) : (float)((code >> (f - 2)) & 1);  // f 5: bit 3, f 6: bit 4
}
// out[b, a_m] += sum_f bit_f * (mf_w[f] + C[b, f]); one thread per (b, slot); a position never repeats an action
__global__ void chess_mf_bias(const precision_t* __restrict__ obs, const precision_t* __restrict__ C,
        const precision_t* __restrict__ mfw, precision_t* __restrict__ out, int B, int A) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CHESS_MF_SLOTS) return;
    int m = i % CHESS_MF_SLOTS; long b = i / CHESS_MF_SLOTS;
    const precision_t* o = obs + b * OBS_SIZE + O_MOVE_FEATS + 3 * m;
    int code = (int)to_float(o[2]);
    if (code > 31) return;
    int a = (int)to_float(o[0]) * 64 + (int)to_float(o[1]);
    float s = 0.0f;
    for (int f = 0; f < CHESS_MF; f++)
        s += chess_mf_bit(code, f) * (to_float(mfw[f]) + to_float(C[b * CHESS_MF + f]));
    long k = b * (A + 1) + a;
    out[k] = from_float(to_float(out[k]) + s);
}
// dC[b, f] = sum over slots of bit_f * grad_logits[b, a_m]
__global__ void chess_mf_grad(const precision_t* __restrict__ obs, const float* __restrict__ gl,
        precision_t* __restrict__ dC, int B, int A) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= B) return;
    float acc[CHESS_MF] = {0};
    const precision_t* o = obs + (long)b * OBS_SIZE + O_MOVE_FEATS;
    for (int m = 0; m < CHESS_MF_SLOTS; m++) {
        int code = (int)to_float(o[3 * m + 2]);
        if (code > 31) continue;
        float g = gl[(long)b * A + (int)to_float(o[3 * m]) * 64 + (int)to_float(o[3 * m + 1])];
        for (int f = 0; f < CHESS_MF; f++) acc[f] += chess_mf_bit(code, f) * g;
    }
    for (int f = 0; f < CHESS_MF; f++) dC[(long)b * CHESS_MF + f] = from_float(acc[f]);
}
// dmf_w[f] = sum_b dC[b, f]: one block per f
__global__ void chess_mf_wgrad(const precision_t* __restrict__ dC, precision_t* __restrict__ dw, int B) {
    __shared__ float sh[BLOCK_SIZE];
    int f = blockIdx.x;
    float s = 0.0f;
    for (int b = threadIdx.x; b < B; b += blockDim.x) s += to_float(dC[(long)b * CHESS_MF + f]);
    sh[threadIdx.x] = s;
    __syncthreads();
    for (int k = blockDim.x / 2; k > 0; k >>= 1) {
        if (threadIdx.x < k) sh[threadIdx.x] += sh[threadIdx.x + k];
        __syncthreads();
    }
    if (threadIdx.x == 0) dw[f] = from_float(sh[0]);
}
__global__ void chess_mf_add(precision_t* __restrict__ a, const precision_t* __restrict__ b, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) a[i] = from_float(to_float(a[i]) + to_float(b[i]));
}
__global__ void chess_mf_zero(precision_t* __restrict__ a, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) a[i] = from_float(0.0f);
}
#endif

// The trainer reads ->continuous and ->logstd through DecoderWeights*, so this starts with the same fields.
struct ChessDecoderWeights {
    Prec weight, logstd;
    int hidden_dim, output_dim;
    bool continuous;
    Prec w1, w2;
#ifdef CHESS_MOVE_FEATS
    Prec mf_w, mf_u;  // (7), (7, hidden)
#endif
    int hidden, A;
};
struct ChessDecoderActivations {
    Prec out, saved_input, grad_out, grad_input, obs;
    Prec feats, z, a, f, gf, ga;
    Prec dweight, dw1, dw2;
#ifdef CHESS_MOVE_FEATS
    Prec mfc, dmfc, mftmp, dmf_w, dmf_u;  // C = h @ mf_u^T (B, 7), dC, dC @ mf_u (B, hidden), grads
#endif
};

static void chess_decoder_bind(void* activations, Prec observation) {
    ((ChessDecoderActivations*)activations)->obs = observation;
}
static Prec chess_decoder_forward(void* w, void* activations, Prec input, cudaStream_t st) {
    ChessDecoderWeights* dw = (ChessDecoderWeights*)w;
    ChessDecoderActivations* a = (ChessDecoderActivations*)activations;
    int B = input.shape[0], A = dw->A;
    if (a->saved_input.data) puf_copy(&a->saved_input, &input, st);
    Prec out = {.data = a->out.data, .shape = {B, A + 1}};
    puf_mm(&input, &dw->weight, &out, st);
    chess_critic_features<<<chess_dgrid(2L * B * CHESS_CF), BLOCK_SIZE, 0, st>>>(a->obs.data, a->feats.data, B);
    Prec feats = {.data = a->feats.data, .shape = {2 * B, CHESS_CF}};
    Prec z = {.data = a->z.data, .shape = {2 * B, CHESS_CH}}, act = {.data = a->a.data, .shape = {2 * B, CHESS_CH}};
    Prec f = {.data = a->f.data, .shape = {2 * B, 1}};
    puf_mm(&feats, &dw->w1, &z, st);
    chess_relu_copy<<<chess_dgrid(2L * B * CHESS_CH), BLOCK_SIZE, 0, st>>>(z.data, act.data, 2L * B * CHESS_CH);
    puf_mm(&act, &dw->w2, &f, st);
#ifndef CHESS_PAIRED_OFF
    chess_paired_value<<<chess_dgrid(B), BLOCK_SIZE, 0, st>>>(f.data, out.data, B, A);
#endif
#ifdef CHESS_MOVE_FEATS
    Prec mfc = {.data = a->mfc.data, .shape = {B, CHESS_MF}};
    puf_mm(&input, &dw->mf_u, &mfc, st);
    chess_mf_bias<<<chess_dgrid((long)B * CHESS_MF_SLOTS), BLOCK_SIZE, 0, st>>>(a->obs.data, mfc.data,
        dw->mf_w.data, out.data, B, A);
#endif
    return out;
}
static Prec chess_decoder_backward(void* w, void* activations, Float grad_logits, Float grad_logstd,
        Float grad_value, cudaStream_t st) {
    (void)grad_logstd;
    ChessDecoderWeights* dw = (ChessDecoderWeights*)w;
    ChessDecoderActivations* a = (ChessDecoderActivations*)activations;
    int B = a->saved_input.shape[0], A = dw->A;
    chess_logit_grad<<<chess_dgrid((long)B * (A + 1)), BLOCK_SIZE, 0, st>>>(a->grad_out.data, grad_logits.data, grad_value.data, B, A);
    Prec go = {.data = a->grad_out.data, .shape = {B, A + 1}};
    puf_mm_tn(&go, &a->saved_input, &a->dweight, st);
    puf_mm_nn(&go, &dw->weight, &a->grad_input, st);
#ifdef CHESS_MOVE_FEATS
    {
        Prec dmfc = {.data = a->dmfc.data, .shape = {B, CHESS_MF}};
        Prec tmp = {.data = a->mftmp.data, .shape = {B, dw->hidden}};
        chess_mf_grad<<<chess_dgrid(B), BLOCK_SIZE, 0, st>>>(a->obs.data, grad_logits.data, dmfc.data, B, A);
        chess_mf_wgrad<<<CHESS_MF, BLOCK_SIZE, 0, st>>>(dmfc.data, a->dmf_w.data, B);
        puf_mm_tn(&dmfc, &a->saved_input, &a->dmf_u, st);
        puf_mm_nn(&dmfc, &dw->mf_u, &tmp, st);
        chess_mf_add<<<chess_dgrid((long)B * dw->hidden), BLOCK_SIZE, 0, st>>>(a->grad_input.data, tmp.data,
            (long)B * dw->hidden);
    }
#endif
    // critic
    chess_paired_grad<<<chess_dgrid(B), BLOCK_SIZE, 0, st>>>(grad_value.data, a->out.data, a->gf.data, B, A);
    Prec gf = {.data = a->gf.data, .shape = {2 * B, 1}}, act = {.data = a->a.data, .shape = {2 * B, CHESS_CH}};
    Prec ga = {.data = a->ga.data, .shape = {2 * B, CHESS_CH}};
    Prec feats = {.data = a->feats.data, .shape = {2 * B, CHESS_CF}};
    puf_mm_tn(&gf, &act, &a->dw2, st);
    puf_mm_nn(&gf, &dw->w2, &ga, st);
    chess_relu_mask<<<chess_dgrid(2L * B * CHESS_CH), BLOCK_SIZE, 0, st>>>(ga.data, a->z.data, 2L * B * CHESS_CH);
    puf_mm_tn(&ga, &feats, &a->dw1, st);
    return a->grad_input;
}
static void chess_decoder_init(void* w, ulong* seed, cudaStream_t st) {
    ChessDecoderWeights* dw = (ChessDecoderWeights*)w;
    puf_kaiming_init(&dw->weight, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->w1, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->w2, 1.0f, (*seed)++, st);
#ifdef CHESS_MOVE_FEATS  // start with no feature bias (checkpoints from bc_train.py --move-feats set them)
    chess_mf_zero<<<1, BLOCK_SIZE, 0, st>>>(dw->mf_w.data, CHESS_MF);
    chess_mf_zero<<<chess_dgrid((long)CHESS_MF * dw->hidden), BLOCK_SIZE, 0, st>>>(dw->mf_u.data, (long)CHESS_MF * dw->hidden);
#endif
}
static void chess_decoder_params(void* w, Allocator* alloc) {
    ChessDecoderWeights* dw = (ChessDecoderWeights*)w;
    dw->weight = {.shape = {dw->A + 1, dw->hidden}};
    dw->w1 = {.shape = {CHESS_CH, CHESS_CF}};
    dw->w2 = {.shape = {1, CHESS_CH}};
    alloc_register(alloc, &dw->weight); alloc_register(alloc, &dw->w1); alloc_register(alloc, &dw->w2);
#ifdef CHESS_MOVE_FEATS
    // mf_w 1-D: Muon skips it (plain momentum SGD). As {1, 7} Muon normalized every update to a unit vector, a fixed
    // lr-sized step on 7 logit scales: 0.5B RL steps pushed "loses >= 2.5 pawns" from -0.28 to -4.9 (Maia greedy
    // 1713 -> 1425, 2026-11-14)
    dw->mf_w = {.shape = {CHESS_MF}};
    dw->mf_u = {.shape = {CHESS_MF, dw->hidden}};
    alloc_register(alloc, &dw->mf_w); alloc_register(alloc, &dw->mf_u);
#endif
}
static void chess_decoder_buffers(ChessDecoderWeights* dw, ChessDecoderActivations* a, Allocator* acts, int B) {
    a->out = {.shape = {B, dw->A + 1}};
    a->feats = {.shape = {2 * B, CHESS_CF}};
    a->z = {.shape = {2 * B, CHESS_CH}}; a->a = {.shape = {2 * B, CHESS_CH}}; a->f = {.shape = {2 * B, 1}};
    alloc_register(acts, &a->out); alloc_register(acts, &a->feats);
    alloc_register(acts, &a->z); alloc_register(acts, &a->a); alloc_register(acts, &a->f);
#ifdef CHESS_MOVE_FEATS
    a->mfc = {.shape = {B, CHESS_MF}};
    alloc_register(acts, &a->mfc);
#endif
}
static void chess_decoder_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B) {
    ChessDecoderWeights* dw = (ChessDecoderWeights*)w;
    ChessDecoderActivations* a = (ChessDecoderActivations*)activations;
    chess_decoder_buffers(dw, a, acts, B);
    a->saved_input = {.shape = {B, dw->hidden}}; a->grad_out = {.shape = {B, dw->A + 1}};
    a->grad_input = {.shape = {B, dw->hidden}};
    a->gf = {.shape = {2 * B, 1}}; a->ga = {.shape = {2 * B, CHESS_CH}};
    alloc_register(acts, &a->saved_input); alloc_register(acts, &a->grad_out); alloc_register(acts, &a->grad_input);
    alloc_register(acts, &a->gf); alloc_register(acts, &a->ga);
    a->dweight = {.shape = {dw->A + 1, dw->hidden}};
    a->dw1 = {.shape = {CHESS_CH, CHESS_CF}};
    a->dw2 = {.shape = {1, CHESS_CH}};
    alloc_register(grads, &a->dweight); alloc_register(grads, &a->dw1); alloc_register(grads, &a->dw2);
#ifdef CHESS_MOVE_FEATS
    a->dmfc = {.shape = {B, CHESS_MF}}; a->mftmp = {.shape = {B, dw->hidden}};
    alloc_register(acts, &a->dmfc); alloc_register(acts, &a->mftmp);
    a->dmf_w = {.shape = {CHESS_MF}};
    a->dmf_u = {.shape = {CHESS_MF, dw->hidden}};
    alloc_register(grads, &a->dmf_w); alloc_register(grads, &a->dmf_u);  // same order as the parameters
#endif
}
static void chess_decoder_rollout(void* w, void* activations, Allocator* alloc, int B) {
    chess_decoder_buffers((ChessDecoderWeights*)w, (ChessDecoderActivations*)activations, alloc, B);
}
static void* chess_decoder_create(void* self) {
    Decoder* d = (Decoder*)self;
    ChessDecoderWeights* dw = (ChessDecoderWeights*)calloc(1, sizeof(ChessDecoderWeights));
    dw->hidden = dw->hidden_dim = d->hidden_dim;
    dw->A = dw->output_dim = d->output_dim;
    dw->continuous = false;
    return dw;
}
static void create_chess_decoder(Decoder* d) {
    d->forward = chess_decoder_forward;
    d->bind_observation = chess_decoder_bind;
    d->backward = chess_decoder_backward;
    d->init_weights = chess_decoder_init;
    d->reg_params = chess_decoder_params;
    d->reg_train = chess_decoder_train;
    d->reg_rollout = chess_decoder_rollout;
    d->create_weights = chess_decoder_create;
    d->activation_size = sizeof(ChessDecoderActivations);
}
