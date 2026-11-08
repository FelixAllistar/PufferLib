#include "dino.h"
#include "raylib.h"
#include "cache.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADER_DIR "ocean/cretaceous/shaders/"
#define DINO_DIR "ocean/cretaceous/assets/dinos/"
#define POSE_W (DINO_MAX_JOINTS * 3)

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline float approach(float cur, float target, float rate, float dt) { return cur + (target - cur) * (1 - expf(-rate * dt)); }
static inline float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

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

// ---------------------------------------------------------------- loading
typedef struct { float p[3], uv[2]; int8_t n[4], t[4]; uint8_t j[4], w[4]; } DVert;

static bool rd(FILE* f, void* p, size_t n) { return fread(p, 1, n, f) == n; }

// Skin regrade: a gradient map by luminance (dark markings, base, pale belly)
// blended over the source albedo, so the model's own pattern and shading
// stay while the colour moves toward a plausible palette. The raptor's source
// skin is a cool grey-green with a blue racing stripe; modern reconstructions
// suggest a drab brown/olive animal with a paler underside, so the stripe
// becomes a muted rust. The pachycephalosaur's source is salmon pink.
typedef struct { const char* asset; float mix; unsigned char dark[3], base[3], light[3]; bool rust_blue; } SkinGrade;
static const SkinGrade SKIN[] = {
    {"raptor", 0.95f, {30, 24, 14}, {74, 60, 34}, {150, 128, 86}, true},
    {"pachy", 0.72f, {50, 38, 28}, {122, 104, 78}, {190, 174, 142}, false},
};
static void regrade_skin(const char* asset, uint8_t* px, size_t n) {
    const SkinGrade* g = NULL;
    for (size_t i = 0; i < sizeof SKIN / sizeof SKIN[0]; i++) if (!strcmp(SKIN[i].asset, asset)) g = &SKIN[i];
    if (!g) return;
    // Luminance percentiles of the painted (non-background) texels.
    unsigned hist[256] = {0}; size_t cnt = 0;
    for (size_t i = 0; i < n; i++) { int l = (int)(0.299f * px[4 * i] + 0.587f * px[4 * i + 1] + 0.114f * px[4 * i + 2]); hist[l]++; cnt++; }
    float pct[3] = {0.04f, 0.5f, 0.995f}; int lv[3] = {0, 128, 255};
    for (int q = 0; q < 3; q++) { size_t acc = 0; for (int l = 0; l < 256; l++) { acc += hist[l]; if (acc >= cnt * pct[q]) { lv[q] = l; break; } } }
    if (lv[1] <= lv[0]) lv[1] = lv[0] + 1; if (lv[2] <= lv[1]) lv[2] = lv[1] + 1;
    for (size_t i = 0; i < n; i++) {
        float r = px[4 * i], gg = px[4 * i + 1], b = px[4 * i + 2], l = 0.299f * r + 0.587f * gg + 0.114f * b;
        if (r > 1.8f * gg && r > 1.8f * b) continue;   // mouth, gums, tongue
        float out[3];
        if (l <= lv[1]) { float t = clampf((l - lv[0]) / (float)(lv[1] - lv[0]), 0, 1); t = t * t * (3 - 2 * t) * 0.6f + t * 0.4f;
            for (int c = 0; c < 3; c++) out[c] = g->dark[c] + (g->base[c] - g->dark[c]) * t; }
        else { float t = clampf((l - lv[1]) / (float)(lv[2] - lv[1]), 0, 1);
            for (int c = 0; c < 3; c++) out[c] = g->base[c] + (g->light[c] - g->base[c]) * t; }
        float w = g->mix;
        if (g->rust_blue && b > r + 18 && b > gg + 6) {   // the stripe: muted rust
            float s = clampf((b - r - 18) / 40.0f, 0, 1);
            out[0] = out[0] * (1 - s) + 118 * s; out[1] = out[1] * (1 - s) + 70 * s; out[2] = out[2] * (1 - s) + 44 * s; w = fmaxf(w, s);
        }
        for (int c = 0; c < 3; c++) px[4 * i + c] = (uint8_t)clampf(px[4 * i + c] * (1 - w) + out[c] * w, 0, 255);
    }
}

static void load_material(DinoModel* m, int k, const char* name) {
    const char* ts = getenv("CRET_DINO_TEX"); int cap = ts ? atoi(ts) : 2048; if (cap < 128) cap = 2048;
    Image im[3]; static const char* suf[3] = {"diff", "nor", "arm"};
    int S = 0;
    for (int q = 0; q < 3; q++) {
        char path[512]; snprintf(path, sizeof path, DINO_DIR "%s_%s.jpg", name, suf[q]); im[q] = LoadImage(path);
        if (im[q].data && im[q].width > S) S = im[q].width;
    }
    if (S == 0) S = 64;
    if (S > cap) S = cap;
    for (int q = 0; q < 3; q++) {
        if (!im[q].data) im[q] = GenImageColor(S, S, q == 1 ? (Color){128, 128, 255, 255} : q == 2 ? (Color){255, 180, 0, 255} : GRAY);
        if (im[q].width != S || im[q].height != S) ImageResize(&im[q], S, S);
        ImageFormat(&im[q], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    if (strstr(name, "body")) regrade_skin(m->name, im[0].data, (size_t)S * S);
    uint8_t* pa = malloc((size_t)S * S * 4); uint8_t* pb = malloc((size_t)S * S * 4);
    const uint8_t *cd = im[0].data, *cn = im[1].data, *cr = im[2].data;
    for (size_t i = 0; i < (size_t)S * S; i++) {
        pa[4 * i] = cd[4 * i]; pa[4 * i + 1] = cd[4 * i + 1]; pa[4 * i + 2] = cd[4 * i + 2]; pa[4 * i + 3] = cr[4 * i];
        pb[4 * i] = cr[4 * i + 1]; pb[4 * i + 1] = cn[4 * i + 1]; pb[4 * i + 2] = cr[4 * i + 2]; pb[4 * i + 3] = cn[4 * i];
    }
    bool bc = gfx_use_bc3();
    m->t_albedo[k] = gfx_tex2d(S, S, bc ? 0x8C4F : GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, pa, true, true, true);
    m->t_detail[k] = gfx_tex2d(S, S, bc ? 0x83F3 : GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, pb, true, true, true);
    for (int i = 0; i < 2; i++) { glBindTexture(GL_TEXTURE_2D, i ? m->t_detail[k] : m->t_albedo[k]); glTexParameterf(GL_TEXTURE_2D, 0x84FE, 8.0f); }
    glBindTexture(GL_TEXTURE_2D, 0);
    free(pa); free(pb);
    for (int q = 0; q < 3; q++) UnloadImage(im[q]);
}

static GfxMesh upload(const DVert* v, uint32_t nv, const uint32_t* idx, uint32_t ni, GLuint inst_vbo) {
    GfxMesh g = {0};
    glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(DVert) * nv, v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(DVert), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_BYTE, GL_TRUE, sizeof(DVert), (void*)20);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_BYTE, GL_TRUE, sizeof(DVert), (void*)24);
    glEnableVertexAttribArray(4); glVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, sizeof(DVert), (void*)28);
    glEnableVertexAttribArray(5); glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(DVert), (void*)32);
    glBindBuffer(GL_ARRAY_BUFFER, inst_vbo);
    glEnableVertexAttribArray(6); glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 16, (void*)0); glVertexAttribDivisor(6, 1);
    glGenBuffers(1, &g.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * ni, idx, GL_STATIC_DRAW);
    glBindVertexArray(0); g.index_count = (int)ni;
    return g;
}

// Neck and tail chains for the procedural layers. The head is the first joint
// named "head" (else the frontmost joint above the legs); the neck runs back
// from it through single-child joints (or ones named "neck") up to 0.45 of the
// body; every joint below that root, and every "neck" joint, turns with a
// share that grows from 0 at the neck base to 1 at the head. The tail is the
// joints named "tail" (else the chain behind the hips ending at the rearmost
// joint), its share growing from base to tip. Limbs never turn.
static bool limb_name(const char* nm) {
    static const char* bad[] = {"arm", "hand", "finger", "leg", "foot", "toe", "thigh", "shin", "clav", "shoulder", "wrist", "calf", "knee", "elbow", "ik", "pole"};
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) if (strstr(nm, bad[k])) return true;
    return false;
}
static bool below(const DinoModel* m, int j, int root) { for (int k = 0; j >= 0 && k < DINO_MAX_JOINTS; k++, j = m->parent[j]) if (j == root) return true; return false; }
static void classify_joints(DinoModel* m, char (*nm)[32]) {
    static float G[DINO_MAX_JOINTS][12]; float X[DINO_MAX_JOINTS], Y[DINO_MAX_JOINTS]; int nch[DINO_MAX_JOINTS] = {0}; bool limb[DINO_MAX_JOINTS], neckish[DINO_MAX_JOINTS];
    int n = m->njoint; float H = m->height, Lx = m->bmax[0] - m->bmin[0];
    for (int j = 0; j < n; j++) {
        float L[12]; m34_trs(m->rest[j], m->rest[j] + 3, m->rest[j][7], L);
        if (m->parent[j] < 0) memcpy(G[j], L, sizeof L); else m34_mul(G[m->parent[j]], L, G[j]);
        X[j] = G[j][3]; Y[j] = G[j][7]; if (m->parent[j] >= 0) nch[m->parent[j]]++;
    }
    for (int j = 0; j < n; j++) limb[j] = limb_name(nm[j]) || Y[j] < 0.3f * H;
    for (int j = 0; j < n; j++) { neckish[j] = false; for (int k = j, d = 0; k >= 0 && d < DINO_MAX_JOINTS; k = m->parent[k], d++) if (strstr(nm[k], "neck")) { neckish[j] = true; break; } }
    int h = -1;
    for (int j = 0; j < n && h < 0; j++) if (strstr(nm[j], "head") && !strstr(nm[j], "end")) h = j;
    if (h < 0) { float bx = -1e30f; for (int j = 0; j < n; j++) if (!limb[j] && X[j] > bx) { bx = X[j]; h = j; } }
    if (h >= 0) {
        int r = h;
        for (int k = 0; k < 6 && m->parent[r] >= 0; k++) {
            int p = m->parent[r];
            if (!(nch[p] == 1 || strstr(nm[p], "neck")) || X[p] <= X[h] - 0.45f * Lx) break;
            r = p;
        }
        float x0 = 1e30f, y0 = Y[h];
        for (int j = 0; j < n; j++) if (!limb[j] && (below(m, j, r) || neckish[j]) && X[j] < x0) { x0 = X[j]; y0 = Y[j]; }
        for (int j = 0; j < n; j++) m->neck_w[j] = !limb[j] && (below(m, j, r) || neckish[j]) ? (X[h] > x0 ? sstep(x0, X[h], X[j]) : 1.0f) : 0;
        m->neck_pivot[0] = x0; m->neck_pivot[1] = y0; m->neck_pivot[2] = 0; m->has_neck = x0 < 1e29f;
    }
    bool named = false;
    for (int j = 0; j < n; j++) if (strstr(nm[j], "tail")) named = true;
    bool in[DINO_MAX_JOINTS];
    if (named) for (int j = 0; j < n; j++) in[j] = strstr(nm[j], "tail") != NULL;
    else {
        int tip = -1; float tx = 1e30f;
        for (int j = 0; j < n; j++) if (!limb[j] && X[j] < tx) { tx = X[j]; tip = j; }
        int b = tip; float xc = 0.5f * (m->bmin[0] + m->bmax[0]);
        while (b >= 0 && m->parent[b] >= 0 && nch[m->parent[b]] == 1 && X[m->parent[b]] < xc) b = m->parent[b];
        for (int j = 0; j < n; j++) in[j] = b >= 0 && !limb[j] && below(m, j, b);
    }
    float xb = -1e30f, xt = 1e30f, yb = 0;
    for (int j = 0; j < n; j++) if (in[j]) { if (X[j] > xb) { xb = X[j]; yb = Y[j]; } if (X[j] < xt) xt = X[j]; }
    m->has_tail = xb > xt + 0.05f;
    for (int j = 0; j < n; j++) m->tail_w[j] = in[j] && m->has_tail ? sstep(xb, xt, X[j]) : 0;
    m->tail_pivot[0] = xb; m->tail_pivot[1] = yb; m->tail_pivot[2] = 0;
}

static int find_clip(const DinoModel* m, const char* name) {
    for (int i = 0; i < m->nclip; i++) if (!strcmp(m->clip[i].name, name)) return i;
    return -1;
}

// The standing frame of a gait clip: where the skeleton is highest on
// average (mid-stance, legs under the body).
static float stance_frame(const DinoModel* m, int ci) {
    const DinoClip* C = &m->clip[ci]; static float G[DINO_MAX_JOINTS][12];
    int best = 0; float bh = -1e30f;
    for (int f = 0; f < C->nframes; f++) {
        const float* F = C->frames + (size_t)f * m->njoint * 8; float h = 0;
        for (int j = 0; j < m->njoint; j++) {
            float L[12]; m34_trs(F + j * 8, F + j * 8 + 3, F[j * 8 + 7], L);
            if (m->parent[j] < 0) memcpy(G[j], L, sizeof L); else m34_mul(G[m->parent[j]], L, G[j]);
            h += G[j][7];
        }
        if (h > bh) { bh = h; best = f; }
    }
    return best / 30.0f;
}

static bool load_model(DinoModel* m, const char* asset, GLuint inst_vbo) {
    char path[512]; snprintf(path, sizeof path, DINO_DIR "%s.dino", asset);
    FILE* f = fopen(path, "rb"); if (!f) return false;
    memset(m, 0, sizeof *m); snprintf(m->name, sizeof m->name, "%s", asset);
    uint32_t hdr[2], nj = 0, nmat = 0, nlod = 0, nclip = 0;
    bool ok = rd(f, hdr, 8) && hdr[0] == 0x4F4E4443u && hdr[1] == 1 && rd(f, &m->length, 4) && rd(f, &m->height, 4) && rd(f, m->bmin, 12) && rd(f, m->bmax, 12);
    ok = ok && rd(f, &nj, 4) && nj > 0 && nj <= DINO_MAX_JOINTS;
    int feet = 0; static char lname[DINO_MAX_JOINTS][32];
    for (uint32_t j = 0; ok && j < nj; j++) {
        char nm[32] = {0}; int32_t par = -1; float ib[16] = {0};
        ok = rd(f, nm, 32) && rd(f, &par, 4) && rd(f, ib, 64) && rd(f, m->rest[j], 32) && par < (int32_t)j && par >= -1;
        m->parent[j] = par; nm[31] = 0;
        // column-major 4x4 -> 3x4 rows
        for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) m->inv_bind[j][r * 4 + c] = ib[c * 4 + r];
        char low[32]; for (int c = 0; c < 32; c++) low[c] = (char)(nm[c] >= 'A' && nm[c] <= 'Z' ? nm[c] + 32 : nm[c]);
        if (strstr(low, "foot") && !strstr(low, "ik")) feet++;
        memcpy(lname[j], low, 32); lname[j][31] = 0;
    }
    m->njoint = (int)nj; m->quadruped = feet >= 4 || strstr(path, "sauropelta");   // its rig has generic bone names
    char mats[DINO_MAX_MATS][32];
    ok = ok && rd(f, &nmat, 4) && nmat > 0 && nmat <= DINO_MAX_MATS;
    for (uint32_t k = 0; ok && k < nmat; k++) { uint32_t fl = 0; ok = rd(f, mats[k], 32) && rd(f, &fl, 4); mats[k][31] = 0; m->mat_flags[k] = (int)fl; }
    m->nmat = (int)nmat;
    ok = ok && rd(f, &nlod, 4) && nlod >= 1 && nlod <= 8;
    for (uint32_t l = 0; ok && l < nlod; l++) {
        uint32_t nv, ni, ns; ok = rd(f, &nv, 4) && rd(f, &ni, 4) && rd(f, &ns, 4) && nv > 0 && nv < (1u << 22) && ni > 0 && ni < (1u << 24) && ni % 3 == 0 && ns <= DINO_MAX_SUBS;
        if (!ok) break;
        uint32_t sub[DINO_MAX_SUBS][3]; ok = rd(f, sub, 12 * ns);
        DVert* vb = malloc(sizeof(DVert) * nv); uint32_t* ib = malloc(4 * (size_t)ni);
        ok = ok && rd(f, vb, sizeof(DVert) * nv) && rd(f, ib, 4 * (size_t)ni);
        for (uint32_t i = 0; ok && i < ni; i++) if (ib[i] >= nv) ok = false;
        for (uint32_t i = 0; ok && i < nv; i++) for (int q = 0; q < 4; q++) if (vb[i].j[q] >= nj) ok = false;
        for (uint32_t s = 0; ok && s < ns; s++) if (sub[s][0] >= nmat || (uint64_t)sub[s][1] + sub[s][2] > ni) ok = false;
        if (ok && l < DINO_LODS) {
            DinoLod* L = &m->lod[l]; L->mesh = upload(vb, nv, ib, ni, inst_vbo); L->tris = (int)ni / 3; L->nsub = (int)ns;
            for (uint32_t s = 0; s < ns; s++) { L->sub_mat[s] = (int)sub[s][0]; L->sub_first[s] = (int)sub[s][1]; L->sub_count[s] = (int)sub[s][2]; }
        }
        free(vb); free(ib);
    }
    for (uint32_t l = nlod; ok && l < DINO_LODS; l++) m->lod[l] = m->lod[nlod - 1];   // fewer LODs: reuse the last
    ok = ok && rd(f, &nclip, 4) && nclip <= DINO_MAX_CLIPS;
    for (uint32_t c = 0; ok && c < nclip; c++) {
        DinoClip* C = &m->clip[c]; uint32_t fl = 0, nf = 0;
        ok = rd(f, C->name, 32) && rd(f, &C->duration, 4) && rd(f, &C->speed, 4) && rd(f, &fl, 4) && rd(f, &nf, 4) && nf >= 1 && nf < 100000;
        if (!ok) break;
        C->name[31] = 0; C->loop = fl & 1; C->nframes = (int)nf;
        C->frames = malloc(sizeof(float) * 8 * nj * nf);
        ok = rd(f, C->frames, sizeof(float) * 8 * nj * nf);
        m->nclip = (int)c + 1;
    }
    fclose(f);
    if (!ok) { fprintf(stderr, "cretaceous: bad dino file %s\n", path); return false; }
    CRET_LOAD_MARK("dino: model file");
    for (int k = 0; k < m->nmat; k++) load_material(m, k, mats[k]);
    CRET_LOAD_MARK("dino: materials");
    static const char* roles[DCLIP_COUNT] = {"idle", "idle2", "walk", "trot", "run", "jump", "eat", "drink", "call", "attack", "sleep", "hurt", "death"};
    for (int r = 0; r < DCLIP_COUNT; r++) m->slot[r] = find_clip(m, roles[r]);
    if (m->slot[DCLIP_CALL] < 0) m->slot[DCLIP_CALL] = find_clip(m, "roar");
    if (m->slot[DCLIP_DRINK] < 0) m->slot[DCLIP_DRINK] = m->slot[DCLIP_EAT];
    // No idle clip (models with a single walk or run cycle): stand in a
    // frozen frame of the gait, and shorten the stride at low speed (animate).
    m->idle_loco = m->slot[DCLIP_IDLE] < 0;
    if (m->slot[DCLIP_IDLE] < 0) m->slot[DCLIP_IDLE] = m->slot[DCLIP_WALK] >= 0 ? m->slot[DCLIP_WALK] : m->slot[DCLIP_RUN] >= 0 ? m->slot[DCLIP_RUN] : 0;
    if (m->idle_loco) m->idle_frame = stance_frame(m, m->slot[DCLIP_IDLE]);
    // Gaits in role order (walk, trot, run); speeds forced increasing so the
    // blend by speed is well defined.
    int loco[3] = {m->slot[DCLIP_WALK], m->slot[DCLIP_TROT], m->slot[DCLIP_RUN]};
    for (int i = 0; i < 3; i++) {
        if (loco[i] < 0 || m->clip[loco[i]].speed <= 0.05f) continue;
        DinoClip* C = &m->clip[loco[i]];
        if (m->nloco && C->speed <= m->clip[m->loco[m->nloco - 1]].speed) C->speed = m->clip[m->loco[m->nloco - 1]].speed * 1.3f;
        m->loco[m->nloco++] = loco[i];
    }
    m->ok = m->nloco > 0;
    classify_joints(m, lname);
    int tris[DINO_LODS]; for (int l = 0; l < DINO_LODS; l++) tris[l] = m->lod[l].tris;
    fprintf(stderr, "cretaceous: dino %s: %d joints, %d clips, LOD tris %d/%d/%d, %s, gaits", asset, m->njoint, m->nclip, tris[0], tris[1], tris[2], m->quadruped ? "quadruped" : "biped");
    for (int i = 0; i < m->nloco; i++) fprintf(stderr, " %s %.1f m/s", m->clip[m->loco[i]].name, m->clip[m->loco[i]].speed);
    fprintf(stderr, "\n");
    return m->ok;
}

bool cret_dino_init(CretDinos* d) {
    memset(d, 0, sizeof *d);
    glGenBuffers(1, &d->inst_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, d->inst_vbo); glBufferData(GL_ARRAY_BUFFER, 16 * DINO_SLOTS * 2, NULL, GL_STREAM_DRAW);
    int n = 0;
    for (int s = 0; s < CRET_SPECIES; s++) n += load_model(&d->model[s], CRET_SPECIES_TABLE[s].asset, d->inst_vbo);
    if (!n) { fprintf(stderr, "cretaceous: no dinosaur models (run ocean/cretaceous/tools/fetch_dinos.sh); agents draw as capsules\n"); glDeleteBuffers(1, &d->inst_vbo); return false; }
    if (!gfx_program_load(&d->prog, SHADER_DIR "dino.vs", SHADER_DIR "dino.fs")) return false;
    gfx_program_load(&d->shadow_prog, SHADER_DIR "dino.vs", SHADER_DIR "depth.fs");
    d->pose = calloc((size_t)DINO_SLOTS * POSE_W * 4, sizeof(float));
    glGenTextures(1, &d->pose_tex); glBindTexture(GL_TEXTURE_2D, d->pose_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, POSE_W, DINO_SLOTS, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (int i = 0; i < DINO_SLOTS; i++) d->lod[i] = d->lod_last[i] = -1;
    return true;
}

bool cret_dino_has(const CretDinos* d, int species) { return species >= 0 && species < CRET_SPECIES && d->model[species].ok; }

// ---------------------------------------------------------------- animation
// Local pose accumulator: weighted sums of T, R (sign-aligned), S.
// Per joint weights, so a layer can cover part of the body (mask: per joint 0..1).
typedef struct { float t[DINO_MAX_JOINTS][3], q[DINO_MAX_JOINTS][4], s[DINO_MAX_JOINTS], jw[DINO_MAX_JOINTS], w; } Pose;

static void pose_add_mask(Pose* p, const DinoModel* m, int ci, float t, float w, const float* mask) {
    if (ci < 0 || w <= 1e-4f) return;
    const DinoClip* C = &m->clip[ci];
    float fr = t * 30.0f; int n = C->nframes;
    int i0, i1; float a;
    if (C->loop && n > 1) {
        float span = (float)(n - 1); fr = fmodf(fr, span); if (fr < 0) fr += span;
        i0 = (int)fr; a = fr - i0; i1 = i0 + 1; if (i1 > n - 1) i1 = n - 1;
    } else {
        fr = clampf(fr, 0, (float)(n - 1)); i0 = (int)fr; a = fr - i0; i1 = i0 + 1 < n ? i0 + 1 : n - 1;
    }
    const float* A = C->frames + (size_t)i0 * m->njoint * 8; const float* B = C->frames + (size_t)i1 * m->njoint * 8;
    float w0 = w;
    for (int j = 0; j < m->njoint; j++) {
        w = mask ? w0 * mask[j] : w0;
        if (w <= 1e-5f) continue;
        bool first = p->jw[j] <= 0;
        const float* x = A + j * 8; const float* y = B + j * 8;
        float d = x[3] * y[3] + x[4] * y[4] + x[5] * y[5] + x[6] * y[6], sg = d < 0 ? -1.0f : 1.0f;
        float q[4]; for (int k = 0; k < 4; k++) q[k] = x[3 + k] * (1 - a) + y[3 + k] * sg * a;
        if (!first && q[0] * p->q[j][0] + q[1] * p->q[j][1] + q[2] * p->q[j][2] + q[3] * p->q[j][3] < 0) for (int k = 0; k < 4; k++) q[k] = -q[k];
        float ql = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]); if (ql < 1e-8f) ql = 1;
        for (int k = 0; k < 3; k++) p->t[j][k] += w * (x[k] * (1 - a) + y[k] * a);
        for (int k = 0; k < 4; k++) p->q[j][k] += w * q[k] / ql;
        p->s[j] += w * (x[7] * (1 - a) + y[7] * a);
        p->jw[j] += w;
    }
    p->w += w0;
}
static void pose_add(Pose* p, const DinoModel* m, int ci, float t, float w) { pose_add_mask(p, m, ci, t, w, NULL); }

// Yaw a joint's model-space frame by `ang` about the vertical through `p`
// (the same sense as the body's yaw: +x turns toward +z).
static void yaw_joint(float* g, float ang, const float* p) {
    if (fabsf(ang) < 1e-4f) return;
    float c = cosf(ang), s = sinf(ang);
    for (int k = 0; k < 3; k++) { float x = g[k], z = g[8 + k]; g[k] = c * x - s * z; g[8 + k] = s * x + c * z; }
    float x = g[3] - p[0], z = g[11] - p[2]; g[3] = c * x - s * z + p[0]; g[11] = s * x + c * z + p[2];
}
// A tail swipe's yaw at time t into the secondary attack: wind up away from
// the target side, sweep through it at the hit with follow-through, settle.
static float tail_swipe(float t, float hit, float len, float target, float amp, int side) {
    float sg = fabsf(target) > 0.05f ? (target > 0 ? 1.0f : -1.0f) : (side ? 1.0f : -1.0f);
    float wind = -sg * 0.55f * amp, strike = target + sg * 0.5f * amp, t0 = hit * 0.65f, t1 = hit + 0.12f;
    if (t <= 0) return 0;
    if (t < t0) return wind * sstep(0, t0, t);
    if (t < t1) return wind + (strike - wind) * sstep(t0, t1, t);
    return strike * (1 - sstep(t1, fmaxf(len, t1 + 0.2f), t));
}
// Attack clips' own hit frames (seconds): the raptor's bite, the stegosaur's
// tail, the tyrannosaur's bite, the pachycephalosaur's headbutt.
static const float CLIP_HIT[CRET_SPECIES] = {[CRET_SP_RAPTOR] = 1.0f, [CRET_SP_STEGO] = 0.8f, [CRET_SP_TREX] = 1.6f, [CRET_SP_PACHY] = 1.3f};

static float clip_dur(const DinoModel* m, int ci) { return ci >= 0 ? fmaxf(m->clip[ci].duration, 1.0f / 30) : 1.0f; }
// Standing pose. Models without an idle clip (one walk or run cycle) stand in
// the average of two gait frames half a cycle apart: each leg is halfway
// between its forward and back positions, so the feet come together under
// the body instead of freezing mid-stride.
static void pose_stand(Pose* p, const DinoModel* m, float idle_t, float w) {
    if (w <= 0) return;
    int ci = m->slot[DCLIP_IDLE];
    if (!m->idle_loco) { pose_add(p, m, ci, idle_t, w); return; }
    float d = clip_dur(m, ci), t2 = fmodf(m->idle_frame + d * 0.5f, d);
    pose_add(p, m, ci, m->idle_frame, w * 0.5f); pose_add(p, m, ci, t2, w * 0.5f);
}

// Body transform: feet origin at (x, y, z), heading yaw, pitch about the
// side axis, roll about the long axis (model forward is +x).
static void body_matrix(float x, float y, float z, float yaw, float pitch, float roll, float scale, float* o) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    // R = Ry(yaw) * Rz(pitch) * Rx(roll), with Ry mapping +x to (cos yaw, 0, sin yaw).
    float Ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy};
    float Rz[9] = {cp, -sp, 0, sp, cp, 0, 0, 0, 1};
    float Rx[9] = {1, 0, 0, 0, cr, -sr, 0, sr, cr};
    float t[9], r[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i * 3 + j] = Rz[i * 3] * Rx[j] + Rz[i * 3 + 1] * Rx[3 + j] + Rz[i * 3 + 2] * Rx[6 + j];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i * 3 + j] = Ry[i * 3] * t[j] + Ry[i * 3 + 1] * t[3 + j] + Ry[i * 3 + 2] * t[6 + j];
    for (int i = 0; i < 3; i++) { o[i * 4] = r[i * 3] * scale; o[i * 4 + 1] = r[i * 3 + 1] * scale; o[i * 4 + 2] = r[i * 3 + 2] * scale; }
    o[3] = x; o[7] = y; o[11] = z;
}

static void animate(CretDinos* d, const CretWorld* w, int i, float dt) {
    const DinoSubject* a = &d->subj[i]; DinoAnim* A = &d->anim[i]; const DinoModel* m = &d->model[a->species];
    if (!A->init || A->species != a->species) {
        memset(A, 0, sizeof *A); A->init = true; A->species = a->species; A->y = a->y; A->last_yaw = a->yaw;
        A->phase = (float)((a->seed * 0.37) - floor(a->seed * 0.37)); A->idle_t = a->seed * 0.71f; A->act_clip = A->prev_act_clip = -1;
        uint32_t h = (uint32_t)a->seed * 2654435761u; A->tint = ((h >> 8) & 255) / 255.0f;
        A->lod_prev = -1; A->lod_t = 1; A->hurt_t = 10; d->lod_last[i] = -1;
    }
    float gs = cret_growth_scale(a->growth);
    // Ground speed (sim) smoothed; airborne and swimming layers.
    A->speed = approach(A->speed, a->speed, 10.0f, dt);
    float v = A->speed;
    bool air = !a->on_ground && !a->swimming;
    if (air) { A->air_t += dt; A->land_t = 0; } else { A->land_t += dt; if (A->land_t > 0.25f) A->air_t = 0; }
    A->w_air = approach(A->w_air, air ? 1.0f : 0.0f, air ? 14.0f : 7.0f, dt);
    // Locomotion: pick the two gait clips around the speed. Clip speeds are
    // for the adult; a smaller animal's stride is shorter in proportion.
    float vref = v / gs;
    int ca = m->loco[0], cb = m->loco[0]; float wb = 0;
    if (m->nloco > 1) {
        for (int k = 0; k + 1 < m->nloco; k++) {
            float s0 = m->clip[m->loco[k]].speed, s1 = m->clip[m->loco[k + 1]].speed;
            if (vref <= s1 || k + 2 == m->nloco) { ca = m->loco[k]; cb = m->loco[k + 1]; wb = clampf((vref - s0) / fmaxf(s1 - s0, 0.01f), 0, 1); break; }
        }
    }
    float clip_speed = (m->clip[ca].speed * (1 - wb) + m->clip[cb].speed * wb) * gs;
    float dur = clip_dur(m, ca) * (1 - wb) + clip_dur(m, cb) * wb;
    // One gait clip only: below its speed the stride shortens (blended with
    // the standing frame) instead of the clip playing slowly; above it, the
    // clip plays faster (cadence).
    float amp = 1;
    if (m->idle_loco && m->nloco == 1) amp = clampf(v / fmaxf(clip_speed, 0.05f), 0.3f, 1.0f);
    float rate = m->nloco == 1 ? clampf(v / fmaxf(clip_speed * amp, 0.05f), 0.4f, 4.5f) : clampf(v / fmaxf(clip_speed, 0.05f), 0.55f, 2.4f);
    A->phase += dt * rate / dur; A->phase -= floorf(A->phase);
    float walk_v = m->clip[m->loco[0]].speed;
    A->w_move = approach(A->w_move, sstep(0.08f, 0.45f, v / fmaxf(walk_v * gs, 0.3f)), 8.0f, dt);
    // Idle alternates between its variants every ~12 s.
    A->idle_t += dt;
    int idle2 = m->slot[DCLIP_IDLE2];
    float cyc = fmodf(A->idle_t + i * 3.1f, 24.0f);
    A->idle2_w = approach(A->idle2_w, idle2 >= 0 && cyc > 14.0f ? 1.0f : 0.0f, 1.5f, dt);
    // Actions.
    int want = -1;
    switch (a->act) {
    case CRET_ACT_EAT: want = m->slot[DCLIP_EAT]; break;
    case CRET_ACT_DRINK: want = m->slot[DCLIP_DRINK]; break;
    case CRET_ACT_CALL: want = m->slot[DCLIP_CALL]; break;
    case CRET_ACT_ATTACK: want = m->slot[DCLIP_ATTACK]; break;
    case CRET_ACT_ATTACK2: want = CRET_SPECIES_TABLE[a->species].alt.kind == CRET_ALT_SLASH ? m->slot[DCLIP_ATTACK] : -1; break;   // tail swipes are procedural
    case CRET_ACT_REST: want = m->slot[DCLIP_REST]; break;
    }
    const CretSpecies* SS = &CRET_SPECIES_TABLE[a->species];
    bool attack = cret_act_is_attack(a->act);
    if (want != A->act_clip || (a->act != A->act && attack)) {
        if (A->act_clip >= 0) { A->prev_act_clip = A->act_clip; A->prev_act_t = A->act_ct; A->prev_w = A->w_act; A->prev_rate = A->act_rate; }
        A->act_clip = want; A->act_t = 0; A->w_act = A->act_clip >= 0 && A->prev_act_clip >= 0 ? 0 : A->w_act;
        // Attack clips land their blow on the sim's hit time: clip time = clip hit + (t - hit) * rate,
        // at most 2.5x (a faster blow starts partway into the clip).
        A->act_rate = 1; A->act_ct = 0;
        if (attack && want >= 0 && CLIP_HIT[a->species] > 0) {
            float hit = a->act == CRET_ACT_ATTACK2 ? SS->alt.hit_time : SS->hit_time;
            A->act_rate = clampf(CLIP_HIT[a->species] / fmaxf(hit, 0.05f), 0.5f, 2.5f);
            A->act_ct = fmaxf(CLIP_HIT[a->species] - hit * A->act_rate, 0);
        }
    }
    A->act = a->act;
    A->act_t += dt; A->act_ct += dt * A->act_rate; A->prev_act_t += dt * (A->prev_rate > 0 ? A->prev_rate : 1);
    A->w_act = approach(A->w_act, want >= 0 ? 1.0f : 0.0f, 6.0f, dt);
    A->prev_w = approach(A->prev_w, 0.0f, 6.0f, dt);

    // Blend: idle/locomotion base, then air and action layers over it.
    static Pose P; memset(&P, 0, sizeof P);
    float w_air = A->w_air * (m->slot[DCLIP_JUMP] >= 0), w_act = A->w_act * (1 - w_air), w_prev = A->act_clip < 0 ? A->prev_w * (1 - w_air) : 0;
    // On the move an attack clip plays over the neck and head (the
    // stegosaur's tail) only, and the legs keep their gait.
    const float* mask = SS->tail ? m->tail_w : m->neck_w;
    float w_mask = 0;
    if (attack && (m->has_neck || m->has_tail)) { float mv = A->w_move; w_mask = w_act * mv * 4.0f; w_act *= 1 - mv; }
    float base = fmaxf(0, 1 - w_air - w_act - w_prev);
    float w_move = base * A->w_move, w_idle = base - w_move;
    float idle_t = m->idle_loco ? m->idle_frame : A->idle_t;
    pose_stand(&P, m, idle_t, (w_idle + w_move * (1 - amp)) * (1 - A->idle2_w));
    pose_add(&P, m, idle2, A->idle_t, w_idle * A->idle2_w);
    w_move *= amp;
    pose_add(&P, m, ca, A->phase * clip_dur(m, ca), w_move * (1 - wb));
    if (cb != ca) pose_add(&P, m, cb, A->phase * clip_dur(m, cb), w_move * wb);
    // Leap: hold mid-flight while airborne, play the landing out after.
    if (w_air > 0) { float jd = clip_dur(m, m->slot[DCLIP_JUMP]); pose_add(&P, m, m->slot[DCLIP_JUMP], fminf(A->air_t * 1.2f + 0.15f * jd, 0.55f * jd), w_air); }
    if (w_act > 0) pose_add(&P, m, A->act_clip, A->act_ct, w_act);
    if (w_mask > 0) pose_add_mask(&P, m, A->act_clip, A->act_ct, w_mask, mask);
    if (w_prev > 0) pose_add(&P, m, A->prev_act_clip, A->prev_act_t, w_prev);
    // Flinch on a hit (the hurt clip briefly over everything).
    if (a->hurt > A->last_hurt) A->hurt_t = 0;
    A->last_hurt = a->hurt; A->hurt_t += dt;
    int hc = m->slot[DCLIP_HURT];
    if (hc >= 0 && A->hurt_t < 0.7f) pose_add(&P, m, hc, A->hurt_t + 0.1f, 0.8f * sstep(0, 0.06f, A->hurt_t) * (1 - sstep(0.2f, 0.7f, A->hurt_t)));
    if (P.w <= 0) pose_stand(&P, m, idle_t, 1);
    // Dead: the death clip, held on its last frame. Without one (the
    // tyrannosaur), the body freezes and topples onto its side.
    bool dead = a->dead_t >= 0;
    if (dead) {
        memset(&P, 0, sizeof P);
        int dc = m->slot[DCLIP_DEATH];
        if (dc >= 0) pose_add(&P, m, dc, fminf(a->dead_t, clip_dur(m, dc) - 1.0f / 30), 1);
        else pose_stand(&P, m, m->idle_loco ? m->idle_frame : 0, 1);
    }

    // Body: follow the ground slope (quadrupeds more), lean into turns.
    float L = m->length * gs, cy = cosf(a->yaw), sy = sinf(a->yaw);
    float reach = L * (m->quadruped ? 0.32f : 0.22f), side = fmaxf(m->bmax[2] - m->bmin[2], 0.4f) * 0.45f * gs;
    float hf = cret_height(w, a->x + cy * reach, a->z + sy * reach), hb = cret_height(w, a->x - cy * reach, a->z - sy * reach);
    float hl = cret_height(w, a->x - sy * side, a->z + cy * side), hr = cret_height(w, a->x + sy * side, a->z - cy * side);
    float follow = m->quadruped ? 0.85f : 0.45f;
    float pitch = a->swimming ? 0.0f : clampf(atan2f(hf - hb, 2 * reach) * follow, -0.5f, 0.5f);
    float roll = a->swimming ? 0.0f : clampf(atan2f(hl - hr, 2 * side) * follow * 0.6f, -0.25f, 0.25f);
    // Procedural stand-ins for clips a model lacks (Sauropelta, Gallimimus):
    // the whole body lunges and twists for an attack, dips its head to eat or
    // drink, lifts it to call, and breathes while standing.
    float px_off = 0, p_extra = 0, yaw_extra = 0, r_extra = 0, y_extra = 0, head_yaw = 0, tail_yaw = 0, swipe = 0;
    bool swiping = false;
    if (!dead) {
        bool slash = a->act == CRET_ACT_ATTACK2 && SS->alt.kind == CRET_ALT_SLASH;
        if ((a->act == CRET_ACT_ATTACK || slash) && m->slot[DCLIP_ATTACK] < 0) {   // the sauropelta's bash, the gallimimus's kicks
            float len = slash ? SS->alt.attack_time : SS->attack_time, amp = slash ? 0.6f : 1.0f;
            float p = clampf(A->act_t / fmaxf(len, 0.2f), 0, 1), k = sinf(3.14159f * p);
            px_off = k * L * 0.07f * amp; p_extra = -k * 0.10f * amp; yaw_extra = sinf(6.2832f * p) * 0.22f * amp; r_extra = k * 0.10f * amp;
        } else if (slash) {   // the raptor's slash in passing: a lean into the strike side
            float p = clampf(A->act_t / fmaxf(SS->alt.attack_time, 0.2f), 0, 1), k = sinf(3.14159f * p);
            r_extra = k * 0.12f * (A->aim_s >= 0 ? -1.0f : 1.0f); px_off = k * L * 0.03f;
        }
        // Head aim (the stegosaur aims its tail, and only while it attacks).
        A->aim_s = approach(A->aim_s, isfinite(a->aim) ? clampf(a->aim, -1, 1) : 0, 10.0f, dt);
        A->tail_aim = approach(A->tail_aim, SS->tail && a->act == CRET_ACT_ATTACK ? 1.0f : 0.0f, 8.0f, dt);
        if (!SS->tail) head_yaw = A->aim_s * SS->neck_arc * 0.9f;
        tail_yaw = A->tail_aim * A->aim_s * SS->neck_arc;
        // Tail swipe: the tail whips round toward the aimed side, the body twists against it.
        if (a->act == CRET_ACT_ATTACK2 && SS->alt.kind == CRET_ALT_TAIL && m->has_tail) {
            float target = SS->tail ? 0.5f * A->aim_s * SS->neck_arc : -0.6f * A->aim_s;
            swiping = true;
            swipe = tail_swipe(A->act_t, SS->alt.hit_time, SS->alt.attack_time, target, SS->tail ? 1.25f : 1.0f, a->seed & 1);
            yaw_extra -= 0.22f * swipe;
        }
        if ((a->act == CRET_ACT_EAT || a->act == CRET_ACT_DRINK) && m->slot[a->act == CRET_ACT_EAT ? DCLIP_EAT : DCLIP_DRINK] < 0) {
            float k = sstep(0, 0.6f, A->act_t); p_extra = -k * (0.11f + 0.025f * sinf(A->act_t * 2.3f)); y_extra = -k * m->height * gs * 0.04f;
        }
        if (a->act == CRET_ACT_CALL && m->slot[DCLIP_CALL] < 0) p_extra = sstep(0, 0.4f, A->act_t) * (1 - sstep(1.4f, 2.0f, A->act_t)) * 0.08f;
        if (m->idle_loco) { A->breath += dt * 1.6f; y_extra += sinf(A->breath) * m->height * gs * 0.006f * (1 - A->w_move); }
        if (a->limp && v > 0.2f) r_extra += sinf(A->phase * 6.2832f) * 0.05f;   // broken bone: a lurching gait
    }
    float yaw_rate = wrap_pi(a->yaw - A->last_yaw) / fmaxf(dt, 1e-3f); A->last_yaw = a->yaw;
    A->lean = approach(A->lean, clampf(-yaw_rate * v * 0.02f, -0.18f, 0.18f), 6.0f, dt);
    A->pitch = approach(A->pitch, pitch, 8.0f, dt); A->roll = approach(A->roll, roll + A->lean, 8.0f, dt);
    // Feet stay on the ground; the midpoint height under the body covers bumps.
    float yt = a->y;
    if (a->on_ground && !a->swimming) yt = fminf(a->y, 0.5f * (hf + hb)) + 0.0f;
    A->y = (a->on_ground && !a->swimming) ? approach(A->y, yt, 20.0f, dt) : yt;
    float body_roll = A->roll + r_extra, by = A->y + y_extra;
    if (dead) {
        if (m->slot[DCLIP_DEATH] < 0) {   // topple: roll about the long axis, lifted by the body's half width
            float k = sstep(0.1f, 1.4f, a->dead_t); k = k * k * (2.2f - 1.2f * k);   // falls faster at the end, slight settle
            float side_sign = (a->seed & 1) ? 1.0f : -1.0f;
            body_roll += side_sign * 1.45f * k; by += sinf(1.45f * k) * (m->bmax[2] - m->bmin[2]) * 0.1f * gs;
        }
        by -= a->decay * a->decay * m->height * gs * 0.8f;   // rots into the ground
    }
    float M[12]; body_matrix(a->x + cy * px_off, by, a->z + sy * px_off, a->yaw + yaw_extra, A->pitch + p_extra, body_roll, gs, M);
    memcpy(d->model_m[i], M, sizeof M);

    // Joint matrices: model * global * inverse bind, 3 rows per joint. The
    // procedural yaws turn whole global frames about the neck and tail bases.
    static float G[DINO_MAX_JOINTS][12];
    float* row = d->pose + (size_t)i * POSE_W * 4;
    for (int j = 0; j < m->njoint; j++) {
        float t[3], q[4], s;
        if (P.jw[j] > 1e-6f) {
            float inv = 1.0f / P.jw[j]; t[0] = P.t[j][0] * inv; t[1] = P.t[j][1] * inv; t[2] = P.t[j][2] * inv; s = P.s[j] * inv;
            float ql = sqrtf(P.q[j][0] * P.q[j][0] + P.q[j][1] * P.q[j][1] + P.q[j][2] * P.q[j][2] + P.q[j][3] * P.q[j][3]);
            if (ql < 1e-8f) { q[0] = q[1] = q[2] = 0; q[3] = 1; } else for (int k = 0; k < 4; k++) q[k] = P.q[j][k] / ql;
        } else { memcpy(t, m->rest[j], sizeof t); memcpy(q, m->rest[j] + 3, sizeof q); s = m->rest[j][7]; }
        float Lm[12]; m34_trs(t, q, s, Lm);
        if (m->parent[j] < 0) memcpy(G[j], Lm, sizeof Lm); else m34_mul(G[m->parent[j]], Lm, G[j]);
    }
    for (int j = 0; j < m->njoint; j++) {
        float g[12]; memcpy(g, G[j], sizeof g);
        if (m->neck_w[j] > 0) yaw_joint(g, head_yaw * m->neck_w[j], m->neck_pivot);
        if (m->tail_w[j] > 0) {
            float tw = m->tail_w[j], sw = swiping ? tail_swipe(A->act_t - 0.09f * tw, SS->alt.hit_time, SS->alt.attack_time,
                                                              SS->tail ? 0.5f * A->aim_s * SS->neck_arc : -0.6f * A->aim_s, SS->tail ? 1.25f : 1.0f, a->seed & 1) : 0;
            yaw_joint(g, (tail_yaw + sw) * tw, m->tail_pivot);
        }
        float S[12]; m34_mul(g, m->inv_bind[j], S); m34_mul(M, S, row + j * 12);
    }
}

void cret_dino_update(CretDinos* d, const CretSim* sim, const CretWorld* w, const float cam[3], float dt) {
    if (dt <= 0) dt = 1.0f / 60; if (dt > 0.1f) dt = 0.1f;
    // Subjects: live agents, then carcasses.
    for (int i = 0; i < DINO_SLOTS; i++) {
        DinoSubject* u = &d->subj[i]; memset(u, 0, sizeof *u); u->dead_t = -1;
        if (i < CRET_MAX_AGENTS) {
            if (i >= sim->cfg.num_agents) continue;
            const CretAgent* a = &sim->agents[i]; if (!a->alive) continue;
            *u = (DinoSubject){true, a->species, a->act, a->x, a->y, a->z, a->yaw, a->speed, a->growth > 0 ? a->growth : 1.0f,
                               a->on_ground, a->swimming, a->hurt_ticks, -1.0f, 0.0f, i, a->broken > 0, a->aim};
        } else {
            const CretCarcass* k = &sim->carcasses[i - CRET_MAX_AGENTS]; if (!k->active) continue;
            float life = sim->cfg.carcass_seconds > 0 ? sim->cfg.carcass_seconds : 300.0f, t = k->age * CRET_DT;
            float left = k->max_meat > 0 ? k->meat / k->max_meat : 0;
            float decay = fmaxf(sstep(0.85f, 1.0f, t / life), 1 - sstep(0.0f, 0.12f, left));   // old, or picked clean
            *u = (DinoSubject){true, k->species, 0, k->x, k->y, k->z, k->yaw, 0, k->growth > 0 ? k->growth : 1.0f,
                               true, false, 0, t, decay, k->agent >= 0 ? k->agent : i, false, 0};
        }
    }
    glBindTexture(GL_TEXTURE_2D, d->pose_tex);
    for (int i = 0; i < DINO_SLOTS; i++) {
        const DinoSubject* a = &d->subj[i]; DinoAnim* A = &d->anim[i];
        d->lod[i] = -1;
        if (!a->on || !cret_dino_has(d, a->species)) { A->init = false; d->lod_last[i] = -1; continue; }
        if (A->init && fabsf(a->x - d->model_m[i][3]) + fabsf(a->z - d->model_m[i][11]) > 25.0f) A->init = false;   // respawned elsewhere
        const DinoModel* m = &d->model[a->species]; float gs = cret_growth_scale(a->growth), len = m->length * gs;
        float dx = a->x - cam[0], dy = a->y + m->height * gs * 0.5f - cam[1], dz = a->z - cam[2], dist = sqrtf(dx * dx + dy * dy + dz * dz);
        float r = dist / fmaxf(len, 0.5f);
        if (dist > fmaxf(900.0f, len * 90)) { A->init = false; d->lod_last[i] = -1; continue; }
        int lod = r < 9 ? 0 : r < 30 ? 1 : 2;
        animate(d, w, i, dt);
        // Crossfade: when the LOD changes, the old one dithers out as the new one dithers in.
        if (A->lod_t >= 1 && d->lod_last[i] >= 0 && d->lod_last[i] != lod) { A->lod_prev = d->lod_last[i]; A->lod_t = 0; }
        else if (A->lod_t < 1 && lod != d->lod_last[i] && d->lod_last[i] >= 0) lod = d->lod_last[i];   // hold the target until the fade ends
        A->lod_t = fminf(A->lod_t + dt / 0.3f, 1.0f);
        d->lod[i] = lod; d->lod_last[i] = lod;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, i, m->njoint * 3, 1, GL_RGBA, GL_FLOAT, d->pose + (size_t)i * POSE_W * 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------- drawing
typedef struct { float row, fade, tint, water; } DInst;

static bool sphere_in(const float* vp, float x, float y, float z, float r) {
    // Clip-space test of a bounding sphere against the six frustum planes.
    float m[4][4]; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) m[i][j] = vp[j * 4 + i];
    for (int p = 0; p < 6; p++) {
        int ax = p / 2; float sg = p % 2 ? -1.0f : 1.0f;
        float a = m[3][0] + sg * m[ax][0], b = m[3][1] + sg * m[ax][1], c = m[3][2] + sg * m[ax][2], dd = m[3][3] + sg * m[ax][3];
        float l = sqrtf(a * a + b * b + c * c);
        if ((a * x + b * y + c * z + dd) / l < -r) return false;
    }
    return true;
}

// Per-species albedo multiplier (the raptor and pachycephalosaur are
// regraded at load instead: regrade_skin).
static const float SPECIES_TINT[CRET_SPECIES][3] = {
    [CRET_SP_RAPTOR] = {1, 1, 1}, [CRET_SP_STEGO] = {1, 1, 1}, [CRET_SP_TREX] = {1, 1, 1},
    [CRET_SP_PACHY] = {1, 1, 1}, [CRET_SP_SAUROPELTA] = {0.88f, 0.86f, 0.80f}, [CRET_SP_GALLI] = {1, 1, 1},
};

static int draw_species(CretDinos* d, const CretSim* sim, const GfxProgram* g, int sp, const float* vp, int shadow_k, const CretCsm* csm) {
    const DinoModel* m = &d->model[sp]; int drawn = 0;
    if (!m->ok) return 0;
    for (int l = 0; l < DINO_LODS; l++) {
        DInst inst[DINO_SLOTS]; int n = 0;
        for (int i = 0; i < DINO_SLOTS; i++) {
            const DinoSubject* a = &d->subj[i]; const DinoAnim* A = &d->anim[i];
            if (d->lod[i] < 0 || a->species != sp) continue;
            int lod = d->lod[i]; float fade = 1.0f;
            if (shadow_k >= 2) lod = 2; else if (shadow_k >= 0 && lod < shadow_k) lod = shadow_k;
            else if (shadow_k < 0 && A->lod_prev >= 0 && A->lod_t < 1 && A->lod_prev != lod) {
                // Crossfade: the new LOD keeps noise <= t, the old one the rest.
                if (A->lod_prev == l) { lod = l; fade = -(1.0f + A->lod_t); } else fade = A->lod_t;
            }
            if (lod != l) continue;
            float gs = cret_growth_scale(a->growth);
            float r = 0.6f * m->length * gs, cx = d->model_m[i][3], cyy = d->model_m[i][7] + m->height * gs * 0.5f, cz = d->model_m[i][11];
            if (shadow_k >= 0) { float pc[3] = {cx, cyy, cz}; if (!cret_csm_caster_visible(csm, shadow_k, pc, r, m->height * gs)) continue; }
            else if (!sphere_in(vp, cx, cyy, cz, r)) continue;
            float water = cret_water(sim->world, a->x, a->z);
            inst[n++] = (DInst){(float)i, fade, d->anim[i].tint, water > cret_height(sim->world, a->x, a->z) - 0.05f ? water : -1e4f};
        }
        if (!n) continue;
        const DinoLod* L = &m->lod[l];
        glBindBuffer(GL_ARRAY_BUFFER, d->inst_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(DInst) * n, inst);
        glBindVertexArray(L->mesh.vao);
        for (int s = 0; s < L->nsub; s++) {
            if (!L->sub_count[s]) continue;
            int mt = L->sub_mat[s];
            if (shadow_k < 0) {
                gfx_set3f(g, "u_tint", SPECIES_TINT[sp][0], SPECIES_TINT[sp][1], SPECIES_TINT[sp][2]);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m->t_albedo[mt]);
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m->t_detail[mt]);
            }
            if (m->mat_flags[mt] & 1) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            glDrawElementsInstanced(GL_TRIANGLES, L->sub_count[s], GL_UNSIGNED_INT, (void*)(sizeof(uint32_t) * L->sub_first[s]), n);
        }
        drawn += n; d->drawn_tris += n * L->tris;
    }
    glBindVertexArray(0); glEnable(GL_CULL_FACE);
    (void)g;
    return drawn;
}

void cret_dino_draw(CretDinos* d, const CretSim* sim, const VegFrameDesc* f) {
    GfxProgram* g = &d->prog; glUseProgram(g->id);
    d->drawn = 0; d->drawn_tris = 0;
    gfx_setmat4(g, "u_viewproj", f->viewproj);
    gfx_set3f(g, "u_cam", f->cam[0], f->cam[1], f->cam[2]); gfx_set3f(g, "u_sun", f->sun[0], f->sun[1], f->sun[2]);
    gfx_set1f(g, "u_far", f->far_plane); gfx_set1f(g, "u_time", f->time); gfx_set1i(g, "u_frame", f->frame);
    gfx_set1f(g, "u_cloud_cover", f->cloud_cover); gfx_set2f(g, "u_map", (float)f->map_n, f->map_cell);
    gfx_set1f(g, "u_overcast", f->overcast); gfx_set1f(g, "u_wet", f->wet);   // view.c weather
    gfx_set2f(g, "u_depth", f->depth_ab[0], f->depth_ab[1]);
    gfx_set1i(g, "t_albedo", 0); gfx_set1i(g, "t_detail", 1);
    gfx_bind_tex(g, "t_pose", 2, GL_TEXTURE_2D, d->pose_tex);
    gfx_bind_tex(g, "t_trans", 3, GL_TEXTURE_2D, f->t_trans);
    gfx_bind_tex(g, "t_skyview", 4, GL_TEXTURE_2D, f->t_skyview);
    gfx_bind_tex(g, "t_horizon0", 5, GL_TEXTURE_2D, f->t_horizon0);
    gfx_bind_tex(g, "t_horizon1", 6, GL_TEXTURE_2D, f->t_horizon1);
    gfx_bind_tex(g, "t_weather", 7, GL_TEXTURE_2D, f->t_weather);
    gfx_bind_tex(g, "t_misc", 8, GL_TEXTURE_2D, f->t_misc);
    cret_csm_bind(f->csm, g, 9);
    glCullFace(GL_BACK);
    for (int s = 0; s < CRET_SPECIES; s++) d->drawn += draw_species(d, sim, g, s, f->viewproj, -1, NULL);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
}

int cret_dino_draw_shadow(CretDinos* d, const CretSim* sim, const CretCsm* c, int k) {
    GfxProgram* g = &d->shadow_prog; glUseProgram(g->id);
    gfx_setmat4(g, "u_viewproj", c->vp[k]); gfx_set2f(g, "u_depth", 0, 0);
    gfx_bind_tex(g, "t_pose", 2, GL_TEXTURE_2D, d->pose_tex);
    glCullFace(GL_FRONT);
    int n = 0; for (int s = 0; s < CRET_SPECIES; s++) n += draw_species(d, sim, g, s, c->vp[k], k, c);
    glCullFace(GL_BACK); glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    return n;
}

void cret_dino_reload_shaders(CretDinos* d) { gfx_program_reload_if_changed(&d->prog); gfx_program_reload_if_changed(&d->shadow_prog); }

void cret_dino_close(CretDinos* d) {
    for (int s = 0; s < CRET_SPECIES; s++) {
        DinoModel* m = &d->model[s];
        for (int l = 0; l < DINO_LODS; l++) {
            bool dup = false; for (int q = 0; q < l; q++) if (m->lod[q].mesh.vao == m->lod[l].mesh.vao) dup = true;
            if (!dup && m->lod[l].mesh.vao) gfx_mesh_free(&m->lod[l].mesh);
        }
        for (int k = 0; k < m->nmat; k++) { glDeleteTextures(1, &m->t_albedo[k]); glDeleteTextures(1, &m->t_detail[k]); }
        for (int c = 0; c < m->nclip; c++) free(m->clip[c].frames);
    }
    glDeleteTextures(1, &d->pose_tex); glDeleteBuffers(1, &d->inst_vbo); free(d->pose);
    gfx_program_free(&d->prog); gfx_program_free(&d->shadow_prog);
    memset(d, 0, sizeof *d);
}
