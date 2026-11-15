#include "dino_ik.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline float approach(float cur, float target, float rate, float dt) { return cur + (target - cur) * (1 - expf(-rate * dt)); }

// ---------------------------------------------------------------- 3x4 affine math (row-major rows of [R|t])
static void m34_mul(const float* a, const float* b, float* o) {
    float r[12];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        r[i * 4 + 3] = a[i * 4] * b[3] + a[i * 4 + 1] * b[7] + a[i * 4 + 2] * b[11] + a[i * 4 + 3];
    }
    memcpy(o, r, sizeof r);
}
static void m34_trs(const float* t, const float* q, float s, float* o) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    o[0] = (1 - 2 * (y * y + z * z)) * s; o[1] = 2 * (x * y - z * w) * s; o[2] = 2 * (x * z + y * w) * s; o[3] = t[0];
    o[4] = 2 * (x * y + z * w) * s; o[5] = (1 - 2 * (x * x + z * z)) * s; o[6] = 2 * (y * z - x * w) * s; o[7] = t[1];
    o[8] = 2 * (x * z - y * w) * s; o[9] = 2 * (y * z + x * w) * s; o[10] = (1 - 2 * (x * x + y * y)) * s; o[11] = t[2];
}

// ---------------------------------------------------------------- legs
// True if joint j is root or below it.
static bool leg_below(const int* parent, int j, int root) { for (int k = 0; j >= 0 && k < DINO_MAX_JOINTS; k++, j = parent[j]) if (j == root) return true; return false; }

// Legs by joint name (the rigs' names without their "_NN" suffix; %c is the
// side): hip, knee, ankle, and toes rigged outside the leg (the
// gallimimus's IK toes). Each must be the parent of the next.
static const char* LEG_NAMES[][4] = {
    {"upperleg.%c", "lowerleg.%c", "ankle.%c", NULL},               // raptor, pachycephalosaur
    {"upperleg.%c", "lowerleg.%c", "backfoot.%c", NULL},            // stegosaur
    {"upperarm.%c", "lowerarm.%c", "foot.%c", NULL},                // stegosaur front legs
    {"def-thigh.%c", "def-shin.%c", "def-metatarsus.%c", NULL},     // tyrannosaur
    {"bip_hip_%c", "bip_knee_%c", "bip_foot_%c", "legik.%c"},       // gallimimus
    {"bone.%c", "bone.%c.001", "bone.%c.002", NULL},                // sauropelta front
    {"bone.%c.003", "bone.%c.004", "bone.%c.005", NULL},            // sauropelta back
};
static int joint_named(int njoint, char (*nm)[32], const char* fmt, char side) {
    char want[40]; snprintf(want, sizeof want, fmt, side); size_t n = strlen(want);
    for (int j = 0; j < njoint; j++) {
        if (strncmp(nm[j], want, n)) continue;
        const char* r = nm[j] + n;
        if (*r == 0) return j;
        if (*r++ != '_' || !*r) continue;
        while (*r >= '0' && *r <= '9') r++;
        if (!*r) return j;
    }
    return -1;
}
void dino_find_legs(DinoLegs* m, int njoint, const int* parent, const float (*rest)[8], float height, char (*nm)[32]) {
    static float G[DINO_MAX_JOINTS][12];
    for (int j = 0; j < njoint; j++) {
        float L[12]; m34_trs(rest[j], rest[j] + 3, rest[j][7], L);
        if (parent[j] < 0) memcpy(G[j], L, sizeof L); else m34_mul(G[parent[j]], L, G[j]);
    }
    m->nleg = 0; memset(m->leg_bits, 0, sizeof m->leg_bits);
    for (size_t e = 0; e < sizeof LEG_NAMES / sizeof LEG_NAMES[0]; e++)
        for (int sd = 0; sd < 2 && m->nleg < 4; sd++) {
            char side = sd ? 'r' : 'l';
            int h = joint_named(njoint, nm, LEG_NAMES[e][0], side), k = joint_named(njoint, nm, LEG_NAMES[e][1], side), a = joint_named(njoint, nm, LEG_NAMES[e][2], side);
            if (h < 0 || k < 0 || a < 0 || parent[k] != h || parent[a] != k) continue;
            bool dup = false; for (int l = 0; l < m->nleg; l++) dup |= m->leg[l].hip == h;
            if (dup) continue;
            int l = m->nleg++, att = LEG_NAMES[e][3] ? joint_named(njoint, nm, LEG_NAMES[e][3], side) : -1;
            m->leg[l].hip = h; m->leg[l].knee = k; m->leg[l].ankle = a;
            for (int j = 0; j < njoint; j++) {
                if (leg_below(parent, j, h)) m->leg_bits[j] |= 1u << l;
                if (leg_below(parent, j, k)) m->leg_bits[j] |= 1u << (4 + l);
                if (leg_below(parent, j, a) || (att >= 0 && leg_below(parent, j, att))) m->leg_bits[j] |= 1u << (8 + l);
            }
        }
    // Front legs: hips ahead of the middle of all hips (only on four legs).
    float xs = 0, ys = 0, zl = 0, zr = 0; int nl = 0, nr = 0;
    for (int l = 0; l < m->nleg; l++) xs += G[m->leg[l].hip][3];
    float xm = m->nleg ? xs / m->nleg : 0, fx = 0, bx = 0; int nf = 0, nb = 0;
    for (int l = 0; l < m->nleg; l++) {
        const float* g = G[m->leg[l].hip];
        m->leg[l].front = m->nleg == 4 && g[3] > xm;
        if (m->leg[l].front) { fx += g[3]; nf++; } else { bx += g[3]; nb++; }
        ys += g[7];
        if (g[11] < 0) { zl += g[11]; nl++; } else { zr += g[11]; nr++; }
    }
    m->hip_y = m->nleg ? ys / m->nleg : height * 0.5f;
    m->front_x = nf ? fx / nf : 0; m->hind_x = nb ? bx / nb : 0;
    m->hip_z = nl && nr ? 0.5f * (zr / nr - zl / nl) : 0;
}

// ---------------------------------------------------------------- foot IK
static float v3dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void v3cross(const float* a, const float* b, float* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static float v3unit(float* a) { float l = sqrtf(v3dot(a, a)); if (l > 1e-8f) { a[0] /= l; a[1] /= l; a[2] /= l; } return l; }
// Row-major rotation turning unit vector a toward unit vector b, by at most max_ang radians.
static void rot_toward(const float* a, const float* b, float max_ang, float* R) {
    float u[3]; v3cross(a, b, u); float s = v3unit(u), ang = fminf(atan2f(s, v3dot(a, b)), max_ang);
    if (s < 1e-7f || ang < 1e-6f) { u[0] = 1; u[1] = u[2] = 0; ang = 0; }
    float c = cosf(ang), sn = sinf(ang), t = 1 - c, x = u[0], y = u[1], z = u[2];
    float r[9] = {t * x * x + c, t * x * y - sn * z, t * x * z + sn * y, t * x * y + sn * z, t * y * y + c, t * y * z - sn * x,
                  t * x * z - sn * y, t * y * z + sn * x, t * z * z + c};
    memcpy(R, r, sizeof r);
}
// Turns a joint's model-space frame by R about the point p.
static void turn_frame(float* g, const float* R, const float* p) {
    float r[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i * 3 + j] = R[i * 3] * g[j] + R[i * 3 + 1] * g[4 + j] + R[i * 3 + 2] * g[8 + j];
    float d[3] = {g[3] - p[0], g[7] - p[1], g[11] - p[2]};
    for (int i = 0; i < 3; i++) { g[i * 4] = r[i * 3]; g[i * 4 + 1] = r[i * 3 + 1]; g[i * 4 + 2] = r[i * 3 + 2]; g[i * 4 + 3] = p[i] + R[i * 3] * d[0] + R[i * 3 + 1] * d[1] + R[i * 3 + 2] * d[2]; }
}
// Foot IK over the animated model-space frames G (body transform M, whose
// linear part is gs times a rotation). Each foot keeps its height above the
// animation's ground plane but over the real ground (terrain, or a rock or
// log's top, cret_ground_at) under it. The pelvis drops (smoothed) so the
// lowest foot reaches, though no further than the highest foot can fold
// to; the legs then bend (hip, then knee, in the leg's own bend plane), and
// planted feet tilt about their contact toward the ground's normal. wt
// fades the whole thing (air, water, death).
// crouch (m): the body sits this much lower than the animation has it, on legs that fold to keep the feet where they are.
void dino_foot_ik(const CretSim* sim, const DinoLegs* m, int njoint, float* pelvis, float (*G)[12], float* M, float gs, float wt, float dt, float crouch) {
    static float G0[DINO_MAX_JOINTS][12];
    float hip_w = m->hip_y * gs, dy[4], nrm[4][3], plant[4];
    int con[4];
    float lo = 1e30f, hi = -1e30f;
    for (int l = 0; l < m->nleg; l++) {
        int c = m->leg[l].ankle;   // contact: the foot's lowest joint
        for (int j = 0; j < njoint; j++) if ((m->leg_bits[j] >> (8 + l) & 1) && G[j][7] < G[c][7]) c = j;
        const float* p = &G[c][0]; con[l] = c;
        float F[3]; for (int r = 0; r < 3; r++) F[r] = M[r * 4] * p[3] + M[r * 4 + 1] * p[7] + M[r * 4 + 2] * p[11] + M[r * 4 + 3];
        float plane_y = M[4] * p[3] + M[6] * p[11] + M[7];   // the animation's ground plane under the foot
        float g = cret_ground_at(sim, F[0], F[2], F[1] + 0.5f * hip_w, nrm[l]);
        dy[l] = g - plane_y;
        plant[l] = 1 - sstep(0.06f, 0.25f, p[7] / fmaxf(m->hip_y, 0.1f));
        lo = fminf(lo, dy[l]); hi = fmaxf(hi, dy[l]);
    }
    float fold = 0.55f * hip_w;
    float pel = clampf(fmaxf(lo, hi - fold), -0.45f * hip_w, 0.25f * hip_w) * wt;
    *pelvis = approach(*pelvis, pel, 12.0f, dt);
    M[7] += *pelvis - crouch * wt;
    if (wt < 0.01f) return;
    memcpy(G0, G, sizeof(float) * 12 * njoint);
    float s2 = gs * gs;
    for (int l = 0; l < m->nleg; l++) {
        int h = m->leg[l].hip, k = m->leg[l].knee, an = m->leg[l].ankle;
        float rem = clampf(wt * dy[l] - *pelvis + crouch * wt, -0.3f * hip_w, 0.75f * hip_w);
        float md[3] = {M[4] * rem / s2, M[5] * rem / s2, M[6] * rem / s2};   // world (0, rem, 0) in model space
        // The foot moved by md, tilted about its contact toward the ground's normal (planted feet only).
        float C[3] = {G0[con[l]][3] + md[0], G0[con[l]][7] + md[1], G0[con[l]][11] + md[2]}, R[9];
        float nm[3] = {M[0] * nrm[l][0] + M[4] * nrm[l][1] + M[8] * nrm[l][2], M[1] * nrm[l][0] + M[5] * nrm[l][1] + M[9] * nrm[l][2],
                       M[2] * nrm[l][0] + M[6] * nrm[l][1] + M[10] * nrm[l][2]}, up[3] = {0, 1, 0};
        v3unit(nm); rot_toward(up, nm, 0.4f * plant[l] * wt, R);
        float P[3] = {G[an][3], G[an][7], G[an][11]}, Q[3] = {P[0] + md[0] - C[0], P[1] + md[1] - C[1], P[2] + md[2] - C[2]}, W[3];
        for (int r = 0; r < 3; r++) W[r] = C[r] + R[r * 3] * Q[0] + R[r * 3 + 1] * Q[1] + R[r * 3 + 2] * Q[2];   // where the ankle wants to be
        float H[3] = {G[h][3], G[h][7], G[h][11]}, K[3] = {G[k][3], G[k][7], G[k][11]};
        float hk[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]}, kp[3] = {P[0] - K[0], P[1] - K[1], P[2] - K[2]};
        float a = sqrtf(v3dot(hk, hk)), b = sqrtf(v3dot(kp, kp));
        float u[3] = {W[0] - H[0], W[1] - H[1], W[2] - H[2]}, dist = v3unit(u);
        if (a < 1e-4f || b < 1e-4f || dist < 1e-4f) continue;
        dist = clampf(dist, fabsf(a - b) + 1e-3f, (a + b) * 0.999f);
        float T[3] = {H[0] + u[0] * dist, H[1] + u[1] * dist, H[2] + u[2] * dist};
        // Knee: same side of the hip-ankle line as the animation had it.
        float v[3] = {hk[0] - v3dot(hk, u) * u[0], hk[1] - v3dot(hk, u) * u[1], hk[2] - v3dot(hk, u) * u[2]};
        if (v3unit(v) < 1e-5f) { float zx[3] = {0, 0, 1}; v3cross(u, zx, v); v3unit(v); }
        float ca = clampf((a * a + dist * dist - b * b) / (2 * a * dist), -1, 1), sa = sqrtf(1 - ca * ca);
        float K2[3] = {H[0] + a * (ca * u[0] + sa * v[0]), H[1] + a * (ca * u[1] + sa * v[1]), H[2] + a * (ca * u[2] + sa * v[2])};
        float d0[3] = {hk[0] / a, hk[1] / a, hk[2] / a}, d1[3] = {K2[0] - H[0], K2[1] - H[1], K2[2] - H[2]}, Rl[9];
        v3unit(d1); rot_toward(d0, d1, 3.2f, Rl);
        for (int j = 0; j < njoint; j++) if (m->leg_bits[j] >> l & 1) turn_frame(G[j], Rl, H);
        float e0[3] = {G[an][3] - K2[0], G[an][7] - K2[1], G[an][11] - K2[2]}, e1[3] = {T[0] - K2[0], T[1] - K2[1], T[2] - K2[2]};
        v3unit(e0); v3unit(e1); rot_toward(e0, e1, 3.2f, Rl);
        for (int j = 0; j < njoint; j++) if (m->leg_bits[j] >> (4 + l) & 1) turn_frame(G[j], Rl, K2);
        // The foot: as animated, moved and tilted, then shifted with the ankle if the leg fell short.
        float sh[3] = {T[0] - W[0], T[1] - W[1], T[2] - W[2]};
        for (int j = 0; j < njoint; j++) if (m->leg_bits[j] >> (8 + l) & 1) {
            memcpy(G[j], G0[j], sizeof G0[j]); G[j][3] += md[0]; G[j][7] += md[1]; G[j][11] += md[2];
            turn_frame(G[j], R, C);
            G[j][3] += sh[0]; G[j][7] += sh[1]; G[j][11] += sh[2];
        }
    }
}
