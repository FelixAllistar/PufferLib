// Structured Gen 3 encoder and pointer decoder (2026-11-07). Opt-in with policy.pokemon3_net = 1 (pk3_net.h); without
// it the trainer keeps its default linear encoder/decoder and old checkpoints load as before. Gen 1's design
// (ocean/pokemon/pokemon_encoder.cu) over Gen 3's observation:
//   encoder: every move slot (70 features + its matchup eff/dmg/KO) -> shared move MLP -> E; every Pokemon (124
//            features without the move block, the mean of its known moves' embeddings, 2 defensive matchup values,
//            own flag) -> shared Pokemon MLP -> E; 12 x E + both sides + field -> fusion MLP -> hidden.
//   decoder: a linear head (128 logits + value, as the default decoder; it alone scores the draft) plus, in battle,
//            pointer scores: move action j += q_move . embed(own active's move j) / sqrt(E), switch to party slot
//            m += q_switch . embed(own Pokemon m) / sqrt(E). The decoder embeds own moves and Pokemon with its own
//            move/Pokemon MLPs (as Gen 1's decoder has its own tables), so encoder and decoder stay independent.
// CPU reference: pk3_net_cpu.h. Parity and numerical-gradient test: tests/test_net.cu.
#include "pk3_net.h"
#include "pk3_mlp.cuh"

// ---------------------------------------------------------------- encoder
struct P3EncWeights { P3MLPWeights move, mon, fusion; };
struct P3EncActs { P3MLPActs move, mon, fusion; Prec obs; };

static void* p3_enc_weights(void* self) {
    Encoder* enc = (Encoder*)self;
    if (enc->in_dim != P3_OBS || enc->out_dim < 8 || enc->out_dim % 8) {
        fprintf(stderr, "Pokemon3 structured net needs obs %d (got %d) and hidden_size a multiple of 8\n", P3_OBS, enc->in_dim);
        exit(1);
    }
    P3EncWeights* w = (P3EncWeights*)calloc(1, sizeof(*w));
    int E = pk3_embed, M = pk3_mid;
    w->move = {P3_MOVE_IN, M, E, true, sqrtf(2.0f), {}, {}};
    w->mon = {P3_MON_IN(E), M, E, true, sqrtf(2.0f), {}, {}};
    w->fusion = {P3_FUSION(E), enc->out_dim, enc->out_dim, true, sqrtf(2.0f), {}, {}};
    return w;
}
static void p3_enc_params(void* weights, Allocator* alloc) {
    P3EncWeights* w = (P3EncWeights*)weights;
    p3_nn_mlp_params(&w->move, alloc); p3_nn_mlp_params(&w->mon, alloc); p3_nn_mlp_params(&w->fusion, alloc);
}
static void p3_enc_init(void* weights, ulong* seed, cudaStream_t stream) {
    P3EncWeights* w = (P3EncWeights*)weights;
    p3_nn_mlp_init(&w->move, seed, stream); p3_nn_mlp_init(&w->mon, seed, stream); p3_nn_mlp_init(&w->fusion, seed, stream);
}
static void p3_enc_acts(void* weights, void* activations, Allocator* acts, Allocator* grads, int rows) {
    P3EncWeights* w = (P3EncWeights*)weights; P3EncActs* a = (P3EncActs*)activations;
    p3_nn_mlp_acts(&w->move, &a->move, acts, grads, rows * P3_MONS * 4, false);
    p3_nn_mlp_acts(&w->mon, &a->mon, acts, grads, rows * P3_MONS, true);
    p3_nn_mlp_acts(&w->fusion, &a->fusion, acts, grads, rows, true);
}
static void p3_enc_rollout(void* w, void* a, Allocator* alloc, int rows) { p3_enc_acts(w, a, alloc, NULL, rows); }

// Move slot rows (row, Pokemon m of `mons`, slot j); `first` is the first Pokemon (0 encoder, own only in the decoder).
__global__ static void p3_move_rows(precision_t* dst, const precision_t* obs, int rows, int mons) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= rows * mons * 4 * P3_AUG(P3_MOVE_IN)) return;
    int rr = i / P3_AUG(P3_MOVE_IN), f = i % P3_AUG(P3_MOVE_IN);
    int r = rr / (mons * 4), m = rr % (mons * 4) / 4, j = rr % 4;
    float v = f < P3_MOVE_IN ? p3_move_input(obs + (long)r * P3_OBS, m, j, f) : f == P3_MOVE_IN ? 1.0f : 0.0f;
    dst[i] = from_float(v);
}
// Pokemon rows: base features, the mean of its known moves' embeddings (moves: rows x mons x 4 x E), the tail.
__global__ static void p3_mon_rows(precision_t* dst, const precision_t* obs, const precision_t* moves, int rows, int mons, int E) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int width = P3_AUG(P3_MON_IN(E));
    if (i >= rows * mons * width) return;
    int rm = i / width, f = i % width, r = rm / mons, m = rm % mons;
    const precision_t* o = obs + (long)r * P3_OBS;
    float v = 0;
    if (f < P3_BASE) v = p3_mon_base(o, m, f);
    else if (f < P3_BASE + E) {
        int n = p3_move_count(o, m);
        for (int j = 0; j < 4; j++) if (p3_move_known(o, m, j)) v += to_float(moves[((long)rm * 4 + j) * E + f - P3_BASE]);
        v = n ? v / n : 0;
    } else if (f < P3_MON_IN(E)) v = p3_mon_tail(o, m, f - P3_BASE - E);
    else if (f == P3_MON_IN(E)) v = 1;
    dst[i] = from_float(v);
}
__global__ static void p3_fusion_rows(precision_t* dst, const precision_t* obs, const precision_t* mons, int rows, int E) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int width = P3_AUG(P3_FUSION(E));
    if (i >= rows * width) return;
    int r = i / width, f = i % width;
    float v = 0;
    if (f < P3_MONS * E) v = to_float(mons[(long)r * P3_MONS * E + f]);
    else if (f < P3_FUSION(E)) v = to_float(obs[(long)r * P3_OBS + P3_SIDE_AT + f - P3_MONS * E]);
    else if (f == P3_FUSION(E)) v = 1;
    dst[i] = from_float(v);
}
static Prec p3_enc_forward(void* weights, void* activations, Prec obs, cudaStream_t stream) {
    P3EncWeights* w = (P3EncWeights*)weights; P3EncActs* a = (P3EncActs*)activations;
    int rows = a->fusion.out.shape[0], E = w->mon.out;
    a->obs = obs;
    p3_move_rows<<<grid_size(rows * P3_MONS * 4 * P3_AUG(P3_MOVE_IN)), BLOCK_SIZE, 0, stream>>>(
        a->move.input_aug.data, obs.data, rows, P3_MONS);
    p3_nn_mlp_forward(&w->move, &a->move, stream);
    p3_mon_rows<<<grid_size(rows * P3_MONS * P3_AUG(P3_MON_IN(E))), BLOCK_SIZE, 0, stream>>>(
        a->mon.input_aug.data, obs.data, a->move.out.data, rows, P3_MONS, E);
    p3_nn_mlp_forward(&w->mon, &a->mon, stream);
    p3_fusion_rows<<<grid_size(rows * P3_AUG(P3_FUSION(E))), BLOCK_SIZE, 0, stream>>>(
        a->fusion.input_aug.data, obs.data, a->mon.out.data, rows, E);
    return p3_nn_mlp_forward(&w->fusion, &a->fusion, stream);
}
// grad of an MLP's output rows (rows x cols) from a slice [offset, offset + cols) of a wider gradient.
__global__ static void p3_slice(precision_t* dst, const precision_t* src, long n, int cols, int src_cols, int offset) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < n) dst[i] = src[(i / cols) * src_cols + offset + i % cols];
}
// Move embedding grads from the Pokemon MLP's input grads (the mean-move slice), split over its known moves.
// add = 1 accumulates (the decoder's moves also get pointer grads).
__global__ static void p3_mean_grad(precision_t* dst, const precision_t* mon_grad_input, const precision_t* obs,
        int rows, int mons, int E, int add) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i >= (long)rows * mons * 4 * E) return;
    int e = i % E;
    long rmj = i / E;
    int j = rmj % 4;
    long rm = rmj / 4;
    int r = rm / mons, m = rm % mons;
    const precision_t* o = obs + (long)r * P3_OBS;
    int n = p3_move_count(o, m);
    float g = n && p3_move_known(o, m, j) ? to_float(mon_grad_input[rm * P3_MON_IN(E) + P3_BASE + e]) / n : 0;
    dst[i] = from_float(g + (add ? to_float(dst[i]) : 0));
}
static void p3_enc_backward(void* weights, void* activations, Prec grad, cudaStream_t stream) {
    P3EncWeights* w = (P3EncWeights*)weights; P3EncActs* a = (P3EncActs*)activations;
    int rows = a->fusion.out.shape[0], E = w->mon.out;
    Prec fused = p3_nn_mlp_backward(&w->fusion, &a->fusion, grad, stream);
    long n = (long)rows * P3_MONS * E;
    p3_slice<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(a->mon.grad_out.data, fused.data, n, P3_MONS * E, P3_FUSION(E), 0);
    Prec mon_in = p3_nn_mlp_backward(&w->mon, &a->mon, a->mon.grad_out, stream);
    p3_mean_grad<<<grid_size((long)rows * P3_MONS * 4 * E), BLOCK_SIZE, 0, stream>>>(
        a->move.grad_out.data, mon_in.data, a->obs.data, rows, P3_MONS, E, 0);
    p3_nn_mlp_backward(&w->move, &a->move, a->move.grad_out, stream);
}
static void create_pokemon3_encoder(Encoder* enc) {
    if (!pk3_net_on) return;
    enc->forward = p3_enc_forward; enc->backward = p3_enc_backward;
    enc->reg_train = p3_enc_acts; enc->reg_rollout = p3_enc_rollout;
    enc->reg_params = p3_enc_params; enc->init_weights = p3_enc_init;
    enc->create_weights = p3_enc_weights; enc->activation_size = sizeof(P3EncActs);
}

// ---------------------------------------------------------------- decoder
struct P3DecWeights { Prec lin; P3MLPWeights query, move, mon; int hidden; };
struct P3DecActs {
    P3MLPActs query, move, mon;
    Prec obs, out, saved_input, grad_out, grad_input, dlin;
};
static void* p3_dec_weights(void* self) {
    Decoder* dec = (Decoder*)self;
    if (dec->continuous || dec->output_dim != P3_ACTIONS || dec->hidden_dim < 8 || dec->hidden_dim % 8) {
        fprintf(stderr, "Invalid Pokemon3 structured decoder dimensions\n"); exit(1);
    }
    P3DecWeights* w = (P3DecWeights*)calloc(1, sizeof(*w));
    int E = pk3_embed, M = pk3_mid, H = dec->hidden_dim;
    w->hidden = H;
    w->query = {H, H, 2 * E, false, 0.01f, {}, {}};
    w->move = {P3_MOVE_IN, M, E, false, 1.0f, {}, {}};
    w->mon = {P3_MON_IN(E), M, E, false, 1.0f, {}, {}};
    return w;
}
static void p3_dec_params(void* weights, Allocator* alloc) {
    P3DecWeights* w = (P3DecWeights*)weights;
    w->lin = {.shape = {P3_ACTIONS + 1, w->hidden}};
    alloc_register(alloc, &w->lin);
    p3_nn_mlp_params(&w->query, alloc); p3_nn_mlp_params(&w->move, alloc); p3_nn_mlp_params(&w->mon, alloc);
}
static void p3_dec_init(void* weights, ulong* seed, cudaStream_t stream) {
    P3DecWeights* w = (P3DecWeights*)weights;
    puf_kaiming_init(&w->lin, 1.0f, (*seed)++, stream);  // as the default decoder
    p3_nn_mlp_init(&w->query, seed, stream); p3_nn_mlp_init(&w->move, seed, stream); p3_nn_mlp_init(&w->mon, seed, stream);
}
static void p3_dec_acts(void* weights, void* activations, Allocator* acts, Allocator* grads, int rows) {
    P3DecWeights* w = (P3DecWeights*)weights; P3DecActs* a = (P3DecActs*)activations;
    *a = {};
    a->out = {.shape = {rows, P3_ACTIONS + 1}};
    alloc_register(acts, &a->out);
    if (grads) {
        a->saved_input = {.shape = {rows, w->hidden}};
        a->grad_out = {.shape = {rows, P3_ACTIONS + 1}};
        a->grad_input = {.shape = {rows, w->hidden}};
        alloc_register(acts, &a->saved_input);
        alloc_register(acts, &a->grad_out);
        alloc_register(acts, &a->grad_input);
        a->dlin = {.shape = {P3_ACTIONS + 1, w->hidden}};
        alloc_register(grads, &a->dlin);  // same order as the parameters: lin, query, move, mon
    }
    p3_nn_mlp_acts(&w->query, &a->query, acts, grads, rows, true);
    p3_nn_mlp_acts(&w->move, &a->move, acts, grads, rows * P3_OWN_MOVES, false);
    p3_nn_mlp_acts(&w->mon, &a->mon, acts, grads, rows * 6, true);
}
static void p3_dec_rollout(void* w, void* a, Allocator* alloc, int rows) { p3_dec_acts(w, a, alloc, NULL, rows); }
static void p3_dec_bind(void* acts, Prec obs) { ((P3DecActs*)acts)->obs = obs; }

// Battle pointer scores added to the linear logits (actions 0-3: own active's moves; 4-9: own party slots).
__global__ static void p3_pointer(precision_t* out, const precision_t* obs, const precision_t* q,
        const precision_t* moves, const precision_t* mons, int rows, int E) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= rows * 10) return;
    int r = i / 10, act = i % 10;
    const precision_t* o = obs + (long)r * P3_OBS;
    if (!p3_battle(o)) return;
    const precision_t* qr = q + (long)r * 2 * E;
    const precision_t* x;
    if (act < 4) {
        int m = p3_active(o);
        if (m < 0) return;
        x = moves + ((long)r * P3_OWN_MOVES + m * 4 + act) * E;
    } else {
        x = mons + ((long)r * 6 + act - 4) * E;
        qr += E;
    }
    float v = 0;
    for (int e = 0; e < E; e++) v += to_float(qr[e]) * to_float(x[e]);
    long k = (long)r * (P3_ACTIONS + 1) + act;
    out[k] = from_float(to_float(out[k]) + v * rsqrtf((float)E));
}
static Prec p3_dec_forward(void* weights, void* activations, Prec input, cudaStream_t stream) {
    P3DecWeights* w = (P3DecWeights*)weights; P3DecActs* a = (P3DecActs*)activations;
    int rows = a->out.shape[0], E = w->mon.out;
    if (a->saved_input.data) puf_copy(&a->saved_input, &input, stream);
    puf_mm(&input, &w->lin, &a->out, stream);
    p3_nn_pack_aug<<<grid_size(rows * P3_AUG(w->hidden)), BLOCK_SIZE, 0, stream>>>(
        a->query.input_aug.data, input.data, rows, w->hidden, false);
    p3_nn_mlp_forward(&w->query, &a->query, stream);
    p3_move_rows<<<grid_size(rows * P3_OWN_MOVES * P3_AUG(P3_MOVE_IN)), BLOCK_SIZE, 0, stream>>>(
        a->move.input_aug.data, a->obs.data, rows, 6);
    p3_nn_mlp_forward(&w->move, &a->move, stream);
    p3_mon_rows<<<grid_size(rows * 6 * P3_AUG(P3_MON_IN(E))), BLOCK_SIZE, 0, stream>>>(
        a->mon.input_aug.data, a->obs.data, a->move.out.data, rows, 6, E);
    p3_nn_mlp_forward(&w->mon, &a->mon, stream);
    p3_pointer<<<grid_size(rows * 10), BLOCK_SIZE, 0, stream>>>(a->out.data, a->obs.data, a->query.out.data,
        a->move.out.data, a->mon.out.data, rows, E);
    return a->out;
}
__global__ static void p3_assemble(precision_t* dst, const float* logits, const float* value, int rows) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= rows * (P3_ACTIONS + 1)) return;
    int r = i / (P3_ACTIONS + 1), c = i % (P3_ACTIONS + 1);
    dst[i] = from_float(c < P3_ACTIONS ? logits[r * P3_ACTIONS + c] : value[r]);
}
// One thread per (row, e): pointer grads to the queries and to the own move / Pokemon embeddings.
__global__ static void p3_pointer_grad(precision_t* gq, precision_t* gmoves, precision_t* gmons, const float* logits,
        const precision_t* obs, const precision_t* q, const precision_t* moves, const precision_t* mons, int rows, int E) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= rows * E) return;
    int r = i / E, e = i % E;
    const precision_t* o = obs + (long)r * P3_OBS;
    float s = rsqrtf((float)E), qm = 0, qs = 0;
    for (int k = 0; k < P3_OWN_MOVES; k++) gmoves[((long)r * P3_OWN_MOVES + k) * E + e] = from_float(0.0f);
    for (int k = 0; k < 6; k++) gmons[((long)r * 6 + k) * E + e] = from_float(0.0f);
    if (p3_battle(o)) {
        const float* g = logits + (long)r * P3_ACTIONS;
        float q_move = to_float(q[(long)r * 2 * E + e]), q_switch = to_float(q[(long)r * 2 * E + E + e]);
        int m = p3_active(o);
        if (m >= 0) for (int j = 0; j < 4; j++) {
            long k = ((long)r * P3_OWN_MOVES + m * 4 + j) * E + e;
            qm += g[j] * to_float(moves[k]) * s;
            gmoves[k] = from_float(g[j] * q_move * s);
        }
        for (int j = 0; j < 6; j++) {
            long k = ((long)r * 6 + j) * E + e;
            qs += g[4 + j] * to_float(mons[k]) * s;
            gmons[k] = from_float(g[4 + j] * q_switch * s);
        }
    }
    gq[(long)r * 2 * E + e] = from_float(qm);
    gq[(long)r * 2 * E + E + e] = from_float(qs);
}
__global__ static void p3_add(precision_t* dst, const precision_t* src, long n) {
    long i = blockIdx.x * (long)blockDim.x + threadIdx.x;
    if (i < n) dst[i] = from_float(to_float(dst[i]) + to_float(src[i]));
}
static Prec p3_dec_backward(void* weights, void* activations, Float logits, Float logstd, Float value, cudaStream_t stream) {
    P3DecWeights* w = (P3DecWeights*)weights; P3DecActs* a = (P3DecActs*)activations;
    int rows = a->out.shape[0], E = w->mon.out;
    p3_assemble<<<grid_size(rows * (P3_ACTIONS + 1)), BLOCK_SIZE, 0, stream>>>(a->grad_out.data, logits.data, value.data, rows);
    puf_mm_tn(&a->grad_out, &a->saved_input, &a->dlin, stream);
    puf_mm_nn(&a->grad_out, &w->lin, &a->grad_input, stream);
    p3_pointer_grad<<<grid_size(rows * E), BLOCK_SIZE, 0, stream>>>(a->query.grad_out.data, a->move.grad_out.data,
        a->mon.grad_out.data, logits.data, a->obs.data, a->query.out.data, a->move.out.data, a->mon.out.data, rows, E);
    Prec mon_in = p3_nn_mlp_backward(&w->mon, &a->mon, a->mon.grad_out, stream);
    p3_mean_grad<<<grid_size((long)rows * 6 * 4 * E), BLOCK_SIZE, 0, stream>>>(
        a->move.grad_out.data, mon_in.data, a->obs.data, rows, 6, E, 1);
    p3_nn_mlp_backward(&w->move, &a->move, a->move.grad_out, stream);
    Prec h = p3_nn_mlp_backward(&w->query, &a->query, a->query.grad_out, stream);
    long n = (long)rows * w->hidden;
    p3_add<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(a->grad_input.data, h.data, n);
    return a->grad_input;
}
static void create_pokemon3_decoder(Decoder* dec) {
    if (!pk3_net_on) return;
    dec->forward = p3_dec_forward; dec->backward = p3_dec_backward;
    dec->bind_observation = p3_dec_bind;
    dec->reg_train = p3_dec_acts; dec->reg_rollout = p3_dec_rollout;
    dec->reg_params = p3_dec_params; dec->init_weights = p3_dec_init;
    dec->create_weights = p3_dec_weights; dec->activation_size = sizeof(P3DecActs);
}
