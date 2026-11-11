// Procedural small-animal meshes (included by render/critters.c; no GL here,
// so tests can build them too). Each kind is modelled once, in code, at body
// length 1 (snout to rump; a fish snout to tail fork, a bird bill to tail
// tip), standing on y = 0, facing +x, with +z on its right side. Nothing is
// downloaded, so licensing stays clean and the web build stays small.
//
// Shapes are built from four primitives:
//   - a loft: elliptical cross-sections along the spine (body, head, snout and
//     tail in one smooth tube, so the silhouette has no seams);
//   - a tube along a polyline (legs: hip, knee, ankle, toes);
//   - an ellipsoid (eyes, ears, a bird's thigh);
//   - a panel: a thin outline with a little thickness (wings, a bird's tail,
//     fish fins).
// Every vertex carries what the vertex shader needs to animate it without
// bones (shaders/critter.vs): the spine coordinate it bends with, its part
// (trunk, one of four legs, wing, eye, ear, beak, fin, tail fan), how far
// along its limb it lies, and the joint it turns about.
//
// Three LODs per kind: LOD0 a few hundred triangles (seen from a hatchling's
// eye, 1-8 m), LOD1 about a third of that, LOD2 a few dozen (a body, a tail,
// at most a pair of wings: at 20-70 m a mouse is a handful of pixels).
#ifndef CRET_CRITTER_MESH_H
#define CRET_CRITTER_MESH_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Parts (a_seg.y in the shader). Legs: front left, front right, hind left, hind right.
enum { CRM_TRUNK, CRM_LEG_FL, CRM_LEG_FR, CRM_LEG_HL, CRM_LEG_HR, CRM_WING, CRM_EYE, CRM_EAR, CRM_BEAK, CRM_FIN, CRM_TAILFAN, CRM_PARTS };
#define CRM_LODS 3
#define CRM_KINDS 5   // CRET_CR_KINDS: mouse, large mammal, lizard, bird, fish

typedef struct {
    float pos[3], nrm[3];
    float seg[4];   // spine x it bends with, part, limb parameter (0 at the joint, 1 at the tip), panel span (0 root, 1 tip)
    float piv[3];   // joint the part turns about (hip, shoulder, fin root)
} CrmVertex;
typedef struct { CrmVertex* v; uint16_t* idx; int nv, ni, cap_v, cap_i; } CrmMesh;

// Skeleton landmarks the vertex shader needs, per kind (body length 1):
// neck joint (x, y), tail root (x, y), body centre height, hip height
// (for crouching), and the gait's stride in body lengths.
typedef struct { float neck_x, neck_y, tail_x, tail_y, centre_y, hip_y, stride; } CrmRig;
static const CrmRig CRM_RIG[CRM_KINDS] = {
    {0.17f, 0.19f, -0.47f, 0.15f, 0.17f, 0.15f, 0.55f},    // mouse (multituberculate)
    {0.25f, 0.20f, -0.50f, 0.19f, 0.19f, 0.15f, 0.70f},    // Repenomamus
    {0.20f, 0.075f, -0.48f, 0.06f, 0.07f, 0.06f, 0.45f},   // lizard
    {0.17f, 0.37f, -0.26f, 0.28f, 0.30f, 0.20f, 0.35f},    // bird
    {0.26f, 0.0f, -0.20f, 0.0f, 0.0f, 0.0f, 0.80f},        // fish
};

static void crm_reserve(CrmMesh* m, int nv, int ni) {
    if (m->nv + nv > m->cap_v) { m->cap_v = (m->nv + nv) * 2; m->v = realloc(m->v, sizeof(CrmVertex) * m->cap_v); }
    if (m->ni + ni > m->cap_i) { m->cap_i = (m->ni + ni) * 2; m->idx = realloc(m->idx, sizeof(uint16_t) * m->cap_i); }
}
static int crm_vert(CrmMesh* m, float x, float y, float z, float sx, int part, float t, float span, const float piv[3]) {
    crm_reserve(m, 1, 0);
    CrmVertex* v = &m->v[m->nv];
    *v = (CrmVertex){{x, y, z}, {0, 0, 0}, {sx, (float)part, t, span}, {piv ? piv[0] : 0, piv ? piv[1] : 0, piv ? piv[2] : 0}};
    return m->nv++;
}
static void crm_tri(CrmMesh* m, int a, int b, int c) {
    crm_reserve(m, 0, 3);
    m->idx[m->ni++] = (uint16_t)a; m->idx[m->ni++] = (uint16_t)b; m->idx[m->ni++] = (uint16_t)c;
}
static void crm_quad(CrmMesh* m, int a, int b, int c, int d) { crm_tri(m, a, b, c); crm_tri(m, a, c, d); }

// ---------------------------------------------------------------- loft
// A station of the loft: centre (x, y), half width and half height, and the
// lowest LOD it is dropped from (3: kept at every LOD). The first and last
// stations are the tips (one vertex each).
typedef struct { float x, y, hw, hh; int lods; } CrmStation;

// Rings run from the top of the section round the right side (+z), so with
// stations ordered tail to snout the triangles face outward.
static void crm_loft(CrmMesh* m, const CrmStation* st, int n, int sides, int lod, int part) {
    int keep[64] = {0}, nk = 0;
    for (int i = 0; i < n && nk < 64; i++) if (i == 0 || i == n - 1 || st[i].lods > lod) keep[nk++] = i;
    int first = m->nv;
    const CrmStation* a = &st[keep[0]];
    int tip0 = crm_vert(m, a->x, a->y, 0, a->x, part, 0, 0, NULL);
    for (int r = 1; r < nk - 1; r++) {
        const CrmStation* s = &st[keep[r]];
        for (int k = 0; k < sides; k++) {
            float ang = 6.2831853f * k / sides;
            crm_vert(m, s->x, s->y + s->hh * cosf(ang), s->hw * sinf(ang), s->x, part, 0, 0, NULL);
        }
    }
    const CrmStation* b = &st[keep[nk - 1]];
    int tip1 = crm_vert(m, b->x, b->y, 0, b->x, part, 0, 0, NULL);
    int rings = nk - 2;
    for (int k = 0; k < sides; k++) {
        int k1 = (k + 1) % sides, r0 = first + 1;
        crm_tri(m, tip0, r0 + k1, r0 + k);
        for (int r = 0; r + 1 < rings; r++) {
            int a0 = first + 1 + r * sides, b0 = a0 + sides;
            crm_quad(m, a0 + k, a0 + k1, b0 + k1, b0 + k);
        }
        int rl = first + 1 + (rings - 1) * sides;
        crm_tri(m, tip1, rl + k, rl + k1);
    }
}

// ---------------------------------------------------------------- tube
// A tapered tube through n joints (a leg), rounded at both ends by a tip
// vertex a little past them. The limb parameter runs 0 at the first joint to
// 1 at the last; the part turns about the first joint.
static void crm_tube(CrmMesh* m, const float (*p)[3], const float* r, int n, int sides, int part, float spine_x) {
    float len[16] = {0}, total = 0;
    for (int i = 1; i < n; i++) {
        float dx = p[i][0] - p[i - 1][0], dy = p[i][1] - p[i - 1][1], dz = p[i][2] - p[i - 1][2];
        total += sqrtf(dx * dx + dy * dy + dz * dz); len[i] = total;
    }
    int first = m->nv;
    for (int i = 0; i < n; i++) {
        // Direction: the mean of the segments meeting here.
        float d[3] = {0, 0, 0};
        if (i > 0) for (int q = 0; q < 3; q++) d[q] += p[i][q] - p[i - 1][q];
        if (i + 1 < n) for (int q = 0; q < 3; q++) d[q] += p[i + 1][q] - p[i][q];
        float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); for (int q = 0; q < 3; q++) d[q] /= fmaxf(dl, 1e-6f);
        // Ring basis: u across the limb (sideways when it hangs down), w = d x u.
        float ref[3] = {0, 0, 1}; if (fabsf(d[2]) > 0.8f) { ref[0] = 1; ref[2] = 0; }
        float u[3] = {d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0]};
        float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); for (int q = 0; q < 3; q++) u[q] /= fmaxf(ul, 1e-6f);
        float w[3] = {d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0]};
        for (int k = 0; k < sides; k++) {
            float ang = 6.2831853f * k / sides, c = cosf(ang) * r[i], s = sinf(ang) * r[i];
            crm_vert(m, p[i][0] + u[0] * c + w[0] * s, p[i][1] + u[1] * c + w[1] * s, p[i][2] + u[2] * c + w[2] * s,
                     spine_x, part, len[i] / fmaxf(total, 1e-6f), 0, p[0]);
        }
    }
    // Tips: half a radius beyond each end, along the end segment.
    float e0[3], e1[3];
    for (int q = 0; q < 3; q++) {
        float d0 = p[0][q] - p[1][q], d1 = p[n - 1][q] - p[n - 2][q];
        e0[q] = d0; e1[q] = d1;
    }
    float l0 = sqrtf(e0[0] * e0[0] + e0[1] * e0[1] + e0[2] * e0[2]), l1 = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    int t0 = crm_vert(m, p[0][0] + e0[0] / l0 * r[0] * 0.5f, p[0][1] + e0[1] / l0 * r[0] * 0.5f, p[0][2] + e0[2] / l0 * r[0] * 0.5f, spine_x, part, 0, 0, p[0]);
    int t1 = crm_vert(m, p[n - 1][0] + e1[0] / l1 * r[n - 1] * 0.5f, p[n - 1][1] + e1[1] / l1 * r[n - 1] * 0.5f, p[n - 1][2] + e1[2] / l1 * r[n - 1] * 0.5f,
                      spine_x, part, 1, 0, p[0]);
    for (int k = 0; k < sides; k++) {
        int k1 = (k + 1) % sides;
        crm_tri(m, t0, first + k1, first + k);
        for (int i = 0; i + 1 < n; i++) {
            int a0 = first + i * sides, b0 = a0 + sides;
            crm_quad(m, a0 + k, a0 + k1, b0 + k1, b0 + k);
        }
        int l = first + (n - 1) * sides;
        crm_tri(m, t1, l + k, l + k1);
    }
}

// ---------------------------------------------------------------- ellipsoid
// Centre c, semi-axes r, tilted by `pitch` about z then `yaw` about y (ears lean back and out).
static void crm_ellipsoid(CrmMesh* m, const float c[3], const float r[3], float pitch, float yaw, int lat, int lon, int part, float spine_x, const float piv[3]) {
    int first = m->nv;
    float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
    for (int i = 0; i <= lat; i++) {
        float th = 3.14159265f * i / lat, y = cosf(th), rr = sinf(th);
        int ring = (i == 0 || i == lat) ? 1 : lon;
        for (int k = 0; k < ring; k++) {
            float ph = 6.2831853f * k / lon;
            float lx = rr * cosf(ph) * r[0], ly = y * r[1], lz = rr * sinf(ph) * r[2];
            float x1 = lx * cp - ly * sp, y1 = lx * sp + ly * cp;   // pitch about z
            float x2 = x1 * cy + lz * sy, z2 = -x1 * sy + lz * cy;  // yaw about y
            crm_vert(m, c[0] + x2, c[1] + y1, c[2] + z2, spine_x, part, 0, 0, piv);
        }
    }
    // Rings: 0 is the top pole, 1..lat-1 have lon vertices, lat is the bottom pole.
    int top = first, bot = first + 1 + (lat - 1) * lon;
    for (int k = 0; k < lon; k++) {
        int k1 = (k + 1) % lon;
        crm_tri(m, top, first + 1 + k1, first + 1 + k);
        for (int i = 1; i + 1 < lat; i++) {
            int a0 = first + 1 + (i - 1) * lon, b0 = a0 + lon;
            crm_quad(m, a0 + k, a0 + k1, b0 + k1, b0 + k);
        }
        int l = first + 1 + (lat - 2) * lon;
        crm_tri(m, bot, l + k, l + k1);
    }
}

// ---------------------------------------------------------------- panel
// Spine coordinate for parts that bend along their own length (a fish's
// tail fin follows the body wave instead of moving as one piece).
#define CRM_OWN_X (-10.0f)
// A thin plate: an outline (n points, star-shaped about its centroid, either
// winding) in the plane origin + u * a + v * b, given `thick` at the centroid
// and thinning to a third of that at the rim, plus an edge strip. Panel span
// (seg.w) is the outline's u over `span`, 0 at the root.
static void crm_panel(CrmMesh* m, const float o[3], const float a[3], const float b[3], const float (*uv)[2], int n, float thick, int part,
                      float spine_x, const float piv[3], float span) {
    float nrm[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    float nl = sqrtf(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]); for (int q = 0; q < 3; q++) nrm[q] /= fmaxf(nl, 1e-6f);
    float cu = 0, cv = 0, rmax = 0; for (int i = 0; i < n; i++) { cu += uv[i][0] / n; cv += uv[i][1] / n; }
    float area = 0;
    for (int i = 0; i < n; i++) { rmax = fmaxf(rmax, hypotf(uv[i][0] - cu, uv[i][1] - cv)); area += uv[i][0] * uv[(i + 1) % n][1] - uv[(i + 1) % n][0] * uv[i][1]; }
    bool flip = area < 0;   // clockwise outline: reverse every triangle so the faces still point away from the plate
    int base[2];
    for (int side = 0; side < 2; side++) {
        float sg = side ? -1.0f : 1.0f;
        base[side] = m->nv;
        for (int i = -1; i < n; i++) {   // -1: the centroid
            float pu = i < 0 ? cu : uv[i][0], pv = i < 0 ? cv : uv[i][1];
            float t = thick * 0.5f * (1.0f - 0.67f * hypotf(pu - cu, pv - cv) / fmaxf(rmax, 1e-6f)) * sg;
            float x = o[0] + a[0] * pu + b[0] * pv + nrm[0] * t, y = o[1] + a[1] * pu + b[1] * pv + nrm[1] * t, z = o[2] + a[2] * pu + b[2] * pv + nrm[2] * t;
            crm_vert(m, x, y, z, spine_x <= CRM_OWN_X ? x : spine_x, part, 0, span > 0 ? fmaxf(pu, 0) / span : 0, piv);
        }
        for (int i = 0; i < n; i++) {
            int c = base[side], p0 = c + 1 + i, p1 = c + 1 + (i + 1) % n;
            if ((side == 0) != flip) crm_tri(m, c, p0, p1); else crm_tri(m, c, p1, p0);
        }
    }
    // Edge strip: its own vertices, so the rim shades as an edge.
    int e = m->nv;
    for (int i = 0; i < n; i++) for (int side = 0; side < 2; side++) {
        const CrmVertex* s = &m->v[base[side] + 1 + i];
        crm_vert(m, s->pos[0], s->pos[1], s->pos[2], s->seg[0], part, 0, s->seg[3], piv);
    }
    for (int i = 0; i < n; i++) {
        int i1 = (i + 1) % n;
        if (flip) crm_quad(m, e + i * 2, e + i1 * 2, e + i1 * 2 + 1, e + i * 2 + 1);
        else crm_quad(m, e + i * 2, e + i * 2 + 1, e + i1 * 2 + 1, e + i1 * 2);
    }
}

// Smooth normals from the faces around each vertex (parts and panel faces
// have their own vertices, so their edges stay sharp).
static void crm_normals(CrmMesh* m) {
    for (int i = 0; i < m->nv; i++) m->v[i].nrm[0] = m->v[i].nrm[1] = m->v[i].nrm[2] = 0;
    for (int t = 0; t < m->ni; t += 3) {
        CrmVertex *a = &m->v[m->idx[t]], *b = &m->v[m->idx[t + 1]], *c = &m->v[m->idx[t + 2]];
        float e1[3], e2[3], n[3];
        for (int q = 0; q < 3; q++) { e1[q] = b->pos[q] - a->pos[q]; e2[q] = c->pos[q] - a->pos[q]; }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1]; n[1] = e1[2] * e2[0] - e1[0] * e2[2]; n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for (int q = 0; q < 3; q++) { a->nrm[q] += n[q]; b->nrm[q] += n[q]; c->nrm[q] += n[q]; }
    }
    for (int i = 0; i < m->nv; i++) {
        float* n = m->v[i].nrm; float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-12f) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0; n[1] = 1; n[2] = 0; }
    }
}

// A leg pair at the lowest LOD: one upright card from the hip to the ground,
// `w` wide (both legs overlap seen from the side, the usual far view); it
// swings with the pair's left leg.
static void crm_leg_card(CrmMesh* m, float x, float hip_y, float w, int part) {
    float piv[3] = {x, hip_y, 0};
    int a = crm_vert(m, x - w * 0.5f, hip_y, 0, x, part, 0, 0, piv), b = crm_vert(m, x + w * 0.5f, hip_y, 0, x, part, 0, 0, piv);
    int c = crm_vert(m, x + w * 0.4f, 0, 0, x, part, 1, 0, piv), d = crm_vert(m, x - w * 0.4f, 0, 0, x, part, 1, 0, piv);
    crm_quad(m, a, d, c, b);
}

// ---------------------------------------------------------------- kinds
// A pair of legs, mirrored: joints given for the right side (+z).
static void crm_legs(CrmMesh* m, const float (*p)[3], const float* r, int n, int sides, int part_left) {
    float q[8][3];
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < n; i++) { q[i][0] = p[i][0]; q[i][1] = p[i][1]; q[i][2] = side ? p[i][2] : -p[i][2]; }
        crm_tube(m, (const float (*)[3])q, r, n, sides, part_left + side, p[0][0]);
    }
}
static void crm_eyes(CrmMesh* m, float x, float y, float z, float r, int lod) {
    if (lod >= 2) return;
    int lat = lod == 0 ? 4 : 3, lon = lod == 0 ? 6 : 4;
    for (int side = -1; side <= 1; side += 2) {
        float c[3] = {x, y, z * side}, rr[3] = {r, r, r * 0.8f};
        crm_ellipsoid(m, c, rr, 0, 0, lat, lon, CRM_EYE, x, NULL);
    }
}
// Ears: flattened ellipsoids leaning back, or (LOD1) flat tabs.
static void crm_ears(CrmMesh* m, float x, float y, float z, float w, float h, int lod) {
    if (lod >= 2) return;
    for (int side = -1; side <= 1; side += 2) {
        float c[3] = {x, y, z * side}, r[3] = {w, h, w * 0.35f};
        crm_ellipsoid(m, c, r, 0.35f, -0.5f * side, lod == 0 ? 4 : 2, lod == 0 ? 6 : 4, CRM_EAR, x, NULL);
    }
}

static void crm_mouse(CrmMesh* m, int lod) {
    // Multituberculate: a rodent-like body, round and low, a pointed snout,
    // small rounded ears and a long thin tail.
    static const CrmStation S[] = {
        {-1.30f, 0.025f, 0.004f, 0.004f, 3}, {-1.10f, 0.03f, 0.009f, 0.009f, 1}, {-0.90f, 0.045f, 0.012f, 0.012f, 2}, {-0.70f, 0.075f, 0.016f, 0.016f, 1},
        {-0.56f, 0.12f, 0.024f, 0.024f, 3}, {-0.49f, 0.15f, 0.07f, 0.065f, 3}, {-0.43f, 0.17f, 0.125f, 0.105f, 3}, {-0.32f, 0.185f, 0.158f, 0.128f, 1},
        {-0.15f, 0.185f, 0.165f, 0.125f, 3}, {0.02f, 0.18f, 0.145f, 0.11f, 1}, {0.13f, 0.18f, 0.105f, 0.09f, 3}, {0.21f, 0.19f, 0.088f, 0.082f, 1},
        {0.29f, 0.19f, 0.08f, 0.072f, 3}, {0.37f, 0.178f, 0.058f, 0.052f, 1}, {0.44f, 0.165f, 0.032f, 0.03f, 2}, {0.50f, 0.155f, 0.008f, 0.008f, 3},
    };
    crm_loft(m, S, sizeof S / sizeof *S, lod == 0 ? 10 : lod == 1 ? 6 : 4, lod, CRM_TRUNK);
    if (lod < 2) {
        static const float F[4][3] = {{0.08f, 0.15f, 0.085f}, {0.06f, 0.075f, 0.10f}, {0.09f, 0.012f, 0.10f}, {0.15f, 0.008f, 0.105f}};
        static const float FR[4] = {0.04f, 0.022f, 0.016f, 0.012f};
        static const float H[4][3] = {{-0.30f, 0.15f, 0.10f}, {-0.20f, 0.08f, 0.12f}, {-0.30f, 0.025f, 0.12f}, {-0.19f, 0.01f, 0.12f}};
        static const float HR[4] = {0.06f, 0.035f, 0.018f, 0.013f};
        int sd = lod == 0 ? 5 : 3;
        crm_legs(m, F, FR, 4, sd, CRM_LEG_FL); crm_legs(m, H, HR, 4, sd, CRM_LEG_HL);
    } else { crm_leg_card(m, 0.09f, 0.12f, 0.06f, CRM_LEG_FL); crm_leg_card(m, -0.27f, 0.12f, 0.09f, CRM_LEG_HL); }
    crm_ears(m, 0.235f, 0.255f, 0.055f, 0.035f, 0.04f, lod);
    crm_eyes(m, 0.335f, 0.205f, 0.052f, 0.017f, lod);
}

static void crm_badger(CrmMesh* m, int lod) {
    // Repenomamus: long, low and heavy, a big broad head on a thick neck,
    // short sprawling legs and a thick tail of about a third of the body.
    static const CrmStation S[] = {
        {-0.88f, 0.12f, 0.01f, 0.01f, 3}, {-0.80f, 0.13f, 0.03f, 0.03f, 1}, {-0.68f, 0.155f, 0.045f, 0.045f, 2}, {-0.56f, 0.18f, 0.065f, 0.065f, 1},
        {-0.49f, 0.19f, 0.10f, 0.09f, 3}, {-0.40f, 0.20f, 0.14f, 0.11f, 1}, {-0.22f, 0.20f, 0.16f, 0.115f, 3}, {-0.02f, 0.195f, 0.155f, 0.11f, 1},
        {0.14f, 0.20f, 0.135f, 0.105f, 3}, {0.24f, 0.205f, 0.115f, 0.095f, 1}, {0.32f, 0.205f, 0.12f, 0.095f, 3}, {0.40f, 0.19f, 0.098f, 0.078f, 1},
        {0.46f, 0.175f, 0.068f, 0.056f, 2}, {0.51f, 0.165f, 0.034f, 0.03f, 1}, {0.53f, 0.16f, 0.008f, 0.008f, 3},
    };
    crm_loft(m, S, sizeof S / sizeof *S, lod == 0 ? 10 : lod == 1 ? 6 : 4, lod, CRM_TRUNK);
    if (lod < 2) {
        static const float F[4][3] = {{0.13f, 0.15f, 0.10f}, {0.11f, 0.075f, 0.14f}, {0.14f, 0.015f, 0.15f}, {0.20f, 0.01f, 0.16f}};
        static const float FR[4] = {0.055f, 0.038f, 0.03f, 0.022f};
        static const float H[4][3] = {{-0.34f, 0.16f, 0.11f}, {-0.25f, 0.08f, 0.15f}, {-0.31f, 0.02f, 0.15f}, {-0.24f, 0.012f, 0.16f}};
        static const float HR[4] = {0.07f, 0.045f, 0.03f, 0.022f};
        int sd = lod == 0 ? 6 : 3;
        crm_legs(m, F, FR, 4, sd, CRM_LEG_FL); crm_legs(m, H, HR, 4, sd, CRM_LEG_HL);
    } else { crm_leg_card(m, 0.14f, 0.13f, 0.08f, CRM_LEG_FL); crm_leg_card(m, -0.29f, 0.13f, 0.1f, CRM_LEG_HL); }
    crm_ears(m, 0.29f, 0.285f, 0.075f, 0.02f, 0.021f, lod);
    crm_eyes(m, 0.405f, 0.225f, 0.07f, 0.013f, lod);
}

static void crm_lizard(CrmMesh* m, int lod) {
    // A lizard: flat body slung between sprawling legs, a flat wedge of a
    // head wider than the neck, and a tail half again as long as the body
    // tapering to a point.
    static const CrmStation S[] = {
        {-1.85f, 0.025f, 0.003f, 0.003f, 3}, {-1.55f, 0.03f, 0.009f, 0.008f, 1}, {-1.25f, 0.035f, 0.017f, 0.015f, 2}, {-0.95f, 0.042f, 0.028f, 0.024f, 1},
        {-0.70f, 0.052f, 0.045f, 0.034f, 2}, {-0.52f, 0.06f, 0.068f, 0.042f, 1}, {-0.42f, 0.065f, 0.088f, 0.046f, 3}, {-0.25f, 0.07f, 0.11f, 0.05f, 1},
        {-0.05f, 0.072f, 0.112f, 0.05f, 3}, {0.10f, 0.072f, 0.092f, 0.046f, 1}, {0.19f, 0.075f, 0.066f, 0.04f, 3}, {0.27f, 0.078f, 0.074f, 0.036f, 1},
        {0.36f, 0.074f, 0.064f, 0.031f, 3}, {0.44f, 0.068f, 0.04f, 0.022f, 1}, {0.50f, 0.064f, 0.01f, 0.008f, 3},
    };
    crm_loft(m, S, sizeof S / sizeof *S, lod == 0 ? 8 : lod == 1 ? 5 : 4, lod, CRM_TRUNK);
    if (lod < 2) {
        // Upper limbs go out sideways, lower limbs down, feet turned out.
        static const float F[4][3] = {{0.12f, 0.065f, 0.07f}, {0.13f, 0.07f, 0.17f}, {0.15f, 0.008f, 0.20f}, {0.22f, 0.004f, 0.25f}};
        static const float FR[4] = {0.026f, 0.02f, 0.014f, 0.007f};
        static const float H[4][3] = {{-0.40f, 0.062f, 0.075f}, {-0.36f, 0.068f, 0.21f}, {-0.42f, 0.008f, 0.25f}, {-0.52f, 0.004f, 0.31f}};
        static const float HR[4] = {0.036f, 0.026f, 0.016f, 0.007f};
        int sd = lod == 0 ? 5 : 3;
        crm_legs(m, F, FR, 4, sd, CRM_LEG_FL); crm_legs(m, H, HR, 4, sd, CRM_LEG_HL);
    }
    crm_eyes(m, 0.355f, 0.09f, 0.048f, 0.012f, lod);
}

static void crm_bird(CrmMesh* m, int lod) {
    // A small bird (an early avialan): a round body held tilted up, a round
    // head on a short neck, a short pointed bill, a fanned tail, two thin legs
    // under the body and wings modelled open (span along +-z) that the vertex
    // shader folds against the flanks on the ground.
    static const CrmStation S[] = {
        {-0.29f, 0.265f, 0.01f, 0.01f, 3}, {-0.24f, 0.27f, 0.06f, 0.06f, 1}, {-0.15f, 0.28f, 0.115f, 0.115f, 2}, {-0.04f, 0.295f, 0.135f, 0.14f, 1},
        {0.05f, 0.315f, 0.125f, 0.13f, 3}, {0.13f, 0.35f, 0.088f, 0.095f, 1}, {0.18f, 0.385f, 0.07f, 0.075f, 2}, {0.24f, 0.425f, 0.085f, 0.085f, 1},
        {0.30f, 0.435f, 0.075f, 0.072f, 3}, {0.345f, 0.43f, 0.035f, 0.035f, 1}, {0.36f, 0.428f, 0.008f, 0.008f, 3},
    };
    crm_loft(m, S, sizeof S / sizeof *S, lod == 0 ? 10 : lod == 1 ? 6 : 4, lod, CRM_TRUNK);
    // Bill: a short cone.
    if (lod < 2) {
        static const CrmStation B[] = {{0.33f, 0.425f, 0.002f, 0.002f, 3}, {0.34f, 0.425f, 0.03f, 0.028f, 3}, {0.40f, 0.42f, 0.016f, 0.014f, 1}, {0.46f, 0.414f, 0.002f, 0.002f, 3}};
        crm_loft(m, B, 4, lod == 0 ? 6 : 4, lod, CRM_BEAK);
    }
    // Tail: a flat fan from the rump.
    {
        float o[3] = {-0.23f, 0.285f, 0}, a[3] = {-0.97f, -0.24f, 0}, b[3] = {0, 0, 1};   // a: back and a little down
        static const float T[7][2] = {{0.0f, 0.035f}, {0.22f, 0.055f}, {0.36f, 0.08f}, {0.40f, 0.0f}, {0.36f, -0.08f}, {0.22f, -0.055f}, {0.0f, -0.035f}};
        static const float T2[4][2] = {{0.0f, 0.04f}, {0.38f, 0.075f}, {0.38f, -0.075f}, {0.0f, -0.04f}};
        float piv[3] = {o[0], o[1], 0};
        if (lod < 2) crm_panel(m, o, a, b, T, 7, 0.012f, CRM_TAILFAN, o[0], piv, 0.4f);
        else crm_panel(m, o, a, b, T2, 4, 0.01f, CRM_TAILFAN, o[0], piv, 0.4f);
    }
    // Wings (right side; the left is mirrored): u spans out from the shoulder, v is the chord (+ leading).
    for (int side = -1; side <= 1; side += 2) {
        float o[3] = {0.07f, 0.37f, 0.075f * side}, a[3] = {0, 0, (float)side}, b[3] = {1, 0, 0};
        // Outline in (span, chord): leading edge out to the wrist and the tip, then the primaries and secondaries back to the body.
        static const float W[9][2] = {{0.0f, 0.03f}, {0.22f, 0.05f}, {0.42f, 0.02f}, {0.58f, -0.04f}, {0.56f, -0.10f}, {0.44f, -0.17f}, {0.28f, -0.20f}, {0.12f, -0.19f}, {0.0f, -0.12f}};
        static const float W2[4][2] = {{0.0f, 0.03f}, {0.55f, -0.02f}, {0.45f, -0.16f}, {0.0f, -0.13f}};
        float piv[3] = {o[0], o[1], o[2]};
        // Panel normals follow a x b: flip the left wing's chord so both upper surfaces face up.
        if (side < 0) { b[0] = -1; }
        float Wm[9][2]; const float (*src)[2] = lod < 2 ? W : W2; int n = lod < 2 ? 9 : 4;
        for (int i = 0; i < n; i++) { Wm[i][0] = src[i][0]; Wm[i][1] = side < 0 ? -src[i][1] : src[i][1]; }
        crm_panel(m, o, a, b, (const float (*)[2])Wm, n, lod < 2 ? 0.022f : 0.015f, CRM_WING, o[0], piv, 0.58f);
    }
    if (lod < 2) {
        // Thighs (feathered, mostly inside the body) and bare legs: tarsus and toes.
        for (int side = -1; side <= 1; side += 2) {
            float c[3] = {-0.01f, 0.22f, 0.05f * side}, r[3] = {0.06f, 0.05f, 0.035f};
            crm_ellipsoid(m, c, r, 0, 0, lod == 0 ? 4 : 3, lod == 0 ? 6 : 4, CRM_TRUNK, -0.01f, NULL);
        }
        static const float L[4][3] = {{-0.02f, 0.20f, 0.045f}, {-0.03f, 0.10f, 0.05f}, {0.0f, 0.012f, 0.05f}, {0.09f, 0.006f, 0.055f}};
        static const float LR[4] = {0.016f, 0.011f, 0.009f, 0.006f};
        crm_legs(m, L, LR, 4, lod == 0 ? 4 : 3, CRM_LEG_HL);
    } else crm_leg_card(m, -0.02f, 0.2f, 0.03f, CRM_LEG_HL);
    crm_eyes(m, 0.29f, 0.45f, 0.06f, 0.016f, lod);
}

static void crm_fish(CrmMesh* m, int lod) {
    // A fusiform freshwater fish, deep and narrow, centred on y = 0: a forked
    // tail, dorsal and anal fins, paired pectoral fins.
    static const CrmStation S[] = {
        {-0.38f, 0.0f, 0.004f, 0.01f, 3}, {-0.35f, 0.0f, 0.018f, 0.03f, 1}, {-0.27f, 0.0f, 0.03f, 0.06f, 2}, {-0.14f, 0.0f, 0.05f, 0.10f, 1},
        {0.0f, 0.005f, 0.062f, 0.12f, 3}, {0.14f, 0.003f, 0.06f, 0.11f, 1}, {0.26f, -0.004f, 0.05f, 0.085f, 2}, {0.36f, -0.01f, 0.036f, 0.06f, 1},
        {0.43f, -0.015f, 0.02f, 0.035f, 2}, {0.47f, -0.02f, 0.004f, 0.006f, 3},
    };
    crm_loft(m, S, sizeof S / sizeof *S, lod == 0 ? 10 : lod == 1 ? 6 : 4, lod, CRM_TRUNK);
    float up[3] = {0, 1, 0}, back[3] = {-1, 0, 0}, z0[3] = {0, 0, 0};
    // Tail fin: a vertical forked panel behind the peduncle.
    {
        float o[3] = {-0.36f, 0, 0};
        static const float T[7][2] = {{0.0f, 0.03f}, {0.12f, 0.12f}, {0.15f, 0.11f}, {0.09f, 0.0f}, {0.15f, -0.11f}, {0.12f, -0.12f}, {0.0f, -0.03f}};
        static const float T2[4][2] = {{0.0f, 0.03f}, {0.14f, 0.11f}, {0.14f, -0.11f}, {0.0f, -0.03f}};
        if (lod < 2) crm_panel(m, o, back, up, T, 7, 0.008f, CRM_FIN, CRM_OWN_X, z0, 0.15f);
        else crm_panel(m, o, back, up, T2, 4, 0.008f, CRM_FIN, CRM_OWN_X, z0, 0.15f);
    }
    if (lod >= 2) return;
    // Dorsal and anal fins: vertical panels on the midline.
    {
        float o[3] = {0.02f, 0.11f, 0}, fwd[3] = {1, 0, 0};
        static const float D[4][2] = {{0.0f, 0.0f}, {-0.02f, 0.07f}, {-0.09f, 0.03f}, {-0.13f, 0.0f}};
        crm_panel(m, o, fwd, up, D, 4, 0.006f, CRM_FIN, -0.04f, z0, 0);
        float o2[3] = {-0.17f, -0.07f, 0};
        static const float A[4][2] = {{0.0f, 0.0f}, {-0.07f, 0.0f}, {-0.09f, -0.035f}, {-0.02f, -0.03f}};
        crm_panel(m, o2, fwd, up, A, 4, 0.005f, CRM_FIN, -0.2f, z0, 0);
    }
    // Pectoral fins: behind the gills, angled back and out, fluttering about their root.
    for (int side = -1; side <= 1; side += 2) {
        float o[3] = {0.27f, -0.04f, 0.045f * side}, a[3] = {-0.85f, -0.2f, 0.45f * side}, b[3] = {0, 1, 0};
        if (side < 0) { b[1] = -1; }
        static const float P[4][2] = {{0.0f, 0.015f}, {0.08f, 0.012f}, {0.07f, -0.02f}, {0.0f, -0.015f}};
        float Pm[4][2]; for (int i = 0; i < 4; i++) { Pm[i][0] = P[i][0]; Pm[i][1] = side < 0 ? -P[i][1] : P[i][1]; }
        crm_panel(m, o, a, b, (const float (*)[2])Pm, 4, 0.005f, CRM_FIN, 0.27f, o, 0.08f);
    }
    crm_eyes(m, 0.385f, 0.012f, 0.03f, 0.014f, lod);
}

// Builds kind k (CRET_CR_* order) at one LOD into an empty mesh.
static void crm_build(CrmMesh* m, int kind, int lod) {
    memset(m, 0, sizeof *m);
    switch (kind) {
    case 0: crm_mouse(m, lod); break;
    case 1: crm_badger(m, lod); break;
    case 2: crm_lizard(m, lod); break;
    case 3: crm_bird(m, lod); break;
    default: crm_fish(m, lod); break;
    }
    crm_normals(m);
}
static void crm_free(CrmMesh* m) { free(m->v); free(m->idx); memset(m, 0, sizeof *m); }
#endif
