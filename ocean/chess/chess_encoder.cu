// SE-ResNet board encoder for chess (2026-11-02). Included by src/ocean.cu when PUFFER_CHESS is defined.
// Input: the 0/1 observation of chess.h (12 one-hot piece planes, legal-from and legal-to planes, then 27 scalar
// flags), gathered into 41 channels per square (14 planes + the 27 flags broadcast to every square).
// Body: 3x3 conv stem 41 -> C, then NB residual blocks: conv3x3 -> ReLU -> conv3x3 -> squeeze-and-excitation
// (mean over the 64 squares -> FC C -> C/4 -> ReLU -> FC -> C -> sigmoid, channel scale) -> + skip -> ReLU.
// Head: flatten 64*C -> linear -> hidden, ReLU (then the usual recurrent core and decoder).
// Activations are rows (B*64, channels), square-major; convs are im2col + GEMM, no biases (as in nmmo3.cu).

#define CHESS_SQ 64
#define CHESS_PLANES 14
#define CHESS_FLAGS (OBS_SIZE - CHESS_PLANES * CHESS_SQ)  // 27
#define CHESS_IN (CHESS_PLANES + CHESS_FLAGS)             // 41
#ifndef CHESS_C
#define CHESS_C 64
#endif
#ifndef CHESS_NB
#define CHESS_NB 4
#endif
#define CHESS_SE (CHESS_C / 4)

__global__ void chess_gather_input(const precision_t* __restrict__ obs, precision_t* __restrict__ x, int B) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= B * CHESS_SQ * CHESS_IN) return;
    int c = i % CHESS_IN, r = i / CHESS_IN, s = r % CHESS_SQ, b = r / CHESS_SQ;
    const precision_t* o = obs + (long)b * OBS_SIZE;
    x[i] = c < CHESS_PLANES ? o[c * CHESS_SQ + s] : o[CHESS_PLANES * CHESS_SQ + (c - CHESS_PLANES)];
}

// col[(b*64+s), k*Cin + c] = x[b, s shifted by kernel offset k, c] (zero outside the board)
__global__ void chess_im2col(const precision_t* __restrict__ x, precision_t* __restrict__ col, int B, int Cin) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    long n = (long)B * CHESS_SQ * 9 * Cin;
    if (i >= n) return;
    int c = i % Cin; long t = i / Cin; int k = t % 9; long r = t / 9;
    int s = r % CHESS_SQ; long b = r / CHESS_SQ;
    int y = s / 8 + k / 3 - 1, xx = s % 8 + k % 3 - 1;
    col[i] = (y < 0 || y > 7 || xx < 0 || xx > 7) ? from_float(0.0f) : x[(b * CHESS_SQ + y * 8 + xx) * Cin + c];
}

// dx[b, s, c] = sum over k of dcol[b, s - offset_k, k*Cin + c]  (gather; no atomics)
__global__ void chess_col2im(const precision_t* __restrict__ dcol, precision_t* __restrict__ dx, int B, int Cin) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CHESS_SQ * Cin) return;
    int c = i % Cin; long r = i / Cin; int s = r % CHESS_SQ; long b = r / CHESS_SQ;
    float sum = 0.0f;
    for (int k = 0; k < 9; k++) {
        int y = s / 8 - (k / 3 - 1), xx = s % 8 - (k % 3 - 1);
        if (y < 0 || y > 7 || xx < 0 || xx > 7) continue;
        sum += to_float(dcol[((b * CHESS_SQ + y * 8 + xx) * 9 + k) * Cin + c]);
    }
    dx[i] = from_float(sum);
}

__global__ void chess_relu(precision_t* __restrict__ x, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && to_float(x[i]) < 0.0f) x[i] = from_float(0.0f);
}
__global__ void chess_relu_bwd(precision_t* __restrict__ g, const precision_t* __restrict__ y, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && to_float(y[i]) <= 0.0f) g[i] = from_float(0.0f);
}
// p[b, c] = mean over squares of y[b, s, c]
__global__ void chess_pool(const precision_t* __restrict__ y, precision_t* __restrict__ p, int B) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= B * CHESS_C) return;
    int c = i % CHESS_C, b = i / CHESS_C;
    float sum = 0.0f;
    for (int s = 0; s < CHESS_SQ; s++) sum += to_float(y[((long)b * CHESS_SQ + s) * CHESS_C + c]);
    p[i] = from_float(sum / CHESS_SQ);
}
__global__ void chess_sigmoid(precision_t* __restrict__ x, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) x[i] = from_float(1.0f / (1.0f + __expf(-to_float(x[i]))));
}
__global__ void chess_copy_relu(const precision_t* __restrict__ src, precision_t* __restrict__ dst, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) dst[i] = from_float(fmaxf(0.0f, to_float(src[i])));
}
// out = relu(y * s[b, c] + skip)
__global__ void chess_se_out(const precision_t* __restrict__ y, const precision_t* __restrict__ s,
        const precision_t* __restrict__ skip, precision_t* __restrict__ out, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CHESS_SQ * CHESS_C) return;
    int c = i % CHESS_C; long b = i / CHESS_C / CHESS_SQ;
    out[i] = from_float(fmaxf(0.0f, to_float(y[i]) * to_float(s[b * CHESS_C + c]) + to_float(skip[i])));
}
// gy = g * s[b, c]
__global__ void chess_scale_grad(const precision_t* __restrict__ g, const precision_t* __restrict__ s,
        precision_t* __restrict__ gy, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CHESS_SQ * CHESS_C) return;
    int c = i % CHESS_C; long b = i / CHESS_C / CHESS_SQ;
    gy[i] = from_float(to_float(g[i]) * to_float(s[b * CHESS_C + c]));
}
// gz2[b, c] = (sum_s g * y) * s * (1 - s)   (gradient at the pre-sigmoid SE output)
__global__ void chess_se_gate_grad(const precision_t* __restrict__ g, const precision_t* __restrict__ y,
        const precision_t* __restrict__ s, precision_t* __restrict__ gz2, int B) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= B * CHESS_C) return;
    int c = i % CHESS_C, b = i / CHESS_C;
    float sum = 0.0f;
    for (int q = 0; q < CHESS_SQ; q++) {
        long k = ((long)b * CHESS_SQ + q) * CHESS_C + c;
        sum += to_float(g[k]) * to_float(y[k]);
    }
    float sv = to_float(s[i]);
    gz2[i] = from_float(sum * sv * (1.0f - sv));
}
// gy[b, s, c] += gp[b, c] / 64   (the pooling's gradient)
__global__ void chess_pool_grad_add(precision_t* __restrict__ gy, const precision_t* __restrict__ gp, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * CHESS_SQ * CHESS_C) return;
    int c = i % CHESS_C; long b = i / CHESS_C / CHESS_SQ;
    gy[i] = from_float(to_float(gy[i]) + to_float(gp[b * CHESS_C + c]) / CHESS_SQ);
}
__global__ void chess_add(precision_t* __restrict__ a, const precision_t* __restrict__ b, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) a[i] = from_float(to_float(a[i]) + to_float(b[i]));
}
static int chess_grid(long n) { return (int)((n + BLOCK_SIZE - 1) / BLOCK_SIZE); }

struct ChessEncoderWeights {
    Prec stem;                       // (C, 9*41)
    Prec wa[CHESS_NB], wb[CHESS_NB];  // (C, 9*C)
    Prec w1[CHESS_NB];                // (C/4, C)
    Prec w2[CHESS_NB];                // (C, C/4)
    Prec proj;                       // (hidden, 64*C)
    int hidden;
};
struct ChessEncoderActivations {
    Prec x0, u[CHESS_NB + 1], h1[CHESS_NB], y2[CHESS_NB], p[CHESS_NB], z1[CHESS_NB], a1[CHESS_NB], s[CHESS_NB];
    Prec col, out;
    Prec g1, g2, g3, gp, gs, ga;     // backward scratch
    Prec dstem, dwa[CHESS_NB], dwb[CHESS_NB], dw1[CHESS_NB], dw2[CHESS_NB], dproj;
};
static Prec chess_view(Prec t, long rows, long cols) { return Prec{.data = t.data, .shape = {rows, cols}}; }

// y = conv3x3(x) as rows (B*64, Cout); col is scratch
static void chess_conv(Prec x, Prec w, Prec y, Prec col, int B, int Cin, int Cout, cudaStream_t st) {
    long R = (long)B * CHESS_SQ;
    chess_im2col<<<chess_grid(R * 9 * Cin), BLOCK_SIZE, 0, st>>>(x.data, col.data, B, Cin);
    Prec c = chess_view(col, R, 9 * Cin), yy = chess_view(y, R, Cout), ww = chess_view(w, Cout, 9 * Cin);
    puf_mm(&c, &ww, &yy, st);
}
// given gy (rows, Cout) and the conv input x: dw = gy^T @ col(x); gx = col2im(gy @ w) (if gx.data)
static void chess_conv_bwd(Prec x, Prec w, Prec gy, Prec dw, Prec gx, Prec col, int B, int Cin, int Cout,
        cudaStream_t st) {
    long R = (long)B * CHESS_SQ;
    chess_im2col<<<chess_grid(R * 9 * Cin), BLOCK_SIZE, 0, st>>>(x.data, col.data, B, Cin);
    Prec c = chess_view(col, R, 9 * Cin), g = chess_view(gy, R, Cout), ww = chess_view(w, Cout, 9 * Cin);
    Prec d = chess_view(dw, Cout, 9 * Cin);
    puf_mm_tn(&g, &c, &d, st);
    if (!gx.data) return;
    puf_mm_nn(&g, &ww, &c, st);  // col now holds d(col)
    chess_col2im<<<chess_grid(R * Cin), BLOCK_SIZE, 0, st>>>(col.data, gx.data, B, Cin);
}

static Prec chess_encoder_forward(void* w, void* activations, Prec input, cudaStream_t st) {
    ChessEncoderWeights* ew = (ChessEncoderWeights*)w;
    ChessEncoderActivations* a = (ChessEncoderActivations*)activations;
    int B = input.shape[0];
    long R = (long)B * CHESS_SQ, N = R * CHESS_C;
    chess_gather_input<<<chess_grid(R * CHESS_IN), BLOCK_SIZE, 0, st>>>(input.data, a->x0.data, B);
    chess_conv(a->x0, ew->stem, a->u[0], a->col, B, CHESS_IN, CHESS_C, st);
    chess_relu<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->u[0].data, N);
    for (int l = 0; l < CHESS_NB; l++) {
        chess_conv(a->u[l], ew->wa[l], a->h1[l], a->col, B, CHESS_C, CHESS_C, st);
        chess_relu<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->h1[l].data, N);
        chess_conv(a->h1[l], ew->wb[l], a->y2[l], a->col, B, CHESS_C, CHESS_C, st);
        chess_pool<<<chess_grid(B * CHESS_C), BLOCK_SIZE, 0, st>>>(a->y2[l].data, a->p[l].data, B);
        Prec p = chess_view(a->p[l], B, CHESS_C), z1 = chess_view(a->z1[l], B, CHESS_SE);
        Prec a1 = chess_view(a->a1[l], B, CHESS_SE), s = chess_view(a->s[l], B, CHESS_C);
        puf_mm(&p, &ew->w1[l], &z1, st);
        chess_copy_relu<<<chess_grid(B * CHESS_SE), BLOCK_SIZE, 0, st>>>(z1.data, a1.data, B * CHESS_SE);
        puf_mm(&a1, &ew->w2[l], &s, st);
        chess_sigmoid<<<chess_grid(B * CHESS_C), BLOCK_SIZE, 0, st>>>(s.data, B * CHESS_C);
        chess_se_out<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->y2[l].data, s.data, a->u[l].data, a->u[l + 1].data, B);
    }
    Prec flat = chess_view(a->u[CHESS_NB], B, CHESS_SQ * CHESS_C), out = chess_view(a->out, B, ew->hidden);
    puf_mm(&flat, &ew->proj, &out, st);
    chess_relu<<<chess_grid((long)B * ew->hidden), BLOCK_SIZE, 0, st>>>(out.data, (long)B * ew->hidden);
    return out;
}

static void chess_encoder_backward(void* w, void* activations, Prec grad, cudaStream_t st) {
    ChessEncoderWeights* ew = (ChessEncoderWeights*)w;
    ChessEncoderActivations* a = (ChessEncoderActivations*)activations;
    int B = grad.shape[0];
    long R = (long)B * CHESS_SQ, N = R * CHESS_C;
    chess_relu_bwd<<<chess_grid((long)B * ew->hidden), BLOCK_SIZE, 0, st>>>(grad.data, a->out.data, (long)B * ew->hidden);
    Prec g = chess_view(grad, B, ew->hidden), flat = chess_view(a->u[CHESS_NB], B, CHESS_SQ * CHESS_C);
    Prec dproj = chess_view(a->dproj, ew->hidden, CHESS_SQ * CHESS_C), gflat = chess_view(a->g1, B, CHESS_SQ * CHESS_C);
    puf_mm_tn(&g, &flat, &dproj, st);
    puf_mm_nn(&g, &ew->proj, &gflat, st);  // g1 = d(u[NB])
    for (int l = CHESS_NB - 1; l >= 0; l--) {
        // g1 = d(out of block l); out = relu(y2*s + u)
        chess_relu_bwd<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->g1.data, a->u[l + 1].data, N);
        // SE gate: gs -> gz2 (in gs), then dw2, ga = gz2 @ w2, gz1 = ga * (z1 > 0), dw1, gp = gz1 @ w1
        chess_se_gate_grad<<<chess_grid(B * CHESS_C), BLOCK_SIZE, 0, st>>>(a->g1.data, a->y2[l].data, a->s[l].data,
            a->gs.data, B);
        Prec gz2 = chess_view(a->gs, B, CHESS_C), a1 = chess_view(a->a1[l], B, CHESS_SE);
        Prec dw2 = chess_view(a->dw2[l], CHESS_C, CHESS_SE), ga = chess_view(a->ga, B, CHESS_SE);
        puf_mm_tn(&gz2, &a1, &dw2, st);
        puf_mm_nn(&gz2, &ew->w2[l], &ga, st);
        chess_relu_bwd<<<chess_grid(B * CHESS_SE), BLOCK_SIZE, 0, st>>>(ga.data, a->z1[l].data, B * CHESS_SE);
        Prec p = chess_view(a->p[l], B, CHESS_C), dw1 = chess_view(a->dw1[l], CHESS_SE, CHESS_C);
        Prec gp = chess_view(a->gp, B, CHESS_C);
        puf_mm_tn(&ga, &p, &dw1, st);
        puf_mm_nn(&ga, &ew->w1[l], &gp, st);
        // g2 = d(y2) = g1 * s + gp / 64
        chess_scale_grad<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->g1.data, a->s[l].data, a->g2.data, B);
        chess_pool_grad_add<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->g2.data, gp.data, B);
        // conv b: g3 = d(h1), then through the ReLU
        chess_conv_bwd(a->h1[l], ew->wb[l], a->g2, a->dwb[l], a->g3, a->col, B, CHESS_C, CHESS_C, st);
        chess_relu_bwd<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->g3.data, a->h1[l].data, N);
        // conv a: g2 = d(u[l]) through the conv; then + skip (g1)
        chess_conv_bwd(a->u[l], ew->wa[l], a->g3, a->dwa[l], a->g2, a->col, B, CHESS_C, CHESS_C, st);
        chess_add<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->g2.data, a->g1.data, N);
        cudaMemcpyAsync(a->g1.data, a->g2.data, N * sizeof(precision_t), cudaMemcpyDeviceToDevice, st);
    }
    chess_relu_bwd<<<chess_grid(N), BLOCK_SIZE, 0, st>>>(a->g1.data, a->u[0].data, N);
    Prec none = {};
    chess_conv_bwd(a->x0, ew->stem, a->g1, a->dstem, none, a->col, B, CHESS_IN, CHESS_C, st);
}

static void chess_encoder_init(void* w, ulong* seed, cudaStream_t st) {
    ChessEncoderWeights* ew = (ChessEncoderWeights*)w;
    puf_kaiming_init(&ew->stem, 1.0f, (*seed)++, st);
    for (int l = 0; l < CHESS_NB; l++) {
        puf_kaiming_init(&ew->wa[l], 1.0f, (*seed)++, st);
        puf_kaiming_init(&ew->wb[l], 1.0f, (*seed)++, st);
        puf_kaiming_init(&ew->w1[l], 1.0f, (*seed)++, st);
        puf_kaiming_init(&ew->w2[l], 1.0f, (*seed)++, st);
    }
    puf_kaiming_init(&ew->proj, 1.0f, (*seed)++, st);
}
static void chess_encoder_params(void* w, Allocator* alloc) {
    ChessEncoderWeights* ew = (ChessEncoderWeights*)w;
    ew->stem = {.shape = {CHESS_C, 9 * CHESS_IN}};
    alloc_register(alloc, &ew->stem);
    for (int l = 0; l < CHESS_NB; l++) {
        ew->wa[l] = {.shape = {CHESS_C, 9 * CHESS_C}}; ew->wb[l] = {.shape = {CHESS_C, 9 * CHESS_C}};
        ew->w1[l] = {.shape = {CHESS_SE, CHESS_C}}; ew->w2[l] = {.shape = {CHESS_C, CHESS_SE}};
        alloc_register(alloc, &ew->wa[l]); alloc_register(alloc, &ew->wb[l]);
        alloc_register(alloc, &ew->w1[l]); alloc_register(alloc, &ew->w2[l]);
    }
    ew->proj = {.shape = {ew->hidden, CHESS_SQ * CHESS_C}};
    alloc_register(alloc, &ew->proj);
}
static void chess_encoder_buffers(ChessEncoderWeights* ew, ChessEncoderActivations* a, Allocator* acts, int B) {
    long R = (long)B * CHESS_SQ;
    a->x0 = {.shape = {R, CHESS_IN}}; alloc_register(acts, &a->x0);
    for (int l = 0; l <= CHESS_NB; l++) { a->u[l] = {.shape = {R, CHESS_C}}; alloc_register(acts, &a->u[l]); }
    for (int l = 0; l < CHESS_NB; l++) {
        a->h1[l] = {.shape = {R, CHESS_C}}; a->y2[l] = {.shape = {R, CHESS_C}};
        a->p[l] = {.shape = {B, CHESS_C}}; a->z1[l] = {.shape = {B, CHESS_SE}};
        a->a1[l] = {.shape = {B, CHESS_SE}}; a->s[l] = {.shape = {B, CHESS_C}};
        alloc_register(acts, &a->h1[l]); alloc_register(acts, &a->y2[l]); alloc_register(acts, &a->p[l]);
        alloc_register(acts, &a->z1[l]); alloc_register(acts, &a->a1[l]); alloc_register(acts, &a->s[l]);
    }
    a->col = {.shape = {R, 9 * CHESS_C}}; alloc_register(acts, &a->col);
    a->out = {.shape = {B, ew->hidden}}; alloc_register(acts, &a->out);
}
static void chess_encoder_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B) {
    ChessEncoderWeights* ew = (ChessEncoderWeights*)w;
    ChessEncoderActivations* a = (ChessEncoderActivations*)activations;
    chess_encoder_buffers(ew, a, acts, B);
    long R = (long)B * CHESS_SQ;
    a->g1 = {.shape = {R, CHESS_C}}; a->g2 = {.shape = {R, CHESS_C}}; a->g3 = {.shape = {R, CHESS_C}};
    a->gp = {.shape = {B, CHESS_C}}; a->gs = {.shape = {B, CHESS_C}}; a->ga = {.shape = {B, CHESS_SE}};
    alloc_register(acts, &a->g1); alloc_register(acts, &a->g2); alloc_register(acts, &a->g3);
    alloc_register(acts, &a->gp); alloc_register(acts, &a->gs); alloc_register(acts, &a->ga);
    // gradients in the same order as the parameters
    a->dstem = {.shape = {CHESS_C, 9 * CHESS_IN}}; alloc_register(grads, &a->dstem);
    for (int l = 0; l < CHESS_NB; l++) {
        a->dwa[l] = {.shape = {CHESS_C, 9 * CHESS_C}}; a->dwb[l] = {.shape = {CHESS_C, 9 * CHESS_C}};
        a->dw1[l] = {.shape = {CHESS_SE, CHESS_C}}; a->dw2[l] = {.shape = {CHESS_C, CHESS_SE}};
        alloc_register(grads, &a->dwa[l]); alloc_register(grads, &a->dwb[l]);
        alloc_register(grads, &a->dw1[l]); alloc_register(grads, &a->dw2[l]);
    }
    a->dproj = {.shape = {ew->hidden, CHESS_SQ * CHESS_C}}; alloc_register(grads, &a->dproj);
}
static void chess_encoder_rollout(void* w, void* activations, Allocator* alloc, int B) {
    chess_encoder_buffers((ChessEncoderWeights*)w, (ChessEncoderActivations*)activations, alloc, B);
}
static void* chess_encoder_create(void* self) {
    Encoder* e = (Encoder*)self;
    ChessEncoderWeights* ew = (ChessEncoderWeights*)calloc(1, sizeof(ChessEncoderWeights));
    ew->hidden = e->out_dim;
    return ew;
}
static void create_chess_encoder(Encoder* e) {
    e->forward = chess_encoder_forward;
    e->backward = chess_encoder_backward;
    e->init_weights = chess_encoder_init;
    e->reg_params = chess_encoder_params;
    e->reg_train = chess_encoder_train;
    e->reg_rollout = chess_encoder_rollout;
    e->create_weights = chess_encoder_create;
    e->activation_size = sizeof(ChessEncoderActivations);
}
