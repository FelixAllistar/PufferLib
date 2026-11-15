// Procedural animation layers (see dino_proc.h). Pure math, deterministic:
// the same inputs and state give the same outputs on every run.
#include "dino_proc.h"
#include "../sim.h"
#include <math.h>
#include <string.h>

const char* const DPROC_NAMES[DPROC_LAYERS] = {"lean", "slope", "spine", "tail", "head", "look", "breath", "feet"};
unsigned cret_dino_proc_flags = DPROC_ALL;

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float finite_or(float x, float d) { return isfinite(x) ? x : d; }
static inline float wrap_pi(float a) {
    if (!isfinite(a)) return 0;
    a = fmodf(a + 3.14159265f, 6.2831853f); if (a < 0) a += 6.2831853f;
    return a - 3.14159265f;
}

int dproc_layer(const char* name) {
    if (!name) return -1;
    if (!strcmp(name, "all")) return DPROC_LAYERS;
    for (int k = 0; k < DPROC_LAYERS; k++) if (!strcmp(name, DPROC_NAMES[k])) return k;
    return -1;
}
unsigned dproc_parse_flags(const char* spec, unsigned flags) {
    if (!spec) return flags;
    char w[24]; const char* p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;
        int n = 0; bool off = false;
        if (*p == '-' || *p == '+') { off = *p == '-'; p++; }
        while (*p && *p != ',' && *p != ' ' && n < 23) w[n++] = *p++;
        while (*p && *p != ',' && *p != ' ') p++;
        w[n] = 0;
        if (!strcmp(w, "none")) { flags = 0; continue; }
        int k = dproc_layer(w); if (k < 0) continue;
        unsigned bits = k == DPROC_LAYERS ? DPROC_ALL : 1u << k;
        flags = off ? flags & ~bits : flags | bits;
    }
    return flags & DPROC_ALL;
}

// ---------------------------------------------------------------- springs
// Closed form of x'' = -w^2 (x - t) - 2 w x' over dt with t constant:
// y = x - t, j = v + w y, x(dt) = t + (y + j dt) e^{-w dt}, v(dt) = (v - j w dt) e^{-w dt}.
void dp_spring(DpSpring* s, float target, float omega, float dt) {
    if (!(dt > 0)) return;
    if (!isfinite(target)) target = s->x;
    if (!isfinite(s->x) || !isfinite(s->v)) { s->x = target; s->v = 0; }
    if (!(omega > 0)) return;
    float y = s->x - target, j = s->v + omega * y, e = expf(-omega * dt);
    s->x = target + (y + j * dt) * e;
    s->v = (s->v - j * omega * dt) * e;
}

// ---------------------------------------------------------------- data
// Adult values per species. Mass and length (sim.c): raptor 80 kg 3 m, stegosaur 5 t 8.5 m,
// tyrannosaur 8 t 12 m, pachycephalosaur 450 kg 4.5 m, sauropelta 1.5 t 5.2 m, gallimimus 400 kg 6 m.
// Heavier animals bank less, bend less and their tails swing slower (rate ~ 1/sqrt(length));
// breathing ~ mass^-1/4 (about 27 breaths a minute for the raptor, 8 for the tyrannosaur);
// the stegosaur's and sauropelta's armoured tails are stiff and short in swing.
const DinoProcParams DPROC_SPECIES[CRET_SPECIES] = {
    //                    lean k/max/w     chest k/max/w     bend k/max/w/front      tail lag/acc/vacc/lift   tail w/tip/yaw/pitch    sway      stab k/w/max       look yaw/pitch/w/range  breath
    [CRET_SP_RAPTOR] =     {0.35f, 0.25f, 7.0f, 0.45f, 0.22f, 6.0f, 0.10f, 0.30f, 8.0f, 0.60f, 0.12f, 0.025f, 0.006f, 0.06f, 9.0f, 0.55f, 0.45f, 0.25f, 0.040f, 0.35f, 0.70f, 4.0f, 0.25f, 0.95f, 0.40f, 7.0f, 6.0f, 0.45f, 0.022f},
    [CRET_SP_STEGO] =      {0.12f, 0.06f, 4.0f, 0.30f, 0.10f, 3.5f, 0.05f, 0.12f, 4.5f, 0.55f, 0.10f, 0.010f, 0.003f, 0.00f, 4.5f, 0.70f, 0.30f, 0.12f, 0.025f, 0.18f, 0.40f, 2.5f, 0.15f, 0.60f, 0.30f, 4.0f, 3.0f, 0.15f, 0.018f},
    [CRET_SP_TREX] =       {0.20f, 0.10f, 3.5f, 0.40f, 0.14f, 3.5f, 0.06f, 0.15f, 4.0f, 0.60f, 0.14f, 0.015f, 0.004f, 0.03f, 4.0f, 0.60f, 0.30f, 0.15f, 0.030f, 0.15f, 0.60f, 2.5f, 0.20f, 0.70f, 0.35f, 4.0f, 3.0f, 0.13f, 0.022f},
    [CRET_SP_PACHY] =      {0.30f, 0.20f, 6.0f, 0.40f, 0.18f, 5.0f, 0.09f, 0.25f, 7.0f, 0.60f, 0.12f, 0.020f, 0.005f, 0.05f, 7.0f, 0.60f, 0.40f, 0.20f, 0.035f, 0.30f, 0.65f, 3.5f, 0.22f, 0.80f, 0.40f, 6.0f, 5.0f, 0.30f, 0.022f},
    [CRET_SP_SAUROPELTA] = {0.12f, 0.06f, 4.5f, 0.30f, 0.10f, 4.0f, 0.04f, 0.10f, 5.0f, 0.55f, 0.08f, 0.010f, 0.003f, 0.00f, 6.0f, 0.70f, 0.25f, 0.10f, 0.020f, 0.20f, 0.35f, 3.0f, 0.12f, 0.50f, 0.25f, 4.5f, 3.0f, 0.20f, 0.018f},
    [CRET_SP_GALLI] =      {0.40f, 0.28f, 6.5f, 0.45f, 0.20f, 5.5f, 0.10f, 0.30f, 7.0f, 0.60f, 0.12f, 0.025f, 0.006f, 0.06f, 7.0f, 0.55f, 0.45f, 0.25f, 0.040f, 0.30f, 0.80f, 4.0f, 0.25f, 0.90f, 0.45f, 7.0f, 6.0f, 0.30f, 0.022f},
};
void dproc_params(int species, float size_scale, DinoProcParams* out) {
    if (species < 0 || species >= CRET_SPECIES) species = 0;
    *out = DPROC_SPECIES[species];
    float s = clampf(finite_or(size_scale, 1), 0.2f, 1.5f), r = 1.0f / sqrtf(s);
    out->lean_omega *= r; out->chest_omega *= r; out->bend_omega *= r; out->tail_omega *= r; out->stab_omega *= r; out->look_omega *= r;
    out->sway_hz *= r; out->breath_hz *= powf(s, -0.75f);
}

// ---------------------------------------------------------------- rig
static bool is_below(const int* parent, int n, int j, int root) {
    for (int k = 0; j >= 0 && k < n; k++, j = parent[j]) if (j == root) return true;
    return false;
}
// Ancestors of j from its parent up to (not including) stop, nearest first.
static int path_up(const int* parent, int n, int j, int stop, int* out, int max) {
    int c = 0;
    for (int k = 0, a = j >= 0 ? parent[j] : -1; a >= 0 && a != stop && k < n; k++, a = parent[a]) if (c < max) out[c++] = a;
    return c;
}
void dproc_rig(DinoProcRig* r, const int* parent, int n, const float* neck_w, const float* tail_w, int head_j, int hip_j) {
    memset(r, 0, sizeof *r); r->chest = r->neck_base = r->tail_base = r->pelvis = -1;
    if (n <= 0) return;
    // Neck base: walk up from the head while the joints turn with the neck; the first that does not.
    int nb = -1;
    if (head_j >= 0 && head_j < n) { nb = head_j; for (int k = 0; k < n && nb >= 0 && neck_w[nb] > 0; k++) nb = parent[nb]; }
    // Tail base: the parent of the topmost tail joint (the tail's weights grow from 0 at its base).
    int tb = -1, best = 1 << 30;
    for (int j = 0; j < n; j++) if (tail_w[j] > 0) {
        int a = parent[j]; if (a < 0 || tail_w[a] > 0) continue;
        int d = 0; for (int q = a; q >= 0 && d < n; q = parent[q]) d++;
        if (d < best) { best = d; tb = a; }
    }
    // The pelvis: the lowest common ancestor of the neck base and the tail base (else a hip's parent, else the root).
    int other = tb >= 0 ? tb : hip_j >= 0 && hip_j < n ? parent[hip_j] : -1, pel = -1;
    if (nb >= 0 && other >= 0) { for (int a = nb; a >= 0 && pel < 0; a = parent[a]) if (is_below(parent, n, other, a)) pel = a; }
    if (pel < 0) { pel = 0; while (pel < n && parent[pel] >= 0) pel = parent[pel]; }
    r->neck_base = nb; r->tail_base = tb; r->pelvis = pel;
    if (nb >= 0 && nb != pel && is_below(parent, n, nb, pel)) {
        int up[DPROC_SPINE]; int c = path_up(parent, n, nb, pel, up, DPROC_SPINE);
        // Front spine, hips first; the neck base itself only when nothing lies between it and the pelvis.
        if (c == 0) { r->front[0] = nb; r->nfront = 1; }
        else for (int k = 0; k < c; k++) r->front[r->nfront++] = up[c - 1 - k];
        int ci = (r->nfront * 2) / 3; r->chest = r->front[ci < r->nfront ? ci : r->nfront - 1];   // two thirds of the way to the neck
    } else if (nb >= 0) r->chest = nb;
    if (r->chest < 0) r->chest = pel;
    if (tb >= 0 && tb != pel && is_below(parent, n, tb, pel)) {
        int up[DPROC_SPINE]; int c = path_up(parent, n, tb, pel, up, DPROC_SPINE - 1);
        for (int k = 0; k < c; k++) r->rear[r->nrear++] = up[c - 1 - k];
        r->rear[r->nrear++] = tb;   // the tail base turns the whole tail with the hips' bend
    }
}

// ---------------------------------------------------------------- body layers
void dproc_step(DinoProcState* s, const DinoProcParams* p, const DinoProcIn* in, unsigned flags, DinoProcOut* out) {
    memset(out, 0, sizeof *out);
    float dt = in->dt; if (!(dt > 0)) dt = 1.0f / 60; if (dt > 0.1f) dt = 0.1f;
    float yaw = finite_or(in->yaw, 0), speed = finite_or(in->speed, 0), y = finite_or(in->y, 0);
    if (!s->init) {
        memset(s, 0, sizeof *s); s->init = true; s->last_yaw = yaw; s->yaw_acc = 0; s->yaw.x = 0;
        s->vel.x = speed; s->vy.x = y; s->sway_t = (float)((unsigned)in->seed % 997) * 0.37f; s->breath_ph = (float)((unsigned)in->seed % 613) * 0.01f;
        s->breath_rate.x = p->breath_hz;
    }
    // Heading unwrapped and low-passed; its spring's velocity is the yaw rate (no per-tick spikes).
    s->yaw_acc += wrap_pi(yaw - s->last_yaw); s->last_yaw = yaw;
    if (fabsf(s->yaw_acc) > 64.0f) { float k = 6.2831853f * floorf(s->yaw_acc / 6.2831853f); s->yaw_acc -= k; s->yaw.x -= k; }
    dp_spring(&s->yaw, s->yaw_acc, 14.0f, dt);
    dp_spring(&s->vel, speed, 10.0f, dt);
    dp_spring(&s->vy, y, 14.0f, dt);
    float vy = s->vy.v, ay = (vy - s->last_vy) / dt; s->last_vy = vy;
    dp_spring(&s->vacc, clampf(ay, -30, 30), 10.0f, dt);
    float act = clampf(finite_or(in->active, 0), 0, 1);
    float w = clampf(s->yaw.v, -4, 4), v = fmaxf(s->vel.x, 0), afwd = clampf(s->vel.v, -15, 15), avert = clampf(s->vacc.x, -30, 30);
    float runf = in->sprint > in->walk ? clampf((v - in->walk) / (in->sprint - in->walk), 0, 1) : 0;
    // Lean: the bank angle a runner needs, atan(v w / g), in the body's roll sense (dino.c: -w v).
    float lean = (flags & DPROC_LEAN) ? -clampf(p->lean_k * atanf(v * w / 9.81f), -p->lean_max, p->lean_max) * act : 0;
    dp_spring(&s->lean, lean, p->lean_omega, dt); out->lean = s->lean.x;
    // Chest: the part of the slope the whole body does not take.
    float ch = (flags & DPROC_SLOPE) ? clampf((finite_or(in->slope, 0) - finite_or(in->body_pitch, 0)) * p->chest_k, -p->chest_max, p->chest_max) * act : 0;
    dp_spring(&s->chest, ch, p->chest_omega, dt); out->chest_pitch = s->chest.x;
    // Spine bend into the turn (also turning on the spot), front and rear in opposite yaw.
    float bend = (flags & DPROC_SPINE_BEND) ? clampf(p->bend_k * w, -p->bend_max, p->bend_max) * act : 0;
    dp_spring(&s->bend, bend, p->bend_omega, dt);
    out->bend_front = s->bend.x * p->bend_front; out->bend_rear = -s->bend.x * (1 - p->bend_front);
    // Tail: the base pivot is driven, each further pivot follows the one before, slower toward the tip (a whip).
    int nt = in->ntail < 0 ? 0 : in->ntail > DPROC_CHAIN ? DPROC_CHAIN : in->ntail;
    s->sway_t += dt;
    float rest = 1 - clampf(finite_or(in->move, 0), 0, 1);
    float sway = p->sway_amp * (0.35f + 0.65f * rest) * (sinf(6.2831853f * p->sway_hz * s->sway_t) + 0.3f * sinf(6.2831853f * p->sway_hz * 2.37f * s->sway_t + 1.3f));
    float ty = (flags & DPROC_TAIL) ? clampf(p->tail_lag * w + sway, -p->tail_yaw_max, p->tail_yaw_max) * act : 0;
    // Lift (+ up): held higher at a run, swings down as the body accelerates up or forward, up as it brakes.
    float tl = (flags & DPROC_TAIL) ? clampf(p->tail_lift * runf - p->tail_acc * afwd - p->tail_vacc * avert, -p->tail_pitch_max, p->tail_pitch_max) * act : 0;
    for (int k = 0; k < DPROC_CHAIN; k++) {
        float f = nt > 1 ? (float)k / (nt - 1) : 0, om = p->tail_omega * (1 - (1 - p->tail_tip) * f);
        float tgt_y = k == 0 || k >= nt ? ty : s->tail_yaw[k - 1].x, tgt_p = k == 0 || k >= nt ? tl : s->tail_pitch[k - 1].x;
        dp_spring(&s->tail_yaw[k], tgt_y, om, dt); dp_spring(&s->tail_pitch[k], tgt_p, om, dt);
        out->tail_yaw[k] = clampf(s->tail_yaw[k].x, -p->tail_yaw_max, p->tail_yaw_max);
        out->tail_pitch[k] = clampf(s->tail_pitch[k].x, -p->tail_pitch_max, p->tail_pitch_max);
    }
    // Look-at: toward the target within the limits; a target far behind is let go.
    float free = clampf(finite_or(in->head_free, 0), 0, 1) * act;
    bool look = (flags & DPROC_LOOK) && in->look_on && isfinite(in->look_yaw) && isfinite(in->look_pitch) && fabsf(in->look_yaw) < 2.0f * p->look_yaw_max + 0.3f;
    float lyaw = look ? clampf(in->look_yaw, -p->look_yaw_max, p->look_yaw_max) * free : 0;
    float lpit = look ? clampf(in->look_pitch, -p->look_pitch_max, p->look_pitch_max) * free : 0;
    dp_spring(&s->look_yaw, lyaw, p->look_omega, dt); dp_spring(&s->look_pitch, lpit, p->look_omega, dt);
    out->look_yaw = clampf(s->look_yaw.x, -p->look_yaw_max, p->look_yaw_max); out->look_pitch = clampf(s->look_pitch.x, -p->look_pitch_max, p->look_pitch_max);
    // Breathing: faster and deeper with spent stamina and a sprint; the rate changes slowly.
    float spent = 1 - clampf(finite_or(in->stamina, 1), 0, 1), ex = clampf(spent + (in->sprinting ? 0.6f : 0) + 0.3f * runf, 0, 1.6f);
    dp_spring(&s->breath_rate, p->breath_hz * (1 + 1.6f * ex), 0.8f, dt);
    dp_spring(&s->breath_depth, p->breath_amp * (1 + 1.1f * ex), 0.8f, dt);
    s->breath_ph += 6.2831853f * fmaxf(s->breath_rate.x, 0) * dt; s->breath_ph -= 6.2831853f * floorf(s->breath_ph / 6.2831853f);
    // In quicker, out slower: a skewed cycle 0..1.
    float c = 0.5f - 0.5f * cosf(s->breath_ph + 0.35f * sinf(s->breath_ph));
    out->breath = (flags & DPROC_BREATH) ? fmaxf(s->breath_depth.x, 0) * c * (act > 0 ? 1.0f : 0.0f) : 0;
}

void dproc_head(DinoProcState* s, const DinoProcParams* p, float elev, float azim, float body_pitch, float body_yaw,
                float weight, float dt, unsigned flags, float* neck_pitch, float* neck_yaw) {
    *neck_pitch = *neck_yaw = 0;
    if (!(dt > 0)) dt = 1.0f / 60; if (dt > 0.1f) dt = 0.1f;
    float e = finite_or(elev, 0) + finite_or(body_pitch, 0), a = finite_or(azim, 0) + finite_or(body_yaw, 0);
    if (!s->head_init) { s->head_init = true; s->stab_e.x = e; s->stab_a.x = a; s->stab_e.v = s->stab_a.v = 0; s->last_e = e; s->last_a = a; }
    a = s->last_a + wrap_pi(a - s->last_a); s->last_a = a; s->last_e = e;
    dp_spring(&s->stab_e, e, p->stab_omega, dt); dp_spring(&s->stab_a, a, p->stab_omega, dt);
    if (!(flags & DPROC_HEAD)) return;
    float k = p->stab_k * clampf(finite_or(weight, 0), 0, 1);
    *neck_pitch = clampf(-k * (e - s->stab_e.x), -p->stab_max, p->stab_max);
    *neck_yaw = clampf(-k * wrap_pi(a - s->stab_a.x), -p->stab_max, p->stab_max);
}

// ---------------------------------------------------------------- legs
static float d3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float unit3(float* a) { float l = sqrtf(d3(a, a)); if (l > 1e-8f) { a[0] /= l; a[1] /= l; a[2] /= l; } return l; }
bool dp_two_bone(const float H[3], const float K[3], const float P[3], const float W[3], float K2[3], float T[3]) {
    float hk[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]}, kp[3] = {P[0] - K[0], P[1] - K[1], P[2] - K[2]};
    float a = sqrtf(d3(hk, hk)), b = sqrtf(d3(kp, kp));
    float u[3] = {W[0] - H[0], W[1] - H[1], W[2] - H[2]}, dist = unit3(u);
    if (!(a >= 1e-4f) || !(b >= 1e-4f) || !(dist >= 1e-4f) || !isfinite(dist)) return false;
    dist = clampf(dist, fabsf(a - b) + 1e-3f, (a + b) * 0.999f);
    // The pole: the knee's offset from the hip-target line, as animated.
    float v[3] = {hk[0] - d3(hk, u) * u[0], hk[1] - d3(hk, u) * u[1], hk[2] - d3(hk, u) * u[2]};
    if (unit3(v) < 1e-5f) {   // a straight leg: bend forward (+x) or, along x, toward +z
        float f[3] = {1, 0, 0}; if (fabsf(u[0]) > 0.9f) { f[0] = 0; f[2] = 1; }
        float q = d3(f, u); v[0] = f[0] - q * u[0]; v[1] = f[1] - q * u[1]; v[2] = f[2] - q * u[2]; unit3(v);
    }
    float ca = clampf((a * a + dist * dist - b * b) / (2 * a * dist), -1, 1), sa = sqrtf(fmaxf(0, 1 - ca * ca));
    for (int r = 0; r < 3; r++) { T[r] = H[r] + u[r] * dist; K2[r] = H[r] + a * (ca * u[r] + sa * v[r]); }
    return true;
}

void dp_foot_lock(DpFootLock* L, float x, float z, float plant, float max_off, float dt, float* off_x, float* off_z) {
    *off_x = *off_z = 0;
    if (!(dt > 0)) dt = 1.0f / 60; if (dt > 0.1f) dt = 0.1f;
    if (!isfinite(x) || !isfinite(z)) { L->on = false; L->w = 0; return; }
    bool want = plant > 0.8f;
    float dx = L->x - x, dz = L->z - z, d = sqrtf(dx * dx + dz * dz);
    if (L->on && (!want || d > max_off)) L->on = false;
    if (!L->on && want && L->w < 0.05f) { L->on = true; L->x = x; L->z = z; dx = dz = d = 0; }
    L->w += ((L->on ? 1.0f : 0.0f) - L->w) * (1 - expf(-(L->on ? 25.0f : 12.0f) * dt));
    if (L->w < 1e-3f) return;
    if (d > max_off && d > 1e-6f) { dx *= max_off / d; dz *= max_off / d; }
    *off_x = dx * L->w; *off_z = dz * L->w;
}
