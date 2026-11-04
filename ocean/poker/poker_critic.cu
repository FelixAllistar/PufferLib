// N-way zero-sum critic for poker (2026-11-04). Included by src/ocean.cu with PUFFER_POKER and POKER_ZS_CRITIC.
//
// Encoder: the default PufferNet encoder (one bias-free matrix, kaiming sqrt 2) over the public POKER_OBS columns
// only; the privileged tail of the observation (poker.h poker_privileged) never reaches the policy. Same weight shape
// as a normal poker checkpoint's encoder.
//
// Decoder: logits are the usual linear head on the recurrent state (its value row is unused). The value is a
// zero-sum split of the hand over the seats, computed from everything (all hole cards, the dealt board, chips) plus
// the learner's recurrent state (Baisero & Amato 2022: a history + state critic stays unbiased):
//   a folded seat's hand result is exactly minus what it put in, so it leaves the critic;
//   each live seat gets a score g_s = w2 . relu(Ws x_s + Wh x_table) (one MLP shared by all seats);
//   V_learner = g_0 - mean_live(g) + (chips the folded seats left in the pot - rake) / live seats + reward offset,
// so the live seats' values sum to what they can win back minus the rake, for any number of live seats (2 to 9).
// The rake r >= 0 is learned (softplus of a table-only head). The value gradient trains only the critic; the logit
// gradient flows to the recurrent state as before. Checkpoints hold the usual decoder matrix first, then Ws, Wh, w2,
// wr (poker.h's bank loader skips them by size).

#define POKER_CH POKER_CRITIC_CH                      // critic hidden width (poker.h, shared with the bank loader)
#define POKER_XT POKER_CRITIC_XT                      // table features: dealt board, street, reward offset
static_assert(POKER_PRIV == POKER_PLAYERS * POKER_SEAT_PRIV + POKER_XT, "privileged block layout");

static int pk_grid(long n) { return (int)((n + BLOCK_SIZE - 1) / BLOCK_SIZE); }

// ---------------------------------------------------------------- encoder: public columns only
__global__ void pk_public_cols(const precision_t* __restrict__ obs, precision_t* __restrict__ dst, long B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= B * POKER_OBS) return;
    long b = i / POKER_OBS; int k = i % POKER_OBS;
    dst[i] = obs[b * OBS_SIZE + k];
}
struct PokerEncoderWeights { Prec weight; int in_dim, out_dim; };
struct PokerEncoderActivations { Prec out, pub, wgrad; };
static Prec poker_encoder_forward(void* w, void* activations, Prec input, cudaStream_t st) {
    PokerEncoderWeights* ew = (PokerEncoderWeights*)w;
    PokerEncoderActivations* a = (PokerEncoderActivations*)activations;
    long B = input.shape[0];
    pk_public_cols<<<pk_grid(B * POKER_OBS), BLOCK_SIZE, 0, st>>>(input.data, a->pub.data, B);
    Prec pub = {.data = a->pub.data, .shape = {(int)B, POKER_OBS}};
    puf_mm(&pub, &ew->weight, &a->out, st);
    return a->out;
}
static void poker_encoder_backward(void* w, void* activations, Prec grad, cudaStream_t st) {
    (void)w;
    PokerEncoderActivations* a = (PokerEncoderActivations*)activations;
    Prec pub = {.data = a->pub.data, .shape = {grad.shape[0], POKER_OBS}};
    puf_mm_tn(&grad, &pub, &a->wgrad, st);
}
static void poker_encoder_init(void* w, ulong* seed, cudaStream_t st) {
    puf_kaiming_init(&((PokerEncoderWeights*)w)->weight, sqrtf(2.0f), (*seed)++, st);
}
static void poker_encoder_params(void* w, Allocator* alloc) {
    PokerEncoderWeights* ew = (PokerEncoderWeights*)w;
    ew->weight = {.shape = {ew->out_dim, POKER_OBS}};
    alloc_register(alloc, &ew->weight);
}
static void poker_encoder_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B) {
    PokerEncoderWeights* ew = (PokerEncoderWeights*)w;
    PokerEncoderActivations* a = (PokerEncoderActivations*)activations;
    a->out = {.shape = {B, ew->out_dim}}; a->pub = {.shape = {B, POKER_OBS}};
    a->wgrad = {.shape = {ew->out_dim, POKER_OBS}};
    alloc_register(acts, &a->out); alloc_register(acts, &a->pub); alloc_register(grads, &a->wgrad);
}
static void poker_encoder_rollout(void* w, void* activations, Allocator* alloc, int B) {
    PokerEncoderWeights* ew = (PokerEncoderWeights*)w;
    PokerEncoderActivations* a = (PokerEncoderActivations*)activations;
    a->out = {.shape = {B, ew->out_dim}}; a->pub = {.shape = {B, POKER_OBS}};
    alloc_register(alloc, &a->out); alloc_register(alloc, &a->pub);
}
static void* poker_encoder_create(void* self) {
    Encoder* e = (Encoder*)self;
    PokerEncoderWeights* ew = (PokerEncoderWeights*)calloc(1, sizeof(PokerEncoderWeights));
    ew->in_dim = POKER_OBS; ew->out_dim = e->out_dim;
    return ew;
}
static void create_poker_encoder(Encoder* e) {
    e->forward = poker_encoder_forward; e->backward = poker_encoder_backward;
    e->init_weights = poker_encoder_init; e->reg_params = poker_encoder_params;
    e->reg_train = poker_encoder_train; e->reg_rollout = poker_encoder_rollout;
    e->create_weights = poker_encoder_create; e->activation_size = sizeof(PokerEncoderActivations);
}

// ---------------------------------------------------------------- decoder: zero-sum critic
// xs[b * P + p] = seat p's privileged features; xh[b] = [table features, recurrent state]
__global__ void pk_critic_inputs(const precision_t* __restrict__ obs, const precision_t* __restrict__ h,
        precision_t* __restrict__ xs, precision_t* __restrict__ xh, int B, int H) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    long ns = (long)B * POKER_PLAYERS * POKER_SEAT_PRIV, nh = (long)B * (POKER_XT + H);
    if (i < ns) {
        long b = i / (POKER_PLAYERS * POKER_SEAT_PRIV); int k = i % (POKER_PLAYERS * POKER_SEAT_PRIV);
        xs[i] = obs[b * OBS_SIZE + POKER_OBS + k];
    } else if (i < ns + nh) {
        long j = i - ns, b = j / (POKER_XT + H); int k = j % (POKER_XT + H);
        xh[j] = k < POKER_XT ? obs[b * OBS_SIZE + POKER_OBS + POKER_PLAYERS * POKER_SEAT_PRIV + k] : h[b * H + k - POKER_XT];
    }
}
// z[bp] += zh[b] (pre-activation kept in z), a = relu(z); ah = relu(zh)
__global__ void pk_critic_hidden(precision_t* __restrict__ z, const precision_t* __restrict__ zh,
        precision_t* __restrict__ a, precision_t* __restrict__ ah, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    long n = (long)B * POKER_PLAYERS * POKER_CH;
    if (i < n) {
        long bp = i / POKER_CH; int c = i % POKER_CH; long b = bp / POKER_PLAYERS;
        float v = to_float(z[i]) + to_float(zh[b * POKER_CH + c]);
        z[i] = from_float(v);
        a[i] = from_float(fmaxf(v, 0.0f));
    } else if (i < n + (long)B * POKER_CH) {
        long j = i - n;
        ah[j] = from_float(fmaxf(to_float(zh[j]), 0.0f));
    }
}
__device__ static inline float pk_softplus(float x) { return x > 20.0f ? x : log1pf(expf(x)); }
// Per row: live seats, folded chips, the zero-sum value of seat 0 -> out[b, A]
__global__ void pk_critic_value(const precision_t* __restrict__ obs, const precision_t* __restrict__ g,
        const precision_t* __restrict__ rr, precision_t* __restrict__ out, int B, int A) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= B) return;
    const precision_t* f = obs + (long)b * OBS_SIZE + POKER_OBS;
    float offset = to_float(f[POKER_PLAYERS * POKER_SEAT_PRIV + POKER_XT - 1]);
    float sum_g = 0.0f, folded = 0.0f; int n = 0;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        const precision_t* s = f + p * POKER_SEAT_PRIV;
        if (to_float(s[34]) > 0.5f) { sum_g += to_float(g[(long)b * POKER_PLAYERS + p]); n++; }
        else folded += to_float(s[35]);
    }
    float v;
    if (to_float(f[34]) > 0.5f && n > 0)
        v = to_float(g[(long)b * POKER_PLAYERS]) - sum_g / n + (folded - pk_softplus(to_float(rr[b]))) / n + offset;
    else
        v = -to_float(f[35]) + offset;  // the learner has folded: its result is fixed
    out[(long)b * (A + 1) + A] = from_float(v);
}
__global__ void pk_logit_grad(precision_t* __restrict__ grad_out, const float* __restrict__ gl, int B, int A) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * (A + 1)) return;
    int a = i % (A + 1); long b = i / (A + 1);
    grad_out[i] = from_float(a < A ? gl[b * A + a] : 0.0f);
}
// d V / d g_p = [p == 0] - 1/n for live p; d V / d rr = -sigmoid(rr) / n
__global__ void pk_critic_vgrad(const precision_t* __restrict__ obs, const float* __restrict__ gv,
        const precision_t* __restrict__ rr, precision_t* __restrict__ gg, precision_t* __restrict__ grr, int B) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= B) return;
    const precision_t* f = obs + (long)b * OBS_SIZE + POKER_OBS;
    int n = 0;
    for (int p = 0; p < POKER_PLAYERS; p++) n += to_float(f[p * POKER_SEAT_PRIV + 34]) > 0.5f;
    int on = to_float(f[34]) > 0.5f && n > 0;
    float inv = on ? 1.0f / n : 0.0f;
    for (int p = 0; p < POKER_PLAYERS; p++) {
        int live = to_float(f[p * POKER_SEAT_PRIV + 34]) > 0.5f;
        gg[(long)b * POKER_PLAYERS + p] = from_float(on && live ? gv[b] * ((p == 0) - inv) : 0.0f);
    }
    float x = to_float(rr[b]);
    grr[b] = from_float(on ? -gv[b] * inv / (1.0f + expf(-x)) : 0.0f);
}
__global__ void pk_relu_mask(precision_t* __restrict__ g, const precision_t* __restrict__ z, long n) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && to_float(z[i]) <= 0.0f) g[i] = from_float(0.0f);
}
// gzh[b, c] = sum_p ga[b*P + p, c] + grr[b] * wr[c] * (zh[b, c] > 0)
__global__ void pk_critic_table_grad(const precision_t* __restrict__ ga, const precision_t* __restrict__ grr,
        const precision_t* __restrict__ wr, const precision_t* __restrict__ zh, precision_t* __restrict__ gzh, int B) {
    long i = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= (long)B * POKER_CH) return;
    long b = i / POKER_CH; int c = i % POKER_CH;
    float s = 0.0f;
    for (int p = 0; p < POKER_PLAYERS; p++) s += to_float(ga[(b * POKER_PLAYERS + p) * POKER_CH + c]);
    if (to_float(zh[i]) > 0.0f) s += to_float(grr[b]) * to_float(wr[c]);
    gzh[i] = from_float(s);
}

// The trainer reads ->continuous and ->logstd through DecoderWeights*, so this starts with the same fields.
struct PokerDecoderWeights {
    Prec weight, logstd;
    int hidden_dim, output_dim;
    bool continuous;
    Prec ws, wh, w2, wr;
    int hidden, A;
};
struct PokerDecoderActivations {
    Prec out, saved_input, grad_out, grad_input, obs;
    Prec xs, xh, z, a, zh, ah, g, rr;
    Prec gg, grr, ga, gzh;
    Prec dweight, dws, dwh, dw2, dwr;
};
static void poker_decoder_bind(void* activations, Prec observation) {
    ((PokerDecoderActivations*)activations)->obs = observation;
}
static Prec poker_decoder_forward(void* w, void* activations, Prec input, cudaStream_t st) {
    PokerDecoderWeights* dw = (PokerDecoderWeights*)w;
    PokerDecoderActivations* a = (PokerDecoderActivations*)activations;
    int B = input.shape[0], A = dw->A, H = dw->hidden, BP = B * POKER_PLAYERS;
    if (a->saved_input.data) puf_copy(&a->saved_input, &input, st);
    Prec out = {.data = a->out.data, .shape = {B, A + 1}};
    puf_mm(&input, &dw->weight, &out, st);
    long ni = (long)BP * POKER_SEAT_PRIV + (long)B * (POKER_XT + H);
    pk_critic_inputs<<<pk_grid(ni), BLOCK_SIZE, 0, st>>>(a->obs.data, input.data, a->xs.data, a->xh.data, B, H);
    Prec xs = {.data = a->xs.data, .shape = {BP, POKER_SEAT_PRIV}}, xh = {.data = a->xh.data, .shape = {B, POKER_XT + H}};
    Prec z = {.data = a->z.data, .shape = {BP, POKER_CH}}, zh = {.data = a->zh.data, .shape = {B, POKER_CH}};
    puf_mm(&xs, &dw->ws, &z, st);
    puf_mm(&xh, &dw->wh, &zh, st);
    pk_critic_hidden<<<pk_grid((long)BP * POKER_CH + (long)B * POKER_CH), BLOCK_SIZE, 0, st>>>(
        a->z.data, a->zh.data, a->a.data, a->ah.data, B);
    Prec act = {.data = a->a.data, .shape = {BP, POKER_CH}}, ah = {.data = a->ah.data, .shape = {B, POKER_CH}};
    Prec g = {.data = a->g.data, .shape = {BP, 1}}, rr = {.data = a->rr.data, .shape = {B, 1}};
    puf_mm(&act, &dw->w2, &g, st);
    puf_mm(&ah, &dw->wr, &rr, st);
    pk_critic_value<<<pk_grid(B), BLOCK_SIZE, 0, st>>>(a->obs.data, a->g.data, a->rr.data, out.data, B, A);
    return out;
}
static Prec poker_decoder_backward(void* w, void* activations, Float grad_logits, Float grad_logstd,
        Float grad_value, cudaStream_t st) {
    (void)grad_logstd;
    PokerDecoderWeights* dw = (PokerDecoderWeights*)w;
    PokerDecoderActivations* a = (PokerDecoderActivations*)activations;
    int B = a->saved_input.shape[0], A = dw->A, H = dw->hidden, BP = B * POKER_PLAYERS;
    pk_logit_grad<<<pk_grid((long)B * (A + 1)), BLOCK_SIZE, 0, st>>>(a->grad_out.data, grad_logits.data, B, A);
    Prec go = {.data = a->grad_out.data, .shape = {B, A + 1}};
    puf_mm_tn(&go, &a->saved_input, &a->dweight, st);
    puf_mm_nn(&go, &dw->weight, &a->grad_input, st);
    // critic (its own parameters only)
    pk_critic_vgrad<<<pk_grid(B), BLOCK_SIZE, 0, st>>>(a->obs.data, grad_value.data, a->rr.data, a->gg.data, a->grr.data, B);
    Prec gg = {.data = a->gg.data, .shape = {BP, 1}}, grr = {.data = a->grr.data, .shape = {B, 1}};
    Prec act = {.data = a->a.data, .shape = {BP, POKER_CH}}, ah = {.data = a->ah.data, .shape = {B, POKER_CH}};
    Prec ga = {.data = a->ga.data, .shape = {BP, POKER_CH}}, gzh = {.data = a->gzh.data, .shape = {B, POKER_CH}};
    Prec xs = {.data = a->xs.data, .shape = {BP, POKER_SEAT_PRIV}}, xh = {.data = a->xh.data, .shape = {B, POKER_XT + H}};
    puf_mm_tn(&gg, &act, &a->dw2, st);
    puf_mm_tn(&grr, &ah, &a->dwr, st);
    puf_mm_nn(&gg, &dw->w2, &ga, st);
    pk_relu_mask<<<pk_grid((long)BP * POKER_CH), BLOCK_SIZE, 0, st>>>(ga.data, a->z.data, (long)BP * POKER_CH);
    puf_mm_tn(&ga, &xs, &a->dws, st);
    pk_critic_table_grad<<<pk_grid((long)B * POKER_CH), BLOCK_SIZE, 0, st>>>(ga.data, a->grr.data, dw->wr.data,
        a->zh.data, a->gzh.data, B);
    puf_mm_tn(&gzh, &xh, &a->dwh, st);
    return a->grad_input;
}
static void poker_decoder_init(void* w, ulong* seed, cudaStream_t st) {
    PokerDecoderWeights* dw = (PokerDecoderWeights*)w;
    puf_kaiming_init(&dw->weight, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->ws, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->wh, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->w2, 1.0f, (*seed)++, st);
    puf_kaiming_init(&dw->wr, 1.0f, (*seed)++, st);
}
static void poker_decoder_params(void* w, Allocator* alloc) {
    PokerDecoderWeights* dw = (PokerDecoderWeights*)w;
    dw->weight = {.shape = {dw->A + 1, dw->hidden}};
    dw->ws = {.shape = {POKER_CH, POKER_SEAT_PRIV}};
    dw->wh = {.shape = {POKER_CH, POKER_XT + dw->hidden}};
    dw->w2 = {.shape = {1, POKER_CH}};
    dw->wr = {.shape = {1, POKER_CH}};
    alloc_register(alloc, &dw->weight); alloc_register(alloc, &dw->ws); alloc_register(alloc, &dw->wh);
    alloc_register(alloc, &dw->w2); alloc_register(alloc, &dw->wr);
}
static void poker_decoder_buffers(PokerDecoderWeights* dw, PokerDecoderActivations* a, Allocator* acts, int B) {
    int BP = B * POKER_PLAYERS;
    a->out = {.shape = {B, dw->A + 1}};
    a->xs = {.shape = {BP, POKER_SEAT_PRIV}}; a->xh = {.shape = {B, POKER_XT + dw->hidden}};
    a->z = {.shape = {BP, POKER_CH}}; a->a = {.shape = {BP, POKER_CH}};
    a->zh = {.shape = {B, POKER_CH}}; a->ah = {.shape = {B, POKER_CH}};
    a->g = {.shape = {BP, 1}}; a->rr = {.shape = {B, 1}};
    alloc_register(acts, &a->out); alloc_register(acts, &a->xs); alloc_register(acts, &a->xh);
    alloc_register(acts, &a->z); alloc_register(acts, &a->a); alloc_register(acts, &a->zh); alloc_register(acts, &a->ah);
    alloc_register(acts, &a->g); alloc_register(acts, &a->rr);
}
static void poker_decoder_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B) {
    PokerDecoderWeights* dw = (PokerDecoderWeights*)w;
    PokerDecoderActivations* a = (PokerDecoderActivations*)activations;
    int BP = B * POKER_PLAYERS;
    poker_decoder_buffers(dw, a, acts, B);
    a->saved_input = {.shape = {B, dw->hidden}}; a->grad_out = {.shape = {B, dw->A + 1}};
    a->grad_input = {.shape = {B, dw->hidden}};
    a->gg = {.shape = {BP, 1}}; a->grr = {.shape = {B, 1}};
    a->ga = {.shape = {BP, POKER_CH}}; a->gzh = {.shape = {B, POKER_CH}};
    alloc_register(acts, &a->saved_input); alloc_register(acts, &a->grad_out); alloc_register(acts, &a->grad_input);
    alloc_register(acts, &a->gg); alloc_register(acts, &a->grr); alloc_register(acts, &a->ga); alloc_register(acts, &a->gzh);
    // gradients in parameter order: weight, ws, wh, w2, wr
    a->dweight = {.shape = {dw->A + 1, dw->hidden}};
    a->dws = {.shape = {POKER_CH, POKER_SEAT_PRIV}};
    a->dwh = {.shape = {POKER_CH, POKER_XT + dw->hidden}};
    a->dw2 = {.shape = {1, POKER_CH}};
    a->dwr = {.shape = {1, POKER_CH}};
    alloc_register(grads, &a->dweight); alloc_register(grads, &a->dws); alloc_register(grads, &a->dwh);
    alloc_register(grads, &a->dw2); alloc_register(grads, &a->dwr);
}
static void poker_decoder_rollout(void* w, void* activations, Allocator* alloc, int B) {
    poker_decoder_buffers((PokerDecoderWeights*)w, (PokerDecoderActivations*)activations, alloc, B);
}
static void* poker_decoder_create(void* self) {
    Decoder* d = (Decoder*)self;
    PokerDecoderWeights* dw = (PokerDecoderWeights*)calloc(1, sizeof(PokerDecoderWeights));
    dw->hidden = dw->hidden_dim = d->hidden_dim;
    dw->A = dw->output_dim = d->output_dim;
    dw->continuous = false;
    return dw;
}
static void create_poker_decoder(Decoder* d) {
    d->forward = poker_decoder_forward; d->bind_observation = poker_decoder_bind;
    d->backward = poker_decoder_backward; d->init_weights = poker_decoder_init;
    d->reg_params = poker_decoder_params; d->reg_train = poker_decoder_train;
    d->reg_rollout = poker_decoder_rollout; d->create_weights = poker_decoder_create;
    d->activation_size = sizeof(PokerDecoderActivations);
}
