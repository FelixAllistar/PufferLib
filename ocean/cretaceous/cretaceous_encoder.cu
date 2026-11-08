// Structured Cretaceous encoder (2026-11-07). Included by src/ocean.cu under PUFFER_CRETACEOUS.
// Opt-in with policy.cretaceous_net = 1 (config/cretaceous.ini); 0 keeps PufferLib's default linear encoder,
// so old checkpoints still load. Architecture, parameter counts and ablations: docs/encoder.md.
//
//   self   39 + 22 (contract 4 block) -> MLP 64 -> 64 (ReLU)           -> cat[  0: 64]
//   species one-hot 6 -> embedding 16 (linear)                         -> cat[256:272], and every neighbour's input
//   patch  7x7x2 -> conv3x3 16 (5x5) -> conv3x3 32 (3x3) -> FC 64      -> cat[ 64:128]
//   neighbours 6 x (10 + 4 (contract 4) + species embedding 16) -> shared MLP 64 -> 64 (ReLU), then over present slots only:
//          4-head attention pool, query = linear(self embedding)       -> cat[128:192]
//          max pool                                                    -> cat[192:256]
//   cat 272 -> FC 256 (ReLU) -> FC hidden (ReLU) -> MinGRU -> decoder
//
// Every linear layer has a bias: its input carries a constant 1 column, padded to a multiple of 8 (CE_AUG), so the
// bias is a column of the weight matrix (Muon then treats it with the matrix, as in pokemon3_encoder.cu). Every
// parameter therefore has numel % 8 == 0, which the flat bf16 param/grad buffers need (Muon walks them by numel while
// the allocator aligns each tensor to 16 bytes). No biases elsewhere, as in the other encoders.
// Activations are rows (row, features), convs are im2col + GEMM (as in chess_encoder.cu), dW = dY^T X overwrite the
// gradient buffers each backward, registered in the same order as the parameters.
// Pooling is permutation-invariant and masked by each neighbour's present flag (obs feature 0), so empty slots and
// slot order never change the output.

// Contract 4 appends a self block (22) and per-neighbour features (4) after the contract 3 layout; the gathers below
// join them to the self and neighbour inputs.
static_assert(CRET_CONTRACT_VERSION == 4, "cretaceous_encoder.cu reads policy contract 4; update it with sim.h");
static_assert(CRET_OBS_SIZE == 243 && CRET_OBS_V3 == 197 && CRET_SELF_OBS == 39 && CRET_SELF_OBS4 == 22 && CRET_PATCH == 7
    && CRET_NEIGHBORS == 6 && CRET_NEIGHBOR_OBS == 10 && CRET_NEIGHBOR_OBS4 == 4 && CRET_SPECIES == 6,
    "cretaceous obs layout changed: update cretaceous_encoder.cu");

#define CE_AUG(n) (((n) + 8) & ~7)        // n inputs + the bias column, padded to 8
#define CE_D 64                            // self, patch and neighbour embedding width
#define CE_DA CE_AUG(CE_D)                 // 72
#define CE_HEADS 4
#define CE_DH (CE_D / CE_HEADS)            // 16
#define CE_SP 16                           // species embedding
#define CE_SELF_IN (CRET_SELF_OBS + CRET_SELF_OBS4)   // 61
#define CE_SELF_AUG CE_AUG(CE_SELF_IN)     // 64
#define CE_OBS_SELF4 CRET_OBS_V3           // contract 4 self block
#define CE_OBS_NEIGH4 (CRET_OBS_V3 + CRET_SELF_OBS4)   // contract 4 neighbour features
#define CE_SP_AUG CE_AUG(CRET_SPECIES)     // 8
#define CE_ENT_OBS (CRET_NEIGHBOR_OBS + CRET_NEIGHBOR_OBS4)   // 14
#define CE_ENT_IN (CE_ENT_OBS + CE_SP)     // 30
#define CE_ENT_AUG CE_AUG(CE_ENT_IN)       // 32
#define CE_NB CRET_NEIGHBORS
#define CE_P CRET_PATCH                    // 7
#define CE_P1 (CE_P - 2)                   // 5: conv1 output side (valid 3x3)
#define CE_P2 (CE_P1 - 2)                  // 3: conv2 output side
#define CE_C1 16
#define CE_C2 32
#define CE_COL1 CE_AUG(9 * 2)              // 24
#define CE_COL2 CE_AUG(9 * CE_C1)          // 152
#define CE_FLAT (CE_P2 * CE_P2 * CE_C2)    // 288
#define CE_FLAT_AUG CE_AUG(CE_FLAT)        // 296
#define CE_CAT_SELF 0
#define CE_CAT_PATCH CE_D
#define CE_CAT_ATTN (2 * CE_D)
#define CE_CAT_MAX (3 * CE_D)
#define CE_CAT_SP (4 * CE_D)
#define CE_CAT (4 * CE_D + CE_SP)          // 272
#define CE_CAT_AUG CE_AUG(CE_CAT)          // 280
#define CE_TRUNK 256
#define CE_TRUNK_AUG CE_AUG(CE_TRUNK)      // 264

// policy.cretaceous_net (read before the trainer builds its nets, for train and eval/match).
static int cret_net_on = 0;
static inline void cret_configure(Ini* ini, const char* mode) {
    (void)mode;
    Dict* policy = puf_ini_section(ini, "policy", 0);
    DictItem* on = policy ? dict_find(policy, "cretaceous_net") : NULL;
    cret_net_on = on ? (int)on->value : 0;
}
#ifdef PUF_CONFIGURE
#error "cretaceous: PUF_CONFIGURE is already defined"
#endif
#define PUF_CONFIGURE(ini, mode) cret_configure(ini, mode)

enum { CE_W_SP, CE_W_S1, CE_W_S2, CE_W_Q, CE_W_C1, CE_W_C2, CE_W_P3, CE_W_E1, CE_W_E2, CE_W_F1, CE_W_F2, CE_NW };
static const char* CE_W_NAMES[CE_NW] = {"species", "self1", "self2", "query", "conv1", "conv2", "patch_fc",
    "neighbour1", "neighbour2", "trunk1", "trunk2"};
// (rows = outputs, cols = inputs incl. bias column)
static void ce_shape(int i, int hidden, long* rows, long* cols) {
    static const int R[CE_NW] = {CE_SP, CE_D, CE_D, CE_D, CE_C1, CE_C2, CE_D, CE_D, CE_D, CE_TRUNK, 0};
    static const int C[CE_NW] = {CE_SP_AUG, CE_SELF_AUG, CE_DA, CE_DA, CE_COL1, CE_COL2, CE_FLAT_AUG, CE_ENT_AUG,
        CE_DA, CE_CAT_AUG, CE_TRUNK_AUG};
    *rows = i == CE_W_F2 ? hidden : R[i]; *cols = C[i];
}

struct CretEncoderWeights {
    Prec w[CE_NW];
    int hidden;
};

struct CretEncoderActivations {
    // forward (rollout and train)
    Prec self_x, sp_x, s1a, selfa, q, sp;     // (B, 64) (B, 8) (B, 72) (B, 72) (B, 64) (B, 16)
    Prec col1, c1, col2, p3a;                 // (B*25, 24) (B*25, 16) (B*9, 152) (B, 296)
    Prec ent_x, e1a, h;                       // (B*6, 32) (B*6, 72) (B*6, 64)
    Prec cat, f1a, out, y;                    // (B, 280) (B, 264) (B, H); y: GEMM scratch
    // backward (train)
    Prec d_out, d_f1a, d_f1, d_cat, d_q, d_selfa, d_self, d_s1a, d_s1;
    Prec d_p, d_p3a, d_c2, d_col2, d_c1;
    Prec d_h, d_e1a, d_e1, d_ent_x, d_sp;
    Prec dw[CE_NW];
};

static Prec ce_view(Prec t, long rows, long cols) { return Prec{.data = t.data, .shape = {rows, cols}}; }
static int ce_grid(long n) { return (int)((n + BLOCK_SIZE - 1) / BLOCK_SIZE); }

// dst[r, off + c] = act(y[r, c]) for c < n; with aug, dst[r, off + n] = 1 and the rest of the row (to ld) = 0.
__global__ void ce_act(precision_t* __restrict__ dst, int ld, int off, const precision_t* __restrict__ y,
        int rows, int n, int relu, int aug) {
    int width = aug ? ld - off : n;
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)rows * width) return;
    int c = i % width; long r = i / width;
    float v = c < n ? to_float(y[r * n + c]) : (c == n ? 1.0f : 0.0f);
    if (relu && c < n) v = fmaxf(0.0f, v);
    dst[r * ld + off + c] = from_float(v);
}

// dst[r, c] = (g[r, g_off + c] + g2[r, g2_off + c]) * (y[r, y_off + c] > 0), c < n (g2 optional): ReLU backward
// through a post-activation buffer y.
__global__ void ce_relu_grad(precision_t* __restrict__ dst, const precision_t* __restrict__ g, int g_ld, int g_off,
        const precision_t* __restrict__ g2, int g2_ld, int g2_off, const precision_t* __restrict__ y, int y_ld,
        int y_off, int rows, int n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)rows * n) return;
    int c = i % n; long r = i / n;
    float v = to_float(g[r * g_ld + g_off + c]);
    if (g2) v += to_float(g2[r * g2_ld + g2_off + c]);
    dst[i] = from_float(to_float(y[r * y_ld + y_off + c]) > 0.0f ? v : 0.0f);
}

// self_x = [self obs, contract 4 self block, 1, 0], sp_x = [species one-hot, 1, 0]
__global__ void ce_gather_self(const precision_t* __restrict__ obs, precision_t* __restrict__ self_x,
        precision_t* __restrict__ sp_x, int B) {
    int W = CE_SELF_AUG + CE_SP_AUG;
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * W) return;
    int c = i % W; long b = i / W;
    const precision_t* o = obs + b * CRET_OBS_SIZE;
    if (c < CE_SELF_AUG) {
        self_x[b * CE_SELF_AUG + c] = c < CRET_SELF_OBS ? o[c] : c < CE_SELF_IN ? o[CE_OBS_SELF4 + c - CRET_SELF_OBS]
            : from_float(c == CE_SELF_IN ? 1.0f : 0.0f);
    } else {
        c -= CE_SELF_AUG;
        sp_x[b * CE_SP_AUG + c] = c < CRET_SPECIES ? o[CRET_OBS_SPECIES + c] : from_float(c == CRET_SPECIES ? 1.0f : 0.0f);
    }
}

// col1[(b*25 + oy*5 + ox), k*2 + ch] = patch[b, oy + k/3, ox + k%3, ch]; col 18 = 1
__global__ void ce_im2col1(const precision_t* __restrict__ obs, precision_t* __restrict__ col, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CE_P1 * CE_P1 * CE_COL1) return;
    int k = i % CE_COL1; long r = i / CE_COL1; int pos = r % (CE_P1 * CE_P1); long b = r / (CE_P1 * CE_P1);
    if (k >= 18) { col[i] = from_float(k == 18 ? 1.0f : 0.0f); return; }
    int kk = k / 2, ch = k % 2, y = pos / CE_P1 + kk / 3, x = pos % CE_P1 + kk % 3;
    col[i] = obs[b * CRET_OBS_SIZE + CRET_OBS_PATCH + (y * CE_P + x) * 2 + ch];
}

// col2[(b*9 + oy*3 + ox), k*16 + ch] = c1[b, oy + k/3, ox + k%3, ch]; col 144 = 1
__global__ void ce_im2col2(const precision_t* __restrict__ c1, precision_t* __restrict__ col, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CE_P2 * CE_P2 * CE_COL2) return;
    int k = i % CE_COL2; long r = i / CE_COL2; int pos = r % (CE_P2 * CE_P2); long b = r / (CE_P2 * CE_P2);
    if (k >= 9 * CE_C1) { col[i] = from_float(k == 9 * CE_C1 ? 1.0f : 0.0f); return; }
    int kk = k / CE_C1, ch = k % CE_C1, y = pos / CE_P2 + kk / 3, x = pos % CE_P2 + kk % 3;
    col[i] = c1[(b * CE_P1 * CE_P1 + y * CE_P1 + x) * CE_C1 + ch];
}

// d_c1[b, pos, ch] = (sum over the conv2 outputs that read pos) * (c1 > 0)   (gather, no atomics)
__global__ void ce_col2im2(const precision_t* __restrict__ dcol, const precision_t* __restrict__ c1,
        precision_t* __restrict__ dc1, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CE_P1 * CE_P1 * CE_C1) return;
    int ch = i % CE_C1; long r = i / CE_C1; int pos = r % (CE_P1 * CE_P1); long b = r / (CE_P1 * CE_P1);
    int y = pos / CE_P1, x = pos % CE_P1;
    float sum = 0.0f;
    for (int kk = 0; kk < 9; kk++) {
        int oy = y - kk / 3, ox = x - kk % 3;
        if (oy < 0 || oy >= CE_P2 || ox < 0 || ox >= CE_P2) continue;
        sum += to_float(dcol[(b * CE_P2 * CE_P2 + oy * CE_P2 + ox) * CE_COL2 + kk * CE_C1 + ch]);
    }
    dc1[i] = from_float(to_float(c1[i]) > 0.0f ? sum : 0.0f);
}

// ent_x[b*6 + n] = [neighbour n (10), its contract 4 features (4), species embedding (16), 1, 0...]
__global__ void ce_gather_ent(const precision_t* __restrict__ obs, const precision_t* __restrict__ sp,
        precision_t* __restrict__ ent_x, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CE_NB * CE_ENT_AUG) return;
    int c = i % CE_ENT_AUG; long r = i / CE_ENT_AUG; int n = r % CE_NB; long b = r / CE_NB;
    precision_t v;
    if (c < CRET_NEIGHBOR_OBS) v = obs[b * CRET_OBS_SIZE + CRET_OBS_NEIGHBOR + n * CRET_NEIGHBOR_OBS + c];
    else if (c < CE_ENT_OBS) v = obs[b * CRET_OBS_SIZE + CE_OBS_NEIGH4 + n * CRET_NEIGHBOR_OBS4 + c - CRET_NEIGHBOR_OBS];
    else if (c < CE_ENT_IN) v = sp[b * CE_SP + c - CE_ENT_OBS];
    else v = from_float(c == CE_ENT_IN ? 1.0f : 0.0f);
    ent_x[i] = v;
}

__device__ __forceinline__ bool ce_present(const precision_t* ent_x, long b, int n) {
    return to_float(ent_x[(b * CE_NB + n) * CE_ENT_AUG]) > 0.5f;
}

// Softmax weights of head g over the present neighbours (all zero when none is present).
__device__ __forceinline__ void ce_attn_weights(const precision_t* __restrict__ h, const precision_t* __restrict__ q,
        const precision_t* __restrict__ ent_x, long b, int g, float* alpha, bool* valid) {
    const float scale = 0.25f;   // 1 / sqrt(CE_DH)
    float s[CE_NB], best = -1e30f;
    for (int n = 0; n < CE_NB; n++) {
        valid[n] = ce_present(ent_x, b, n);
        float dot = 0.0f;
        for (int d = 0; d < CE_DH; d++)
            dot += to_float(q[b * CE_D + g * CE_DH + d]) * to_float(h[(b * CE_NB + n) * CE_D + g * CE_DH + d]);
        s[n] = dot * scale;
        if (valid[n]) best = fmaxf(best, s[n]);
    }
    float sum = 0.0f;
    for (int n = 0; n < CE_NB; n++) { alpha[n] = valid[n] ? __expf(s[n] - best) : 0.0f; sum += alpha[n]; }
    for (int n = 0; n < CE_NB; n++) alpha[n] = sum > 0.0f ? alpha[n] / sum : 0.0f;
}

// One thread per (row, head): attention pool -> cat[CE_CAT_ATTN + g*16 + d], max pool -> cat[CE_CAT_MAX + ...].
__global__ void ce_pool_fwd(const precision_t* __restrict__ h, const precision_t* __restrict__ q,
        const precision_t* __restrict__ ent_x, precision_t* __restrict__ cat, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CE_HEADS) return;
    int g = i % CE_HEADS; long b = i / CE_HEADS;
    float alpha[CE_NB]; bool valid[CE_NB];
    ce_attn_weights(h, q, ent_x, b, g, alpha, valid);
    for (int d = 0; d < CE_DH; d++) {
        float att = 0.0f, mx = 0.0f; bool any = false;
        for (int n = 0; n < CE_NB; n++) {
            if (!valid[n]) continue;
            float v = to_float(h[(b * CE_NB + n) * CE_D + g * CE_DH + d]);
            att += alpha[n] * v; mx = any ? fmaxf(mx, v) : v; any = true;
        }
        cat[b * CE_CAT_AUG + CE_CAT_ATTN + g * CE_DH + d] = from_float(att);
        cat[b * CE_CAT_AUG + CE_CAT_MAX + g * CE_DH + d] = from_float(mx);
    }
}

// Backward of ce_pool_fwd and of the neighbour MLP's last ReLU: d_h (B*6, 64), d_q (B, 64).
__global__ void ce_pool_bwd(const precision_t* __restrict__ h, const precision_t* __restrict__ q,
        const precision_t* __restrict__ ent_x, const precision_t* __restrict__ d_cat,
        precision_t* __restrict__ d_h, precision_t* __restrict__ d_q, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CE_HEADS) return;
    int g = i % CE_HEADS; long b = i / CE_HEADS;
    const float scale = 0.25f;
    float alpha[CE_NB]; bool valid[CE_NB];
    ce_attn_weights(h, q, ent_x, b, g, alpha, valid);
    float da[CE_DH], dalpha[CE_NB], ds[CE_NB], mean = 0.0f;
    for (int d = 0; d < CE_DH; d++) da[d] = to_float(d_cat[b * CE_CAT_AUG + CE_CAT_ATTN + g * CE_DH + d]);
    for (int n = 0; n < CE_NB; n++) {
        float dot = 0.0f;
        for (int d = 0; d < CE_DH; d++) dot += da[d] * to_float(h[(b * CE_NB + n) * CE_D + g * CE_DH + d]);
        dalpha[n] = dot; mean += alpha[n] * dot;
    }
    for (int n = 0; n < CE_NB; n++) ds[n] = alpha[n] * (dalpha[n] - mean);
    for (int d = 0; d < CE_DH; d++) {
        float qd = to_float(q[b * CE_D + g * CE_DH + d]), dm = to_float(d_cat[b * CE_CAT_AUG + CE_CAT_MAX + g * CE_DH + d]);
        int arg = -1; float mx = 0.0f, dq = 0.0f;
        for (int n = 0; n < CE_NB; n++) {
            if (!valid[n]) continue;
            float v = to_float(h[(b * CE_NB + n) * CE_D + g * CE_DH + d]);
            if (arg < 0 || v > mx) { mx = v; arg = n; }
            dq += ds[n] * v * scale;
        }
        d_q[b * CE_D + g * CE_DH + d] = from_float(dq);
        for (int n = 0; n < CE_NB; n++) {
            long k = (b * CE_NB + n) * CE_D + g * CE_DH + d;
            float v = to_float(h[k]);
            float gv = valid[n] ? alpha[n] * da[d] + ds[n] * qd * scale + (n == arg ? dm : 0.0f) : 0.0f;
            d_h[k] = from_float(v > 0.0f ? gv : 0.0f);
        }
    }
}

// d_sp[b, j] = d_cat[b, CE_CAT_SP + j] + sum over neighbours of d_ent_x[b*6 + n, 14 + j]
__global__ void ce_sp_grad(const precision_t* __restrict__ d_cat, const precision_t* __restrict__ d_ent_x,
        precision_t* __restrict__ d_sp, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CE_SP) return;
    int j = i % CE_SP; long b = i / CE_SP;
    float v = to_float(d_cat[b * CE_CAT_AUG + CE_CAT_SP + j]);
    for (int n = 0; n < CE_NB; n++) v += to_float(d_ent_x[(b * CE_NB + n) * CE_ENT_AUG + CE_ENT_OBS + j]);
    d_sp[i] = from_float(v);
}

// y = x @ w^T for row-major views
static void ce_mm(Prec x, long rows, long in, Prec w, long out, Prec y, cudaStream_t st) {
    Prec xv = ce_view(x, rows, in), wv = ce_view(w, out, in), yv = ce_view(y, rows, out);
    puf_mm(&xv, &wv, &yv, st);
}
// dw = dy^T @ x;  dx = dy @ w (if dx.data)
static void ce_mm_bwd(Prec dy, Prec x, Prec w, Prec dw, Prec dx, long rows, long in, long out, cudaStream_t st) {
    Prec dyv = ce_view(dy, rows, out), xv = ce_view(x, rows, in), dwv = ce_view(dw, out, in);
    puf_mm_tn(&dyv, &xv, &dwv, st);
    if (!dx.data) return;
    Prec wv = ce_view(w, out, in), dxv = ce_view(dx, rows, in);
    puf_mm_nn(&dyv, &wv, &dxv, st);
}
static void ce_act_to(Prec dst, int ld, int off, Prec y, long rows, int n, int relu, int aug, cudaStream_t st) {
    long width = aug ? ld - off : n;
    ce_act<<<ce_grid(rows * width), BLOCK_SIZE, 0, st>>>(dst.data, ld, off, y.data, (int)rows, n, relu, aug);
}
static void ce_relu_grad_to(Prec dst, Prec g, int g_ld, int g_off, const precision_t* g2, int g2_ld, int g2_off,
        Prec y, int y_ld, int y_off, long rows, int n, cudaStream_t st) {
    ce_relu_grad<<<ce_grid(rows * n), BLOCK_SIZE, 0, st>>>(dst.data, g.data, g_ld, g_off, g2, g2_ld, g2_off,
        y.data, y_ld, y_off, (int)rows, n);
}

static Prec cret_encoder_forward(void* w, void* activations, Prec input, cudaStream_t st) {
    CretEncoderWeights* ew = (CretEncoderWeights*)w;
    CretEncoderActivations* a = (CretEncoderActivations*)activations;
    Prec* W = ew->w;
    long B = input.shape[0], H = ew->hidden, R1 = B * CE_P1 * CE_P1, R2 = B * CE_P2 * CE_P2, RN = B * CE_NB;
    assert(input.shape[1] == CRET_OBS_SIZE);
    // self, species
    ce_gather_self<<<ce_grid(B * (CE_SELF_AUG + CE_SP_AUG)), BLOCK_SIZE, 0, st>>>(input.data, a->self_x.data,
        a->sp_x.data, (int)B);
    ce_mm(a->sp_x, B, CE_SP_AUG, W[CE_W_SP], CE_SP, a->sp, st);
    ce_act_to(a->cat, CE_CAT_AUG, CE_CAT_SP, a->sp, B, CE_SP, 0, 0, st);
    ce_mm(a->self_x, B, CE_SELF_AUG, W[CE_W_S1], CE_D, a->y, st);
    ce_act_to(a->s1a, CE_DA, 0, a->y, B, CE_D, 1, 1, st);
    ce_mm(a->s1a, B, CE_DA, W[CE_W_S2], CE_D, a->y, st);
    ce_act_to(a->selfa, CE_DA, 0, a->y, B, CE_D, 1, 1, st);
    ce_act_to(a->cat, CE_CAT_AUG, CE_CAT_SELF, a->y, B, CE_D, 1, 0, st);
    ce_mm(a->selfa, B, CE_DA, W[CE_W_Q], CE_D, a->q, st);
    // terrain patch
    ce_im2col1<<<ce_grid(R1 * CE_COL1), BLOCK_SIZE, 0, st>>>(input.data, a->col1.data, (int)B);
    ce_mm(a->col1, R1, CE_COL1, W[CE_W_C1], CE_C1, a->y, st);
    ce_act_to(a->c1, CE_C1, 0, a->y, R1, CE_C1, 1, 0, st);
    ce_im2col2<<<ce_grid(R2 * CE_COL2), BLOCK_SIZE, 0, st>>>(a->c1.data, a->col2.data, (int)B);
    ce_mm(a->col2, R2, CE_COL2, W[CE_W_C2], CE_C2, a->y, st);
    ce_act_to(a->p3a, CE_FLAT_AUG, 0, a->y, B, CE_FLAT, 1, 1, st);   // (B*9, 32) rows are (B, 288) rows
    ce_mm(a->p3a, B, CE_FLAT_AUG, W[CE_W_P3], CE_D, a->y, st);
    ce_act_to(a->cat, CE_CAT_AUG, CE_CAT_PATCH, a->y, B, CE_D, 1, 0, st);
    // neighbours
    ce_gather_ent<<<ce_grid(RN * CE_ENT_AUG), BLOCK_SIZE, 0, st>>>(input.data, a->sp.data, a->ent_x.data, (int)B);
    ce_mm(a->ent_x, RN, CE_ENT_AUG, W[CE_W_E1], CE_D, a->y, st);
    ce_act_to(a->e1a, CE_DA, 0, a->y, RN, CE_D, 1, 1, st);
    ce_mm(a->e1a, RN, CE_DA, W[CE_W_E2], CE_D, a->y, st);
    ce_act_to(a->h, CE_D, 0, a->y, RN, CE_D, 1, 0, st);
    ce_pool_fwd<<<ce_grid(B * CE_HEADS), BLOCK_SIZE, 0, st>>>(a->h.data, a->q.data, a->ent_x.data, a->cat.data, (int)B);
    // bias column of cat (cols CE_CAT.. CE_CAT_AUG): an empty y-less act writes [1, 0, ...]
    ce_act_to(a->cat, CE_CAT_AUG, CE_CAT, a->y, B, 0, 0, 1, st);
    // trunk
    ce_mm(a->cat, B, CE_CAT_AUG, W[CE_W_F1], CE_TRUNK, a->y, st);
    ce_act_to(a->f1a, CE_TRUNK_AUG, 0, a->y, B, CE_TRUNK, 1, 1, st);
    ce_mm(a->f1a, B, CE_TRUNK_AUG, W[CE_W_F2], H, a->y, st);
    Prec out = ce_view(a->out, B, H);
    ce_act_to(out, (int)H, 0, a->y, B, (int)H, 1, 0, st);
    return out;
}

static void cret_encoder_backward(void* w, void* activations, Prec grad, cudaStream_t st) {
    CretEncoderWeights* ew = (CretEncoderWeights*)w;
    CretEncoderActivations* a = (CretEncoderActivations*)activations;
    Prec* W = ew->w; Prec* dW = a->dw; Prec none = {};
    long B = grad.shape[0], H = ew->hidden, R1 = B * CE_P1 * CE_P1, R2 = B * CE_P2 * CE_P2, RN = B * CE_NB;
    // trunk
    ce_relu_grad_to(a->d_out, grad, (int)H, 0, NULL, 0, 0, a->out, (int)H, 0, B, (int)H, st);
    ce_mm_bwd(a->d_out, a->f1a, W[CE_W_F2], dW[CE_W_F2], a->d_f1a, B, CE_TRUNK_AUG, H, st);
    ce_relu_grad_to(a->d_f1, a->d_f1a, CE_TRUNK_AUG, 0, NULL, 0, 0, a->f1a, CE_TRUNK_AUG, 0, B, CE_TRUNK, st);
    ce_mm_bwd(a->d_f1, a->cat, W[CE_W_F1], dW[CE_W_F1], a->d_cat, B, CE_CAT_AUG, CE_TRUNK, st);
    // pooling -> neighbour MLP
    ce_pool_bwd<<<ce_grid(B * CE_HEADS), BLOCK_SIZE, 0, st>>>(a->h.data, a->q.data, a->ent_x.data, a->d_cat.data,
        a->d_h.data, a->d_q.data, (int)B);
    ce_mm_bwd(a->d_h, a->e1a, W[CE_W_E2], dW[CE_W_E2], a->d_e1a, RN, CE_DA, CE_D, st);
    ce_relu_grad_to(a->d_e1, a->d_e1a, CE_DA, 0, NULL, 0, 0, a->e1a, CE_DA, 0, RN, CE_D, st);
    ce_mm_bwd(a->d_e1, a->ent_x, W[CE_W_E1], dW[CE_W_E1], a->d_ent_x, RN, CE_ENT_AUG, CE_D, st);
    // query -> self MLP (self embedding gets the cat slice and the query's gradient)
    ce_mm_bwd(a->d_q, a->selfa, W[CE_W_Q], dW[CE_W_Q], a->d_selfa, B, CE_DA, CE_D, st);
    ce_relu_grad_to(a->d_self, a->d_cat, CE_CAT_AUG, CE_CAT_SELF, a->d_selfa.data, CE_DA, 0, a->selfa, CE_DA, 0,
        B, CE_D, st);
    ce_mm_bwd(a->d_self, a->s1a, W[CE_W_S2], dW[CE_W_S2], a->d_s1a, B, CE_DA, CE_D, st);
    ce_relu_grad_to(a->d_s1, a->d_s1a, CE_DA, 0, NULL, 0, 0, a->s1a, CE_DA, 0, B, CE_D, st);
    ce_mm_bwd(a->d_s1, a->self_x, W[CE_W_S1], dW[CE_W_S1], none, B, CE_SELF_AUG, CE_D, st);
    // species embedding (cat slice + every neighbour's input)
    ce_sp_grad<<<ce_grid(B * CE_SP), BLOCK_SIZE, 0, st>>>(a->d_cat.data, a->d_ent_x.data, a->d_sp.data, (int)B);
    ce_mm_bwd(a->d_sp, a->sp_x, W[CE_W_SP], dW[CE_W_SP], none, B, CE_SP_AUG, CE_SP, st);
    // patch
    ce_relu_grad_to(a->d_p, a->d_cat, CE_CAT_AUG, CE_CAT_PATCH, NULL, 0, 0, a->cat, CE_CAT_AUG, CE_CAT_PATCH,
        B, CE_D, st);
    ce_mm_bwd(a->d_p, a->p3a, W[CE_W_P3], dW[CE_W_P3], a->d_p3a, B, CE_FLAT_AUG, CE_D, st);
    ce_relu_grad_to(a->d_c2, a->d_p3a, CE_FLAT_AUG, 0, NULL, 0, 0, a->p3a, CE_FLAT_AUG, 0, B, CE_FLAT, st);
    ce_mm_bwd(a->d_c2, a->col2, W[CE_W_C2], dW[CE_W_C2], a->d_col2, R2, CE_COL2, CE_C2, st);
    ce_col2im2<<<ce_grid(R1 * CE_C1), BLOCK_SIZE, 0, st>>>(a->d_col2.data, a->c1.data, a->d_c1.data, (int)B);
    ce_mm_bwd(a->d_c1, a->col1, W[CE_W_C1], dW[CE_W_C1], none, R1, CE_COL1, CE_C1, st);
}

static void cret_encoder_init(void* w, ulong* seed, cudaStream_t st) {
    CretEncoderWeights* ew = (CretEncoderWeights*)w;
    for (int i = 0; i < CE_NW; i++) {
        float gain = (i == CE_W_SP || i == CE_W_Q) ? 1.0f : sqrtf(2.0f);   // linear outputs vs ReLU layers
        puf_kaiming_init(&ew->w[i], gain, (*seed)++, st);
    }
}

static void cret_encoder_params(void* w, Allocator* alloc) {
    CretEncoderWeights* ew = (CretEncoderWeights*)w;
    for (int i = 0; i < CE_NW; i++) {
        long r, c; ce_shape(i, ew->hidden, &r, &c);
        ew->w[i] = {.shape = {r, c}};
        assert((r * c) % 8 == 0 && "cretaceous encoder: params must be multiples of 8 (flat bf16 buffers)");
        alloc_register(alloc, &ew->w[i]);
    }
}

static void ce_reg(Allocator* alloc, Prec* t, long rows, long cols) {
    *t = {.shape = {rows, cols}};
    alloc_register(alloc, t);
}

static void cret_encoder_buffers(CretEncoderWeights* ew, CretEncoderActivations* a, Allocator* acts, long B) {
    long H = ew->hidden, R1 = B * CE_P1 * CE_P1, R2 = B * CE_P2 * CE_P2, RN = B * CE_NB;
    ce_reg(acts, &a->self_x, B, CE_SELF_AUG); ce_reg(acts, &a->sp_x, B, CE_SP_AUG);
    ce_reg(acts, &a->s1a, B, CE_DA); ce_reg(acts, &a->selfa, B, CE_DA);
    ce_reg(acts, &a->q, B, CE_D); ce_reg(acts, &a->sp, B, CE_SP);
    ce_reg(acts, &a->col1, R1, CE_COL1); ce_reg(acts, &a->c1, R1, CE_C1);
    ce_reg(acts, &a->col2, R2, CE_COL2); ce_reg(acts, &a->p3a, B, CE_FLAT_AUG);
    ce_reg(acts, &a->ent_x, RN, CE_ENT_AUG); ce_reg(acts, &a->e1a, RN, CE_DA); ce_reg(acts, &a->h, RN, CE_D);
    ce_reg(acts, &a->cat, B, CE_CAT_AUG); ce_reg(acts, &a->f1a, B, CE_TRUNK_AUG); ce_reg(acts, &a->out, B, H);
    long ymax = H > CE_TRUNK ? H : CE_TRUNK;   // widest GEMM output per row: max(H, 256, 25*16, 9*32, 6*64)
    if (ymax < CE_P1 * CE_P1 * CE_C1) ymax = CE_P1 * CE_P1 * CE_C1;
    ce_reg(acts, &a->y, B, ymax);
}

static void cret_encoder_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B) {
    CretEncoderWeights* ew = (CretEncoderWeights*)w;
    CretEncoderActivations* a = (CretEncoderActivations*)activations;
    long H = ew->hidden, R1 = (long)B * CE_P1 * CE_P1, R2 = (long)B * CE_P2 * CE_P2, RN = (long)B * CE_NB;
    *a = {};
    cret_encoder_buffers(ew, a, acts, B);
    ce_reg(acts, &a->d_out, B, H); ce_reg(acts, &a->d_f1a, B, CE_TRUNK_AUG); ce_reg(acts, &a->d_f1, B, CE_TRUNK);
    ce_reg(acts, &a->d_cat, B, CE_CAT_AUG); ce_reg(acts, &a->d_q, B, CE_D); ce_reg(acts, &a->d_selfa, B, CE_DA);
    ce_reg(acts, &a->d_self, B, CE_D); ce_reg(acts, &a->d_s1a, B, CE_DA); ce_reg(acts, &a->d_s1, B, CE_D);
    ce_reg(acts, &a->d_p, B, CE_D); ce_reg(acts, &a->d_p3a, B, CE_FLAT_AUG); ce_reg(acts, &a->d_c2, R2, CE_C2);
    ce_reg(acts, &a->d_col2, R2, CE_COL2); ce_reg(acts, &a->d_c1, R1, CE_C1);
    ce_reg(acts, &a->d_h, RN, CE_D); ce_reg(acts, &a->d_e1a, RN, CE_DA); ce_reg(acts, &a->d_e1, RN, CE_D);
    ce_reg(acts, &a->d_ent_x, RN, CE_ENT_AUG); ce_reg(acts, &a->d_sp, B, CE_SP);
    for (int i = 0; i < CE_NW; i++) {   // gradients in the same order as the parameters
        long r, c; ce_shape(i, ew->hidden, &r, &c);
        ce_reg(grads, &a->dw[i], r, c);
    }
}

static void cret_encoder_rollout(void* w, void* activations, Allocator* alloc, int B) {
    CretEncoderActivations* a = (CretEncoderActivations*)activations;
    *a = {};
    cret_encoder_buffers((CretEncoderWeights*)w, a, alloc, B);
}

static void* cret_encoder_create(void* self) {
    Encoder* e = (Encoder*)self;
    assert(e->in_dim == CRET_OBS_SIZE);
    CretEncoderWeights* ew = (CretEncoderWeights*)calloc(1, sizeof(CretEncoderWeights));
    ew->hidden = e->out_dim;
    return ew;
}

static long cret_encoder_param_count(int hidden) {
    long n = 0;
    for (int i = 0; i < CE_NW; i++) { long r, c; ce_shape(i, hidden, &r, &c); n += r * c; }
    return n;
}

// No-op unless policy.cretaceous_net = 1.
static void create_cretaceous_encoder(Encoder* e) {
    if (!cret_net_on) return;
    e->forward = cret_encoder_forward;
    e->backward = cret_encoder_backward;
    e->init_weights = cret_encoder_init;
    e->reg_params = cret_encoder_params;
    e->reg_train = cret_encoder_train;
    e->reg_rollout = cret_encoder_rollout;
    e->create_weights = cret_encoder_create;
    e->activation_size = sizeof(CretEncoderActivations);
    static bool reported = false;
    if (!reported) {
        printf("cretaceous: structured encoder (policy.cretaceous_net = 1), %ld encoder params at hidden %d\n",
            cret_encoder_param_count(e->out_dim), e->out_dim);
        reported = true;
    }
}
