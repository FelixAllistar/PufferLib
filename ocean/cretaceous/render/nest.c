// Nests, eggs and carried food (render/nest.h). Included by view.c after
// render/critters.c (a caught critter is queued there with a limp pose).
#include "nest.h"
#include "../progress.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline float nclamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float nsmooth(float a, float b, float x) { float t = nclamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline uint32_t nhash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
static inline float nrand(uint32_t* s) { *s = nhash(*s + 0x9e3779b9u); return (*s >> 8) * (1.0f / 16777216.0f); }

// ---------------------------------------------------------------- eggs
// Sizes from fossil clutches where known: dromaeosaur eggs about 16 x 7 cm,
// tyrannosaur-sized theropod eggs about 40 cm long (Macroelongatoolithus),
// ornithomimid eggs small and long; ornithischian eggs are rounder
// (hadrosaur and ankylosaur eggs near spherical). Theropod clutches lie in a
// ring with the narrow ends toward the middle; ornithischian ones in a heap.
typedef struct { float len, wid, taper, col[3]; bool ring; } EggLook;
static const EggLook EGG_LOOK[CRET_SPECIES] = {
    [CRET_SP_RAPTOR]     = {0.17f, 0.075f, 0.20f, {0.28f, 0.31f, 0.28f}, true},    // muted grey-green, like oviraptorosaur shells
    [CRET_SP_STEGO]      = {0.22f, 0.18f,  0.08f, {0.40f, 0.37f, 0.32f}, false},
    [CRET_SP_TREX]       = {0.42f, 0.17f,  0.18f, {0.40f, 0.35f, 0.28f}, true},
    [CRET_SP_PACHY]      = {0.15f, 0.12f,  0.08f, {0.43f, 0.40f, 0.34f}, false},
    [CRET_SP_SAUROPELTA] = {0.20f, 0.17f,  0.06f, {0.37f, 0.34f, 0.29f}, false},
    [CRET_SP_GALLI]      = {0.15f, 0.065f, 0.16f, {0.44f, 0.41f, 0.35f}, true},
};
void cret_egg_size(int species, float* length, float* width) {
    const EggLook* E = &EGG_LOOK[species < CRET_SPECIES ? species : 0]; *length = E->len; *width = E->wid;
}

// A vertex: position, (material, AO) and normal: raylib's attribute layout (gfx.c: 0 pos, 1 uv, 2 normal).
typedef struct { float* v; int n, cap; } NBuf;
static void nb_put(NBuf* b, const float p[3], float kind, float ao, const float nrm[3]) {
    if (b->n + 1 > b->cap) { b->cap = b->cap ? b->cap * 2 : 4096; b->v = realloc(b->v, sizeof(float) * 8 * b->cap); }
    float* o = b->v + 8 * b->n++;
    o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = kind; o[4] = ao; o[5] = nrm[0]; o[6] = nrm[1]; o[7] = nrm[2];
}
static void nb_upload(NBuf* b, GLuint* vao, GLuint* vbo) {
    if (!*vao) { glGenVertexArrays(1, vao); glGenBuffers(1, vbo); }
    glBindVertexArray(*vao); glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8 * (size_t)b->n, b->v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 32, (void*)20);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
}
static void norm3(float v[3]) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; } }

// An egg along +x (the narrow end at +x), at its real size: a surface of
// revolution with the radius tapering toward one end. Normals are exact, so
// the egg is drawn with a rotation only.
static void build_egg(CretNestDraw* d, int sp) {
    const EggLook* E = &EGG_LOOK[sp]; enum { NU = 16, NA = 20 };
    float a = E->len * 0.5f, b = E->wid * 0.5f, k = E->taper;
    float P[NU + 1][NA + 1][3], N[NU + 1][NA + 1][3];
    for (int i = 0; i <= NU; i++) {
        float u = 3.14159265f * i / NU, X = a * cosf(u), R = b * sinf(u) * (1 - k * cosf(u));
        float dX = -a * sinf(u), dR = b * (cosf(u) * (1 - k * cosf(u)) + k * sinf(u) * sinf(u));   // d/du
        float na = dR, nr = -dX, nl = sqrtf(na * na + nr * nr); if (nl < 1e-9f) { na = i == 0 ? 1.0f : -1.0f; nr = 0; nl = 1; }
        for (int j = 0; j <= NA; j++) {
            float t = 6.2831853f * j / NA, c = cosf(t), s = sinf(t);
            P[i][j][0] = X; P[i][j][1] = R * c; P[i][j][2] = R * s;
            N[i][j][0] = na / nl; N[i][j][1] = nr / nl * c; N[i][j][2] = nr / nl * s;
        }
    }
    NBuf B = {0};
    for (int i = 0; i < NU; i++) for (int j = 0; j < NA; j++) {
        int q[6][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j}, {i + 1, j + 1}, {i, j + 1}};
        for (int v = 0; v < 6; v++) { const float* nn = N[q[v][0]][q[v][1]]; nb_put(&B, P[q[v][0]][q[v][1]], 6, 0.55f + 0.45f * (nn[1] * 0.5f + 0.5f), nn); }   // underside in its own shade
    }
    nb_upload(&B, &d->egg_vao[sp], &d->egg_vbo[sp]); d->egg_n[sp] = B.n; free(B.v);
}

// ---------------------------------------------------------------- the nest's shape
// Radius R (progress.c cret_nest_radius); `built` 0..1 raises everything from
// the ground. In units of R (wobbled around the rim by a few low harmonics):
// a shallow bowl floor at the middle, the crest of a low rim at 0.7 (a sixth
// of R high: 37 cm for a tyrannosaur, 17 cm for a raptor), the outer slope
// down to the ground at 1.15, a scatter of loose soil to 1.25 and the last
// ring tucked under the terrain at 1.32 so the edge never shows a seam.
typedef struct { const CretWorld* w; float x0, y0, z0, R, built; float ph[6]; uint32_t seed; } Shape;
static float shape_wob(const Shape* S, float a) { return 1 + 0.07f * sinf(2 * a + S->ph[0]) + 0.045f * sinf(3 * a + S->ph[1]) + 0.025f * sinf(5 * a + S->ph[2]); }
static float shape_u(const Shape* S, float dx, float dz) { return sqrtf(dx * dx + dz * dz) / (S->R * shape_wob(S, atan2f(dz, dx))); }
static float mound_h(const Shape* S, float dx, float dz) {
    float a = atan2f(dz, dx), u = shape_u(S, dx, dz), R = S->R, t = S->built;
    float H = 0.17f * R * t, floor_h = 0.035f * R * t, crest = 0.7f;
    float h = u < crest ? floor_h + (H - floor_h) * powf(nsmooth(0.15f, crest, u), 1.7f) : H * powf(1 - nsmooth(crest, 1.15f, u), 1.3f);
    h += 0.18f * H * sinf(7 * a + S->ph[3]) * sinf(6.5f * u + S->ph[4]) * nsmooth(0.25f, 0.8f, u) * (1 - nsmooth(0.9f, 1.15f, u));   // lumps
    h += 0.06f * H * sinf(17 * a + 3 * S->ph[3]) * sinf(15 * u + S->ph[5]) * (1 - nsmooth(0.95f, 1.15f, u));                         // clods
    h += 0.015f * (1 - nsmooth(1.05f, 1.25f, u)) - 0.03f * R * nsmooth(1.18f, 1.32f, u);   // just over the ground inside, under it at the edge
    return h;
}
// Height above the nest's origin, and the surface normal there.
static float surf_y(const Shape* S, float dx, float dz) { return cret_height(S->w, S->x0 + dx, S->z0 + dz) - S->y0 + mound_h(S, dx, dz); }
static void surf_n(const Shape* S, float dx, float dz, float n[3]) {
    const float e = 0.04f;
    n[0] = surf_y(S, dx - e, dz) - surf_y(S, dx + e, dz); n[1] = 2 * e; n[2] = surf_y(S, dx, dz - e) - surf_y(S, dx, dz + e); norm3(n);
}
static void shape_init(Shape* S, const CretWorld* w, int species, float x, float z, float built) {
    memset(S, 0, sizeof *S); S->w = w; S->x0 = x; S->z0 = z; S->y0 = cret_height(w, x, z); S->R = cret_nest_radius(species); S->built = built;
    S->seed = nhash((uint32_t)(int32_t)lroundf(x * 8) * 73856093u ^ (uint32_t)(int32_t)lroundf(z * 8) * 19349663u ^ (uint32_t)species);
    uint32_t r = S->seed; for (int k = 0; k < 6; k++) S->ph[k] = 6.2831853f * nrand(&r);
}

// A stick from a to b (nest coordinates), bent to lie on the surface: five-sided, capped.
static void put_stick(NBuf* B, const Shape* S, float ax, float az, float bx, float bz, float rad, float tone, float lift) {
    enum { SEG = 3, SIDES = 5 };
    float P[SEG + 1][3];
    for (int i = 0; i <= SEG; i++) {
        float t = (float)i / SEG, x = ax + (bx - ax) * t, z = az + (bz - az) * t;
        P[i][0] = x; P[i][2] = z; P[i][1] = surf_y(S, x, z) + rad * 0.55f + lift * sinf(3.14159f * t);
    }
    float ring[SEG + 1][SIDES][3], rn[SEG + 1][SIDES][3];
    for (int i = 0; i <= SEG; i++) {
        int i0 = i > 0 ? i - 1 : 0, i1 = i < SEG ? i + 1 : SEG;
        float d[3] = {P[i1][0] - P[i0][0], P[i1][1] - P[i0][1], P[i1][2] - P[i0][2]}; norm3(d);
        float side[3] = {-d[2], 0, d[0]}; norm3(side);
        float up[3] = {side[1] * d[2] - side[2] * d[1], side[2] * d[0] - side[0] * d[2], side[0] * d[1] - side[1] * d[0]};
        float r = rad * (1 - 0.35f * (float)i / SEG);   // thinner toward the tip
        for (int k = 0; k < SIDES; k++) {
            float t = 6.2831853f * k / SIDES, c = cosf(t), s = sinf(t);
            for (int q = 0; q < 3; q++) { rn[i][k][q] = side[q] * c + up[q] * s; ring[i][k][q] = P[i][q] + rn[i][k][q] * r; }
        }
    }
    float kind = 2 + tone;
    for (int i = 0; i < SEG; i++) for (int k = 0; k < SIDES; k++) {
        int k1 = (k + 1) % SIDES;
        nb_put(B, ring[i][k], kind, 0.8f, rn[i][k]); nb_put(B, ring[i + 1][k], kind, 0.8f, rn[i + 1][k]); nb_put(B, ring[i + 1][k1], kind, 0.8f, rn[i + 1][k1]);
        nb_put(B, ring[i][k], kind, 0.8f, rn[i][k]); nb_put(B, ring[i + 1][k1], kind, 0.8f, rn[i + 1][k1]); nb_put(B, ring[i][k1], kind, 0.8f, rn[i][k1]);
    }
    for (int e = 0; e <= SEG; e += SEG) {   // the cut ends: pale wood
        int o = e ? SEG - 1 : 1; float cn[3] = {P[e][0] - P[o][0], P[e][1] - P[o][1], P[e][2] - P[o][2]}; norm3(cn);
        for (int k = 0; k < SIDES; k++) { nb_put(B, P[e], 3.0f, 0.9f, cn); nb_put(B, ring[e][k], 3.0f, 0.9f, cn); nb_put(B, ring[e][(k + 1) % SIDES], 3.0f, 0.9f, cn); }
    }
}

// The whole nest: the mound (a polar grid), then sticks round the rim, as
// many as the build has reached. Leaf and fern litter (shaders/nest.fs) lines
// the bowl and covers the rim; the outer slope turns to bare soil toward the
// ground.
static void build_nest(NestMesh* m, const CretWorld* w, int species, float x, float z, float built) {
    Shape S; shape_init(&S, w, species, x, z, built);
    enum { NR = 18, NS = 48 };
    static float P[NR + 1][NS + 1][3], N[NR + 1][NS + 1][3], K[NR + 1][NS + 1][2];
    NBuf B = {0};
    for (int i = 0; i <= NR; i++) for (int j = 0; j <= NS; j++) {
        float u = 1.32f * powf((float)i / NR, 0.85f), a = 6.2831853f * (j % NS) / NS, r = u * S.R * shape_wob(&S, a);
        float dx = cosf(a) * r, dz = sinf(a) * r;
        P[i][j][0] = dx; P[i][j][2] = dz; P[i][j][1] = surf_y(&S, dx, dz);
        surf_n(&S, dx, dz, N[i][j]);
        float lit = (0.6f + 0.3f * nsmooth(0.3f, 0.6f, u)) * (1 - 0.8f * nsmooth(0.95f, 1.3f, u)) * nclamp(built * 1.5f, 0, 1);
        K[i][j][0] = lit; K[i][j][1] = 0.62f + 0.38f * nsmooth(0.1f, 0.72f, u);   // the bowl sits in its own shade
    }
    for (int i = 0; i < NR; i++) for (int j = 0; j < NS; j++) {
        int q[6][2] = {{i, j}, {i + 1, j + 1}, {i + 1, j}, {i, j}, {i, j + 1}, {i + 1, j + 1}};
        for (int v = 0; v < 6; v++) { int a = q[v][0], b = q[v][1]; nb_put(&B, P[a][b], K[a][b][0], K[a][b][1], N[a][b]); }
    }
    uint32_t r = S.seed;
    // Sticks: mostly along the rim, some across it, a few leaning into the bowl.
    int nst = (int)((14 + 26 * S.R) * nclamp(built * 1.2f, 0, 1));
    for (int k = 0; k < nst; k++) {
        float a = 6.2831853f * nrand(&r), uc = 0.5f + 0.6f * nrand(&r), rr = uc * S.R * shape_wob(&S, a);
        float cx = cosf(a) * rr, cz = sinf(a) * rr, dir = a + 1.5708f + (nrand(&r) - 0.5f) * 1.6f;
        float len = S.R * (0.35f + 0.45f * nrand(&r)), rad = (0.01f + 0.009f * S.R) * (0.6f + 0.8f * nrand(&r));
        put_stick(&B, &S, cx - cosf(dir) * len * 0.5f, cz - sinf(dir) * len * 0.5f, cx + cosf(dir) * len * 0.5f, cz + sinf(dir) * len * 0.5f,
                  rad, nrand(&r) < 0.3f ? 0.6f + 0.4f * nrand(&r) : 0.3f * nrand(&r), rad * 2 * nrand(&r));
    }
    nb_upload(&B, &m->vao, &m->vbo); m->nverts = B.n; free(B.v);
    m->species = species; m->x = x; m->z = z; m->built = built; m->used = true;
}

// Where egg i of a clutch lies (nest coordinates, its centre) and how: a
// theropod ring with the narrow ends inward and down, an ornithischian heap.
static void egg_pose(const Shape* S, int species, int i, float p[3], float* yaw, float* pitch) {
    const EggLook* E = &EGG_LOOK[species]; uint32_t r = S->seed ^ (uint32_t)(i * 7919);
    float a, rr;
    if (E->ring) { a = S->ph[5] + 6.2831853f * i / 5 + 0.25f * (nrand(&r) - 0.5f); rr = fmaxf(0.24f * S->R, E->len * 0.62f); *yaw = a + 3.14159f; *pitch = -0.35f; }
    else if (i == 0) { a = S->ph[5]; rr = 0; *yaw = 6.2831853f * nrand(&r); *pitch = 0.1f; }
    else { a = S->ph[5] + 6.2831853f * (i - 1) / 4 + 0.3f * (nrand(&r) - 0.5f); rr = E->wid * 1.05f; *yaw = 6.2831853f * nrand(&r); *pitch = 0.15f * (nrand(&r) - 0.5f); }
    p[0] = cosf(a) * rr; p[2] = sinf(a) * rr;
    p[1] = surf_y(S, p[0], p[2]) + E->wid * 0.5f * 0.72f;   // a quarter sunk in the lining
}

// ---------------------------------------------------------------- per frame
void cret_nest_draw_init(CretNestDraw* d) {
    memset(d, 0, sizeof *d);
    for (int sp = 0; sp < CRET_SPECIES; sp++) build_egg(d, sp);
    d->last_time = -1; d->ok = true;
}
void cret_nest_draw_close(CretNestDraw* d) {
    for (int k = 0; k < NEST_CACHE; k++) if (d->mesh[k].vao) { glDeleteVertexArrays(1, &d->mesh[k].vao); glDeleteBuffers(1, &d->mesh[k].vbo); }
    for (int sp = 0; sp < CRET_SPECIES; sp++) if (d->egg_vao[sp]) { glDeleteVertexArrays(1, &d->egg_vao[sp]); glDeleteBuffers(1, &d->egg_vbo[sp]); }
    memset(d, 0, sizeof *d);
}

// What to draw: the sim's nests, the shown ones, and nests being built.
typedef struct { int slot, species, eggs; float x, z, built; } NestItem;
static int nest_items(const CretNestDraw* d, const CretSim* sim, NestItem* out) {
    int n = 0;
    for (int k = 0; k < CRET_MAX_NESTS; k++) {
        const CretNest* q = &sim->nests[k]; if (!q->active || q->species >= CRET_SPECIES) continue;
        out[n++] = (NestItem){k, q->species, q->eggs, q->x, q->z, 1};
    }
    for (int k = 0; k < d->nshow; k++) {
        const CretNestShow* q = &d->show[k];
        out[n++] = (NestItem){CRET_MAX_NESTS + k, q->species, q->eggs, q->x, q->z, nclamp(q->built, 0.02f, 1)};
    }
    int nb = 0;
    for (int i = 0; i < sim->cfg.num_agents && nb < NEST_BUILDS; i++) {
        const CretAgent* a = &sim->agents[i]; if (!a->alive || a->act != CRET_ACT_NEST || a->species >= CRET_SPECIES) continue;
        float t = fmaxf(a->nest_ticks / (5.0f * CRET_TICK_HZ), d->build_t[i] / 5.0f); if (t <= 0.01f) continue;
        float L = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth) * 0.45f;   // where progress.c puts it
        out[n++] = (NestItem){CRET_MAX_NESTS + NEST_SHOW_MAX + nb++, a->species, 0, a->x + cosf(a->yaw) * L, a->z + sinf(a->yaw) * L, nclamp(t, 0.02f, 0.97f)};
    }
    return n;
}

void cret_nest_draw_update(CretNestDraw* d, const CretSim* sim, const CretWorld* w, const float cam[3], float time) {
    if (!d->ok || !w) return;
    float dt = d->last_time < 0 ? 0 : nclamp(time - d->last_time, 0, 0.1f); d->last_time = time;
    for (int i = 0; i < sim->cfg.num_agents; i++) d->build_t[i] = sim->agents[i].alive && sim->agents[i].act == CRET_ACT_NEST ? d->build_t[i] + dt : 0;
    NestItem it[NEST_CACHE]; int n = nest_items(d, sim, it);
    bool live[NEST_CACHE] = {0};
    for (int k = 0; k < n; k++) {
        const NestItem* q = &it[k]; NestMesh* m = &d->mesh[q->slot]; live[q->slot] = true;
        if (hypotf(q->x - cam[0], q->z - cam[2]) > NEST_DRAW_RANGE + 20) continue;
        float bq = roundf(q->built * 50) / 50;   // a rising nest is rebuilt in 2% steps
        if (!m->used || m->species != q->species || m->x != q->x || m->z != q->z || m->built != bq) build_nest(m, w, q->species, q->x, q->z, bq);
    }
    for (int k = 0; k < NEST_CACHE; k++) if (!live[k]) d->mesh[k].used = false;
}

// Model matrices: a translation (meshes in nest coordinates), and an egg turned to (yaw, pitch).
static void m_translate(float* f, float x, float y, float z) { memset(f, 0, 64); f[0] = f[5] = f[10] = f[15] = 1; f[12] = x; f[13] = y; f[14] = z; }
static void m_egg(float* f, float x, float y, float z, float yaw, float pitch) {
    Matrix m = MatrixMultiply(MatrixRotateZ(pitch), MatrixMultiply(MatrixRotateY(-yaw), MatrixTranslate(x, y, z)));
    float16 v = MatrixToFloatV(m); memcpy(f, v.v, 64);
}
static void draw_egg_mesh(const CretNestDraw* d, int sp, GLint l_model, const float* mf) {
    glUniformMatrix4fv(l_model, 1, GL_FALSE, mf); glBindVertexArray(d->egg_vao[sp]); glDrawArrays(GL_TRIANGLES, 0, d->egg_n[sp]);
}

// Each egg of every nest in range, and the eggs and critters lying loose or carried. emit(...) draws one.
typedef struct {
    const CretNestDraw* d; const GfxProgram* p; GLint l_model, l_color; bool color;
    unsigned int ball; int ball_tris; bool ball_idx; int n;
} EggPass;
static void egg_color(EggPass* e, int sp, uint32_t seed) {
    if (!e->color || e->l_color < 0) return;   // l_color: the nest program's u_egg
    const float* c = EGG_LOOK[sp].col; uint32_t r = seed; float j = 0.94f + 0.12f * nrand(&r);
    glUniform3f(e->l_color, c[0] * j, c[1] * j, c[2] * (0.97f + 0.06f * nrand(&r)));
}
static void draw_clutches(EggPass* e, const CretSim* sim, const NestItem* it, int n, const float cam[3], float range) {
    const CretWorld* w = sim->world;
    for (int k = 0; k < n; k++) {
        const NestItem* q = &it[k]; if (q->eggs <= 0 || hypotf(q->x - cam[0], q->z - cam[2]) > range) continue;
        Shape S; shape_init(&S, w, q->species, q->x, q->z, 1);
        for (int i = 0; i < q->eggs && i < 5; i++) {
            float p[3], yaw, pitch, mf[16]; egg_pose(&S, q->species, i, p, &yaw, &pitch);
            egg_color(e, q->species, S.seed + (uint32_t)i * 31u);
            m_egg(mf, S.x0 + p[0], S.y0 + p[1], S.z0 + p[2], yaw, pitch); draw_egg_mesh(e->d, q->species, e->l_model, mf); e->n++;
        }
    }
}
// Where a loose item (a carcass slot that is not a body) is drawn: on the
// ground, or in a carrier's jaws (at its drawn head when the model has one).
static bool loose_pose(const CretSim* sim, int c, CretHeadFn head, const void* hctx, float* x, float* y, float* z, float* yaw) {
    const CretCarcass* k = &sim->carcasses[c];
    *x = k->x; *y = k->y; *z = k->z; *yaw = k->yaw; int ca = k->carrier - 1;
    bool held = ca >= 0 && ca < sim->cfg.num_agents && sim->agents[ca].alive;
    if (held) {
        float hp[3]; const CretAgent* a = &sim->agents[ca]; *yaw = a->yaw + 1.5708f;   // crosswise in the jaws
        if (head && head(hctx, ca, hp)) { *x = hp[0]; *y = hp[1] - 0.1f * a->height; *z = hp[2]; } else cret_mouth_point(a, x, y, z);
    }
    return held;
}
// Eggs that are carcass slots, lying loose or carried.
static void draw_loose(EggPass* e, const CretSim* sim, const float cam[3], float range, CretHeadFn head, const void* hctx) {
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &sim->carcasses[c];
        if (!k->active || k->item != CRET_ITEM_EGG || hypotf(k->x - cam[0], k->z - cam[2]) > range) continue;
        float x, y, z, yaw; bool held = loose_pose(sim, c, head, hctx, &x, &y, &z, &yaw);
        int sp = k->species < CRET_SPECIES ? k->species : 0; float mf[16];
        egg_color(e, sp, (uint32_t)c * 977u);
        m_egg(mf, x, held ? y : y + EGG_LOOK[sp].wid * 0.45f, z, yaw, held ? 0.0f : 0.05f); draw_egg_mesh(e->d, sp, e->l_model, mf); e->n++;
    }
}

void cret_nest_queue_critters(const CretSim* sim, const float cam[3], CretHeadFn head, const void* head_ctx) {
    for (int c = 0; c < CRET_MAX_CARCASSES; c++) {
        const CretCarcass* k = &sim->carcasses[c];
        if (!k->active || k->item != CRET_ITEM_CRITTER || k->critter >= CRET_CR_KINDS || hypotf(k->x - cam[0], k->z - cam[2]) > NEST_DRAW_RANGE * 0.4f) continue;
        float x, y, z, yaw; bool held = loose_pose(sim, c, head, head_ctx, &x, &y, &z, &yaw);
        float q[4], g[3], pos[3]; cret_critter_yaw_quat(yaw, q); cret_critter_grip(k->critter, k->max_meat, g);
        pos[0] = x; pos[1] = held ? y - g[1] : y; pos[2] = z;   // held: its middle at the jaw (the grip lies on the up axis)
        CretCritterPose pose = {.death = 1, .seed = (float)((c * 37) % 100) / 100.0f};
        cret_critter_draw_one(k->critter, k->max_meat, pos, q, &pose);
    }
}

void cret_nest_draw(CretNestDraw* d, const CretSim* sim, const GfxProgram* np, const GfxProgram* op, unsigned int ball_vao, int ball_tris, bool ball_indexed,
                    const float cam[3], CretHeadFn head, const void* head_ctx) {
    d->drawn_nests = d->drawn_eggs = 0;
    if (!d->ok || !sim->world) return;
    NestItem it[NEST_CACHE]; int n = nest_items(d, sim, it);
    glUseProgram(np->id); GLint l_model = gfx_loc(np, "u_model");
    for (int k = 0; k < n; k++) {
        const NestMesh* m = &d->mesh[it[k].slot];
        if (!m->used || !m->nverts || hypotf(m->x - cam[0], m->z - cam[2]) > NEST_DRAW_RANGE) continue;
        float mf[16]; m_translate(mf, m->x, cret_height(sim->world, m->x, m->z), m->z);
        glUniformMatrix4fv(l_model, 1, GL_FALSE, mf); glBindVertexArray(m->vao); glDrawArrays(GL_TRIANGLES, 0, m->nverts); d->drawn_nests++;
    }
    // Eggs with the nest program (the same light as the nest they lie in); caught critters are queued
    // with critters.c (cret_nest_queue_critters).
    (void)op;
    EggPass e = {d, np, l_model, gfx_loc(np, "u_egg"), true, ball_vao, ball_tris, ball_indexed, 0};
    draw_clutches(&e, sim, it, n, cam, NEST_DRAW_RANGE * 0.6f);
    draw_loose(&e, sim, cam, NEST_DRAW_RANGE * 0.4f, head, head_ctx);
    glBindVertexArray(0); d->drawn_eggs = e.n;
}

int cret_nest_clearings(const CretNestDraw* d, const CretSim* sim, float* xzr, int max) {
    if (!d->ok) return 0;
    NestItem it[NEST_CACHE]; int n = nest_items(d, sim, it), k = 0;
    for (int q = 0; q < n && k < max; q++, k++) {
        xzr[3 * k] = it[q].x; xzr[3 * k + 1] = it[q].z; xzr[3 * k + 2] = cret_nest_radius(it[q].species) * 1.15f * fminf(it[q].built * 2, 1);
    }
    return k;
}

int cret_nest_draw_shadow(CretNestDraw* d, const CretSim* sim, const GfxProgram* p, const CretCsm* c, int k, const float cam[3], CretHeadFn head, const void* head_ctx) {
    if (!d->ok || !sim->world) return 0;
    NestItem it[NEST_CACHE]; int n = nest_items(d, sim, it), drawn = 0;
    GLint l_model = gfx_loc(p, "u_model");
    for (int q = 0; q < n; q++) {
        const NestMesh* m = &d->mesh[it[q].slot];
        if (!m->used || !m->nverts || hypotf(m->x - cam[0], m->z - cam[2]) > NEST_DRAW_RANGE * 0.6f) continue;
        float y = cret_height(sim->world, m->x, m->z), R = cret_nest_radius(m->species), pc[3] = {m->x, y, m->z};
        if (!cret_csm_caster_visible(c, k, pc, R * 1.4f, R * 0.5f)) continue;
        float mf[16]; m_translate(mf, m->x, y, m->z);
        glUniformMatrix4fv(l_model, 1, GL_FALSE, mf); glBindVertexArray(m->vao); glDrawArrays(GL_TRIANGLES, 0, m->nverts); drawn++;
    }
    EggPass e = {d, p, l_model, -1, false, 0, 0, false, 0};
    draw_clutches(&e, sim, it, n, cam, NEST_DRAW_RANGE * 0.3f);
    glBindVertexArray(0);
    return drawn + e.n;
}
