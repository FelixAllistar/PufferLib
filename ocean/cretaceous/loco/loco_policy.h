// CPU inference of a locomotion checkpoint (the trainer's default net:
// linear encoder, MinGRU layers, decoder; continuous action heads), for
// tools/loco_play.c and tools/loco_view.c. The same math as the game's
// policy_cpu.c with its linear encoder, for the loco observation and action
// sizes. The policy acts with its mean (the trainer samples around it with a
// learned standard deviation).
//
// File layout (src/algo.cu weights_create, fp32): the trainer's parameter
// buffer copied flat, in registration order:
//   encoder (hidden x LOCO_OBS), no bias
//   decoder ((LOCO_MAX_DOF + 1) x hidden): action means, then the value
//   decoder log std (LOCO_MAX_DOF)
//   MinGRU layer l: (3 hidden x hidden), l = 0 .. layers - 1
// The trainer's allocator starts every tensor on a 16-byte boundary of the
// parameter buffer (8 bf16 values; 4 floats in a --float build), so the
// 69-value log std is followed by 3 padding values and the MinGRU layers start
// at the next multiple of 8. The file holds only as many values as there are
// parameters, so it ends 3 values short of the last layer: those 3 weights
// (the last row's last inputs) stay at the trainer's random init, which a
// reader cannot know; this runner takes them as 0. The hidden size and layer
// count are found from the file's size. Checked against the trainer's own
// actions on recorded observations (docs/locomotion/README.md, run 2).
#ifndef CRET_LOCO_POLICY_H
#define CRET_LOCO_POLICY_H
#include "loco_env.h"

typedef struct LocoPolicy {
    int hidden, layers, max_agents;
    float *w, *enc, *dec, *gru[8];
    float *state, *x, *comb, *out, *y;
} LocoPolicy;

// A tensor's length rounded up to the trainer allocator's alignment (8 values).
#define LOCO_POLICY_ALIGN(n) (((n) + 7L) / 8 * 8)
static long loco_policy_count(int h, int l) { return (long)h * LOCO_OBS + (long)(LOCO_MAX_DOF + 1) * h + LOCO_MAX_DOF + (long)l * 3 * h * h; }

static void loco_policy_free(LocoPolicy* p) {
    if (!p) return;
    free(p->w); free(p->state); free(p->x); free(p->comb); free(p->out); free(p->y); free(p);
}
// Takes ownership of n weights. NULL with a message when no (hidden, layers) fits n.
static LocoPolicy* loco_policy_create(float* w, long n, int max_agents, char* err, size_t errn) {
    int fh = 0, fl = 0, fits = 0;
    for (int h = 16; h <= 4096; h *= 2) for (int l = 1; l <= 8; l++) if (loco_policy_count(h, l) == n) { fh = h; fl = l; fits++; }
    if (fits != 1) { snprintf(err, errn, "%ld weights fit no single (hidden, layers) of the loco net (obs %d, actions %d)", n, LOCO_OBS, LOCO_MAX_DOF); free(w); return NULL; }
    LocoPolicy* p = (LocoPolicy*)calloc(1, sizeof(LocoPolicy));
    p->hidden = fh; p->layers = fl; p->max_agents = max_agents;
    // The MinGRU weights start after the log std's padding (see the layout above); the
    // buffer is grown to the padded length so the last layer's last values read as 0.
    long gru0 = (long)fh * LOCO_OBS + (long)(LOCO_MAX_DOF + 1) * fh + LOCO_POLICY_ALIGN(LOCO_MAX_DOF);
    long padded = gru0 + (long)fl * 3 * fh * fh;
    float* grown = (float*)realloc(w, sizeof(float) * padded);
    if (!grown) { free(w); free(p); snprintf(err, errn, "out of memory"); return NULL; }
    w = grown; memset(w + n, 0, sizeof(float) * (padded - n));
    p->w = w; p->enc = w; p->dec = p->enc + (long)fh * LOCO_OBS;
    float* g = w + gru0;
    for (int l = 0; l < fl; l++) { p->gru[l] = g; g += 3L * fh * fh; }
    p->state = (float*)calloc((size_t)fl * max_agents * fh, sizeof(float));
    p->x = (float*)calloc((size_t)max_agents * fh, sizeof(float)); p->out = (float*)calloc((size_t)max_agents * fh, sizeof(float));
    p->comb = (float*)calloc((size_t)max_agents * 3 * fh, sizeof(float)); p->y = (float*)calloc((size_t)max_agents * (LOCO_MAX_DOF + 1), sizeof(float));
    return p;
}
static LocoPolicy* loco_policy_load(const char* path, int max_agents, char* err, size_t errn) {
    FILE* f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "cannot read %s", path); return NULL; }
    fseek(f, 0, SEEK_END); long bytes = ftell(f); rewind(f);
    if (bytes <= 0 || bytes % 4) { fclose(f); snprintf(err, errn, "%s is not a checkpoint of fp32 weights", path); return NULL; }
    float* w = (float*)malloc((size_t)bytes); size_t got = fread(w, 4, (size_t)(bytes / 4), f); fclose(f);
    if ((long)got != bytes / 4) { free(w); snprintf(err, errn, "%s: short read", path); return NULL; }
    return loco_policy_create(w, bytes / 4, max_agents, err, errn);
}
// Clears one agent's recurrent state (a new episode), as the trainer does on terminals.
static void loco_policy_reset(LocoPolicy* p, int slot) {
    for (int l = 0; l < p->layers; l++) memset(p->state + ((long)l * p->max_agents + slot) * p->hidden, 0, sizeof(float) * p->hidden);
}
// Y (rows x N) = X (rows x K) W^T, W stored (N x K).
static void loco_mm(const float* X, int rows, int K, const float* W, int N, float* Y) {
    for (int r = 0; r < rows; r++) for (int j = 0; j < N; j++) {
        const float* x = X + (long)r * K; const float* wj = W + (long)j * K; float s = 0;
        for (int k = 0; k < K; k++) s += x[k] * wj[k];
        Y[(long)r * N + j] = s;
    }
}
static inline float loco_sigmoid(float x) { float z = expf(-fabsf(x)); return x >= 0 ? 1 / (1 + z) : z / (1 + z); }
static inline float loco_lerp(float a, float b, float w) { float d = b - a; return fabsf(w) < 0.5f ? a + w * d : b - d * (1 - w); }
// One step for agents 0..n-1 (obs: n rows of LOCO_OBS): their mean actions into act (n rows of LOCO_MAX_DOF).
static void loco_policy_forward(LocoPolicy* p, const float* obs, int n, float* act) {
    int H = p->hidden;
    loco_mm(obs, n, LOCO_OBS, p->enc, H, p->x);
    const float* x = p->x;
    for (int l = 0; l < p->layers; l++) {
        loco_mm(x, n, H, p->gru[l], 3 * H, p->comb);
        for (int k = 0; k < n; k++) {
            const float* c = p->comb + (long)k * 3 * H; float* st = p->state + ((long)l * p->max_agents + k) * H;
            const float* xk = x + (long)k * H; float* o = p->out + (long)k * H;
            for (int j = 0; j < H; j++) {
                float hid = c[j], z = loco_sigmoid(c[H + j]), s = loco_sigmoid(c[2 * H + j]);
                float ht = hid >= 0 ? hid + 0.5f : loco_sigmoid(hid);
                float hh = loco_lerp(st[j], ht, z);
                st[j] = hh; o[j] = s * hh + (1 - s) * xk[j];
            }
        }
        x = p->out;
    }
    loco_mm(p->out, n, H, p->dec, LOCO_MAX_DOF + 1, p->y);
    for (int k = 0; k < n; k++) memcpy(act + (long)k * LOCO_MAX_DOF, p->y + (long)k * (LOCO_MAX_DOF + 1), sizeof(float) * LOCO_MAX_DOF);
}
#endif
