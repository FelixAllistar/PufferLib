// CPU inference for trained Cretaceous policies; see policy_cpu.h and docs/encoder.md ("CPU inference").
// The structured encoder follows cretaceous_encoder.cu step by step (the same augmented inputs, im2col column order
// and masked pooling), so the two can be compared tensor by tensor; tools/policy_reference.py checks both encoders,
// the MinGRU and the decoder against NumPy (tests/test_policy.c).
#include "policy_cpu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structured encoder sizes (cretaceous_encoder.cu, CE_*): every FC input carries a bias column, padded to 8.
#define PC_AUG(n) (((n) + 8) & ~7)
#define PC_D 64
#define PC_DA PC_AUG(PC_D)                                    // 72
#define PC_HEADS 4
#define PC_DH (PC_D / PC_HEADS)                               // 16
#define PC_SP 16
#define PC_SELF_IN (CRET_SELF_OBS + CRET_SELF_OBS4)           // 61
#define PC_SELF_AUG PC_AUG(PC_SELF_IN)                        // 64
#define PC_SP_AUG PC_AUG(CRET_SPECIES)                        // 8
#define PC_ENT_OBS (CRET_NEIGHBOR_OBS + CRET_NEIGHBOR_OBS4)   // 14
#define PC_ENT_IN (PC_ENT_OBS + PC_SP)                        // 30
#define PC_ENT_AUG PC_AUG(PC_ENT_IN)                          // 32
#define PC_NB CRET_NEIGHBORS
#define PC_P CRET_PATCH
#define PC_P1 (PC_P - 2)                                      // conv1 output side
#define PC_P2 (PC_P1 - 2)                                     // conv2 output side
#define PC_C1 16
#define PC_C2 32
#define PC_COL1 PC_AUG(9 * 2)                                 // 24
#define PC_COL2 PC_AUG(9 * PC_C1)                             // 152
#define PC_FLAT (PC_P2 * PC_P2 * PC_C2)                       // 288
#define PC_FLAT_AUG PC_AUG(PC_FLAT)                           // 296
#define PC_CAT (4 * PC_D + PC_SP)                             // 272: self, patch, attention, max, species
#define PC_CAT_AUG PC_AUG(PC_CAT)                             // 280
#define PC_TRUNK 256
#define PC_TRUNK_AUG PC_AUG(PC_TRUNK)                         // 264
#define PC_OBS_SELF4 CRET_OBS_V3
#define PC_OBS_NEIGH4 (CRET_OBS_V3 + CRET_SELF_OBS4)

_Static_assert(CRET_OBS_SIZE == 243 && CRET_SELF_OBS == 39 && CRET_SELF_OBS4 == 22 && CRET_PATCH == 7 && CRET_NEIGHBORS == 6
    && CRET_NEIGHBOR_OBS == 10 && CRET_NEIGHBOR_OBS4 == 4 && CRET_SPECIES == 6,
    "cretaceous obs layout changed: update policy_cpu.c with cretaceous_encoder.cu");
_Static_assert(CRET_ACTION_HEADS == 6, "action heads changed: update CRET_POLICY_ATN");

enum { PC_W_SP, PC_W_S1, PC_W_S2, PC_W_Q, PC_W_C1, PC_W_C2, PC_W_P3, PC_W_E1, PC_W_E2, PC_W_F1, PC_W_F2, PC_NW };
static void pc_shape(int i, int hidden, int* rows, int* cols) {
    static const int R[PC_NW] = {PC_SP, PC_D, PC_D, PC_D, PC_C1, PC_C2, PC_D, PC_D, PC_D, PC_TRUNK, 0};
    static const int C[PC_NW] = {PC_SP_AUG, PC_SELF_AUG, PC_DA, PC_DA, PC_COL1, PC_COL2, PC_FLAT_AUG, PC_ENT_AUG, PC_DA,
        PC_CAT_AUG, PC_TRUNK_AUG};
    *rows = i == PC_W_F2 ? hidden : R[i]; *cols = C[i];
}

static const int PC_HEAD_SIZES[CRET_ACTION_HEADS] = CRET_ACTION_SIZES;

struct CretPolicy {
    int net, hidden, layers, max_agents;
    float* weights;                       // the checkpoint, owned
    const float* enc;                     // net 0: (hidden x obs)
    const float* ce[PC_NW];               // net 1: the structured encoder's tensors
    const float* dec;                     // (29 x hidden)
    const float* gru[8];                  // (3 hidden x hidden) per layer
    float* state;                         // (layers, max_agents, hidden)
    int* life;                            // per slot: ticks_alive last seen + 1 (0: no state yet)
    // Per-forward buffers, sized for max_agents rows.
    float *x, *comb, *out, *logits;       // (B, hidden) encoder output; (B, 3 hidden); (B, hidden); (B, 29)
    float *self_x, *sp_x, *sp, *s1a, *selfa, *q, *col1, *c1, *col2, *p3a, *ent_x, *e1a, *h, *cat, *f1a, *y;
    float* obs;                           // cret_policy_act's observation rows
    int* slots;
};

// Y[r, j] = X[r, :] . W[j, :] for r < rows, j < N (K inputs each), with an optional ReLU. Four rows share each weight
// load and every dot product sums 8 lanes in a fixed order, so the result does not depend on the thread count.
// Only layers of 2M multiply-adds or more (about 0.1 ms on one core) split over threads: below that the fork and join
// cost more than they save (hidden 256 with 8 agents ran 0.28 ms on 4 threads against 0.22 ms on one).
static void pc_mm(const float* restrict X, int rows, int K, const float* restrict W, int N, float* restrict Y, int ldy,
        int relu) {
    long work = (long)rows * N * K;
    #pragma omp parallel for schedule(static) if (work >= (1L << 21))
    for (int j = 0; j < N; j++) {
        const float* w = W + (long)j * K;
        int r = 0;
        for (; r + 4 <= rows; r += 4) {
            const float *x0 = X + (long)r * K, *x1 = x0 + K, *x2 = x1 + K, *x3 = x2 + K;
            float a0[8] = {0}, a1[8] = {0}, a2[8] = {0}, a3[8] = {0};
            int k = 0;
            for (; k + 8 <= K; k += 8)
                for (int l = 0; l < 8; l++) {
                    float wk = w[k + l];
                    a0[l] += x0[k + l] * wk; a1[l] += x1[k + l] * wk; a2[l] += x2[k + l] * wk; a3[l] += x3[k + l] * wk;
                }
            for (int l = 0; k < K; k++, l++) {
                a0[l] += x0[k] * w[k]; a1[l] += x1[k] * w[k]; a2[l] += x2[k] * w[k]; a3[l] += x3[k] * w[k];
            }
            float s[4];
            for (int t = 0; t < 4; t++) {
                const float* a = t == 0 ? a0 : t == 1 ? a1 : t == 2 ? a2 : a3;
                s[t] = ((a[0] + a[1]) + (a[2] + a[3])) + ((a[4] + a[5]) + (a[6] + a[7]));
                if (relu && s[t] < 0) s[t] = 0;
                Y[(long)(r + t) * ldy + j] = s[t];
            }
        }
        for (; r < rows; r++) {
            const float* x0 = X + (long)r * K;
            float a[8] = {0};
            int k = 0;
            for (; k + 8 <= K; k += 8)
                for (int l = 0; l < 8; l++) a[l] += x0[k + l] * w[k + l];
            for (int l = 0; k < K; k++, l++) a[l] += x0[k] * w[k];
            float s = ((a[0] + a[1]) + (a[2] + a[3])) + ((a[4] + a[5]) + (a[6] + a[7]));
            if (relu && s < 0) s = 0;
            Y[(long)r * ldy + j] = s;
        }
    }
}

// Copies rows of n floats from y (stride n) into dst (stride ld, column off), with a ReLU; with aug, the column after
// them is the bias input 1 and the rest of the row (to ld) is 0.
static void pc_act(float* dst, int ld, int off, const float* y, int rows, int n, int relu, int aug) {
    for (int r = 0; r < rows; r++) {
        float* d = dst + (long)r * ld + off;
        for (int c = 0; c < n; c++) { float v = y[(long)r * n + c]; d[c] = relu && v < 0 ? 0 : v; }
        if (aug) { d[n] = 1; for (int c = n + 1; c < ld - off; c++) d[c] = 0; }
    }
}

// The structured encoder (cretaceous_encoder.cu, cret_encoder_forward) for B rows of obs into p->x.
static void pc_encode_structured(CretPolicy* p, const float* obs, int B) {
    const float* const* W = p->ce;
    int H = p->hidden, R1 = B * PC_P1 * PC_P1, R2 = B * PC_P2 * PC_P2, RN = B * PC_NB;
    // Self ([self obs, contract 4 self block, 1, 0]) and species ([one-hot, 1, 0]) inputs.
    for (int b = 0; b < B; b++) {
        const float* o = obs + (long)b * CRET_OBS_SIZE;
        float* sx = p->self_x + (long)b * PC_SELF_AUG;
        for (int c = 0; c < PC_SELF_AUG; c++)
            sx[c] = c < CRET_SELF_OBS ? o[c] : c < PC_SELF_IN ? o[PC_OBS_SELF4 + c - CRET_SELF_OBS] : c == PC_SELF_IN ? 1 : 0;
        float* px = p->sp_x + (long)b * PC_SP_AUG;
        for (int c = 0; c < PC_SP_AUG; c++) px[c] = c < CRET_SPECIES ? o[CRET_OBS_SPECIES + c] : c == CRET_SPECIES ? 1 : 0;
    }
    pc_mm(p->sp_x, B, PC_SP_AUG, W[PC_W_SP], PC_SP, p->sp, PC_SP, 0);
    pc_mm(p->self_x, B, PC_SELF_AUG, W[PC_W_S1], PC_D, p->y, PC_D, 1);
    pc_act(p->s1a, PC_DA, 0, p->y, B, PC_D, 0, 1);
    pc_mm(p->s1a, B, PC_DA, W[PC_W_S2], PC_D, p->y, PC_D, 1);
    pc_act(p->selfa, PC_DA, 0, p->y, B, PC_D, 0, 1);
    pc_act(p->cat, PC_CAT_AUG, 0, p->y, B, PC_D, 0, 0);
    pc_mm(p->selfa, B, PC_DA, W[PC_W_Q], PC_D, p->q, PC_D, 0);
    // Terrain patch: im2col with columns (ky, kx, channel), then two valid 3x3 convs and an FC.
    for (int r = 0; r < R1; r++) {
        int b = r / (PC_P1 * PC_P1), pos = r % (PC_P1 * PC_P1), oy = pos / PC_P1, ox = pos % PC_P1;
        const float* patch = obs + (long)b * CRET_OBS_SIZE + CRET_OBS_PATCH;
        float* col = p->col1 + (long)r * PC_COL1;
        for (int k = 0; k < PC_COL1; k++) {
            if (k >= 18) { col[k] = k == 18 ? 1 : 0; continue; }
            int kk = k / 2, ch = k % 2;
            col[k] = patch[((oy + kk / 3) * PC_P + ox + kk % 3) * 2 + ch];
        }
    }
    pc_mm(p->col1, R1, PC_COL1, W[PC_W_C1], PC_C1, p->c1, PC_C1, 1);
    for (int r = 0; r < R2; r++) {
        int b = r / (PC_P2 * PC_P2), pos = r % (PC_P2 * PC_P2), oy = pos / PC_P2, ox = pos % PC_P2;
        float* col = p->col2 + (long)r * PC_COL2;
        for (int k = 0; k < PC_COL2; k++) {
            if (k >= 9 * PC_C1) { col[k] = k == 9 * PC_C1 ? 1 : 0; continue; }
            int kk = k / PC_C1, ch = k % PC_C1;
            col[k] = p->c1[((long)b * PC_P1 * PC_P1 + (oy + kk / 3) * PC_P1 + ox + kk % 3) * PC_C1 + ch];
        }
    }
    pc_mm(p->col2, R2, PC_COL2, W[PC_W_C2], PC_C2, p->y, PC_C2, 1);
    pc_act(p->p3a, PC_FLAT_AUG, 0, p->y, B, PC_FLAT, 0, 1);   // (B*9, 32) rows are (B, 288) rows
    pc_mm(p->p3a, B, PC_FLAT_AUG, W[PC_W_P3], PC_D, p->y, PC_D, 1);
    pc_act(p->cat, PC_CAT_AUG, PC_D, p->y, B, PC_D, 0, 0);
    // Neighbours: [record (10), contract 4 flags (4), species embedding (16), 1, 0] through the shared MLP.
    for (int r = 0; r < RN; r++) {
        int b = r / PC_NB, n = r % PC_NB;
        const float* o = obs + (long)b * CRET_OBS_SIZE;
        float* e = p->ent_x + (long)r * PC_ENT_AUG;
        for (int c = 0; c < PC_ENT_AUG; c++)
            e[c] = c < CRET_NEIGHBOR_OBS ? o[CRET_OBS_NEIGHBOR + n * CRET_NEIGHBOR_OBS + c]
                 : c < PC_ENT_OBS ? o[PC_OBS_NEIGH4 + n * CRET_NEIGHBOR_OBS4 + c - CRET_NEIGHBOR_OBS]
                 : c < PC_ENT_IN ? p->sp[(long)b * PC_SP + c - PC_ENT_OBS] : c == PC_ENT_IN ? 1 : 0;
    }
    pc_mm(p->ent_x, RN, PC_ENT_AUG, W[PC_W_E1], PC_D, p->y, PC_D, 1);
    pc_act(p->e1a, PC_DA, 0, p->y, RN, PC_D, 0, 1);
    pc_mm(p->e1a, RN, PC_DA, W[PC_W_E2], PC_D, p->h, PC_D, 1);
    // Attention (query from the self embedding, score q . h / 4 per head) and max pools over present slots only;
    // both are 0 when no neighbour is present (ce_pool_fwd).
    for (int b = 0; b < B; b++) {
        float* cat = p->cat + (long)b * PC_CAT_AUG;
        bool valid[PC_NB];
        for (int n = 0; n < PC_NB; n++) valid[n] = p->ent_x[((long)b * PC_NB + n) * PC_ENT_AUG] > 0.5f;
        for (int g = 0; g < PC_HEADS; g++) {
            float s[PC_NB], alpha[PC_NB], best = -1e30f, sum = 0;
            for (int n = 0; n < PC_NB; n++) {
                const float* hn = p->h + ((long)b * PC_NB + n) * PC_D + g * PC_DH;
                const float* q = p->q + (long)b * PC_D + g * PC_DH;
                float dot = 0;
                for (int d = 0; d < PC_DH; d++) dot += q[d] * hn[d];
                s[n] = dot * 0.25f;
                if (valid[n]) best = fmaxf(best, s[n]);
            }
            for (int n = 0; n < PC_NB; n++) { alpha[n] = valid[n] ? expf(s[n] - best) : 0; sum += alpha[n]; }
            for (int n = 0; n < PC_NB; n++) alpha[n] = sum > 0 ? alpha[n] / sum : 0;
            for (int d = 0; d < PC_DH; d++) {
                float att = 0, mx = 0; bool any = false;
                for (int n = 0; n < PC_NB; n++) {
                    if (!valid[n]) continue;
                    float v = p->h[((long)b * PC_NB + n) * PC_D + g * PC_DH + d];
                    att += alpha[n] * v; mx = any ? fmaxf(mx, v) : v; any = true;
                }
                cat[2 * PC_D + g * PC_DH + d] = att;
                cat[3 * PC_D + g * PC_DH + d] = mx;
            }
        }
        for (int c = 0; c < PC_SP; c++) cat[4 * PC_D + c] = p->sp[(long)b * PC_SP + c];
        cat[PC_CAT] = 1;
        for (int c = PC_CAT + 1; c < PC_CAT_AUG; c++) cat[c] = 0;
    }
    // Trunk.
    pc_mm(p->cat, B, PC_CAT_AUG, W[PC_W_F1], PC_TRUNK, p->y, PC_TRUNK, 1);
    pc_act(p->f1a, PC_TRUNK_AUG, 0, p->y, B, PC_TRUNK, 0, 1);
    pc_mm(p->f1a, B, PC_TRUNK_AUG, W[PC_W_F2], H, p->x, H, 1);
}

// The trainer's numerically stable sigmoid and lerp (src/algo.cu, mingru_gate).
static inline float pc_sigmoid(float x) { float z = expf(-fabsf(x)); return x >= 0 ? 1 / (1 + z) : z / (1 + z); }
static inline float pc_lerp(float a, float b, float w) { float d = b - a; return fabsf(w) < 0.5f ? a + w * d : b - d * (1 - w); }

void cret_policy_forward(CretPolicy* p, const float* obs, const int* slots, int n) {
    int H = p->hidden;
    if (n <= 0) return;
    if (p->net == 0) pc_mm(obs, n, CRET_OBS_SIZE, p->enc, H, p->x, H, 0);
    else pc_encode_structured(p, obs, n);
    // MinGRU (mingru_forward + mingru_gate): h = lerp(state, h~, sigmoid(gate)), out = s h + (1 - s) x.
    const float* x = p->x;
    for (int l = 0; l < p->layers; l++) {
        pc_mm(x, n, H, p->gru[l], 3 * H, p->comb, 3 * H, 0);
        for (int k = 0; k < n; k++) {
            const float* c = p->comb + (long)k * 3 * H;
            float* st = p->state + ((long)l * p->max_agents + slots[k]) * H;
            const float* xk = x + (long)k * H;
            float* o = p->out + (long)k * H;
            for (int j = 0; j < H; j++) {
                float hidden = c[j], z = pc_sigmoid(c[H + j]), s = pc_sigmoid(c[2 * H + j]);
                float ht = hidden >= 0 ? hidden + 0.5f : pc_sigmoid(hidden);
                float h = pc_lerp(st[j], ht, z);
                st[j] = h;
                o[j] = s * h + (1 - s) * xk[j];
            }
        }
        x = p->out;
    }
    pc_mm(p->out, n, H, p->dec, CRET_POLICY_OUT, p->logits, CRET_POLICY_OUT, 0);
}

const float* cret_policy_output(const CretPolicy* p, int k) { return p->logits + (long)k * CRET_POLICY_OUT; }
const float* cret_policy_encoded(const CretPolicy* p, int k) { return p->x + (long)k * p->hidden; }

// The sim's generator (sim.c xs32 and frand), on a stream the caller owns.
static inline uint32_t pc_xs32(uint32_t* s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x ? x : 0x9e3779b9u; }

void cret_policy_pick(const CretPolicy* p, int k, bool greedy, uint32_t* rng, float* actions) {
    const float* l = cret_policy_output(p, k);
    for (int h = 0; h < CRET_ACTION_HEADS; h++) {
        int A = PC_HEAD_SIZES[h], pick = A - 1;
        float best = l[0];
        for (int a = 1; a < A; a++) best = fmaxf(best, l[a]);
        if (greedy) {
            for (int a = 0; a < A; a++) if (l[a] == best) { pick = a; break; }
        } else {
            // As the trainer's sample_logits: the first action whose cumulative probability exceeds a uniform draw.
            float sum = 0;
            for (int a = 0; a < A; a++) sum += expf(l[a] - best);
            float lse = best + logf(sum), u = (pc_xs32(rng) >> 8) * (1.0f / 16777216.0f), cum = 0;
            for (int a = 0; a < A; a++) { cum += expf(l[a] - lse); if (u < cum) { pick = a; break; } }
        }
        actions[h] = (float)pick;
        l += A;
    }
}

void cret_policy_reset(CretPolicy* p, int slot) {
    for (int l = 0; l < p->layers; l++)
        for (int i = 0; i < p->max_agents; i++)
            if (slot < 0 || i == slot) memset(p->state + ((long)l * p->max_agents + i) * p->hidden, 0, p->hidden * sizeof(float));
    for (int i = 0; i < p->max_agents; i++) if (slot < 0 || i == slot) p->life[i] = 0;
}

void cret_policy_act(CretPolicy* p, const CretSim* s, const bool* mask, bool greedy, uint32_t* rng, CretInput* in) {
    int n = 0, count = s->cfg.num_agents < p->max_agents ? s->cfg.num_agents : p->max_agents;
    for (int i = 0; i < count; i++) {
        if (!mask[i]) continue;
        const CretAgent* a = &s->agents[i];
        // A death or a new life (ticks_alive starts again) is a terminal: the next life starts from a zero state.
        if (!a->alive || a->ticks_alive + 1 < p->life[i]) cret_policy_reset(p, i);
        if (!a->alive) { in[i] = (CretInput){0}; continue; }
        p->life[i] = a->ticks_alive + 1;
        cret_sim_observe(s, i, p->obs + (long)n * CRET_OBS_SIZE);
        p->slots[n++] = i;
    }
    cret_policy_forward(p, p->obs, p->slots, n);
    for (int k = 0; k < n; k++) {
        float act[CRET_ACTION_HEADS];
        cret_policy_pick(p, k, greedy, rng, act);
        in[p->slots[k]] = cret_decode_action(act);
    }
}

long cret_policy_param_count(int net, int hidden, int layers) {
    long n = 0;
    if (net == 0) n += (long)hidden * CRET_OBS_SIZE;
    else for (int i = 0; i < PC_NW; i++) { int r, c; pc_shape(i, hidden, &r, &c); n += (long)r * c; }
    return n + (long)CRET_POLICY_OUT * hidden + (long)layers * 3 * hidden * hidden;
}

// Every tensor the trainer registers is a multiple of 8 floats when hidden is, so the 16-byte alignment of its
// allocator adds no padding and the file is the tensors back to back. Other sizes are refused.
static bool pc_valid(int net, int hidden, int layers) {
    return (net == 0 || net == 1) && hidden >= 8 && hidden <= 4096 && hidden % 8 == 0 && layers >= 1 && layers <= 8;
}

CretPolicy* cret_policy_create(const float* weights, long n, int net, int hidden, int layers, int max_agents,
        char* err, size_t errlen) {
    if (net < 0 || hidden <= 0 || layers <= 0) {
        // Find the sizes from the float count: exactly one (net, hidden, layers) must fit.
        int found = 0, fn = 0, fh = 0, fl = 0;
        for (int tn = 0; tn <= 1; tn++) for (int th = 8; th <= 4096; th += 8) for (int tl = 1; tl <= 8; tl++) {
            if ((net >= 0 && tn != net) || (hidden > 0 && th != hidden) || (layers > 0 && tl != layers)) continue;
            if (cret_policy_param_count(tn, th, tl) == n) { found++; fn = tn; fh = th; fl = tl; }
        }
        if (found != 1) {
            snprintf(err, errlen, "%ld floats fit %s policy of contract %d (obs %d, actions %d); name the sizes as PATH:NET:HIDDEN:LAYERS "
                "(cretaceous_net 0 at 256 x 2 needs %ld, cretaceous_net 1 at 256 x 2 needs %ld)", n, found ? "more than one" : "no",
                CRET_CONTRACT_VERSION, CRET_OBS_SIZE, CRET_POLICY_ATN, cret_policy_param_count(0, 256, 2), cret_policy_param_count(1, 256, 2));
            return NULL;
        }
        net = fn; hidden = fh; layers = fl;
    }
    if (!pc_valid(net, hidden, layers)) {
        snprintf(err, errlen, "cretaceous_net %d, hidden %d x %d layers is not a policy this path runs (net 0 or 1, hidden a multiple of 8 up to 4096, 1-8 layers)",
            net, hidden, layers);
        return NULL;
    }
    long need = cret_policy_param_count(net, hidden, layers);
    if (n != need) {
        snprintf(err, errlen, "%ld floats; contract %d (obs %d, actions %d) with cretaceous_net %d, hidden %d x %d layers needs %ld (another contract, encoder or size?)",
            n, CRET_CONTRACT_VERSION, CRET_OBS_SIZE, CRET_POLICY_ATN, net, hidden, layers, need);
        return NULL;
    }
    if (max_agents < 1) max_agents = 1;
    CretPolicy* p = calloc(1, sizeof *p);
    p->net = net; p->hidden = hidden; p->layers = layers; p->max_agents = max_agents;
    p->weights = malloc((size_t)n * sizeof(float)); memcpy(p->weights, weights, (size_t)n * sizeof(float));
    const float* w = p->weights;
    if (net == 0) { p->enc = w; w += (long)hidden * CRET_OBS_SIZE; }
    else for (int i = 0; i < PC_NW; i++) { int r, c; pc_shape(i, hidden, &r, &c); p->ce[i] = w; w += (long)r * c; }
    p->dec = w; w += (long)CRET_POLICY_OUT * hidden;
    for (int l = 0; l < layers; l++) { p->gru[l] = w; w += 3L * hidden * hidden; }
    long B = max_agents, H = hidden;
    p->state = calloc((size_t)(layers * B * H), sizeof(float));
    p->life = calloc((size_t)B, sizeof(int));
    p->x = calloc((size_t)(B * H), sizeof(float)); p->comb = calloc((size_t)(B * 3 * H), sizeof(float));
    p->out = calloc((size_t)(B * H), sizeof(float)); p->logits = calloc((size_t)(B * CRET_POLICY_OUT), sizeof(float));
    p->obs = calloc((size_t)(B * CRET_OBS_SIZE), sizeof(float)); p->slots = calloc((size_t)B, sizeof(int));
    if (net == 1) {
        long R1 = B * PC_P1 * PC_P1, R2 = B * PC_P2 * PC_P2, RN = B * PC_NB;
        long ymax = B * PC_FLAT;   // widest scratch: conv2's (B*9, 32), the trunk's (B, 256) and the 64-wide FCs fit
        if (ymax < B * PC_TRUNK) ymax = B * PC_TRUNK;
        if (ymax < RN * PC_D) ymax = RN * PC_D;
        p->self_x = calloc((size_t)(B * PC_SELF_AUG), sizeof(float)); p->sp_x = calloc((size_t)(B * PC_SP_AUG), sizeof(float));
        p->sp = calloc((size_t)(B * PC_SP), sizeof(float)); p->s1a = calloc((size_t)(B * PC_DA), sizeof(float));
        p->selfa = calloc((size_t)(B * PC_DA), sizeof(float)); p->q = calloc((size_t)(B * PC_D), sizeof(float));
        p->col1 = calloc((size_t)(R1 * PC_COL1), sizeof(float)); p->c1 = calloc((size_t)(R1 * PC_C1), sizeof(float));
        p->col2 = calloc((size_t)(R2 * PC_COL2), sizeof(float)); p->p3a = calloc((size_t)(B * PC_FLAT_AUG), sizeof(float));
        p->ent_x = calloc((size_t)(RN * PC_ENT_AUG), sizeof(float)); p->e1a = calloc((size_t)(RN * PC_DA), sizeof(float));
        p->h = calloc((size_t)(RN * PC_D), sizeof(float)); p->cat = calloc((size_t)(B * PC_CAT_AUG), sizeof(float));
        p->f1a = calloc((size_t)(B * PC_TRUNK_AUG), sizeof(float)); p->y = calloc((size_t)ymax, sizeof(float));
    }
    return p;
}

CretPolicy* cret_policy_load(const char* path, int net, int hidden, int layers, int max_agents, char* err, size_t errlen) {
    FILE* f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "cannot read %s", path); return NULL; }
    fseek(f, 0, SEEK_END); long bytes = ftell(f); rewind(f);
    if (bytes <= 0 || bytes % 4) { fclose(f); snprintf(err, errlen, "%s: %ld bytes is not a checkpoint of fp32 weights", path, bytes); return NULL; }
    long n = bytes / 4;
    float* w = malloc((size_t)bytes);
    size_t got = fread(w, 4, (size_t)n, f); fclose(f);
    if ((long)got != n) { free(w); snprintf(err, errlen, "%s: short read", path); return NULL; }
    char why[400];
    CretPolicy* p = cret_policy_create(w, n, net, hidden, layers, max_agents, why, sizeof why);
    free(w);
    if (!p) snprintf(err, errlen, "%s holds %s", path, why);
    return p;
}

void cret_policy_free(CretPolicy* p) {
    if (!p) return;
    float* bufs[] = {p->weights, p->state, p->x, p->comb, p->out, p->logits, p->obs, p->self_x, p->sp_x, p->sp, p->s1a,
        p->selfa, p->q, p->col1, p->c1, p->col2, p->p3a, p->ent_x, p->e1a, p->h, p->cat, p->f1a, p->y};
    for (size_t i = 0; i < sizeof bufs / sizeof bufs[0]; i++) free(bufs[i]);
    free(p->life); free(p->slots); free(p);
}

void cret_policy_describe(const CretPolicy* p, char* out, size_t len) {
    snprintf(out, len, "cretaceous_net %d, hidden %d x %d layers", p->net, p->hidden, p->layers);
}

static const char* PC_SPECIES_KEY[CRET_SPECIES] = {"raptor", "stego", "trex", "pachy", "sauropelta", "galli"};

bool cret_policy_who_valid(const char* who) {
    if (!strcmp(who, "all") || !strcmp(who, "herbivores") || !strcmp(who, "carnivores")) return true;
    if (!strncmp(who, "slot", 4) && who[4] >= '0' && who[4] <= '9') return true;
    for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(who, PC_SPECIES_KEY[k])) return true;
    return false;
}

bool cret_policy_who_matches(const char* who, int slot, int species) {
    if (!strcmp(who, "all")) return true;
    if (!strcmp(who, "herbivores")) return CRET_SPECIES_TABLE[species].diet == CRET_DIET_HERBIVORE;
    if (!strcmp(who, "carnivores")) return CRET_SPECIES_TABLE[species].diet == CRET_DIET_CARNIVORE;
    if (!strncmp(who, "slot", 4)) return atoi(who + 4) == slot;
    for (int k = 0; k < CRET_SPECIES; k++) if (!strcmp(who, PC_SPECIES_KEY[k])) return species == k;
    return false;
}

bool cret_policy_parse_arg(const char* arg, CretPolicyArg* out) {
    memset(out, 0, sizeof *out);
    snprintf(out->who, sizeof out->who, "all");
    out->net = out->hidden = out->layers = -1;
    const char* eq = strchr(arg, '=');
    if (eq) {
        size_t wl = (size_t)(eq - arg);
        if (wl == 0 || wl >= sizeof out->who) return false;
        memcpy(out->who, arg, wl); out->who[wl] = 0; arg = eq + 1;
        if (!cret_policy_who_valid(out->who)) return false;
    }
    snprintf(out->path, sizeof out->path, "%s", arg);
    // A trailing ":NET:HIDDEN:LAYERS" (three integers) names the sizes; without it they are found from the file's size.
    char* cut[3]; int found = 0;
    for (char* c = out->path + strlen(out->path); c > out->path && found < 3; c--) {
        if (c[-1] == ':') cut[found++] = c - 1;
        else if (c[-1] < '0' || c[-1] > '9') break;
    }
    if (found == 3 && cut[0][1] && cut[1][1] && cut[2][1]) {
        out->layers = atoi(cut[0] + 1); out->hidden = atoi(cut[1] + 1); out->net = atoi(cut[2] + 1);
        *cut[2] = 0;
        return out->path[0] != 0 && pc_valid(out->net, out->hidden, out->layers);
    }
    return out->path[0] != 0;
}
