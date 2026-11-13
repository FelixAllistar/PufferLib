#include "veg.h"
#include "gprof.h"
#include "trample.h"
#include "texcache.h"
#include <dirent.h>
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define SHADER_DIR "ocean/cretaceous/shaders/"
#define FOLIAGE_DIR "ocean/cretaceous/assets/foliage/"
#define VPI 3.14159265f
#define AT 512                 // foliage atlas layer size (painted at this size)
// Small-memory knobs (the web page sets both on phones, whose 3D view is
// about 600 pixels wide): CRET_VEG_ATLAS uploads the atlas from the mip of
// that size down (256: a quarter of the memory), CRET_VEG_IMP renders the
// impostors at that size instead of 256 (128: a quarter). Powers of two.
static int veg_pow2_env(const char* name, int lo, int dflt) {
    const char* e = getenv(name); int s = e ? atoi(e) : dflt;
    return s >= lo && s <= dflt && !(s & (s - 1)) ? s : dflt;
}
#define IMP veg_pow2_env("CRET_VEG_IMP", 64, 256)   // impostor view size

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline uint32_t hashu(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }
typedef struct { uint32_t s; } VRng;
static inline float vr(VRng* r) { r->s = hashu(r->s + 0x9e3779b9u); return (r->s >> 8) * (1.0f / 16777216.0f); }
static inline float vrr(VRng* r, float a, float b) { return a + (b - a) * vr(r); }

// ---------------------------------------------------------------- atlas painting
// Layers 0..P_COUNT-1 have painted versions; the rest fall back to a painted
// sibling (FALLBACK) when the photo sources in assets/foliage are missing.
// Layers from L_CL_MAGN on are the skeleton generator's (veg_gen.h): leaf
// clusters in 2x2 cells of 256 px (a twig's base at the bottom centre of its
// cell), leaflets, segments and cords in columns, and two tiling surfaces.
// They paint their own fallbacks (new_layer).
enum { L_BARK, L_FERN, L_CYCAD, L_ARAU, L_LEAF, L_PALM, L_HORSE, L_SHRUB, P_COUNT,
       L_FERN2 = P_COUNT, L_TFERN, L_SPRAY, L_PODO, L_LEAF2, L_LEAF3, L_BARK_CON, L_BARK_PALM, L_BARK_FERN, L_GRASS, L_SEDGE, L_REED,
       L_CL_MAGN, L_CL_LAUREL, L_CL_HEART, L_CL_PODO, L_CL_SPRAY, L_CL_SHRUB, L_CL_GINKGO, L_CL_PLANE, L_CL_BERRY,
       L_CL_ROPE,                   // 2x2 leaf clusters (rope: araucaria scale-leaf branchlets)
       L_LEAFLET, L_PSEG, L_ROPE,   // 8 columns: cycad pinnae, palm segments and leaflets, araucaria cords
       L_FPINNA,                    // 4 columns: tree-fern pinnae
       L_SCALES, L_HSTEM,           // tiling: araucaria scale-leaf rope, horsetail stem with node sheaths
       L_GUNNERA, L_LILY,           // one leaf each: gunnera's lobed leaf, a lily pad (centre at the middle)
       L_PETAL, L_FRUIT,            // 8 columns: petals and bracts; fruit, seed and cone skins (opaque)
       L_FUNGUS, L_CLUB,            // 4 columns: bracket and mushroom surfaces; clubmoss sprays and moss
       L_COUNT };
static const int FALLBACK[L_COUNT] = {L_BARK, L_FERN, L_CYCAD, L_ARAU, L_LEAF, L_PALM, L_HORSE, L_SHRUB,
                                      L_FERN, L_FERN, L_ARAU, L_ARAU, L_LEAF, L_LEAF, L_BARK, L_BARK, L_BARK, L_HORSE, L_HORSE, L_HORSE,
                                      L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF, L_LEAF};
static bool is_cluster_layer(int k) { return k >= L_CL_MAGN && k <= L_CL_ROPE; }
static int layer_columns(int k) { return k == L_LEAFLET || k == L_PSEG || k == L_ROPE || k == L_PETAL || k == L_FRUIT ? 8 : k == L_FPINNA || k == L_FUNGUS || k == L_CLUB ? 4 : 0; }
static bool is_bark_layer(int k) { return k == L_BARK || k == L_BARK_CON || k == L_BARK_PALM || k == L_BARK_FERN; }
typedef struct { float r, g, b, a; } Px;
typedef struct { Px* p; } Canvas;

static void blend(Canvas* c, int x, int y, float r, float g, float b, float cov) {
    if (x < 0 || y < 0 || x >= AT || y >= AT || cov <= 0) return;
    Px* q = &c->p[y * AT + x]; cov = clampf(cov, 0, 1);
    q->r = lerpf(q->r, r, cov); q->g = lerpf(q->g, g, cov); q->b = lerpf(q->b, b, cov); q->a = q->a + (1 - q->a) * cov;
}
// Periodic value noise for tileable textures.
static float vnoise(float x, float y, int px, int py, uint32_t s) {
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    #define HV(a, b) ((hashu((uint32_t)(((a) % px + px) % px) * 73856093u ^ (uint32_t)(((b) % py + py) % py) * 19349663u ^ s) >> 8) * (1.0f / 16777216.0f))
    float v00 = HV(ix, iy), v10 = HV(ix + 1, iy), v01 = HV(ix, iy + 1), v11 = HV(ix + 1, iy + 1);
    #undef HV
    return lerpf(lerpf(v00, v10, fx), lerpf(v01, v11, fx), fy);
}
static float vfbm(float x, float y, int px, int py, uint32_t s, int oct) {
    float a = 0, amp = 0.5f, n = 0;
    for (int o = 0; o < oct; o++) { a += amp * vnoise(x, y, px, py, s + o * 101); n += amp; x *= 2; y *= 2; px *= 2; py *= 2; amp *= 0.5f; }
    return a / n;
}

// A leaf: base (bx,by), direction angle `ang` (0 = +y), length L, max width W.
// shape: 0 lanceolate, 1 ovate (widest near the base third), 2 strap.
static void paint_leaf(Canvas* c, float bx, float by, float ang, float L, float W, int shape, float serr,
                       float r, float g, float b, float tip_light) {
    float dx = sinf(ang), dy = cosf(ang), nx = dy, ny = -dx;
    float ex = bx + dx * L, ey = by + dy * L, m = W * 0.6f + 2;
    int x0 = (int)fminf(bx, ex) - (int)m, x1 = (int)fmaxf(bx, ex) + (int)m, y0 = (int)fminf(by, ey) - (int)m, y1 = (int)fmaxf(by, ey) + (int)m;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f - bx, py = y + 0.5f - by;
        float s = px * dx + py * dy, t = px * nx + py * ny;
        if (s < -1 || s > L + 1) continue;
        float u = clampf(s / L, 0, 1), prof;
        if (shape == 1) prof = powf(fmaxf(sinf(VPI * powf(u, 0.75f)), 0), 0.8f);
        else if (shape == 2) prof = sstep(0.0f, 0.12f, u) * (1 - sstep(0.8f, 1.0f, u) * 0.85f);
        else prof = powf(fmaxf(sinf(VPI * u), 0), 0.7f);
        float hw = fmaxf(0.5f * W * prof * (1 + serr * 0.3f * sinf(s * 0.9f)), 0);
        float cov = clampf(hw - fabsf(t) + 0.5f, 0, 1) * clampf(s + 0.5f, 0, 1) * clampf(L - s + 0.5f, 0, 1);
        if (cov <= 0) continue;
        float shade = 0.82f + 0.18f * (1 - fabsf(t) / fmaxf(hw, 0.5f)) + tip_light * u;
        float vein = fabsf(t) < 0.45f + W * 0.03f ? 0.35f : 0.0f;
        blend(c, x, y, r * shade + vein * 0.12f, g * shade + vein * 0.14f, b * shade + vein * 0.05f, cov);
    }
}
// Tapered stem from (x0,y0) to (x1,y1).
static void paint_stem(Canvas* c, float x0, float y0, float x1, float y1, float w0, float w1, float r, float g, float b) {
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy); if (L < 0.5f) return; dx /= L; dy /= L;
    float m = fmaxf(w0, w1) + 2;
    int ax = (int)(fminf(x0, x1) - m), bx = (int)(fmaxf(x0, x1) + m), ay = (int)(fminf(y0, y1) - m), by = (int)(fmaxf(y0, y1) + m);
    for (int y = ay; y <= by; y++) for (int x = ax; x <= bx; x++) {
        float px = x + 0.5f - x0, py = y + 0.5f - y0, s = px * dx + py * dy, t = fabsf(px * dy - py * dx);
        if (s < 0 || s > L) continue;
        float hw = 0.5f * lerpf(w0, w1, s / L), cov = clampf(hw - t + 0.5f, 0, 1);
        blend(c, x, y, r * (0.8f + 0.2f * (1 - t / fmaxf(hw, 0.5f))), g, b, cov);
    }
}

// Araucaria branchlet "rope": a thick cord of overlapping scale leaves. Drawn
// as discs from base to tip, so each pixel keeps the shading of the last
// (outermost) disc over it: cylindrical shading across the cord, a chevron
// of scale tips along it, and a saw-tooth outline where the scales stand off.
static void paint_rope(Canvas* c, float x0, float y0, float ang, float curve, float L, float W, const float base[3], const float tip[3], float tone) {
    float x = x0, y = y0, sc = W * 0.55f;
    for (float s = 0; s < L; s += 1.0f) {
        float u = s / L, a = ang + curve * u, dx = sinf(a), dy = cosf(a), nx = dy, ny = -dx;
        float ph = s / sc - floorf(s / sc);
        float hw = 0.5f * W * (1.0f - 0.55f * u * u) * (1.0f + 0.22f * (1.0f - ph)) * sstep(0.0f, 0.04f, u + 0.02f);
        float young = sstep(0.72f, 1.0f, u);
        float cr = lerpf(base[0], tip[0], young) * tone, cg = lerpf(base[1], tip[1], young) * tone, cb = lerpf(base[2], tip[2], young) * tone;
        int bx0 = (int)floorf(x - hw - 1), bx1 = (int)ceilf(x + hw + 1), by0 = (int)floorf(y - hw - 1), by1 = (int)ceilf(y + hw + 1);
        for (int py = by0; py <= by1; py++) for (int px = bx0; px <= bx1; px++) {
            float qx = px + 0.5f - x, qy = py + 0.5f - y, d = sqrtf(qx * qx + qy * qy);
            float cov = clampf(hw - d + 0.5f, 0, 1); if (cov <= 0) continue;
            float t = (qx * nx + qy * ny) / fmaxf(hw, 0.5f);               // -1..1 across the cord
            float cyl = 0.55f + 0.45f * sqrtf(fmaxf(1.0f - t * t, 0.0f)) + 0.08f * t;
            float sp = (s + fabsf(t) * sc * 0.7f) / sc; sp -= floorf(sp);    // scale chevrons
            float scale = 0.68f + 0.42f * sstep(0.05f, 0.85f, sp);
            float k = cyl * scale;
            blend(c, px, py, cr * k, cg * k, cb * k, cov);
        }
        x += dx; y += dy;
    }
}
// Araucaria branch spray: a woody axis with alternate ropes swept toward the
// tip and curving forward, with gaps between them (no fern-like pinnae).
static void paint_arau(Canvas* c, VRng* r, bool thick) {
    static const float base[3] = {0.026f, 0.066f, 0.028f}, tip[3] = {0.06f, 0.115f, 0.03f};
    paint_stem(c, 256, 0, 256, 470, thick ? 12 : 9, 4, 0.07f, 0.065f, 0.04f);
    int n = thick ? 18 : 24;
    for (int i = 0; i < n; i++) {
        float v = 0.04f + i * (0.86f / n), side = (i & 1) ? 1.0f : -1.0f;
        float L = (thick ? 235 : 220) * (1.0f - 0.45f * v) * vrr(r, 0.85f, 1.1f), W = (thick ? 18 : 14) * vrr(r, 0.9f, 1.1f);
        float tone = vrr(r, 0.82f, 1.15f);
        paint_rope(c, 256 + side * 3, v * AT, side * vrr(r, 0.95f, 1.15f), -side * vrr(r, 0.35f, 0.6f), L, W, base, tip, tone);
    }
    paint_rope(c, 256, 0.86f * AT, vrr(r, -0.05f, 0.05f), 0, 0.13f * AT, thick ? 17 : 13, base, tip, 1.05f);   // leader
}

static void paint_layer(int layer, Px* img) {
    Canvas c = {img}; VRng r = {(uint32_t)layer * 7919u + 13};
    // Background colour (alpha 0) near the foliage colour avoids dark mip fringes.
    float bg[L_COUNT][3] = {{0.3f, 0.25f, 0.2f}, {0.14f, 0.26f, 0.07f}, {0.09f, 0.19f, 0.06f}, {0.08f, 0.17f, 0.07f},
                            {0.15f, 0.27f, 0.08f}, {0.17f, 0.28f, 0.12f}, {0.07f, 0.13f, 0.04f}, {0.12f, 0.22f, 0.07f}};
    for (int i = 0; i < AT * AT; i++) img[i] = (Px){bg[layer][0], bg[layer][1], bg[layer][2], 0};
    switch (layer) {
    case L_BARK:
        for (int y = 0; y < AT; y++) for (int x = 0; x < AT; x++) {
            float n = vfbm(x / 32.0f, y / 128.0f, AT / 32, AT / 128, 3, 5), d = vfbm(x / 64.0f, y / 64.0f, AT / 64, AT / 64, 9, 3);
            float fur = fabsf(sinf((x / (float)AT) * VPI * 14 + 5 * d));
            float v = (0.55f + 0.6f * n) * (0.55f + 0.45f * sstep(0.05f, 0.5f, fur));
            img[y * AT + x] = (Px){0.46f * v, 0.39f * v, 0.3f * v, 1};
        }
        break;
    case L_FERN: {    // bipinnate frond, painted for fronds ~3.5x longer than wide:
        // near-horizontal pinnae here open to ~60 degrees once v is stretched.
        int pairs = 28;
        for (int i = 0; i < pairs; i++) {
            float v = 0.035f + i * (0.94f / pairs);
            float L = 246 * powf(fmaxf(sinf(VPI * clampf(v * 0.9f + 0.08f, 0, 1)), 0), 0.65f);
            for (int side = -1; side <= 1; side += 2) {
                float yy = v * AT + side * 4.0f;                       // sub-opposite pairs
                float ang = side * (1.4f - 0.3f * v + vrr(&r, -0.05f, 0.05f)), dx = sinf(ang), dy = cosf(ang);
                float tone = vrr(&r, 0.8f, 1.15f), yel = sstep(0.25f, 0.0f, v) * 0.5f + vrr(&r, 0.0f, 0.12f);
                float cr = (0.055f + 0.07f * yel) * tone, cg = (0.15f + 0.03f * yel) * tone, cb = 0.035f * tone;
                for (float sd = 5; sd < L - 2; sd += 6.0f) {
                    float t = sd / L, pl = lerpf(7.5f, 3.0f, t * t), lt = 1.0f + 0.35f * t;
                    float px = 256 + dx * sd, py = yy + dy * sd;
                    paint_leaf(&c, px, py, ang - side * 1.35f, pl, 5.5f, 0, 0, cr * lt, cg * lt, cb * lt, 0.2f);
                    paint_leaf(&c, px, py, ang + side * 1.35f, pl * 0.8f, 5.0f, 0, 0, cr * lt * 0.9f, cg * lt * 0.9f, cb * lt, 0.2f);
                }
                paint_stem(&c, 256, yy, 256 + dx * (L - 2), yy + dy * (L - 2), 2.0f, 0.8f, cr * 0.8f, cg * 0.8f, cb * 0.8f);
            }
        }
        paint_stem(&c, 256, 0, 256, 505, 6, 1.5f, 0.12f, 0.15f, 0.06f);
        break; }
    case L_CYCAD:
        for (int i = 0; i < 46; i++) {
            float v = 0.05f + i * 0.0205f, y = v * AT, L = 205 * powf(sinf(VPI * clampf(v * 0.9f + 0.08f, 0, 1)), 0.35f);
            for (int side = -1; side <= 1; side += 2)
                paint_leaf(&c, 256 + side * 3, y, side * (1.2f + vrr(&r, -0.04f, 0.04f)), L, 10, 2, 0, 0.08f, 0.19f, 0.06f, 0.2f);
        }
        paint_stem(&c, 256, 0, 256, 500, 9, 3, 0.2f, 0.22f, 0.1f);
        break;
    case L_ARAU:      // branchlet spray of scale-leaved ropes
        for (int i = 0; i < 11; i++) {
            float v = 0.06f + i * 0.085f, y = v * AT, L = 175 * (1 - 0.55f * v);
            for (int side = -1; side <= 1; side += 2) {
                float ang = side * (0.85f + vrr(&r, -0.1f, 0.1f)), dx = sinf(ang), dy = cosf(ang);
                for (int k = 0; k < (int)L; k += 3) {   // scale-leaf bumps along the rope
                    float x = 256 + dx * k, yy = y + dy * k, w = lerpf(15, 6, k / L) * (1 + 0.25f * sinf(k * 0.7f));
                    float tone = 0.85f + 0.3f * (k % 9 < 4);
                    paint_leaf(&c, x, yy, ang + side * 0.6f, w, w * 0.6f, 0, 0, 0.07f * tone, 0.16f * tone, 0.06f * tone, 0.1f);
                    paint_leaf(&c, x, yy, ang - side * 0.6f, w, w * 0.6f, 0, 0, 0.07f * tone, 0.17f * tone, 0.06f * tone, 0.1f);
                }
            }
        }
        paint_stem(&c, 256, 0, 256, 505, 11, 5, 0.13f, 0.16f, 0.07f);
        break;
    case L_LEAF:      // early angiosperm twig: glossy ovate leaves
        paint_stem(&c, 256, 0, 262, 470, 7, 3, 0.25f, 0.2f, 0.12f);
        for (int i = 0; i < 11; i++) {
            float v = 0.08f + i * 0.08f, side = (i & 1) ? 1 : -1, L = vrr(&r, 140, 200);
            float hue = vrr(&r, -0.05f, 0.05f);
            paint_leaf(&c, 256 + 5 * v, v * AT, side * vrr(&r, 0.5f, 1.1f), L, L * 0.45f, 1, 0, 0.08f + hue, 0.19f + hue * 0.5f, 0.05f, 0.18f);
        }
        paint_leaf(&c, 262, 470, 0.05f, 40, 18, 1, 0, 0.18f, 0.3f, 0.09f, 0.2f);
        break;
    case L_PALM:      // costapalmate fan on a petiole (v < 0.3 is petiole)
        paint_stem(&c, 256, 0, 256, 160, 10, 7, 0.22f, 0.25f, 0.13f);
        for (int i = 0; i < 30; i++) {
            float a = -1.45f + i * (2.9f / 29), L = 330 * (0.8f + 0.2f * cosf(a)) * vrr(&r, 0.92f, 1.0f);
            float hue = vrr(&r, -0.03f, 0.02f);
            paint_leaf(&c, 256, 160, a, L, 34, 0, 0, 0.17f + hue, 0.30f + hue, 0.12f, 0.15f);
        }
        break;
    case L_HORSE:     // jointed stalks: dark node sheaths, whorls of fine drooping branches
        for (int st = 0; st < 4; st++) {
            float x = 120 + st * 90 + vrr(&r, -15, 15), top = vrr(&r, 400, 505), lean = vrr(&r, -25, 25), tone = vrr(&r, 0.85f, 1.15f);
            for (float y = 50; y < top - 40; y += vrr(&r, 30, 40)) {
                float xx = x + lean * y / top, br = lerpf(48, 16, y / top);
                for (int b = 0; b < 6; b++) {
                    float side = (b & 1) ? 1 : -1, len = br * vrr(&r, 0.6f, 1.0f), up = vrr(&r, 4, 18);
                    float ex = xx + side * len, ey = y + up - len * 0.35f;   // out, then droop
                    paint_stem(&c, xx, y, xx + side * len * 0.55f, y + up, 1.6f, 1.2f, 0.04f * tone, 0.078f * tone, 0.026f * tone);
                    paint_stem(&c, xx + side * len * 0.55f, y + up, ex, ey, 1.2f, 0.7f, 0.044f * tone, 0.084f * tone, 0.026f * tone);
                }
            }
            paint_stem(&c, x, 0, x + lean, top, 13, 6, 0.056f * tone, 0.1f * tone, 0.034f * tone);
            for (float y = 30; y < top; y += 35) {
                float xx = x + lean * y / top, w = lerpf(10, 5, y / top);
                paint_stem(&c, xx - w * 0.6f, y, xx + w * 0.6f, y, 4, 4, 0.07f, 0.07f, 0.04f);
            }
            paint_leaf(&c, x + lean, top - 4, 0, 26, 9, 0, 0, 0.16f, 0.14f, 0.07f, 0.1f);   // spore cone
        }
        break;
    case L_SHRUB:     // twiggy angiosperm/bennettitalean scrub: many small ovate leaves
        for (int t = 0; t < 5; t++) {
            float x0 = 150 + t * 52 + vrr(&r, -20, 20), x1 = x0 + vrr(&r, -90, 90);
            paint_stem(&c, x0, 0, x1, 490, 5, 1.5f, 0.2f, 0.16f, 0.1f);
            for (int i = 0; i < 18; i++) {
                float v = 0.12f + i * 0.048f, side = (i & 1) ? 1 : -1, L = vrr(&r, 38, 62) * (1 - 0.3f * v), hue = vrr(&r, -0.035f, 0.035f), tone = vrr(&r, 0.75f, 1.15f);
                float bx = lerpf(x0, x1, v), by = v * 490;
                paint_stem(&c, bx, by, bx + side * L * 0.3f, by + L * 0.15f, 1.5f, 1, 0.18f, 0.16f, 0.09f);
                paint_leaf(&c, bx + side * L * 0.3f, by + L * 0.15f, side * vrr(&r, 0.5f, 1.3f), L, L * 0.5f, 1, 0, (0.07f + hue) * tone, (0.16f + hue) * tone, 0.045f * tone, 0.25f);
            }
        }
        break;
    }
}

// ---------------------------------------------------------------- photo foliage
// CC0 photo sprites (single leaves, fronds, sprays; see assets/foliage/
// SOURCES.md) are composited procedurally into the same layer layouts the
// painted versions use, so meshes, variation and wind stay procedural.
typedef struct { int w, h; float* p; float mean[3]; float aspect; } Sprite;   // linear rgba, row 0 = tip
typedef struct { Sprite s[12]; int n; } SpriteSet;
typedef struct { float* p; float mean[3]; bool ok; } BarkImg;   // 512x512 linear rgb
typedef struct {
    SpriteSet fern, blade, spray, strap, magn, laurel, heart, round, sprig, small, palmate, lobed, serrate;
    BarkImg bark[4];   // angiosperm, conifer, palm, tree fern
} Photos;

static float srgb_lin(uint8_t v) { float c = v / 255.0f; return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
// Source atlases pack leaves tightly, so a cropped sprite can carry slivers
// of its neighbours along the border. Keep only the largest 8-connected
// blob of solid alpha (plus its soft edge within 2 px). With R = 2 (blade
// sprites) curled hair-thin leaf tips are dropped too.
static void keep_main_component(Sprite* sp, int R) {
    int W = sp->w, H = sp->h, N = W * H; int* lab = calloc(N, sizeof(int)); int* st = malloc(sizeof(int) * N);
    // Label only core pixels (solid (2R+1)^2 neighbourhood): threads have no core.
    uint8_t* core = calloc(N, 1);
    int need = (2 * R + 1) * (2 * R + 1) - 2;
    for (int y = R; y < H - R; y++) for (int x = R; x < W - R; x++) {
        int c = 0; for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) c += sp->p[4 * ((y + dy) * W + x + dx) + 3] >= 0.3f;
        core[y * W + x] = c >= need;
    }
    int best = 0, best_n = 0, cur = 0;
    for (int i = 0; i < N; i++) {
        if (lab[i] || !core[i]) continue;
        int top = 0, cnt = 0; st[top++] = i; lab[i] = ++cur;
        while (top) {
            int q = st[--top], qx = q % W, qy = q / W; cnt++;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int x = qx + dx, y = qy + dy; if (x < 0 || y < 0 || x >= W || y >= H) continue;
                int j = y * W + x; if (lab[j] || !core[j]) continue;
                lab[j] = cur; st[top++] = j;
            }
        }
        if (cnt > best_n) { best_n = cnt; best = cur; }
    }
    if (cur >= 1) for (int i = 0; i < N; i++) {
        if (lab[i] == best) continue;
        int x0 = i % W, y0 = i / W; bool near = false;
        for (int dy = -2; dy <= 2 && !near; dy++) for (int dx = -2; dx <= 2 && !near; dx++) {
            int x = x0 + dx, y = y0 + dy; if (x >= 0 && y >= 0 && x < W && y < H && lab[y * W + x] == best) near = true;
        }
        if (!near || lab[i]) sp->p[4 * i + 3] = 0;
    }
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (x == 0 || y == 0 || x == W - 1 || y == H - 1) sp->p[4 * (y * W + x) + 3] = 0;
    free(lab); free(st); free(core);
}
// Loads <id>_00.. into set (appends); sprites wider than max_aspect are skipped.
static void load_sprites(SpriteSet* set, const char* id, int maxn, float max_aspect, int core_r) {
    for (int k = 0; k < maxn && set->n < 12; k++) {
        char path[256]; snprintf(path, sizeof path, FOLIAGE_DIR "sprites/%s_%02d.png", id, k);
        if (!FileExists(path)) break;
        Image im = LoadImage(path); if (!im.data) break;
        if ((float)im.width / im.height > max_aspect) { UnloadImage(im); continue; }
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        Sprite* sp = &set->s[set->n++]; sp->w = im.width; sp->h = im.height; sp->aspect = (float)im.width / im.height;
        sp->p = malloc(sizeof(float) * 4 * sp->w * sp->h);
        const uint8_t* d = im.data; double m[3] = {0}, n = 0;
        for (int i = 0; i < sp->w * sp->h; i++) {
            float a = d[4 * i + 3] / 255.0f;
            for (int c = 0; c < 3; c++) sp->p[4 * i + c] = srgb_lin(d[4 * i + c]);
            sp->p[4 * i + 3] = a;
            if (a > 0.5f) { for (int c = 0; c < 3; c++) m[c] += sp->p[4 * i + c]; n += 1; }
        }
        for (int c = 0; c < 3; c++) sp->mean[c] = n > 0 ? (float)(m[c] / n) : 0.1f;
        UnloadImage(im);
        keep_main_component(sp, core_r);
        // Soft edge pixels carry the source's white backdrop: pull them to the mean leaf colour.
        for (int i = 0; i < sp->w * sp->h; i++) {
            float a = sp->p[4 * i + 3], k = sstep(0.5f, 0.97f, a);
            for (int c = 0; c < 3; c++) sp->p[4 * i + c] = lerpf(fminf(sp->mean[c], sp->p[4 * i + c]), sp->p[4 * i + c], k);
        }
    }
}
static void load_bark(BarkImg* b, const char* name) {
    char path[256]; snprintf(path, sizeof path, FOLIAGE_DIR "bark_%s.png", name);
    b->ok = false; if (!FileExists(path)) return;
    Image im = LoadImage(path); if (!im.data) return;
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8); if (im.width != AT || im.height != AT) ImageResize(&im, AT, AT);
    b->p = malloc(sizeof(float) * 3 * AT * AT); const uint8_t* d = im.data; double m[3] = {0};
    for (int i = 0; i < AT * AT; i++) for (int c = 0; c < 3; c++) { b->p[3 * i + c] = srgb_lin(d[3 * i + c]); m[c] += b->p[3 * i + c]; }
    for (int c = 0; c < 3; c++) b->mean[c] = (float)(m[c] / (AT * AT));
    UnloadImage(im); b->ok = true;
}
static void photos_load(Photos* P) {
    memset(P, 0, sizeof(*P));
    load_sprites(&P->fern, "fern_02", 5, 0.7f, 1);
    load_sprites(&P->blade, "Foliage001", 9, 0.2f, 2); load_sprites(&P->blade, "Foliage006", 6, 0.2f, 2);
    load_sprites(&P->spray, "LeafSet019", 3, 1.0f, 1);
    load_sprites(&P->strap, "LeafSet013", 6, 1.0f, 1); load_sprites(&P->strap, "LeafSet020", 5, 1.0f, 1);
    load_sprites(&P->magn, "LeafSet018", 3, 1.0f, 1);
    load_sprites(&P->laurel, "LeafSet022", 6, 1.0f, 1);
    load_sprites(&P->heart, "LeafSet004", 6, 1.2f, 1);
    load_sprites(&P->round, "LeafSet024", 9, 1.2f, 1);
    load_sprites(&P->sprig, "LeafSet002", 6, 1.0f, 1);
    load_sprites(&P->small, "LeafSet001", 6, 1.0f, 1);
    load_sprites(&P->palmate, "LeafSet010", 6, 1.3f, 1);
    load_sprites(&P->lobed, "LeafSet025", 9, 1.3f, 1);
    load_sprites(&P->serrate, "LeafSet014", 6, 1.0f, 1);
    static const char* bn[4] = {"bark_brown_02", "metasequoia_bark", "palm_bark", "japanese_cedar_bark"};
    for (int i = 0; i < 4; i++) load_bark(&P->bark[i], bn[i]);
}
static void photos_free(Photos* P) {
    SpriteSet* sets[] = {&P->fern, &P->blade, &P->spray, &P->strap, &P->magn, &P->laurel, &P->heart, &P->round, &P->sprig, &P->small};
    for (int k = 0; k < (int)(sizeof sets / sizeof sets[0]); k++) for (int i = 0; i < sets[k]->n; i++) free(sets[k]->s[i].p);
    for (int i = 0; i < 4; i++) free(P->bark[i].p);
}
static void sp_sample(const Sprite* s, float x, float y, float o[4]) {
    x = clampf(x, 0, s->w - 1.001f); y = clampf(y, 0, s->h - 1.001f);
    int ix = (int)x, iy = (int)y; float fx = x - ix, fy = y - iy;
    const float* a = s->p + 4 * (iy * s->w + ix); const float* b = a + 4; const float* c = a + 4 * s->w; const float* d = c + 4;
    float w[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy}, al = 0, rgb[3] = {0};
    const float* q[4] = {a, b, c, d};
    for (int k = 0; k < 4; k++) { float wa = w[k] * q[k][3]; al += wa; for (int ch = 0; ch < 3; ch++) rgb[ch] += q[k][ch] * wa; }
    o[3] = al; for (int ch = 0; ch < 3; ch++) o[ch] = al > 1e-5f ? rgb[ch] / al : 0;
}
// Sprite with its base at (bx,by) pointing along `ang` (0 = +v), length L px
// along the axis and W px across (negative W mirrors). Colour is scaled so
// the sprite's mean matches `target` (times tone).
static void stamp(Canvas* c, const Sprite* s, float bx, float by, float ang, float L, float W, const float target[3], float tone) {
    float ax = sinf(ang), ay = cosf(ang), sx = ay, sy = -ax, hw = fabsf(W) * 0.5f;
    float g[3]; for (int k = 0; k < 3; k++) g[k] = target[k] / fmaxf(s->mean[k], 1e-3f) * tone;
    float xs[4] = {bx - sx * hw, bx + sx * hw, bx + ax * L - sx * hw, bx + ax * L + sx * hw};
    float ys[4] = {by - sy * hw, by + sy * hw, by + ay * L - sy * hw, by + ay * L + sy * hw};
    int x0 = (int)floorf(fminf(fminf(xs[0], xs[1]), fminf(xs[2], xs[3]))), x1 = (int)ceilf(fmaxf(fmaxf(xs[0], xs[1]), fmaxf(xs[2], xs[3])));
    int y0 = (int)floorf(fminf(fminf(ys[0], ys[1]), fminf(ys[2], ys[3]))), y1 = (int)ceilf(fmaxf(fmaxf(ys[0], ys[1]), fmaxf(ys[2], ys[3])));
    x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0; x1 = x1 >= AT ? AT - 1 : x1; y1 = y1 >= AT ? AT - 1 : y1;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float dx = x + 0.5f - bx, dy = y + 0.5f - by, t = (dx * ax + dy * ay) / L, u = (dx * sx + dy * sy) / W + 0.5f;
        if (t < 0 || t > 1 || u < 0 || u > 1) continue;
        float o[4]; sp_sample(s, u * (s->w - 1), (1 - t) * (s->h - 1), o);
        if (o[3] < 0.02f) continue;
        blend(c, x, y, o[0] * g[0], o[1] * g[1], o[2] * g[2], o[3]);
    }
}
static const Sprite* pick(const SpriteSet* s, VRng* r) { return &s->s[(int)(vr(r) * s->n) % s->n]; }

static void paint_podo_shoot(Canvas* c, const Photos* P, VRng* r, float x0, float y0, float ang, float len, float leaf, const float t[3]);
// Fills `img` from photos; false when the sources are missing (painted fallback).
static bool photo_layer(int layer, Px* img, const Photos* P) {
    Canvas c = {img}; VRng r = {(uint32_t)layer * 104729u + 7};
    static const float T[L_COUNT][3] = {   // mean linear albedo per layer
        [L_FERN] = {0.045f, 0.10f, 0.028f}, [L_FERN2] = {0.05f, 0.105f, 0.03f}, [L_TFERN] = {0.038f, 0.086f, 0.027f},
        [L_CYCAD] = {0.026f, 0.066f, 0.028f}, [L_ARAU] = {0.03f, 0.072f, 0.034f}, [L_SPRAY] = {0.045f, 0.098f, 0.036f},
        [L_PODO] = {0.024f, 0.056f, 0.026f}, [L_LEAF] = {0.045f, 0.095f, 0.03f}, [L_LEAF2] = {0.032f, 0.075f, 0.026f},
        [L_LEAF3] = {0.048f, 0.1f, 0.034f}, [L_PALM] = {0.05f, 0.098f, 0.04f}, [L_SHRUB] = {0.036f, 0.08f, 0.028f},
        [L_BARK] = {0.085f, 0.068f, 0.052f}, [L_BARK_CON] = {0.11f, 0.062f, 0.042f}, [L_BARK_PALM] = {0.09f, 0.076f, 0.058f},
        [L_BARK_FERN] = {0.055f, 0.04f, 0.028f},
        [L_GRASS] = {0.046f, 0.102f, 0.027f}, [L_SEDGE] = {0.04f, 0.09f, 0.033f}, [L_REED] = {0.056f, 0.098f, 0.034f},
    };
    const float* t = T[layer];
    if (is_bark_layer(layer)) {
        const BarkImg* b = &P->bark[layer == L_BARK ? 0 : layer == L_BARK_CON ? 1 : layer == L_BARK_PALM ? 2 : 3];
        if (!b->ok) return false;
        // Pale flakes and lichen in a bark photo (the metasequoia photo's
        // white flecks most of all) read as birch-like white blotches once lit:
        // values above the photo's mean are compressed and pulled toward its
        // hue, so they stay lighter bark instead of white paint.
        for (int i = 0; i < AT * AT; i++) {
            float q[3] = {b->p[3 * i] / b->mean[0], b->p[3 * i + 1] / b->mean[1], b->p[3 * i + 2] / b->mean[2]};
            float l = (q[0] + q[1] + q[2]) / 3;
            if (l > 1) {
                float lc = 1 + (l - 1) * 0.3f, k = 0.7f * fminf(l - 1, 1);
                for (int c = 0; c < 3; c++) q[c] = lerpf(q[c] * lc / l, lc, k);
            }
            img[i] = (Px){q[0] * t[0], q[1] * t[1], q[2] * t[2], 1};
        }
        return true;
    }
    for (int i = 0; i < AT * AT; i++) img[i] = (Px){t[0], t[1], t[2], 0};
    switch (layer) {
    case L_FERN: case L_FERN2: {   // one whole frond; the ribbon is ~3.3x longer than wide
        if (!P->fern.n) return false;
        const Sprite* s = &P->fern.s[(layer == L_FERN ? 0 : 1) % P->fern.n];
        stamp(&c, s, 256, 0, 0, 510, fminf(s->aspect * 510 * 3.3f, 500), t, 1.0f);
        return true; }
    case L_TFERN: {   // bipinnate: whole fern fronds as pinnae on a rachis
        if (!P->fern.n) return false;
        paint_stem(&c, 256, 0, 256, 505, 7, 2, t[0] * 0.9f, t[1] * 0.75f, t[2] * 0.7f);
        for (int i = 0; i < 30; i++) {
            float v = 0.03f + i * 0.031f, Lp = 250 * powf(fmaxf(sinf(VPI * clampf(v * 0.9f + 0.07f, 0, 1)), 0), 0.6f);
            for (int side = -1; side <= 1; side += 2) {
                const Sprite* s = pick(&P->fern, &r);
                stamp(&c, s, 256 + side * 2, v * AT + side * 4, side * (1.38f - 0.3f * v + vrr(&r, -0.04f, 0.04f)), Lp,
                      side * Lp * s->aspect * 0.9f, t, vrr(&r, 0.85f, 1.15f));
            }
        }
        stamp(&c, &P->fern.s[0], 256, 0.9f * AT, 0, 0.1f * AT, 60, t, 1.0f);
        return true; }
    case L_CYCAD: {   // stiff linear pinnae from blade photos
        if (!P->blade.n) return false;
        paint_stem(&c, 256, 0, 256, 500, 9, 3, 0.05f, 0.06f, 0.025f);
        for (int i = 0; i < 46; i++) {
            float v = 0.05f + i * 0.0205f, Lp = 215 * powf(fmaxf(sinf(VPI * clampf(v * 0.9f + 0.08f, 0, 1)), 0), 0.35f);
            for (int side = -1; side <= 1; side += 2) {
                const Sprite* s = pick(&P->blade, &r);
                stamp(&c, s, 256 + side * 3, v * AT, side * (1.22f + vrr(&r, -0.05f, 0.05f)), Lp, side * clampf(Lp * s->aspect * 1.4f, 9, 16), t, vrr(&r, 0.85f, 1.1f));
            }
        }
        return true; }
    case L_ARAU: {    // araucaria: painted scale-leaf ropes (no photo matches the cord look)
        paint_arau(&c, &r, false);
        return true; }
    case L_SPRAY: {   // metasequoia: feathery deciduous sprays, opposite pairs
        if (!P->spray.n) return false;
        // Many small opposite sprays on a shoot: fine and soft, not one big frond.
        paint_stem(&c, 256, 0, 256, 480, 5, 1.5f, 0.08f, 0.06f, 0.04f);
        for (int i = 0; i < 15; i++) {
            float v = 0.05f + i * 0.06f, Lp = 150 * (1 - 0.45f * v) * vrr(&r, 0.85f, 1.1f);
            for (int side = -1; side <= 1; side += 2) {
                const Sprite* s = pick(&P->spray, &r);
                stamp(&c, s, 256, v * AT + side * 3, side * vrr(&r, 0.9f, 1.15f), Lp, side * Lp * s->aspect * 0.8f, t, vrr(&r, 0.85f, 1.15f));
            }
        }
        stamp(&c, &P->spray.s[1 % P->spray.n], 256, 0.86f * AT, 0, 0.13f * AT, 0.13f * AT * 0.6f, t, 1.0f);
        return true; }
    case L_PODO: {    // podocarp branch (reduced mesh): the near clusters' shoots, more of them
        static const float tw[3] = {0.07f, 0.05f, 0.032f};
        paint_stem(&c, 256, 0, 262, 490, 9, 3, tw[0], tw[1], tw[2]);
        for (int i = 0; i < 6; i++) {
            float v = 0.1f + i * 0.13f, side = (i & 1) ? 1.0f : -1.0f, a = side * vrr(&r, 0.6f, 0.9f), len = vrr(&r, 230, 300) * (1 - 0.35f * v);
            paint_podo_shoot(&c, P, &r, 256 + 6 * v, v * 490, a, len, 34, t);
        }
        paint_podo_shoot(&c, P, &r, 260, 300, vrr(&r, -0.05f, 0.05f), 200, 34, t);
        return true; }
    case L_LEAF: case L_LEAF2: case L_LEAF3: {   // angiosperm twigs: a main shoot with side shoots, real-sized leaves
        const SpriteSet* set = layer == L_LEAF ? &P->magn : layer == L_LEAF2 ? &P->laurel : &P->heart;
        if (!set->n) return false;
        // Cards are ~1.7 m: magnolia leaves ~0.3 m, laurel ~0.2 m, Cercidiphyllum ~0.15 m.
        float lo = layer == L_LEAF ? 80 : layer == L_LEAF2 ? 52 : 40, hi = layer == L_LEAF ? 110 : layer == L_LEAF2 ? 72 : 56;
        int shoots = layer == L_LEAF ? 4 : 6, per = layer == L_LEAF ? 5 : layer == L_LEAF2 ? 7 : 8;
        paint_stem(&c, 256, 0, 262, 480, 6, 2, 0.09f, 0.07f, 0.045f);
        for (int sh = 0; sh <= shoots; sh++) {
            float bx, by, ang, len;
            if (sh == shoots) { bx = 256; by = 0.45f * AT; ang = vrr(&r, -0.1f, 0.1f); len = 0.53f * AT; }   // leader
            else {
                float v = 0.12f + sh * (0.62f / shoots); bx = 256 + 6 * v; by = v * AT;
                ang = ((sh & 1) ? 1 : -1) * vrr(&r, 0.45f, 0.8f); len = vrr(&r, 150, 210) * (1 - 0.4f * v);
                paint_stem(&c, bx, by, bx + sinf(ang) * len, by + cosf(ang) * len, 3.5f, 1.2f, 0.09f, 0.07f, 0.045f);
            }
            for (int i = 0; i < per; i++) {
                float tt = 0.15f + i * (0.85f / per), side = (layer == L_LEAF3 ? (i >> 1) & 1 : i & 1) ? 1 : -1;
                float px = bx + sinf(ang) * len * tt, py = by + cosf(ang) * len * tt, Lp = vrr(&r, lo, hi) * (1 - 0.2f * tt);
                const Sprite* s = (layer == L_LEAF3 && (i & 2) && P->round.n) ? pick(&P->round, &r) : pick(set, &r);
                stamp(&c, s, px, py, ang + side * vrr(&r, 0.5f, 1.1f), Lp, side * Lp * s->aspect, t, vrr(&r, 0.82f, 1.15f));
            }
            const Sprite* s = pick(set, &r); float Lt = vrr(&r, lo, hi) * 0.9f;
            stamp(&c, s, bx + sinf(ang) * len, by + cosf(ang) * len, ang, Lt, Lt * s->aspect, t, 1.08f);
        }
        return true; }
    case L_PALM: {    // costapalmate fan: blade photos radiating from the hastula
        if (!P->blade.n) return false;
        paint_stem(&c, 256, 0, 256, 190, 10, 7, 0.07f, 0.08f, 0.04f);
        for (int i = 0; i < 26; i++) {   // segments with gaps between them, not a solid disc
            float a = -1.35f + i * (2.7f / 25) + vrr(&r, -0.03f, 0.03f), Lp = 300 * (0.75f + 0.25f * cosf(a)) * vrr(&r, 0.85f, 1.0f);
            const Sprite* s = pick(&P->blade, &r);
            stamp(&c, s, 256, 190, a, Lp, clampf(Lp * s->aspect * 2.2f, 16, 30) * (vr(&r) < 0.5f ? 1 : -1), t, vrr(&r, 0.85f, 1.12f));
        }
        return true; }
    case L_GRASS: case L_SEDGE: {   // tall grass / sedge tuft strip: blade photos fanning up from the base row
        // The ribbon (veg.c VEG_GRASS) is ~0.5 m wide and pinched to a third at
        // its base, so blades spread across the bottom converge into a tuft.
        if (!P->blade.n) return false;
        bool sedge = layer == L_SEDGE;
        static const float straw[3] = {0.15f, 0.12f, 0.055f};
        int nb = sedge ? 30 : 52;
        for (int i = 0; i < nb; i++) {
            float bx = vrr(&r, 36, 476), lean = (bx - 256) / 256.0f * (sedge ? 0.3f : 0.2f) + vrr(&r, -0.08f, 0.08f);
            const Sprite* s = pick(&P->blade, &r);
            float L = vrr(&r, sedge ? 330 : 250, 508);
            float W = clampf(L * s->aspect * (sedge ? 1.5f : 0.85f), sedge ? 13 : 7, sedge ? 28 : 15) * (vr(&r) < 0.5f ? 1 : -1);
            float dry = vr(&r) < (sedge ? 0.06f : 0.1f) ? vrr(&r, 0.25f, 0.7f) : 0.0f, tg[3];
            for (int k = 0; k < 3; k++) tg[k] = lerpf(t[k], straw[k], dry);
            stamp(&c, s, bx, 0, lean, L, W, tg, vrr(&r, 0.78f, 1.2f));
        }
        if (!sedge) for (int i = 0; i < 3; i++) {   // a few flowering stems with spikelets
            float bx = vrr(&r, 120, 392), top = vrr(&r, 440, 506), lean = vrr(&r, -18, 18);
            paint_stem(&c, bx, 0, bx + lean, top, 3.0f, 1.6f, 0.09f, 0.1f, 0.035f);
            for (float y = top - 70; y < top; y += 9) {
                float xx = bx + lean * y / top, side = ((int)y & 8) ? 1.0f : -1.0f;
                paint_leaf(&c, xx, y, side * 0.45f, 13, 5, 1, 0, 0.1f, 0.11f, 0.04f, 0.25f);
            }
            paint_leaf(&c, bx + lean, top - 4, 0, 15, 5, 1, 0, 0.11f, 0.115f, 0.045f, 0.25f);
        }
        return true; }
    case L_REED: {    // reed stand: upright stems with strap leaves, some carrying a brown spike
        if (!P->blade.n) return false;
        for (int st = 0; st < 7; st++) {
            float bx = 60 + st * 65 + vrr(&r, -18, 18), top = vrr(&r, 420, 508), lean = vrr(&r, -20, 20);
            paint_stem(&c, bx, 0, bx + lean, top, 7, 3, t[0] * 1.3f, t[1] * 1.1f, t[2]);
            for (int k = 0; k < 4; k++) {   // leaves sheathing the stem, arching out
                float y = vrr(&r, 20, top * 0.65f), side = (k & 1) ? 1.0f : -1.0f; const Sprite* s = pick(&P->blade, &r);
                float L = vrr(&r, 200, 360);
                stamp(&c, s, bx + lean * y / top, y, side * vrr(&r, 0.12f, 0.4f), fminf(L, 510 - y), side * clampf(L * s->aspect * 1.4f, 12, 24), t, vrr(&r, 0.8f, 1.15f));
            }
            if (vr(&r) < 0.6f) {   // cattail-like head and its bare tip
                float hy = top - vrr(&r, 70, 110), hx = bx + lean * hy / top;
                paint_stem(&c, hx, hy, bx + lean, top - 18, 15, 13, 0.11f, 0.062f, 0.032f);
                paint_stem(&c, bx + lean, top - 18, bx + lean, top, 3, 1.5f, 0.12f, 0.1f, 0.05f);
            }
        }
        return true; }
    case L_SHRUB: {   // twiggy scrub: boxwood-like sprigs and small leaves
        if (!P->sprig.n || !P->small.n) return false;
        for (int tw = 0; tw < 5; tw++) {
            float x0 = 150 + tw * 52 + vrr(&r, -20, 20), x1 = x0 + vrr(&r, -90, 90);
            paint_stem(&c, x0, 0, x1, 490, 5, 1.5f, 0.08f, 0.06f, 0.04f);
            for (int i = 0; i < 9; i++) {
                float v = 0.15f + i * 0.095f, side = (i & 1) ? 1 : -1, bx = lerpf(x0, x1, v), by = v * 490;
                bool sprig = vr(&r) < 0.55f; const Sprite* s = sprig ? pick(&P->sprig, &r) : pick(&P->small, &r);
                float Lp = (sprig ? vrr(&r, 110, 150) : vrr(&r, 55, 80)) * (1 - 0.25f * v);
                stamp(&c, s, bx, by, side * vrr(&r, 0.3f, 1.1f), Lp, side * Lp * s->aspect, t, vrr(&r, 0.8f, 1.15f));
            }
        }
        return true; }
    }
    return false;
}

// ---------------------------------------------------------------- skeleton-generator layers
// A photo leaf from set (the painted shape when the set is empty), base at
// (bx, by) along ang, L px long; width from the sprite's aspect, at most maxw.
static void leaf_px(Canvas* c, const SpriteSet* set, VRng* r, float bx, float by, float ang, float L, float wscale, float maxw, const float t[3], float tone, int shape) {
    if (set && set->n) {
        const Sprite* s = pick(set, r); float W = L * s->aspect * wscale; if (maxw > 0) W = fminf(W, maxw);
        stamp(c, s, bx, by, ang, L, vr(r) < 0.5f ? W : -W, t, tone);
    } else {
        float W = L * (shape == 2 ? 0.12f : 0.42f) * wscale; if (maxw > 0) W = fminf(W, maxw);
        paint_leaf(c, bx, by, ang, L, W, shape, 0, t[0] * tone * 1.6f, t[1] * tone * 1.6f, t[2] * tone * 1.6f, 0.15f);
    }
}
// Podocarp shoot: a thin stem from (x0, y0) along ang, len px, clothed in
// small narrow leaves (leaf px long, about 1:5) set in a tight spiral. Seen
// from the side a spiral leaf points left or right of the shoot by the sine of
// its phase and is foreshortened by the cosine; the ones on the far side are
// drawn under the stem, the near ones over it. Old leaves near the base are
// darker, the young ones at the tip shorter and lighter, as on Podocarpus and
// Prumnopitys, so a crown of these reads dense, dark and fine.
static void paint_podo_shoot(Canvas* c, const Photos* P, VRng* r, float x0, float y0, float ang, float len, float leaf, const float t[3]) {
    static const float tw[3] = {0.07f, 0.05f, 0.032f};
    enum { NMAX = 128 };
    float lx[NMAX], ly[NMAX], la[NMAX], ll[NMAX], lt[NMAX]; bool front[NMAX]; int n = 0;
    float dx = sinf(ang), dy = cosf(ang), step = leaf * 0.11f, ph = vrr(r, 0, 6.283f);
    for (float s = leaf * 0.08f; s < len && n < NMAX; s += step * vrr(r, 0.8f, 1.2f), n++) {
        float u = s / len; ph += 2.39996f;
        float side = sinf(ph), depth = cosf(ph);
        lx[n] = x0 + dx * s; ly[n] = y0 + dy * s;
        la[n] = ang + side * vrr(r, 0.6f, 0.85f) * (1 - 0.3f * u);                    // swept forward, tighter at the tip
        ll[n] = leaf * vrr(r, 0.85f, 1.1f) * (0.45f + 0.55f * fabsf(side)) * (1 - 0.45f * sstep(0.7f, 1.0f, u)) * (0.75f + 0.25f * sstep(0.0f, 0.2f, u));
        lt[n] = vrr(r, 0.85f, 1.05f) * lerpf(0.82f, 1.18f, sstep(0.55f, 1.0f, u));
        front[n] = depth > 0;
    }
    for (int pass = 0; pass < 2; pass++) {
        if (pass) paint_stem(c, x0, y0, x0 + dx * len, y0 + dy * len, fmaxf(leaf * 0.09f, 2.0f), 1.2f, tw[0], tw[1], tw[2]);
        for (int k = 0; k < n; k++) if (front[k] == pass) leaf_px(c, &P->strap, r, lx[k], ly[k], la[k], ll[k], 0.7f, ll[k] * 0.2f + 1.5f, t, lt[k], 2);
    }
    for (int k = -1; k <= 1; k += 2)   // the terminal bud's last pair
        leaf_px(c, &P->strap, r, x0 + dx * len * 0.97f, y0 + dy * len * 0.97f, ang + k * 0.25f, leaf * 0.5f, 0.7f, leaf * 0.12f + 1.5f, t, 1.2f, 2);
}
// Ginkgo leaf: a fan on a long petiole from (bx, by) along ang, R px from
// the petiole's end to the rim, with the shallow middle notch, fine veins
// forking out from the base and a slightly wavy rim.
static void paint_fan(Canvas* c, VRng* r, float bx, float by, float ang, float pet, float R, const float t[3], float tone) {
    float dx = sinf(ang), dy = cosf(ang), cx = bx + dx * pet, cy = by + dy * pet, half = vrr(r, 0.85f, 1.05f), notch = vrr(r, 0.12f, 0.3f);
    paint_stem(c, bx, by, cx, cy, 3, 2, t[0] * 0.9f, t[1] * 0.85f, t[2] * 0.7f);
    int x0 = (int)(cx - R - 2), x1 = (int)(cx + R + 2), y0 = (int)(cy - R - 2), y1 = (int)(cy + R + 2);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f - cx, py = y + 0.5f - cy, d = sqrtf(px * px + py * py);
        float a = atan2f(px * dy - py * dx, px * dx + py * dy);   // 0 along the petiole
        if (fabsf(a) > half) continue;
        float rim = R * (0.94f + 0.06f * cosf(a * 9.0f + 1.3f)) * (1 - notch * expf(-a * a * 90.0f));
        float cov = clampf(rim - d + 0.5f, 0, 1) * clampf((half - fabsf(a)) * d + 0.5f, 0, 1);
        if (cov <= 0) continue;
        float vein = 0.9f + 0.1f * cosf(a * 46.0f), k = tone * vein * (0.78f + 0.3f * d / R);
        blend(c, x, y, t[0] * k * 1.7f, t[1] * k * 1.55f, t[2] * k * 1.1f, cov);
    }
}
// One leaf cluster painted at full layer scale (base (256, 12), tip toward +y).
static void paint_cluster(int layer, Canvas* c, const Photos* P, VRng* r, const float* t) {
    const float tw[3] = {0.07f, 0.055f, 0.035f};   // twig bark
    switch (layer) {
    case L_CL_MAGN: {   // magnolia: leaves crowded in a rosette at the shoot tip
        float tx = 256 + vrr(r, -20, 20), ty = vrr(r, 130, 170);
        paint_stem(c, 256, 0, tx, ty, 10, 6, tw[0], tw[1], tw[2]);
        for (int k = 0; k < 2; k++) leaf_px(c, &P->magn, r, lerpf(256, tx, 0.5f), ty * 0.5f, (k ? 1 : -1) * vrr(r, 1.0f, 1.3f), vrr(r, 150, 190), 1, 0, t, vrr(r, 0.8f, 1.0f), 1);
        int n = 6 + (int)vrr(r, 0, 2);
        for (int k = 0; k < n; k++) {
            float a = -1.15f + 2.3f * (k + 0.5f) / n + vrr(r, -0.12f, 0.12f);
            leaf_px(c, &P->magn, r, tx, ty, a, vrr(r, 250, 320) * (1 - 0.22f * fabsf(a)), 1, 0, t, vrr(r, 0.82f, 1.15f), 1);
        }
        break; }
    case L_CL_LAUREL: {   // a shoot forking into three, alternate leaves
        for (int tw2 = 0; tw2 < 3; tw2++) {   // the outer shoots first, the leading one over them
            int o = tw2 == 2 ? 1 : tw2 * 2;
            float a = (o - 1) * 0.5f + vrr(r, -0.08f, 0.08f), len = o == 1 ? 480 : vrr(r, 330, 400);
            float sx = 256, sy = o == 1 ? 0 : vrr(r, 50, 90), ex = sx + sinf(a) * len, ey = sy + cosf(a) * len;
            paint_stem(c, sx, sy, ex, ey, 6, 2, tw[0], tw[1], tw[2]);
            for (int k = 0; k < 7; k++) {
                float v = 0.12f + 0.8f * k / 7, side = (k & 1) ? 1.0f : -1.0f, bx = lerpf(sx, ex, v), by = lerpf(sy, ey, v);
                leaf_px(c, &P->laurel, r, bx, by, a + side * vrr(r, 0.45f, 1.0f), vrr(r, 165, 215) * (1 - 0.25f * v), 1.15f, 0, t, vrr(r, 0.8f, 1.15f), 0);
            }
            leaf_px(c, &P->laurel, r, ex, ey - 0.08f * len, a, vrr(r, 130, 160), 1, 0, t, 1.05f, 0);
        }
        break; }
    case L_CL_PODO: {   // podocarp: a branchlet of densely leaved shoots, side shoots under the leader
        for (int k = 0; k < 6; k++) {
            float v = 0.12f + 0.12f * k, side = (k & 1) ? 1.0f : -1.0f;
            paint_podo_shoot(c, P, r, 256, v * 480, side * vrr(r, 0.55f, 0.8f), vrr(r, 280, 350) * (1 - 0.4f * v), 62, t);
        }
        paint_podo_shoot(c, P, r, 256, 4, vrr(r, -0.06f, 0.06f), 490, 70, t);
        break; }
    case L_CL_HEART: {  // katsura: two shoots of opposite pairs of round leaves
        for (int tw2 = 0; tw2 < 2; tw2++) {
            float a = (tw2 ? 0.28f : -0.28f) + vrr(r, -0.08f, 0.08f), ex = 256 + sinf(a) * 470, ey = cosf(a) * 470;
            paint_stem(c, 256, 0, ex, ey, 6, 2, tw[0], tw[1], tw[2]);
            for (int k = 0; k < 4; k++) {
                float v = 0.22f + 0.19f * k, bx = lerpf(256, ex, v), by = v * ey;
                for (int sd = -1; sd <= 1; sd += 2)
                    leaf_px(c, (k & 1) && P->round.n ? &P->round : &P->heart, r, bx, by, a + sd * vrr(r, 0.7f, 1.1f), vrr(r, 125, 165), 1, 0, t, vrr(r, 0.82f, 1.12f), 1);
            }
            leaf_px(c, &P->heart, r, ex, ey - 15, a + vrr(r, -0.2f, 0.2f), 130, 1, 0, t, 1.05f, 1);
        }
        break; }
    case L_CL_SPRAY: {  // metasequoia: a twig of three deciduous shoots of opposite needles
        for (int k = 0; k < 3; k++) {
            float a = (k - 1) * 0.42f + vrr(r, -0.06f, 0.06f), L = k == 1 ? 500 : vrr(r, 380, 440);
            if (P->spray.n) {
                const Sprite* sp = pick(&P->spray, r);
                stamp(c, sp, 256, 6, a, L, fminf(L * sp->aspect * 0.85f, 300), t, vrr(r, 0.9f, 1.1f));
            } else {
                float ex = 256 + sinf(a) * L, ey = 6 + cosf(a) * L;
                paint_stem(c, 256, 6, ex, ey, 4, 1.5f, tw[0], tw[1], tw[2]);
                for (float v = 0.05f; v < 0.95f; v += 0.035f) for (int sd = -1; sd <= 1; sd += 2)
                    paint_leaf(c, lerpf(256, ex, v), lerpf(6, ey, v), a + sd * 1.2f, 100 * (1 - 0.5f * v), 13, 2, 0, t[0] * 1.6f, t[1] * 1.6f, t[2] * 1.6f, 0.1f);
            }
        }
        break; }
    case L_CL_ROPE: {   // araucaria: a stout scale-leaf branchlet forking into ropes that curl up and out
        static const float base[3] = {0.026f, 0.066f, 0.028f}, tipc[3] = {0.06f, 0.115f, 0.03f};
        int n = 5 + (int)vrr(r, 0, 1.99f);
        for (int k = 0; k < n; k++) {   // side ropes first, the leading one over them
            int o = k == n - 1 ? n / 2 : (k < n / 2 ? k : k + 1);
            float u = (float)o / (n - 1) * 2 - 1, a = u * 0.75f + vrr(r, -0.08f, 0.08f);
            float y0 = o == n / 2 ? 6 : vrr(r, 40, 140), x0 = 256 + sinf(a) * y0 * 0.3f;
            paint_rope(c, x0, y0, a, -u * 0.35f, (o == n / 2 ? 490 : vrr(r, 300, 400)) - y0 * 0.3f, vrr(r, 36, 46), base, tipc, vrr(r, 0.85f, 1.15f));
        }
        break; }
    case L_CL_GINKGO: { // ginkgo: short spur shoots on a twig, each with a tuft of fans on long petioles
        float ex = 256 + vrr(r, -30, 30), ey = 470;
        paint_stem(c, 256, 0, ex, ey, 7, 4, tw[0] * 0.8f, tw[1] * 0.75f, tw[2] * 0.7f);
        for (int k = 0; k < 4; k++) {
            float v = 0.2f + 0.24f * k, sx = lerpf(256, ex, v), sy = v * ey, side = (k & 1) ? 1.0f : -1.0f;
            float px = sx + side * 18, py = sy + 10; paint_stem(c, sx, sy, px, py, 7, 6, tw[0] * 0.7f, tw[1] * 0.65f, tw[2] * 0.6f);
            int nf = 3 + (int)vrr(r, 0, 2.99f);
            for (int f = 0; f < nf; f++) paint_fan(c, r, px, py, side * 0.5f + (f - nf * 0.5f) * 0.42f + vrr(r, -0.1f, 0.1f), vrr(r, 50, 80), vrr(r, 62, 82), t, vrr(r, 0.85f, 1.15f));
        }
        break; }
    case L_CL_PLANE: {  // river plane: a shoot of big alternate palmate leaves on long stalks
        float a0 = vrr(r, -0.1f, 0.1f), ex = 256 + sinf(a0) * 470, ey = cosf(a0) * 470;
        paint_stem(c, 256, 0, ex, ey, 6, 2, tw[0], tw[1], tw[2]);
        for (int k = 0; k < 4; k++) {
            float v = 0.18f + 0.22f * k, side = (k & 1) ? 1.0f : -1.0f, bx = lerpf(256, ex, v), by = v * ey, a = a0 + side * vrr(r, 0.55f, 0.9f);
            float ps = vrr(r, 40, 60), lx = bx + sinf(a) * ps, ly = by + cosf(a) * ps;
            paint_stem(c, bx, by, lx, ly, 3, 2, tw[0] * 0.9f, tw[1] * 1.1f, tw[2] * 0.8f);
            leaf_px(c, P->palmate.n ? &P->palmate : &P->heart, r, lx, ly, a, vrr(r, 190, 240) * (1 - 0.18f * v), 1, 0, t, vrr(r, 0.85f, 1.12f), 1);
        }
        leaf_px(c, P->palmate.n ? &P->palmate : &P->heart, r, ex, ey - 40, a0, vrr(r, 170, 200), 1, 0, t, 1.0f, 1);
        break; }
    case L_CL_BERRY: {  // berry shrub: arching twigs of alternate serrate leaves
        for (int tw2 = 0; tw2 < 3; tw2++) {
            float a = (tw2 - 1) * 0.5f + vrr(r, -0.1f, 0.1f), ex = 256 + sinf(a) * 450, ey = 10 + cosf(a) * 450;
            paint_stem(c, 256, 0, ex, ey, 5, 1.5f, tw[0] * 1.2f, tw[1], tw[2] * 0.9f);
            for (int k = 0; k < 6; k++) {
                float v = 0.16f + 0.14f * k, side = (k & 1) ? 1.0f : -1.0f;
                leaf_px(c, P->serrate.n ? &P->serrate : &P->small, r, lerpf(256, ex, v), lerpf(10, ey, v), a + side * vrr(r, 0.5f, 0.95f), vrr(r, 120, 160) * (1 - 0.25f * v), 1, 0, t, vrr(r, 0.82f, 1.12f), 0);
            }
        }
        break; }
    case L_CL_SHRUB: {  // scrub: a few twiglets of sprigs and small leaves
        for (int tw2 = 0; tw2 < 3; tw2++) {
            float a = (tw2 - 1) * 0.45f + vrr(r, -0.1f, 0.1f), ex = 256 + sinf(a) * 430, ey = 12 + cosf(a) * 430;
            paint_stem(c, 256, 0, ex, ey, 5, 1.5f, tw[0], tw[1], tw[2]);
            for (int k = 0; k < 5; k++) {
                float v = 0.2f + 0.17f * k, side = (k & 1) ? 1.0f : -1.0f; bool sprig = vr(r) < 0.5f && P->sprig.n;
                leaf_px(c, sprig ? &P->sprig : &P->small, r, lerpf(256, ex, v), lerpf(0, ey, v), a + side * vrr(r, 0.4f, 1.0f),
                        (sprig ? vrr(r, 150, 200) : vrr(r, 90, 130)) * (1 - 0.2f * v), 1, 0, t, vrr(r, 0.8f, 1.15f), 1);
            }
        }
        break; }
    }
}
// Petals and bracts, one per column, base at the bottom: white and pink
// water-lily petals, a magnolia tepal, the herb's yellow and violet petals, a
// Williamsonia bract, a green sepal and a dried petal. Each is a pointed oval
// with veins running out from the base, a flush of colour at the base or tip,
// and a soft edge.
static void paint_petal(Canvas* c, VRng* r, int k, float cx, int cw) {
    static const float BASE[8][3] = {{0.86f, 0.85f, 0.74f}, {0.86f, 0.8f, 0.78f}, {0.82f, 0.78f, 0.66f}, {0.85f, 0.62f, 0.05f},
                                     {0.3f, 0.2f, 0.62f}, {0.62f, 0.6f, 0.3f}, {0.14f, 0.3f, 0.08f}, {0.42f, 0.28f, 0.14f}};
    static const float FLUSH[8][3] = {{0.75f, 0.68f, 0.2f}, {0.82f, 0.36f, 0.5f}, {0.62f, 0.3f, 0.45f}, {0.75f, 0.42f, 0.02f},
                                      {0.5f, 0.42f, 0.78f}, {0.6f, 0.22f, 0.14f}, {0.2f, 0.36f, 0.1f}, {0.3f, 0.18f, 0.1f}};
    static const float WID[8] = {0.36f, 0.36f, 0.42f, 0.62f, 0.5f, 0.3f, 0.34f, 0.4f}, AT_TIP[8] = {0, 1, 0, 0, 0, 1, 0, 1};
    float W = cw * 0.92f, L = 500;
    for (int y = 0; y < AT; y++) for (int x = (int)(cx - cw / 2); x < (int)(cx + cw / 2); x++) {
        float u = (y + 0.5f) / L, sx = (x + 0.5f - cx) / (W * 0.5f);
        if (u > 1) continue;
        float prof = powf(fmaxf(sinf(VPI * powf(u, 0.65f)), 0), 0.75f) * (1 - 0.15f * u), w = (0.75f + 0.35f * WID[k]) * prof;
        float cov = clampf((w - fabsf(sx)) * W * 0.5f + 0.5f, 0, 1);
        if (cov <= 0) continue;
        float fl = AT_TIP[k] > 0 ? sstep(0.45f, 1.0f, u) : 1 - sstep(0.0f, 0.35f, u);
        float vein = 0.93f + 0.07f * cosf(atan2f(sx * w, u + 0.05f) * 40.0f), edge = 0.9f + 0.1f * sstep(0.0f, 0.4f, w - fabsf(sx));
        float n = 0.94f + 0.12f * vfbm(x / 9.0f, y / 9.0f, 1000, 1000, 31u + k, 2);
        float rr = lerpf(BASE[k][0], FLUSH[k][0], fl * 0.8f), gg = lerpf(BASE[k][1], FLUSH[k][1], fl * 0.8f), bb = lerpf(BASE[k][2], FLUSH[k][2], fl * 0.8f);
        float s = vein * edge * n;
        blend(c, x, y, rr * s, gg * s, bb * s, cov);
    }
    (void)r;
}
// Fruit, seed and cone skins, one per column, wrapped round a ball (u round,
// v from the stalk to the tip): woody cone scales, red fleshy, blue-black with
// a waxy bloom, ginkgo yellow, cycad orange, palm purple-brown, unripe green,
// white berry. Opaque; the fruit's shape and light come from the mesh.
static void paint_skin(Canvas* c, VRng* r, int k, int x0, int cw) {
    static const float COL[8][3] = {{0.2f, 0.12f, 0.05f}, {0.5f, 0.03f, 0.03f}, {0.05f, 0.05f, 0.1f}, {0.6f, 0.42f, 0.06f},
                                    {0.62f, 0.2f, 0.03f}, {0.2f, 0.07f, 0.08f}, {0.2f, 0.32f, 0.06f}, {0.72f, 0.72f, 0.66f}};
    for (int y = 0; y < AT; y++) for (int x = x0; x < x0 + cw; x++) {
        float u = (x - x0 + 0.5f) / cw, v = (y + 0.5f) / AT, n = vfbm(x / 6.0f, y / 6.0f, 1000, 1000, 77u + k, 3);
        float kk = 0.85f + 0.3f * n, cr = COL[k][0], cg = COL[k][1], cb = COL[k][2];
        if (k == 0) {   // spiralled woody scales: diamond cells, each with a raised pale umbo and dark seams
            float a = u * 8 + v * 7, b = u * 8 - v * 7, fa = fabsf(fmodf(a + 100, 1.0f) - 0.5f), fb = fabsf(fmodf(b + 100, 1.0f) - 0.5f);
            float seam = sstep(0.36f, 0.48f, fmaxf(fa, fb)), umbo = 1 - sstep(0.05f, 0.18f, fmaxf(fa, fb));
            kk *= (1 - 0.65f * seam) * (1 + 0.6f * umbo);
            cr *= 1 + 0.4f * umbo; cg *= 1 + 0.25f * umbo;
        } else if (k == 2 || k == 3) {   // waxy bloom over dark skin, rubbed off in patches
            float bloom = sstep(0.35f, 0.7f, vfbm(x / 14.0f, y / 14.0f, 1000, 1000, 9u + k, 2));
            cr = lerpf(cr, k == 2 ? 0.22f : 0.68f, bloom * 0.6f); cg = lerpf(cg, k == 2 ? 0.25f : 0.55f, bloom * 0.6f); cb = lerpf(cb, k == 2 ? 0.35f : 0.25f, bloom * 0.6f);
        } else if (k == 5) {   // palm drupe: fine longitudinal fibres
            kk *= 0.88f + 0.12f * sinf(u * 2 * VPI * 18);
        }
        float pole = 1 - 0.35f * sstep(0.85f, 1.0f, v) - 0.2f * (1 - sstep(0.0f, 0.12f, v));   // darker scar at the tip and stalk end
        blend(c, x, y, cr * kk * pole, cg * kk * pole, cb * kk * pole, 1);
    }
    (void)r;
}
// Fungus surfaces, one per column: a bracket's zoned upper face (u across,
// v from the wood out to the rim), its cream pore face, a mushroom cap (v from
// the centre to the rim) and the pale stem and gills.
static void paint_fungus(Canvas* c, VRng* r, int k, int x0, int cw) {
    for (int y = 0; y < AT; y++) for (int x = x0; x < x0 + cw; x++) {
        float v = (y + 0.5f) / AT, n = vfbm(x / 5.0f, y / 5.0f, 1000, 1000, 501u + k, 3), cr, cg, cb;
        if (k == 0) {
            float zone = 0.5f + 0.5f * sinf(v * 2 * VPI * 7 + n * 2.0f), rim = sstep(0.8f, 1.0f, v);
            cr = lerpf(0.2f, 0.42f, zone); cg = lerpf(0.11f, 0.26f, zone); cb = lerpf(0.05f, 0.12f, zone);
            cr = lerpf(cr, 0.75f, rim); cg = lerpf(cg, 0.66f, rim); cb = lerpf(cb, 0.48f, rim);
        } else if (k == 1) { float pore = 0.9f + 0.1f * sinf(x * 1.7f) * sinf(y * 1.7f); cr = 0.74f * pore; cg = 0.68f * pore; cb = 0.52f * pore; }
        else if (k == 2) { float t = sstep(0.0f, 0.7f, v); cr = lerpf(0.3f, 0.62f, t); cg = lerpf(0.17f, 0.44f, t); cb = lerpf(0.08f, 0.26f, t); }
        else { float gill = 0.88f + 0.12f * sinf((x - x0) * 0.9f); cr = 0.78f * gill; cg = 0.74f * gill; cb = 0.62f * gill; }
        float kk = 0.85f + 0.3f * n;
        blend(c, x, y, cr * kk, cg * kk, cb * kk, 1);
    }
    (void)r;
}
// Clubmoss and moss, one per column, base at the bottom: two Selaginella
// sprays (flat, forking, clothed in tiny overlapping scale leaves), an
// upright Lycopodium shoot with its cone, and a cushion of moss shoots.
static void paint_club_branch(Canvas* c, VRng* r, float x, float y, float ang, float len, float w, int depth, const float t[3]) {
    float dx = sinf(ang), dy = cosf(ang);
    for (float s = 0; s < len; s += w * 0.6f) {
        float u = s / len, ww = w * (1 - 0.5f * u);
        for (int sd = -1; sd <= 1; sd += 2)
            paint_leaf(c, x + dx * s, y + dy * s, ang + sd * 0.9f, ww * 1.4f, ww * 0.7f, 0, 0, t[0] * 1.5f, t[1] * 1.6f * vrr(r, 0.9f, 1.1f), t[2] * 1.4f, 0.3f);
    }
    if (depth > 0) for (int sd = -1; sd <= 1; sd += 2) {
        float f = vrr(r, 0.35f, 0.6f);
        paint_club_branch(c, r, x + dx * len * f, y + dy * len * f, ang + sd * vrr(r, 0.35f, 0.6f), len * vrr(r, 0.5f, 0.65f), w * 0.8f, depth - 1, t);
    }
}
static void paint_club(Canvas* c, VRng* r, int k, float cx, int cw, const float t[3]) {
    if (k < 2) paint_club_branch(c, r, cx, 4, vrr(r, -0.05f, 0.05f), 440, cw * 0.11f, 3, t);
    else if (k == 2) {
        paint_club_branch(c, r, cx, 4, 0, 380, cw * 0.1f, 0, t);
        for (float y = 380; y < 500; y += 4) paint_leaf(c, cx, y, 0, 8, cw * 0.18f * (1 - 0.6f * sstep(470, 500, y)), 0, 0, 0.32f, 0.3f, 0.1f, 0.1f);   // the strobilus
    } else for (int s = 0; s < 26; s++) {   // moss: many short shoots of starry leaves
        float x = cx + vrr(r, -cw * 0.4f, cw * 0.4f), h = vrr(r, 200, 480);
        for (float y = 0; y < h; y += 9) paint_leaf(c, x, y, vrr(r, -1.2f, 1.2f), 14, 4, 0, 0, t[0] * 1.3f, t[1] * 1.5f * (0.7f + 0.5f * y / h), t[2] * 1.2f, 0.4f);
    }
}
// A lily pad: round, a slit from the middle to the rim, veins radiating from
// the middle, glossy dark green with a reddish rim and a few holes and
// browned patches. The pad's centre is the layer's centre.
static void paint_lily_pad(Canvas* c, VRng* r, const float t[3]) {
    float R = 246, slit = vrr(r, 0.05f, 0.08f);
    for (int y = 0; y < AT; y++) for (int x = 0; x < AT; x++) {
        float px = x + 0.5f - 256, py = y + 0.5f - 256, d = sqrtf(px * px + py * py), a = atan2f(px, -py);
        float rim = R * (0.97f + 0.03f * sinf(a * 7 + 1.0f));
        float cov = clampf(rim - d + 0.5f, 0, 1) * clampf((fabsf(a) - slit) * d * 0.5f + 0.5f, 0, 1);
        if (cov <= 0) continue;
        float vein = 1 - 0.12f * sstep(0.85f, 1.0f, cosf(a * 22)) * sstep(20, 60, d);
        float n = vfbm(x / 22.0f, y / 22.0f, 1000, 1000, 4242u, 3), brown = sstep(0.68f, 0.8f, n) * 0.7f, edge = sstep(R * 0.86f, R, d);
        float k = vein * (0.8f + 0.35f * n);
        float cr = t[0] * 1.5f * k, cg = t[1] * 1.45f * k, cb = t[2] * 1.4f * k;
        cr = lerpf(cr, 0.22f, edge * 0.7f); cg = lerpf(cg, 0.06f, edge * 0.5f); cb = lerpf(cb, 0.04f, edge * 0.5f);
        cr = lerpf(cr, 0.2f, brown); cg = lerpf(cg, 0.13f, brown); cb = lerpf(cb, 0.05f, brown);
        blend(c, x, y, cr, cg, cb, cov);
    }
}
// Fills the generator's layers (always succeeds: painted when photos are missing).
static void new_layer(int layer, Px* img, const Photos* P) {
    VRng r = {(uint32_t)layer * 7907u + 101};
    static const float T[L_COUNT][3] = {
        [L_CL_MAGN] = {0.045f, 0.095f, 0.03f}, [L_CL_LAUREL] = {0.032f, 0.075f, 0.026f}, [L_CL_HEART] = {0.048f, 0.1f, 0.034f},
        [L_CL_PODO] = {0.024f, 0.056f, 0.026f}, [L_CL_SPRAY] = {0.045f, 0.098f, 0.036f}, [L_CL_SHRUB] = {0.036f, 0.08f, 0.028f}, [L_CL_ROPE] = {0.03f, 0.072f, 0.034f},
        [L_LEAFLET] = {0.026f, 0.066f, 0.028f}, [L_PSEG] = {0.05f, 0.098f, 0.04f}, [L_FPINNA] = {0.038f, 0.086f, 0.027f},
        [L_ROPE] = {0.03f, 0.072f, 0.034f}, [L_SCALES] = {0.03f, 0.072f, 0.034f}, [L_HSTEM] = {0.05f, 0.09f, 0.03f},
        [L_CL_GINKGO] = {0.058f, 0.11f, 0.03f}, [L_CL_PLANE] = {0.05f, 0.1f, 0.034f}, [L_CL_BERRY] = {0.04f, 0.085f, 0.03f},
        [L_GUNNERA] = {0.04f, 0.09f, 0.03f}, [L_LILY] = {0.03f, 0.075f, 0.03f}, [L_CLUB] = {0.04f, 0.1f, 0.026f},
        [L_PETAL] = {0.8f, 0.8f, 0.75f}, [L_FRUIT] = {0.3f, 0.2f, 0.1f}, [L_FUNGUS] = {0.4f, 0.3f, 0.2f},
    };
    const float* t = T[layer];
    for (int i = 0; i < AT * AT; i++) img[i] = (Px){t[0], t[1], t[2], 0};
    Canvas c = {img};
    if (is_cluster_layer(layer)) {   // each cell painted at full size, then halved into its quarter
        Px* tmp = malloc(sizeof(Px) * AT * AT); Canvas tc = {tmp};
        for (int q = 0; q < 4; q++) {
            for (int i = 0; i < AT * AT; i++) tmp[i] = (Px){t[0], t[1], t[2], 0};
            paint_cluster(layer, &tc, P, &r, t);
            int ox = (q & 1) * (AT / 2), oy = (q >> 1) * (AT / 2);
            for (int y = 0; y < AT / 2; y++) for (int x = 0; x < AT / 2; x++) {
                Px a = tmp[2 * y * AT + 2 * x], b = tmp[2 * y * AT + 2 * x + 1], d = tmp[(2 * y + 1) * AT + 2 * x], e = tmp[(2 * y + 1) * AT + 2 * x + 1];
                float wa = a.a + 1e-4f, wb = b.a + 1e-4f, wd = d.a + 1e-4f, we = e.a + 1e-4f, ws = wa + wb + wd + we;
                Px o = {(a.r * wa + b.r * wb + d.r * wd + e.r * we) / ws, (a.g * wa + b.g * wb + d.g * wd + e.g * we) / ws,
                        (a.b * wa + b.b * wb + d.b * wd + e.b * we) / ws, (a.a + b.a + d.a + e.a) * 0.25f};
                if (x < 3 || y < 2 || x >= AT / 2 - 3 || y >= AT / 2 - 3) o.a = 0;   // a clear border: no bleeding into the next cell
                img[(oy + y) * AT + ox + x] = o;
            }
        }
        free(tmp);
        return;
    }
    int cols = layer_columns(layer);
    if (cols) {
        int cw = AT / cols;
        for (int k = 0; k < cols; k++) {
            float cx = k * cw + cw * 0.5f;
            if (layer == L_ROPE) {
                static const float base[3] = {0.026f, 0.066f, 0.028f}, tip[3] = {0.06f, 0.115f, 0.03f};
                paint_rope(&c, cx, 4, vrr(&r, -0.02f, 0.02f), vrr(&r, -0.06f, 0.06f), 500, vrr(&r, 24, 30), base, tip, vrr(&r, 0.85f, 1.15f));
            } else if (layer == L_FPINNA) {
                leaf_px(&c, &P->fern, &r, cx, 4, 0, 504, 1.0f, cw - 8, t, vrr(&r, 0.9f, 1.1f), 0);
            } else if (layer == L_PETAL) {
                paint_petal(&c, &r, k, cx, cw);
            } else if (layer == L_FRUIT) {
                paint_skin(&c, &r, k, k * cw, cw);
            } else if (layer == L_FUNGUS) {
                paint_fungus(&c, &r, k, k * cw, cw);
            } else if (layer == L_CLUB) {
                paint_club(&c, &r, k, cx, cw, t);
            } else {   // a single blade, kept inside its column
                leaf_px(&c, &P->blade, &r, cx, 4, vrr(&r, -0.01f, 0.01f), 504, layer == L_PSEG ? 2.6f : 2.0f, cw - 12, t, vrr(&r, 0.85f, 1.12f), 2);
            }
        }
        if (layer != L_FRUIT && layer != L_FUNGUS)   // skins and fungus surfaces wrap round a mesh: no gaps between columns
            for (int y = 0; y < AT; y++) for (int x = 0; x < AT; x++) { int lx = x % cw; if (lx < 2 || lx >= cw - 2) img[y * AT + x].a = 0; }
        return;
    }
    if (layer == L_GUNNERA) {   // one big lobed leaf, its stalk joining at the bottom centre
        if (P->lobed.n) { const Sprite* sp = &P->lobed.s[0]; stamp(&c, sp, 256, 6, 0, 500, fminf(500 * sp->aspect, 500), t, 1.0f); }
        else paint_leaf(&c, 256, 6, 0, 500, 460, 1, 1, t[0] * 1.6f, t[1] * 1.6f, t[2] * 1.6f, 0.1f);
        return;
    }
    if (layer == L_LILY) { paint_lily_pad(&c, &r, t); return; }
    if (layer == L_SCALES) {   // overlapping pointed scales, tiling both ways (rows drawn tip-ward first)
        for (int i = 0; i < AT * AT; i++) img[i] = (Px){t[0] * 0.55f, t[1] * 0.55f, t[2] * 0.55f, 1};
        for (int row = 23; row >= 0; row--) for (int k = 0; k < 9; k++) {
            float x = k * 64 + (row & 1) * 32 + vrr(&r, -5, 5), y = row * 21.33f + vrr(&r, -3, 3), tone = vrr(&r, 0.75f, 1.3f);
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
                paint_leaf(&c, x + dx * AT, y + dy * AT, vrr(&r, -0.1f, 0.1f), 50, 34, 0, 0, t[0] * tone * 1.4f, t[1] * tone * 1.4f, t[2] * tone * 1.3f, 0.35f);
        }
        for (int i = 0; i < AT * AT; i++) img[i].a = 1;
        return;
    }
    if (layer == L_HSTEM) {    // ridged green stem; a dark toothed sheath at each node (v = 0)
        for (int y = 0; y < AT; y++) for (int x = 0; x < AT; x++) {
            float u = (float)x / AT, v = (float)y / AT, rid = 0.82f + 0.18f * cosf(u * 2 * VPI * 10);
            float teeth = 0.06f + 0.025f * fabsf(sinf(u * 2 * VPI * 10 * 0.5f + 0.3f));
            float sheath = v < teeth ? 1.0f : 0.0f, ring = sstep(teeth, teeth + 0.02f, v) * (1 - sstep(teeth + 0.03f, teeth + 0.08f, v));
            float k = rid * (1 + 0.25f * ring) * (0.9f + 0.1f * v);
            Px o = {t[0] * k, t[1] * k, t[2] * k, 1};
            if (sheath > 0) o = (Px){0.025f, 0.022f, 0.015f, 1};
            img[y * AT + x] = o;
        }
        return;
    }
}

// Albedo grade for the leaf layers. Photo leaves are evenly lit and pale, so
// a frond read as one flat mint shape in sun. Per layer: stretch the
// leaf-to-leaf value contrast, darken toward the rachis and the frond base
// (deep blue-green) and warm the tips (yellow-green), with a few dry,
// yellow-brown tips in patches; then settle the layer's mean a little deeper
// than its photo target. Layers keep their base at y = 0 and the rachis at
// x = AT/2, except the palm fan, which radiates from its hastula.
static void grade_layer(int layer, Px* img) {
    if (is_bark_layer(layer) || layer == L_HORSE || layer == L_SCALES || layer == L_HSTEM || layer == L_ROPE || layer == L_CL_ROPE) return;
    if (layer == L_PETAL || layer == L_FRUIT || layer == L_FUNGUS || layer == L_LILY) return;   // painted to their final colours
    int cols = layer_columns(layer), cw = cols ? AT / cols : AT;
    bool fern = layer == L_FERN || layer == L_FERN2 || layer == L_TFERN;
    float gamma = fern ? 1.7f : 1.4f, core = fern ? 0.5f : 0.75f, base = fern ? 0.65f : 0.85f, dry_amt = fern ? 0.6f : 0.3f;
    float deep = fern ? 0.72f : 0.9f, warm = fern ? 0.82f : 0.92f;   // mean value; blue kept (less mint, more olive)
    double m0 = 0, n = 0;
    for (int i = 0; i < AT * AT; i++) if (img[i].a > 0.5f) { m0 += img[i].r * 0.3f + img[i].g * 0.59f + img[i].b * 0.11f; n++; }
    if (n < 1) return;
    float M = (float)(m0 / n);
    for (int y = 0; y < AT; y++) for (int x = 0; x < AT; x++) {
        Px* q = &img[y * AT + x]; if (q->a <= 0.02f) continue;
        float t, d;   // along the frond (base 0 .. tip 1), out from its axis (0 .. 1)
        if (layer == L_PALM) { float dx = x - 256.0f, dy = y - 190.0f; t = d = clampf(sqrtf(dx * dx + dy * dy) / 300.0f, 0, 1); }
        else if (is_cluster_layer(layer)) { t = (float)(y % (AT / 2)) / (AT / 2); d = clampf(fabsf(x % (AT / 2) - AT / 4.0f) / 110.0f, 0, 1); }
        else if (cols) { t = (float)y / AT; d = clampf(fabsf(x % cw - cw * 0.5f) / (cw * 0.5f), 0, 1); }
        else { t = (float)y / AT; d = clampf(fabsf(x - 256.0f) / 200.0f, 0, 1); }
        float l = q->r * 0.3f + q->g * 0.59f + q->b * 0.11f, rel = l / fmaxf(M, 1e-4f);
        float k = clampf(powf(fmaxf(rel, 1e-3f), gamma - 1.0f), 0.35f, 2.2f);   // value contrast around the layer mean
        k *= lerpf(core, 1.0f, sstep(0.04f, 0.5f, d)) * lerpf(base, 1.0f, sstep(0.0f, 0.45f, t));
        float tip = sstep(0.45f, 1.0f, fmaxf(t * 0.8f, d));
        float hr = lerpf(0.86f, 1.1f, tip), hg = 1.0f, hb = lerpf(0.98f, 0.74f, tip);   // deep blue-green .. yellow-green
        float r = q->r * k * hr, g = q->g * k * hg, b = q->b * k * hb * warm;
        // Dry tips: patches of pinnae browning at their ends.
        float dn = vfbm(x / 48.0f, y / 48.0f, AT / 48 + 1, AT / 48 + 1, 977u + (uint32_t)layer * 31u, 3);
        float dry = sstep(0.56f, 0.75f, dn) * sstep(0.6f, 0.95f, fmaxf(d, t)) * dry_amt;
        float lv = r * 0.3f + g * 0.59f + b * 0.11f;
        q->r = lerpf(r, lv * 1.6f, dry); q->g = lerpf(g, lv * 1.2f, dry); q->b = lerpf(b, lv * 0.45f, dry);
    }
    double m2 = 0;
    for (int i = 0; i < AT * AT; i++) if (img[i].a > 0.5f) m2 += img[i].r * 0.3f + img[i].g * 0.59f + img[i].b * 0.11f;
    float s = (float)(M * deep / fmaxf((float)(m2 / n), 1e-5f));
    for (int i = 0; i < AT * AT; i++) { img[i].r *= s; img[i].g *= s; img[i].b *= s; }
}

// Mips with alpha-coverage preservation, so alpha-tested foliage keeps its
// density in the distance instead of thinning out.
// The levels go into a texture cache payload (texcache.h), uploaded and
// stored under key.
static GLuint upload_atlas(Px** layers, int count, uint64_t key) {
    int skip = 0; while ((AT >> skip) > veg_pow2_env("CRET_VEG_ATLAS", 64, AT)) skip++;   // mips above the uploaded size
    int levels = 0; for (int s = AT >> skip; s >= 1; s >>= 1) levels++;
    size_t psize; uint8_t* pay = cret_tex_payload_alloc(TB_TEX_2D_ARRAY, GL_SRGB8_ALPHA8, AT >> skip, AT >> skip, count, levels, true, true, 4.0f, &psize);
    if (!pay) return 0;
    for (int k = 0; k < count; k++) {
        Px* cur = layers[k]; int s = AT; float cov0 = 0;
        for (int i = 0; i < AT * AT; i++) cov0 += cur[i].a > 0.5f;
        cov0 /= (float)(AT * AT);
        Px* owned = NULL;
        for (int l = 0; l < skip + levels; l++) {
            uint8_t* bytes = l < skip ? NULL : cret_tex_payload_level(pay, l - skip) + (size_t)k * s * s * 4;
            for (int i = 0; bytes && i < s * s; i++) {
                bytes[4 * i] = (uint8_t)(powf(clampf(cur[i].r, 0, 1), 1 / 2.2f) * 255 + 0.5f);
                bytes[4 * i + 1] = (uint8_t)(powf(clampf(cur[i].g, 0, 1), 1 / 2.2f) * 255 + 0.5f);
                bytes[4 * i + 2] = (uint8_t)(powf(clampf(cur[i].b, 0, 1), 1 / 2.2f) * 255 + 0.5f);
                bytes[4 * i + 3] = (uint8_t)(clampf(cur[i].a, 0, 1) * 255 + 0.5f);
            }
            if (s == 1) break;
            int h = s / 2; Px* nx = malloc(sizeof(Px) * h * h);
            for (int y = 0; y < h; y++) for (int x = 0; x < h; x++) {
                Px a = cur[2 * y * s + 2 * x], b = cur[2 * y * s + 2 * x + 1], cc = cur[(2 * y + 1) * s + 2 * x], d = cur[(2 * y + 1) * s + 2 * x + 1];
                float wa = a.a + 1e-3f, wb = b.a + 1e-3f, wc = cc.a + 1e-3f, wd = d.a + 1e-3f, ws = wa + wb + wc + wd;
                nx[y * h + x] = (Px){(a.r * wa + b.r * wb + cc.r * wc + d.r * wd) / ws, (a.g * wa + b.g * wb + cc.g * wc + d.g * wd) / ws,
                                     (a.b * wa + b.b * wb + cc.b * wc + d.b * wd) / ws, (a.a + b.a + cc.a + d.a) * 0.25f};
            }
            if (!is_bark_layer(k)) {
                float lo = 0.5f, hi = 8.0f;
                for (int it = 0; it < 14; it++) {
                    float sc = 0.5f * (lo + hi), cv = 0;
                    for (int i = 0; i < h * h; i++) cv += nx[i].a * sc > 0.5f;
                    if (cv / (h * h) < cov0) lo = sc; else hi = sc;
                }
                for (int i = 0; i < h * h; i++) nx[i].a = clampf(nx[i].a * 0.5f * (lo + hi), 0, 1);
            }
            free(owned); owned = nx; cur = nx; s = h;
        }
        free(owned);
    }
    return cret_tex_from_payload(getenv("CRET_DUMP_ATLAS") ? NULL : "veg_atlas", key, pay, psize);
}

// ---------------------------------------------------------------- mesh building
#include "veg_gen.h"

// Uploads a mesh: the shadow-casting triangles first, then the view-only ones (twigs).
typedef struct { float pos_scale[4], rot[4], extra[4]; } GpuInst;   // extra: fade (far impostors: mesh-range factor), impostor layer, radius, height
// Attributes 4-6 of a plant VAO are per-instance (GpuInst). They are enabled
// with their divisor once, here; each draw then only points them at its list
// (bind_instances). The impostor bake turns them off for its single plants
// and back on after.
static void instance_layout(GLuint vao, bool on) {
    glBindVertexArray(vao);
    for (int k = 4; k < 7; k++) { if (on) glEnableVertexAttribArray(k); else glDisableVertexAttribArray(k); glVertexAttribDivisor(k, on ? 1 : 0); }
}

static GfxMesh upload_mesh(const MB* m, int* shadow_count) {
    GfxMesh g = {0};
    glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(VVert) * m->nv, m->v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_BYTE, GL_TRUE, sizeof(VVert), (void*)20);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(VVert), (void*)24);
    glEnableVertexAttribArray(7); glVertexAttribPointer(7, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VVert), (void*)28);
    instance_layout(g.vao, true);
    uint32_t* idx = malloc(sizeof(uint32_t) * (m->ni + m->nfi + 1));
    if (m->ni) memcpy(idx, m->i, sizeof(uint32_t) * m->ni);
    if (m->nfi) memcpy(idx + m->ni, m->fi, sizeof(uint32_t) * m->nfi);
    glGenBuffers(1, &g.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * (m->ni + m->nfi), idx, GL_STATIC_DRAW);
    free(idx);
    glBindVertexArray(0); g.index_count = m->ni + m->nfi; *shadow_count = m->ni;
    return g;
}

// ---------------------------------------------------------------- instances
// Points a VAO's instance attributes (instance_layout) at a list in vbo.
static void bind_instances(GLuint vao, GLuint vbo, size_t offset) {
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (int k = 0; k < 3; k++) glVertexAttribPointer(4 + k, 4, GL_FLOAT, GL_FALSE, sizeof(GpuInst), (void*)(offset + 16 * k));
}

// is_tree, SPACING, SCALE_LO/HI, DENSITY and the scatter live in veg_place.h (GL-free, shared with world.c).
// Switch to the reduced mesh at tree_lod_dist times this (the smaller tall kinds switch earlier).
static const float TREE_LOD[VEG_KINDS] = {1.0f, 1.0f, 0.6f, 1.0f, 0.8f, 1.0f, 0.9f, 1.0f, 1.0f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f};
// A tree's switch distances (reduced mesh, impostor) scale with its size on
// screen: a plant taller than its species' mean keeps each level further out,
// a smaller one leaves it sooner, as the square root of the height ratio
// within 0.85-1.15 (the mean over a stand stays about 1, and so does the
// cost). The fixed metres let a 35 m conifer turn into an impostor at the
// same distance as a 20 m one, where its larger silhouette shows the swap.
#define TREE_SWITCH_MAX 1.15f
// The group a plant's mean height is taken over: its species, or its kind
// for kinds without species (standing dead trees). Per species, so a short
// species (the stout tree fern) does not push its kind's tall one's meshes out.
static int height_group(const VegInstance* in) { int sp = flora_species(in->kind, in->variant); return sp >= 0 ? sp : FL_COUNT + in->kind; }
static float tree_switch_scale(const CretVeg* v, const VegInstance* in) {
    float h = v->mesh[in->kind][in->variant].height * in->scale, m = v->mean_height[height_group(in)];   // m: the species' mean over the island
    return m > 0 ? clampf(sqrtf(h / m), 0.85f, TREE_SWITCH_MAX) : 1.0f;
}
// Ground cover range per kind relative to the preset's ground_range: scrub
// further out; fallen food and fungi only where a player could see them.
static const float VEG_RANGE[VEG_KINDS] = {1, 1, 1, 1, 1, 1, 1, 1.4f, 1, 1, 1, 0.8f, 0.3f, 0.4f};
// How far ground cover bends under wind and animals (veg.vs): 0 rigid .. 1 grass.
static const float BEND[VEG_KINDS] = {0, 0, 0, 0.75f, 0, 0.6f, 0, 0.3f, 1.0f, 1.0f, 0, 0.1f, 0, 0};

// ---------------------------------------------------------------- impostors
// Orthographic view of the tree from azimuth `az` (camera sits along
// back = (cos az, 0, sin az)); x spans [-R, R], y spans [0, H].
static void ortho_view(float az, float R, float H, float* o) {
    float bx = cosf(az), bz = sinf(az), rx = bz, rz = -bx;   // right = forward x up
    memset(o, 0, sizeof(float) * 16);
    o[0] = rx / R; o[8] = rz / R;                 // x_ndc = dot(right, p) / R
    o[5] = 2 / H; o[13] = -1;                     // y_ndc = 2 y / H - 1
    o[2] = -bx / R; o[10] = -bz / R;              // z_ndc = -dot(back, p) / R
    o[15] = 1;
}

// Albedo at IMP, normal and AO at IMP / 2 (smooth anyway; the 3 GB card
// holds 40 tall variants x 8 views): the bake renders both at IMP and the
// normal is blitted down into its layer.
static void bake_impostors(CretVeg* v) {
    int layers = 0;
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < VEG_VARIANTS; q++) {
        bool on = is_tree(k) && q < veg_nvar(k) && v->mesh[k][q].m.vao;
        v->imp_layer[k][q] = on ? layers : -1; if (on) layers += VEG_IMPOSTOR_VIEWS;
    }
    GLuint tex[2]; glGenTextures(2, tex);
    for (int t = 0; t < 2; t++) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex[t]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, t == 0 ? GL_SRGB8_ALPHA8 : GL_RGBA8, t ? IMP / 2 : IMP, t ? IMP / 2 : IMP, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    GLuint fbo, depth, nfull, blit; glGenFramebuffers(1, &fbo); glGenRenderbuffers(1, &depth); glGenFramebuffers(1, &blit);
    glGenTextures(1, &nfull); glBindTexture(GL_TEXTURE_2D, nfull);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, IMP, IMP, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, IMP, IMP);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, nfull, 0);
    GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1}; glDrawBuffers(2, bufs);
    glViewport(0, 0, IMP, IMP); glEnable(GL_FRAMEBUFFER_SRGB);   // albedo target is sRGB: encode on write
    glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    GfxProgram* p = &v->mesh_prog; glUseProgram(p->id);
    gfx_set1i(p, "u_bake", 1); gfx_set2f(p, "u_depth", 0, 0);
    gfx_bind_tex(p, "t_atlas", 0, GL_TEXTURE_2D_ARRAY, v->t_atlas);
    // Samplers of different types may not share a unit, even if unused.
    static const char* other[5] = {"t_trans", "t_skyview", "t_horizon0", "t_horizon1", "t_weather"};
    for (int i = 0; i < 5; i++) gfx_set1i(p, other[i], 1 + i);
    gfx_set1i(p, "t_csm", 7); gfx_set1i(p, "t_misc", 8); gfx_set1i(p, "t_trample", 9);
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < VEG_VARIANTS; q++) {
        if (v->imp_layer[k][q] < 0) continue;
        VegMesh* vm = &v->mesh[k][q];
        instance_layout(vm->m.vao, false);
        glVertexAttrib4f(4, 0, 0, 0, 1); glVertexAttrib4f(5, 1, 0, 0, 0); glVertexAttrib4f(6, 1, -1, 0, 0);
        for (int view = 0; view < VEG_IMPOSTOR_VIEWS; view++) {
            int layer = v->imp_layer[k][q] + view;
            glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, IMP, IMP);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex[0], 0, layer);
            // Both targets clear to zero, so every texel is premultiplied by its
            // coverage (the alpha test leaves 0 or 1): the half-size blit and the
            // mips then average only what the crown covers, and veg_imp.fs
            // divides by the albedo alpha of the same footprint. Clearing to a
            // leaf green, an up-facing normal and open AO (as before) averaged
            // those into the gaps of distant crowns, which drew impostors
            // lighter and greener than the meshes they replace.
            static const float c_zero[4] = {0, 0, 0, 0};
            glClearBufferfv(GL_COLOR, 0, c_zero); glClearBufferfv(GL_COLOR, 1, c_zero); glClear(GL_DEPTH_BUFFER_BIT);
            float vp[16]; ortho_view(view * 2 * VPI / VEG_IMPOSTOR_VIEWS, vm->radius, vm->height, vp);
            gfx_setmat4(p, "u_viewproj", vp);
            glDrawElements(GL_TRIANGLES, vm->m.index_count, GL_UNSIGNED_INT, 0);
            // Normal and AO down to their half-size layer.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo); glReadBuffer(GL_COLOR_ATTACHMENT1);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blit); glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex[1], 0, layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glBlitFramebuffer(0, 0, IMP, IMP, 0, 0, IMP / 2, IMP / 2, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo); glDrawBuffers(2, bufs);
        }
        instance_layout(vm->m.vao, true);
    }
    glBindVertexArray(0);
    gfx_set1i(p, "u_bake", 0);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fbo); glDeleteFramebuffers(1, &blit); glDeleteRenderbuffers(1, &depth); glDeleteTextures(1, &nfull);
    for (int t = 0; t < 2; t++) { glBindTexture(GL_TEXTURE_2D_ARRAY, tex[t]); glGenerateMipmap(GL_TEXTURE_2D_ARRAY); }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    v->t_imp_albedo = tex[0]; v->t_imp_normal = tex[1];
    if (getenv("CRET_DUMP_IMP")) {            // debug: all impostor layers, albedo with alpha as a grid
        uint8_t* all = malloc((size_t)IMP * IMP * 4 * layers);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex[0]); glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, all);
        FILE* fp = fopen(getenv("CRET_DUMP_IMP"), "wb");
        int cols = VEG_IMPOSTOR_VIEWS, rows = layers / cols, S2 = IMP / 2;
        fprintf(fp, "P6 %d %d 255\n", cols * S2, rows * S2);
        for (int y = rows * S2 - 1; y >= 0; y--) for (int x = 0; x < cols * S2; x++) {
            int L = (y / S2) * cols + x / S2; const uint8_t* q = all + ((size_t)L * IMP * IMP + (size_t)(y % S2) * 2 * IMP + (x % S2) * 2) * 4;
            float a = q[3] / 255.0f; uint8_t rgb[3]; for (int c = 0; c < 3; c++) rgb[c] = (uint8_t)(q[c] * a + 200 * (1 - a));
            fwrite(rgb, 1, 3, fp);
        }
        fclose(fp); free(all); glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
}

// ---------------------------------------------------------------- init
static void load_programs(CretVeg* v) {
    gfx_program_load(&v->mesh_prog, SHADER_DIR "veg.vs", SHADER_DIR "veg.fs");
    gfx_program_load(&v->imp_prog, SHADER_DIR "veg_imp.vs", SHADER_DIR "veg_imp.fs");
    gfx_program_load(&v->shadow_prog, SHADER_DIR "veg.vs", SHADER_DIR "veg_shadow.fs");
    gfx_program_load(&v->imp_shadow_prog, SHADER_DIR "veg_imp_shadow.vs", SHADER_DIR "veg_imp_shadow.fs");
    gfx_program_load(&v->depth_prog, SHADER_DIR "veg.vs", SHADER_DIR "veg_depth.fs");
    gfx_program_load(&v->imp_depth_prog, SHADER_DIR "veg_imp.vs", SHADER_DIR "veg_imp_depth.fs");
}
void cret_veg_reload_shaders(CretVeg* v) {
    v->mesh_prog.vs_mtime = 0; gfx_program_reload_if_changed(&v->mesh_prog);
    v->imp_prog.vs_mtime = 0; gfx_program_reload_if_changed(&v->imp_prog);
    v->shadow_prog.vs_mtime = 0; gfx_program_reload_if_changed(&v->shadow_prog);
    v->imp_shadow_prog.vs_mtime = 0; gfx_program_reload_if_changed(&v->imp_shadow_prog);
    v->depth_prog.vs_mtime = 0; gfx_program_reload_if_changed(&v->depth_prog);
    v->imp_depth_prog.vs_mtime = 0; gfx_program_reload_if_changed(&v->imp_depth_prog);
    if (v->trample) cret_trample_reload(v->trample);
}

// Atlas cache key: the painting code (this file and veg.h; the build time
// when the source is not beside the binary), every foliage photo and the
// layer size and count.
static int cmp_cstr(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }
static uint64_t atlas_key(void) {
    uint64_t h = cret_hash_str(cret_hash_u64(CRET_HASH_SEED, CRET_TEX_CACHE_VERSION), "veg atlas v1");
    h = cret_hash_u64(cret_hash_u64(h, AT), L_COUNT);
    int up = veg_pow2_env("CRET_VEG_ATLAS", 64, AT); if (up < AT) h = cret_hash_u64(h, (uint64_t)up);   // the full size keeps its old key
    uint64_t src = cret_hash_file(cret_hash_file(CRET_HASH_SEED, "ocean/cretaceous/render/veg.c"), "ocean/cretaceous/render/veg.h");
    if (!FileExists("ocean/cretaceous/render/veg.c")) src = cret_hash_str(src, __DATE__ " " __TIME__);
    h = cret_hash_u64(h, src);
    char* names[512]; int n = 0; const char* dirs[2] = {FOLIAGE_DIR, FOLIAGE_DIR "sprites/"};
    for (int d = 0; d < 2; d++) {
        DIR* dp = opendir(dirs[d]); if (!dp) continue; struct dirent* e;
        while ((e = readdir(dp)) && n < 512) {
            size_t L = strlen(e->d_name); if (L < 5 || strcmp(e->d_name + L - 4, ".png")) continue;
            char full[600]; snprintf(full, sizeof full, "%s%s", dirs[d], e->d_name); names[n++] = strdup(full);
        }
        closedir(dp);
    }
    qsort(names, n, sizeof(char*), cmp_cstr);
    h = cret_hash_files(h, (const char* const*)names, n);
    for (int i = 0; i < n; i++) free(names[i]);
    return h;
}

bool cret_veg_init(CretVeg* v, const TerraMap* map, float world_size) {
    memset(v, 0, sizeof(*v)); v->map = map; v->world_size = world_size;
    v->near_range = 150.0f; v->far_range = 4500.0f; v->ground_range = 90.0f; v->density = 1.0f;
    v->carpet_range = getenv("CRET_CARPET") ? (float)atof(getenv("CRET_CARPET")) : 34.0f;   // 0 disables the sward
    if (getenv("CRET_VEG_NEAR")) v->near_range = (float)atof(getenv("CRET_VEG_NEAR"));
    // Tall grass out to half the ground-cover range (45 m on high); CRET_GRASS=0
    // turns it and all bending off, CRET_GRASS_RANGE sets the high-preset range (m).
    v->grass_on = !getenv("CRET_GRASS") || atoi(getenv("CRET_GRASS")) != 0;
    v->grass_frac = getenv("CRET_GRASS_RANGE") ? (float)atof(getenv("CRET_GRASS_RANGE")) / v->ground_range : 0.5f;
    v->grass_cx = v->grass_cz = 1 << 30;
    load_programs(v);
    if (!v->mesh_prog.id || !v->imp_prog.id) { fprintf(stderr, "cretaceous: vegetation shaders failed\n"); return false; }
    // Atlas.
    // The atlas (painted and photo layers with coverage-kept mips) is cached
    // GPU-ready under a key over this file's source, the photos and the size
    // (atlas_key), so editing the painting code or the photos rebuilds it.
    uint64_t akey = atlas_key();
    v->t_atlas = getenv("CRET_DUMP_ATLAS") ? 0 : cret_tex_cache_load("veg_atlas", akey);
    if (v->t_atlas) CRET_LOAD_MARK("veg: atlas from cache");
    else {   // (body kept at its old indentation: a small diff for the parallel veg.c work)
    Px* layers[L_COUNT]; Photos photos; photos_load(&photos); int nphoto = 0; CRET_LOAD_MARK("veg: programs and photo sprites");
    // Painted bark is one neutral layer; species tones it when photos are missing.
    static const float BARK_TINT[4][3] = {{0.95f, 0.82f, 0.68f}, {0.95f, 0.75f, 0.6f}, {1.05f, 1.0f, 0.92f}, {0.45f, 0.33f, 0.22f}};
    #pragma omp parallel for schedule(dynamic, 1) reduction(+:nphoto)
    for (int k = 0; k < L_COUNT; k++) {
        layers[k] = malloc(sizeof(Px) * AT * AT);
        if (k >= L_CL_MAGN) { new_layer(k, layers[k], &photos); grade_layer(k, layers[k]); continue; }
        if (photo_layer(k, layers[k], &photos)) { grade_layer(k, layers[k]); nphoto++; continue; }
        paint_layer(FALLBACK[k], layers[k]);
        if (is_bark_layer(k)) {
            const float* tn = BARK_TINT[k == L_BARK ? 0 : k == L_BARK_CON ? 1 : k == L_BARK_PALM ? 2 : 3];
            for (int i = 0; i < AT * AT; i++) { layers[k][i].r *= tn[0]; layers[k][i].g *= tn[1]; layers[k][i].b *= tn[2]; }
        }
    }
    photos_free(&photos); CRET_LOAD_MARK("veg: atlas layers composited");
    fprintf(stderr, "cretaceous: foliage atlas %d/%d layers from photos\n", nphoto, L_COUNT - 1);
    if (getenv("CRET_DUMP_ATLAS")) {     // debug: atlas layers as a PPM strip
        FILE* fp = fopen(getenv("CRET_DUMP_ATLAS"), "wb");
        if (fp) {
            int W = AT / 2 * L_COUNT, H = AT / 2; fprintf(fp, "P6 %d %d 255\n", W, H);
            for (int y = H - 1; y >= 0; y--) for (int x = 0; x < W; x++) {
                Px q = layers[x / (AT / 2)][(y * 2) * AT + (x % (AT / 2)) * 2];
                float bgc = ((x / 8 + y / 8) & 1) ? 0.9f : 0.6f;
                unsigned char rgb[3] = {(unsigned char)(255 * powf(lerpf(bgc, q.r, q.a), 1 / 2.2f)), (unsigned char)(255 * powf(lerpf(bgc, q.g, q.a), 1 / 2.2f)), (unsigned char)(255 * powf(lerpf(bgc, q.b, q.a), 1 / 2.2f))};
                fwrite(rgb, 1, 3, fp);
            }
            fclose(fp);
        }
    }
    v->t_atlas = upload_atlas(layers, L_COUNT, akey);
    for (int k = 0; k < L_COUNT; k++) free(layers[k]);
    CRET_LOAD_MARK("veg: atlas mips, upload and store");
    }
    // Meshes: every kind x variant, full and reduced, generated on the CPU in
    // parallel (veg_gen.h), then uploaded.
    int tris = 0, nmb = VEG_KINDS * VEG_VARIANTS * 2;
    MB* mbs = calloc(nmb, sizeof(MB));
    #pragma omp parallel for schedule(dynamic, 1)
    for (int j = 0; j < nmb; j++) {
        int k = j / (2 * VEG_VARIANTS), q = (j / 2) % VEG_VARIANTS;
        if (q < veg_nvar(k)) build_species(&mbs[j], k, q, j & 1);
    }
    for (int j = 0; j < nmb; j++) {
        int k = j / (2 * VEG_VARIANTS), q = (j / 2) % VEG_VARIANTS, lod = j & 1; MB* mb = &mbs[j];
        if (q < veg_nvar(k)) {
            VegMesh* vm = lod ? &v->mesh_lo[k][q] : &v->mesh[k][q]; int sc = 0;
            *vm = (VegMesh){upload_mesh(mb, &sc), fmaxf(mb->rad, 0.5f) * 1.08f, mb->height * 1.04f + 0.2f, (mb->ni + mb->nfi) / 3, sc, {0, 0, 0, 0}};
            for (int i = 0, best = -1; i < mb->nv; i++) {   // the showcase point: the outermost, highest fruit or petal vertex
                const VVert* q = &mb->v[i]; if (q->a[0] != L_FRUIT && q->a[0] != L_PETAL) continue;
                float r = sqrtf(q->p[0] * q->p[0] + q->p[2] * q->p[2]) + 0.6f * q->p[1];   // outermost and highest: the seed cones, not the pollen cones
                if (best < 0 || r > vm->show[3] - 1) { best = i; vm->show[0] = q->p[0]; vm->show[1] = q->p[1]; vm->show[2] = q->p[2]; vm->show[3] = r + 1; }
            }
            if (vm->show[3] > 0) vm->show[3] = 1;
            if (!lod) tris += vm->tris;
        }
        free(mb->v); free(mb->i); free(mb->fi);
    }
    free(mbs);
    v->lod_dist = getenv("CRET_VEG_LOD") ? (float)atof(getenv("CRET_VEG_LOD")) : 25.0f;
    // Full tree meshes (twigs, curled leaf clusters) out to ~32 m on high, the reduced mesh beyond.
    v->tree_lod_dist = getenv("CRET_VEG_TREE_LOD") ? (float)atof(getenv("CRET_VEG_TREE_LOD")) : 32.0f;
    CRET_LOAD_MARK("veg: species meshes");
    // Impostors are baked every start (about 0.5 s): caching them needs a GPU
    // readback, which broke later allocations on Mesa's d3d12 driver (WSL).
    bake_impostors(v); CRET_LOAD_MARK("veg: impostors");
    // Static scatter of tall plants per tile.
    v->tiles_n = (int)ceilf(world_size / VEG_TILE); int T = v->tiles_n;
    v->tiles = calloc((size_t)T * T, sizeof(VegTile));
    long total = 0;
    #pragma omp parallel for schedule(dynamic, 4) reduction(+:total)
    for (int t = 0; t < T * T; t++) {
        int tx = t % T, tz = t / T; VegInstance buf[2048]; int n = 0;
        for (int k = 0; k < VEG_KINDS; k++) if (is_tree(k)) n += scatter(map, k, tx, tz, buf + n, 2048 - n);
        VegTile* tl = &v->tiles[t]; tl->ntrees = n; tl->ymin = 1e9f; tl->ymax = -1e9f;
        if (n) { tl->trees = malloc(sizeof(VegInstance) * n); memcpy(tl->trees, buf, sizeof(VegInstance) * n); }
        for (int i = 0; i < n; i++) { tl->ymin = fminf(tl->ymin, buf[i].y); tl->ymax = fmaxf(tl->ymax, buf[i].y + v->mesh[buf[i].kind][buf[i].variant].height * buf[i].scale); }
        total += n;
    }
    // Each tall species' mean height over every scattered plant, so the switch
    // scale (tree_switch_scale) averages about 1 over the island.
    double hsum[FL_COUNT + VEG_KINDS] = {0}; long hn[FL_COUNT + VEG_KINDS] = {0};
    for (int t = 0; t < T * T; t++) for (int i = 0; i < v->tiles[t].ntrees; i++) {
        const VegInstance* in = &v->tiles[t].trees[i]; int g = height_group(in); hsum[g] += v->mesh[in->kind][in->variant].height * in->scale; hn[g]++;
    }
    for (int g = 0; g < FL_COUNT + VEG_KINDS; g++) v->mean_height[g] = hn[g] ? (float)(hsum[g] / hn[g]) : 0;
    CRET_LOAD_MARK("veg: tree scatter");
    fprintf(stderr, "cretaceous: vegetation %ld trees, %d mesh tris over %d species variants\n", total, tris, VEG_KINDS * VEG_VARIANTS);
    if (getenv("CRET_VEG_DEBUG")) {
        static const char* nm[VEG_KINDS] = {"conifer", "broadleaf", "cycad", "fern", "treefern", "horsetail", "palm", "shrub", "carpet", "grass", "snag", "lily", "drop", "fungus"};
        for (int k = 0; k < VEG_KINDS; k++) {
            fprintf(stderr, "  %-9s tris", nm[k]); for (int q = 0; q < VEG_VARIANTS; q++) fprintf(stderr, " %d", v->mesh[k][q].tris);
            fprintf(stderr, "  lod"); for (int q = 0; q < VEG_VARIANTS; q++) fprintf(stderr, " %d", v->mesh_lo[k][q].tris); fprintf(stderr, "\n");
        }
    }
    glGenBuffers(1, &v->inst_vbo); glGenBuffers(CSM_MAX, v->shadow_vbo);
    // Impostor quad: corners in (x: -1..1, y: 0..1).
    float quad[8] = {-1, 0, 1, 0, -1, 1, 1, 1};
    glGenVertexArrays(1, &v->quad_vao); glBindVertexArray(v->quad_vao);
    glGenBuffers(1, &v->quad_vbo); glBindBuffer(GL_ARRAY_BUFFER, v->quad_vbo); glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    instance_layout(v->quad_vao, true);
    glBindVertexArray(0);
    v->ground_cx = v->ground_cz = 1 << 30;
    v->trample = calloc(1, sizeof(CretTrample));
    bool trample_on = v->grass_on && (!getenv("CRET_TRAMPLE") || atoi(getenv("CRET_TRAMPLE")) != 0);
    if (trample_on && !cret_trample_init(v->trample, SHADER_DIR, 256, 0.4f)) fprintf(stderr, "cretaceous: trample map unavailable\n");
    return true;
}

void cret_veg_close(CretVeg* v) {
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < VEG_VARIANTS; q++) { gfx_mesh_free(&v->mesh[k][q].m); if (v->mesh_lo[k][q].m.vao) gfx_mesh_free(&v->mesh_lo[k][q].m); }
    GLuint t[3] = {v->t_atlas, v->t_imp_albedo, v->t_imp_normal}; glDeleteTextures(3, t);
    glDeleteBuffers(1, &v->inst_vbo); glDeleteBuffers(1, &v->quad_vbo); glDeleteVertexArrays(1, &v->quad_vao);
    glDeleteBuffers(CSM_MAX, v->shadow_vbo);
    gfx_program_free(&v->mesh_prog); gfx_program_free(&v->imp_prog); gfx_program_free(&v->shadow_prog); gfx_program_free(&v->imp_shadow_prog);
    gfx_program_free(&v->depth_prog); gfx_program_free(&v->imp_depth_prog);
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) free(v->lists[k][q].a);
    free(v->imp_block); free(v->imp_run); glDeleteBuffers(1, &v->imp_vbo);
    for (int c = 0; c < CSM_MAX; c++) {
        for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) free(v->shadow_lists[c][k][q].a);
        free(v->shadow_cards[c].a);
    }
    for (int i = 0; i < v->tiles_n * v->tiles_n; i++) free(v->tiles[i].trees);
    free(v->tiles); free(v->ground); free(v->scratch); free(v->carpet); free(v->grass); free(v->lineup);
    if (v->trample) { cret_trample_close(v->trample); free(v->trample); }
    memset(v, 0, sizeof(*v));
}

// ---------------------------------------------------------------- per frame
typedef struct { float p[6][4]; } VFrustum;
static VFrustum vfrustum(const float* m) {
    VFrustum f; float r[4][4];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r[i][j] = m[j * 4 + i];
    for (int j = 0; j < 4; j++) {
        f.p[0][j] = r[3][j] + r[0][j]; f.p[1][j] = r[3][j] - r[0][j]; f.p[2][j] = r[3][j] + r[1][j];
        f.p[3][j] = r[3][j] - r[1][j]; f.p[4][j] = r[3][j] + r[2][j]; f.p[5][j] = r[3][j] - r[2][j];
    }
    return f;
}
static bool vbox(const VFrustum* f, float x0, float y0, float z0, float x1, float y1, float z1) {
    for (int i = 0; i < 6; i++) {
        const float* p = f->p[i];
        if (p[0] * (p[0] > 0 ? x1 : x0) + p[1] * (p[1] > 0 ? y1 : y0) + p[2] * (p[2] > 0 ? z1 : z0) + p[3] < 0) return false;
    }
    return true;
}

typedef struct VegList InstList;   // veg.h: a holds GpuInst
static void push(InstList* l, const VegInstance* in, float fade, float layer, float rad, float hgt) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 1024; l->a = realloc(l->a, sizeof(GpuInst) * l->cap); }
    ((GpuInst*)l->a)[l->n++] = (GpuInst){{in->x, in->y, in->z, in->scale}, {in->cos_yaw, in->sin_yaw, in->hue, in->phase}, {fade, layer, rad, hgt}};
}

void cret_veg_set_clearings(CretVeg* v, const float* xzr, int n) {
    v->nclearing = n < 0 ? 0 : n > VEG_CLEARINGS ? VEG_CLEARINGS : n;
    memcpy(v->clearing, xzr, sizeof(float) * 3 * v->nclearing);
}
// A plant at (x, z) whose foot spreads `pad` metres stands in a clearing (not drawn).
static inline bool cleared(const CretVeg* v, float x, float z, float pad) {
    for (int k = 0; k < v->nclearing; k++) {
        float dx = x - v->clearing[k][0], dz = z - v->clearing[k][1], r = v->clearing[k][2] + pad;
        if (dx * dx + dz * dz < r * r) return true;
    }
    return false;
}

// Ground height where small things lie: the drawn B-spline surface when
// the view handed its coefficients over (cret_veg_init's caller), else the
// bilinear one.
static float ground_y(const CretVeg* v, float x, float z) {
    return v->hcoef ? terra_bspline_height(v->hcoef, v->map->n, v->map->cell, x, z) : terra_height(v->map, x, z);
}
static void push_ground(CretVeg* v, VegInstance in) {
    in.cos_yaw = cosf(in.yaw); in.sin_yaw = sinf(in.yaw);
    if (v->nground == v->ground_cap) { v->ground_cap = v->ground_cap ? v->ground_cap * 2 : 4096; v->ground = realloc(v->ground, sizeof(VegInstance) * v->ground_cap); }
    v->ground[v->nground++] = in;
}
// Water lilies on quiet, shallow river or lake water within the lily range:
// a 1.6 m lattice, colonies from a 20 m noise, where the water is 0.25-1.6 m
// deep and its surface nearly level (slow water, not a riffle).
static void place_lilies(CretVeg* v, float cx, float cz) {
    const TerraMap* m = v->map; float R = v->ground_range * VEG_RANGE[VEG_LILY] + 16, sp = SPACING[VEG_LILY];
    int i0 = (int)floorf((cx - R) / sp), i1 = (int)ceilf((cx + R) / sp), j0 = (int)floorf((cz - R) / sp), j1 = (int)ceilf((cz + R) / sp);
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        VRng r = {hashu((uint32_t)i * 2246822519u ^ (uint32_t)j * 3266489917u ^ 0x1111u)};
        float x = (i + vr(&r)) * sp, z = (j + vr(&r)) * sp;
        if (x < 4 || z < 4 || x >= v->world_size - 4 || z >= v->world_size - 4) continue;
        int gx = (int)(x / m->cell + 0.5f), gz = (int)(z / m->cell + 0.5f); int wk = m->water_kind[(size_t)gz * m->n + gx];
        if (wk != TERRA_WATER_RIVER && wk != TERRA_WATER_LAKE) continue;
        if (fl_noise(m->seed * 0x9e3779b1u + 0x11u, x, z, 20.0f) < 0.56f || vr(&r) > 0.75f) continue;
        float w = terra_water(m, x, z), depth = w - terra_height(m, x, z);
        if (depth < 0.25f || depth > 1.6f) continue;
        float gw = fmaxf(fabsf(terra_water(m, x + 3, z) - terra_water(m, x - 3, z)), fabsf(terra_water(m, x, z + 3) - terra_water(m, x, z - 3))) / 6.0f;
        if (gw > 0.012f) continue;
        float u = vr(&r); int var = u < 0.55f ? 0 : u < 0.75f ? 1 : u < 0.9f ? 2 : 3;
        push_ground(v, (VegInstance){x, w, z, vrr(&r, SCALE_LO[VEG_LILY], SCALE_HI[VEG_LILY]), vrr(&r, 0, 2 * VPI), vrr(&r, -1, 1), vrr(&r, 0, 6.283f), VEG_LILY, (uint16_t)var, 0, 0});
    }
}
// Fallen fruit, seeds and cones under the plants that bear them (render/
// flora.h FLORA[].drop), within the fallen-food range: each plant drops about
// drop_n items in clusters of a few, inside three quarters of its crown,
// the same ones every time (hashed from the plant's position).
static void drop_under(CretVeg* v, const VegInstance* in, float cx, float cz, float R) {
    int sp = flora_species(in->kind, in->variant); if (sp < 0 || FLORA[sp].drop < 0) return;
    float dx = in->x - cx, dz = in->z - cz; if (dx * dx + dz * dz > R * R) return;
    VRng r = {hashu((uint32_t)(int)(in->x * 16) * 2654435761u ^ (uint32_t)(int)(in->z * 16) * 40503u ^ 0xd209u)};
    float crown = v->mesh[in->kind][in->variant].radius * in->scale * 0.75f;
    int n = (int)(FLORA[sp].drop_n / 3.0f * in->scale + vr(&r));
    for (int k = 0; k < n; k++) {
        float a = vrr(&r, 0, 2 * VPI), d = crown * sqrtf(vr(&r)) + 0.3f, x = in->x + cosf(a) * d, z = in->z + sinf(a) * d;
        if (terra_water(v->map, x, z) > terra_height(v->map, x, z) + 0.02f) continue;
        push_ground(v, (VegInstance){x, ground_y(v, x, z), z, vrr(&r, SCALE_LO[VEG_DROP], SCALE_HI[VEG_DROP]), vrr(&r, 0, 2 * VPI), 0, vrr(&r, 0, 6.283f), VEG_DROP, (uint16_t)FLORA[sp].drop, 0, 0});
    }
}
static void place_drops(CretVeg* v, float cx, float cz) {
    float R = v->ground_range * VEG_RANGE[VEG_DROP] + 16; int T = v->tiles_n;
    int t0x = (int)floorf((cx - R) / VEG_TILE), t1x = (int)floorf((cx + R) / VEG_TILE), t0z = (int)floorf((cz - R) / VEG_TILE), t1z = (int)floorf((cz + R) / VEG_TILE);
    for (int tz = t0z < 0 ? 0 : t0z; tz <= t1z && tz < T; tz++) for (int tx = t0x < 0 ? 0 : t0x; tx <= t1x && tx < T; tx++) {
        const VegTile* tl = &v->tiles[tz * T + tx];
        for (int i = 0; i < tl->ntrees; i++) drop_under(v, &tl->trees[i], cx, cz, R);
    }
    int n = v->nground;   // the shrubs just scattered (berry shrubs); drop_under appends past them
    for (int i = 0; i < n; i++) if (v->ground[i].kind == VEG_SHRUB) { VegInstance s = v->ground[i]; drop_under(v, &s, cx, cz, R); }
}
// Fungi on the dead wood near the camera (cret_veg_set_deadwood): bracket
// shelves on stumps, facing out, and mushrooms at the feet of stumps and
// beside logs, from each piece's own hash.
void cret_veg_set_deadwood(CretVeg* v, const float* d, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n * 6; i++) { uint32_t b; memcpy(&b, &d[i], 4); h = (h ^ b) * 16777619u; }
    if (h == v->deadwood_hash) return;
    v->deadwood_hash = h; v->nfungi = 0;
    for (int i = 0; i < n; i++) {
        const float* p = d + 6 * i; float x = p[0], y = p[1], z = p[2], rad = p[3], hgt = p[4]; bool stump = p[5] < 0.5f;
        VRng r = {hashu((uint32_t)(int)(x * 8) * 2654435761u ^ (uint32_t)(int)(z * 8) * 40503u ^ 0xf0f0u)};
        if (vr(&r) > (stump ? 0.8f : 0.6f)) continue;   // not every piece has fungi yet
        int nb = stump ? 1 + (int)vrr(&r, 0, 2.5f) : 0, nm = 1 + (int)vrr(&r, 0, 2.2f);
        for (int k = 0; k < nb + nm && v->nfungi < VEG_FUNGI; k++) {
            float a = vrr(&r, 0, 2 * VPI); bool br = k < nb; VegInstance in;
            if (br) {   // a bracket on the stump's side, its +x facing out of the wood
                float tr = rad * 0.5f, hy = hgt * vrr(&r, 0.25f, 0.7f);
                in = (VegInstance){x + cosf(a) * tr, y + hy, z + sinf(a) * tr, vrr(&r, 0.8f, 1.3f), -a, 0, vrr(&r, 0, 6.283f), VEG_FUNGUS, (uint16_t)(vr(&r) < 0.65f ? 0 : 1), 0, 0};
            } else {    // mushrooms on the ground at its edge
                float dd = rad * vrr(&r, stump ? 0.6f : 0.3f, stump ? 0.9f : 0.6f), fx = x + cosf(a) * dd, fz = z + sinf(a) * dd;
                in = (VegInstance){fx, ground_y(v, fx, fz), fz, vrr(&r, 0.8f, 1.25f), vrr(&r, 0, 2 * VPI), 0, vrr(&r, 0, 6.283f), VEG_FUNGUS, (uint16_t)(vr(&r) < 0.7f ? 2 : 3), 0, 0};
            }
            in.cos_yaw = cosf(in.yaw); in.sin_yaw = sinf(in.yaw);
            v->fungi[v->nfungi++] = in;
        }
    }
}
static void refresh_ground(CretVeg* v, float cx, float cz) {
    int gx = (int)floorf(cx / 16.0f), gz = (int)floorf(cz / 16.0f);
    if (v->ground_valid && gx == v->ground_cx && gz == v->ground_cz) return;
    v->ground_cx = gx; v->ground_cz = gz; v->ground_valid = true; v->nground = 0;
    float R = v->ground_range + 16; int t0x = (int)floorf((cx - R) / VEG_TILE), t1x = (int)floorf((cx + R) / VEG_TILE);
    int t0z = (int)floorf((cz - R) / VEG_TILE), t1z = (int)floorf((cz + R) / VEG_TILE);
    static const int GROUND[3] = {VEG_FERN, VEG_HORSETAIL, VEG_SHRUB};
    VegInstance* buf = malloc(sizeof(VegInstance) * 4096);
    for (int tz = t0z; tz <= t1z; tz++) for (int tx = t0x; tx <= t1x; tx++) {
        if (tx < 0 || tz < 0 || tx >= v->tiles_n || tz >= v->tiles_n) continue;
        for (int g = 0; g < 3; g++) {
            int n = scatter(v->map, GROUND[g], tx, tz, buf, 4096);
            if (v->nground + n > v->ground_cap) { v->ground_cap = (v->nground + n) * 2; v->ground = realloc(v->ground, sizeof(VegInstance) * v->ground_cap); }
            memcpy(v->ground + v->nground, buf, sizeof(VegInstance) * n); v->nground += n;
        }
    }
    free(buf);
    place_lilies(v, cx, cz);
    place_drops(v, cx, cz);
    if (v->lineup_r > 0) {   // review lineup: its rows replace the scattered cover around it
        int k = 0;
        for (int i = 0; i < v->nground; i++) {
            float dx = v->ground[i].x - v->lineup_c[0], dz = v->ground[i].z - v->lineup_c[1];
            if (dx * dx + dz * dz > v->lineup_r * v->lineup_r) v->ground[k++] = v->ground[i];
        }
        v->nground = k;
        if (v->nground + v->nlineup > v->ground_cap) { v->ground_cap = v->nground + v->nlineup + 64; v->ground = realloc(v->ground, sizeof(VegInstance) * v->ground_cap); }
        memcpy(v->ground + v->nground, v->lineup, sizeof(VegInstance) * v->nlineup); v->nground += v->nlineup;
    }
    if (getenv("CRET_VEG_DEBUG")) {
        int cnt[VEG_KINDS] = {0}; for (int i = 0; i < v->nground; i++) cnt[v->ground[i].kind]++;
        fprintf(stderr, "veg ground: fern %d horsetail %d shrub %d lily %d fallen food %d\n", cnt[VEG_FERN], cnt[VEG_HORSETAIL], cnt[VEG_SHRUB], cnt[VEG_LILY], cnt[VEG_DROP]);
    }
}

// Sward density from the surface splat: living ground cover (fern, moss,
// some under forest litter), none on rock, scree, sand, mud or bare soil.
static float carpet_density(const TerraMap* m, float x, float z) {
    float gx = clampf(x / m->cell, 0, m->n - 1.001f), gz = clampf(z / m->cell, 0, m->n - 1.001f);
    int ix = (int)gx, iz = (int)gz; float fx = gx - ix, fz = gz - iz; float d[4];
    for (int c = 0; c < 4; c++) {
        const uint8_t* s = m->splat + ((size_t)(iz + (c >> 1)) * m->n + ix + (c & 1)) * TERRA_MAT_COUNT;
        d[c] = (s[TERRA_MAT_FERN] + 0.8f * s[TERRA_MAT_MOSS] + 0.35f * s[TERRA_MAT_FOREST]) / 255.0f;
    }
    return lerpf(lerpf(d[0], d[1], fx), lerpf(d[2], d[3], fx), fz);
}
// Share of the sward that is clubmoss or moss: moss ground and forest
// litter (damp shade), from the splat.
static float carpet_moss(const TerraMap* m, float x, float z) {
    int gx = (int)clampf(x / m->cell + 0.5f, 0, m->n - 1), gz = (int)clampf(z / m->cell + 0.5f, 0, m->n - 1);
    const uint8_t* s = m->splat + ((size_t)gz * m->n + gx) * TERRA_MAT_COUNT;
    return clampf((1.1f * s[TERRA_MAT_MOSS] + 0.45f * s[TERRA_MAT_FOREST]) / 255.0f, 0, 0.75f);
}
static void refresh_carpet(CretVeg* v, float cx, float cz) {
    if (v->carpet_range <= 0) { v->ncarpet = 0; return; }
    int gx = (int)floorf(cx / 6.0f), gz = (int)floorf(cz / 6.0f);
    if (v->carpet_valid && gx == v->carpet_cx && gz == v->carpet_cz) return;
    v->carpet_cx = gx; v->carpet_cz = gz; v->carpet_valid = true; v->ncarpet = 0;
    const TerraMap* m = v->map; const float sp = SPACING[VEG_CARPET] / sqrtf(v->density), R = v->carpet_range + 8.0f;
    int i0 = (int)floorf((cx - R) / sp), i1 = (int)ceilf((cx + R) / sp), j0 = (int)floorf((cz - R) / sp), j1 = (int)ceilf((cz + R) / sp);
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        float x = (i + 0.5f) * sp - cx, z = (j + 0.5f) * sp - cz; if (x * x + z * z > R * R) continue;
        VRng r = {hashu((uint32_t)i * 2654435761u ^ (uint32_t)j * 40503u ^ 0x5bd1e995u)};
        x = (i + vr(&r)) * sp; z = (j + vr(&r)) * sp;
        if (x < 0 || z < 0 || x >= v->world_size || z >= v->world_size) continue;
        float d = carpet_density(m, x, z);
        if (vr(&r) > d * 1.5f) continue;
        float y; if (!placeable(m, x, z, 1.5f, -1.0f, &y)) continue;
        if (v->ncarpet == v->carpet_cap) { v->carpet_cap = v->carpet_cap ? v->carpet_cap * 2 : 16384; v->carpet = realloc(v->carpet, sizeof(VegInstance) * v->carpet_cap); }
        // Stands: a 9 m cell picks fern- or forb-rich sward; on moss and under
        // litter (damp shade) clubmoss mats and moss cushions take part of it.
        uint32_t cell = hashu((uint32_t)(int)floorf(x / 9.0f) * 73856093u ^ (uint32_t)(int)floorf(z / 9.0f) * 19349663u);
        int var = vr(&r) < 0.7f ? (int)(cell % 4) : (int)(vr(&r) * 3.99f);
        if (vr(&r) < carpet_moss(m, x, z)) var = (cell >> 8) & 1 ? 4 : 5;
        VegInstance in = {x, y + 0.1f, z, vrr(&r, SCALE_LO[VEG_CARPET], SCALE_HI[VEG_CARPET]) * (0.75f + 0.5f * d), vrr(&r, 0, 2 * VPI), vrr(&r, -1, 1), vrr(&r, 0, 6.283f), VEG_CARPET, (uint16_t)var, 0, 0};
        in.cos_yaw = cosf(in.yaw); in.sin_yaw = sinf(in.yaw);
        v->carpet[v->ncarpet++] = in;
    }
    if (getenv("CRET_VEG_DEBUG")) fprintf(stderr, "veg carpet: %d tufts\n", v->ncarpet);
}

// ---------------------------------------------------------------- tall grass
// Where tall grass, sedge and reeds grow: open living ground (fern prairie,
// soil, mud, some moss and litter), never rock, scree or sand; thinned
// under a closed canopy (clearings keep it); thickest at water's edges.
// A ~5 m clump noise inside ~40 m meadow patches makes tussocks and swathes
// instead of an even lawn. wet: 0 dry .. 1 bank or lake edge; shade: canopy.
#define GRASS_FLOOR 0.2f   // density between clumps, relative
#define GRASS_DICE 2.4f    // clumps per lattice cell at density 1 (capped at 1)
// Tussocks: two octaves on rotated axes, so clumps are not lattice-aligned blobs.
static float grass_clump(float x, float z) {
    float u = 0.83f * x - 0.56f * z, w = 0.56f * x + 0.83f * z;
    float n = 0.68f * vnoise(u / 6.0f + 500.0f, w / 6.0f + 500.0f, 1 << 20, 1 << 20, 0x6a09e667u)
            + 0.32f * vnoise(w / 2.3f + 900.0f, u / 2.3f + 900.0f, 1 << 20, 1 << 20, 0x3c6ef372u);
    return sstep(0.36f, 0.68f, n);
}
static float grass_density(const TerraMap* m, float x, float z, float* wet, float* shade) {
    float gx = clampf(x / m->cell, 0, m->n - 1.001f), gz = clampf(z / m->cell, 0, m->n - 1.001f);
    int ix = (int)gx, iz = (int)gz; float fx = gx - ix, fz = gz - iz, sp[TERRA_MAT_COUNT];
    for (int k = 0; k < TERRA_MAT_COUNT; k++) {
        const uint8_t* s = m->splat + ((size_t)iz * m->n + ix) * TERRA_MAT_COUNT + k;
        sp[k] = lerpf(lerpf(s[0], s[TERRA_MAT_COUNT], fx), lerpf(s[(size_t)m->n * TERRA_MAT_COUNT], s[(size_t)(m->n + 1) * TERRA_MAT_COUNT], fx), fz) / 255.0f;
    }
    float open = sp[TERRA_MAT_FERN] + 0.55f * sp[TERRA_MAT_DIRT] + 0.75f * sp[TERRA_MAT_MUD] + 0.35f * sp[TERRA_MAT_MOSS] + 0.3f * sp[TERRA_MAT_FOREST];
    open *= 1.0f - clampf((sp[TERRA_MAT_ROCK] + sp[TERRA_MAT_SCREE] + sp[TERRA_MAT_SAND]) * 1.6f, 0, 1);
    float canopy = veg_density(m, VEG_CONIFER, x, z) + veg_density(m, VEG_BROADLEAF, x, z) + 0.6f * veg_density(m, VEG_TREEFERN, x, z);
    *shade = sstep(0.55f, 1.25f, canopy);   // most of the island is open woodland (0.6..1): grass between the trees
    // Lake or river within ~10 m (the sea shore is beach).
    float edge = 0;
    for (int k = 0; k < 16; k++) {
        float a = k * (VPI / 4) + (k >= 8 ? VPI / 8 : 0), r = k >= 8 ? 10.0f : 5.0f;
        int qx = (int)((x + cosf(a) * r) / m->cell + 0.5f), qz = (int)((z + sinf(a) * r) / m->cell + 0.5f);
        if (qx < 0 || qz < 0 || qx >= m->n || qz >= m->n) continue;
        int wk = m->water_kind[(size_t)qz * m->n + qx];
        if (wk == TERRA_WATER_LAKE || wk == TERRA_WATER_RIVER) edge = r < 6 ? 1.0f : fmaxf(edge, 0.6f);
    }
    *wet = clampf(1.4f * sp[TERRA_MAT_MUD] + 0.8f * veg_density(m, VEG_HORSETAIL, x, z) + edge, 0, 1);
    open = fmaxf(open, 0.9f * *wet * (1.0f - clampf((sp[TERRA_MAT_ROCK] + sp[TERRA_MAT_SAND]) * 2.0f, 0, 1)));
    float clump = grass_clump(x, z);
    float patch = sstep(0.3f, 0.7f, vnoise(x / 40.0f, z / 40.0f, 1 << 20, 1 << 20, 0xbb67ae85u));
    return open * (1.0f - 0.85f * *shade * (1.0f - 0.6f * *wet)) * fminf(0.3f + 0.7f * patch + 0.5f * *wet, 1.0f) * (GRASS_FLOOR + (1.0f - GRASS_FLOOR) * clump);
}
float cret_veg_grass_density(const CretVeg* v, float x, float z, float* wet) {
    float w, sh; if (!wet) wet = &w;
    if (x < 0 || z < 0 || x >= v->world_size || z >= v->world_size) { *wet = 0; return 0; }
    return grass_density(v->map, x, z, wet, &sh);
}
static float grass_range(const CretVeg* v) { return v->grass_on ? v->ground_range * v->grass_frac : 0.0f; }
static float grass_lod(const CretVeg* v) { return v->lod_dist * 0.6f; }
static void refresh_grass(CretVeg* v, float cx, float cz) {
    float R = grass_range(v) + 10.0f;
    if (R <= 10.0f) { v->ngrass = 0; return; }
    int gx = (int)floorf(cx / 8.0f), gz = (int)floorf(cz / 8.0f);
    if (v->grass_valid && gx == v->grass_cx && gz == v->grass_cz && R <= v->grass_scatter_r) return;
    double t0 = GetTime();
    v->grass_cx = gx; v->grass_cz = gz; v->grass_valid = true; v->ngrass = 0; v->grass_scatter_r = R;
    const TerraMap* m = v->map; const float sp = SPACING[VEG_GRASS] / sqrtf(v->density);
    int i0 = (int)floorf((cx - R) / sp), i1 = (int)ceilf((cx + R) / sp), j0 = (int)floorf((cz - R) / sp), j1 = (int)ceilf((cz + R) / sp);
    int W = i1 - i0 + 1, H = j1 - j0 + 1;
    // One slot per lattice cell, filled in parallel rows, then compacted.
    if (W * H > v->scratch_cap) { v->scratch_cap = W * H; v->scratch = realloc(v->scratch, sizeof(VegInstance) * v->scratch_cap); }
    VegInstance* cell = v->scratch;
    #pragma omp parallel for schedule(dynamic, 8)
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        VegInstance* out = &cell[(j - j0) * W + (i - i0)]; out->kind = VEG_KINDS;
        float x = (i + 0.5f) * sp - cx, z = (j + 0.5f) * sp - cz; if (x * x + z * z > R * R) continue;
        VRng r = {hashu((uint32_t)i * 2246822519u ^ (uint32_t)j * 3266489917u ^ 0x27d4eb2fu)};
        x = (i + vr(&r)) * sp; z = (j + vr(&r)) * sp;
        if (x < 0 || z < 0 || x >= v->world_size || z >= v->world_size) continue;
        if (v->lineup_r > 0 && (x - v->lineup_c[0]) * (x - v->lineup_c[0]) + (z - v->lineup_c[1]) * (z - v->lineup_c[1]) < v->lineup_r * v->lineup_r) continue;
        float dice = vr(&r);   // density <= the clump factor: most cells between clumps stop here
        if (dice > GRASS_DICE * (GRASS_FLOOR + (1.0f - GRASS_FLOOR) * grass_clump(x, z))) continue;
        float wet, shade, d = grass_density(m, x, z, &wet, &shade);
        if (dice > d * GRASS_DICE) continue;
        bool reeds = wet > 0.55f && vr(&r) < 0.6f, sedge = !reeds && wet > 0.25f && vr(&r) < 0.6f;
        float y; if (!placeable(m, x, z, 0.75f, reeds ? 0.15f : -1.0f, &y)) continue;
        int var = reeds ? 3 : sedge ? 2 : (d > 0.45f && shade < 0.3f && vr(&r) < 0.6f) ? 1 : vr(&r) < 0.08f ? 2 : 0;
        float sc = vrr(&r, SCALE_LO[VEG_GRASS], SCALE_HI[VEG_GRASS]) * (0.8f + 0.3f * fminf(d, 1.0f)) * (1.0f - 0.3f * shade);
        VegInstance in = {x, y + 0.1f, z, sc, vrr(&r, 0, 2 * VPI), vrr(&r, -1, 1), vrr(&r, 0, 6.283f), VEG_GRASS, (uint16_t)var, 0, 0};
        in.cos_yaw = cosf(in.yaw); in.sin_yaw = sinf(in.yaw);
        *out = in;
    }
    for (int k = 0; k < W * H; k++) {
        if (cell[k].kind != VEG_GRASS) continue;
        if (v->ngrass == v->grass_cap) { v->grass_cap = v->grass_cap ? v->grass_cap * 2 : 8192; v->grass = realloc(v->grass, sizeof(VegInstance) * v->grass_cap); }
        v->grass[v->ngrass++] = cell[k];
    }
    if (getenv("CRET_VEG_DEBUG")) fprintf(stderr, "veg grass: %d clumps in %.1f ms\n", v->ngrass, (GetTime() - t0) * 1000.0);
}

bool cret_veg_find(const CretVeg* v, int kind, float x, float z, float radius, float out[3]) {
    int t0x = (int)floorf((x - radius) / VEG_TILE), t1x = (int)floorf((x + radius) / VEG_TILE);
    int t0z = (int)floorf((z - radius) / VEG_TILE), t1z = (int)floorf((z + radius) / VEG_TILE);
    VegInstance* buf = malloc(sizeof(VegInstance) * 4096); float bd = radius * radius; bool found = false;
    for (int tz = t0z; tz <= t1z; tz++) for (int tx = t0x; tx <= t1x; tx++) {
        if (tx < 0 || tz < 0 || tx >= v->tiles_n || tz >= v->tiles_n) continue;
        int n = scatter(v->map, kind, tx, tz, buf, 4096);
        for (int i = 0; i < n; i++) {
            float dx = buf[i].x - x, dz = buf[i].z - z, d2 = dx * dx + dz * dz;
            if (d2 < bd) { bd = d2; out[0] = buf[i].x; out[1] = buf[i].y; out[2] = buf[i].z; found = true; }
        }
    }
    free(buf); return found;
}

int cret_veg_lineup(CretVeg* v, float x0, float z0, float cx, float cz, float clear_r, VegInstance* out, int cap) {
    static const int K[8] = {VEG_CONIFER, VEG_BROADLEAF, VEG_PALM, VEG_TREEFERN, VEG_CYCAD, VEG_SHRUB, VEG_HORSETAIL, VEG_FERN};
    static const float SX[8] = {13, 12, 8, 7, 5, 4, 4.5f, 3.5f};
    const TerraMap* m = v->map; int T = v->tiles_n, n = 0;
    // Clear the tall plants around the site.
    int t0x = (int)floorf((cx - clear_r) / VEG_TILE), t1x = (int)floorf((cx + clear_r) / VEG_TILE);
    int t0z = (int)floorf((cz - clear_r) / VEG_TILE), t1z = (int)floorf((cz + clear_r) / VEG_TILE);
    for (int tz = t0z; tz <= t1z; tz++) for (int tx = t0x; tx <= t1x; tx++) {
        if (tx < 0 || tz < 0 || tx >= T || tz >= T) continue;
        VegTile* tl = &v->tiles[tz * T + tx]; int k = 0;
        for (int i = 0; i < tl->ntrees; i++) {
            float dx = tl->trees[i].x - cx, dz = tl->trees[i].z - cz;
            if (dx * dx + dz * dz > clear_r * clear_r) tl->trees[k++] = tl->trees[i];
        }
        tl->ntrees = k;
    }
    free(v->lineup); v->lineup = malloc(sizeof(VegInstance) * 8 * VEG_VARIANTS); v->nlineup = 0;
    v->lineup_c[0] = cx; v->lineup_c[1] = cz; v->lineup_r = clear_r;
    // One line along +x: each kind's row follows the last, so every row has a clear view from -z.
    float x = x0;
    for (int r = 0; r < 8; r++) {
        for (int q = 0; q < VEG_VARIANTS; q++, x += SX[r]) {
            float z = z0, y = terra_height(m, x, z) - 0.15f;
            VegInstance in = {x, y, z, 1.0f, 0.6f, 0.0f, 1.0f + q * 0.7f, (uint16_t)K[r], (uint16_t)q, 0, 0};
            in.cos_yaw = cosf(in.yaw); in.sin_yaw = sinf(in.yaw);
            if (n < cap) out[n++] = in;
            if (!is_tree(K[r])) { v->lineup[v->nlineup++] = in; continue; }
            int tx = (int)(x / VEG_TILE), tz = (int)(z / VEG_TILE);
            if (tx < 0 || tz < 0 || tx >= T || tz >= T) continue;
            VegTile* tl = &v->tiles[tz * T + tx];
            tl->trees = realloc(tl->trees, sizeof(VegInstance) * (tl->ntrees + 1)); tl->trees[tl->ntrees++] = in;
            if (tl->ntrees == 1) { tl->ymin = 1e9f; tl->ymax = -1e9f; }
            tl->ymin = fminf(tl->ymin, y); tl->ymax = fmaxf(tl->ymax, y + 40.0f);
        }
        x += 8.0f;
    }
    v->ground_valid = false; v->grass_valid = false; v->imp_built = false;
    return n;
}

void cret_veg_crown_cover(const CretVeg* v, int n, float cell, float origin, float* out) {
    float* area = calloc((size_t)n * n, sizeof(float));
    for (int t = 0; t < v->tiles_n * v->tiles_n; t++) {
        const VegTile* tl = &v->tiles[t];
        for (int i = 0; i < tl->ntrees; i++) {
            const VegInstance* in = &tl->trees[i];
            // The crown seen from above: three quarters of the bounding radius
            // (which reaches the longest branch tips), spread bilinearly over
            // the four cells around the trunk.
            float r = v->mesh[in->kind][in->variant].radius * in->scale * 0.75f, a = 3.14159265f * r * r;
            float fx = (in->x - origin) / cell - 0.5f, fz = (in->z - origin) / cell - 0.5f; int x0 = (int)floorf(fx), z0 = (int)floorf(fz);
            float ux = fx - x0, uz = fz - z0;
            for (int q = 0; q < 4; q++) {
                int x = x0 + (q & 1), z = z0 + (q >> 1); if (x < 0 || z < 0 || x >= n || z >= n) continue;
                area[(size_t)z * n + x] += a * ((q & 1) ? ux : 1 - ux) * ((q >> 1) ? uz : 1 - uz);
            }
        }
    }
    // Crowns overlap at random: the covered share of a cell is 1 - e^-(crown area / cell area).
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) out[(size_t)z * n + x] = 1.0f - expf(-area[(size_t)z * n + x] / (cell * cell));
    free(area);
}

float cret_veg_trunk_radius(int kind, int variant, float scale) {
    if (kind < 0 || kind >= VEG_KINDS || variant < 0 || variant >= VEG_VARIANTS || !is_tree(kind)) return 0;
    float r0; species_dims(kind, variant, NULL, &r0);
    return r0 * scale;
}
// Trunks as vertical cylinders (cret_veg_trunk_radius), up to the crown.
float cret_veg_cast_trunks(const CretVeg* v, const float o[3], const float d[3], float len, float r) {
    float hit = len;
    if (!v->tiles || len <= 0) return hit;
    float ex = d[0] * len, ez = d[2] * len, pad = r + 1.5f;
    int t0x = (int)floorf((fminf(o[0], o[0] + ex) - pad) / VEG_TILE), t1x = (int)floorf((fmaxf(o[0], o[0] + ex) + pad) / VEG_TILE);
    int t0z = (int)floorf((fminf(o[2], o[2] + ez) - pad) / VEG_TILE), t1z = (int)floorf((fmaxf(o[2], o[2] + ez) + pad) / VEG_TILE);
    float a = d[0] * d[0] + d[2] * d[2];
    if (a < 1e-6f) return hit;   // straight up or down: no trunk in the way
    for (int tz = t0z; tz <= t1z; tz++) for (int tx = t0x; tx <= t1x; tx++) {
        if (tx < 0 || tz < 0 || tx >= v->tiles_n || tz >= v->tiles_n) continue;
        const VegTile* tl = &v->tiles[tz * v->tiles_n + tx];
        for (int i = 0; i < tl->ntrees; i++) {
            const VegInstance* p = &tl->trees[i];
            if (p->kind >= VEG_KINDS || p->variant >= VEG_VARIANTS) continue;
            const VegMesh* m = &v->mesh[p->kind][p->variant];
            float th = m->height * p->scale, tr = fmaxf(cret_veg_trunk_radius(p->kind, p->variant, p->scale), 0.12f);
            th *= p->kind == VEG_CYCAD ? 0.5f : p->kind == VEG_CONIFER || p->kind == VEG_BROADLEAF ? 0.8f : 0.85f;
            float R = tr + r, cx = o[0] - p->x, cz = o[2] - p->z;
            float b = cx * d[0] + cz * d[2], c = cx * cx + cz * cz - R * R;
            if (c <= 0) continue;   // starts inside this trunk: it cannot be helped from here
            float disc = b * b - a * c; if (disc < 0) continue;
            float t = (-b - sqrtf(disc)) / a;
            if (t < 0 || t >= hit) continue;
            float y = o[1] + d[1] * t;
            if (y < p->y - r || y > p->y + th + r) continue;
            hit = t;
        }
    }
    return hit;
}

static void set_common(const GfxProgram* p, const VegFrameDesc* f, CretVeg* v) {
    gfx_setmat4(p, "u_viewproj", f->viewproj);
    gfx_set3f(p, "u_cam", f->cam[0], f->cam[1], f->cam[2]);
    gfx_set3f(p, "u_sun", f->sun[0], f->sun[1], f->sun[2]);
    gfx_set1f(p, "u_far", f->far_plane); gfx_set1f(p, "u_time", f->time); gfx_set1i(p, "u_frame", f->frame);
    gfx_set1f(p, "u_cloud_cover", f->cloud_cover); gfx_set2f(p, "u_map", (float)f->map_n, f->map_cell);
    gfx_set1f(p, "u_overcast", f->overcast); gfx_set1f(p, "u_wet", f->wet);
    gfx_bind_tex(p, "t_trans", 1, GL_TEXTURE_2D, f->t_trans);
    gfx_bind_tex(p, "t_skyview", 2, GL_TEXTURE_2D, f->t_skyview);
    gfx_bind_tex(p, "t_horizon0", 3, GL_TEXTURE_2D, f->t_horizon0);
    gfx_bind_tex(p, "t_horizon1", 4, GL_TEXTURE_2D, f->t_horizon1);
    gfx_bind_tex(p, "t_weather", 5, GL_TEXTURE_2D, f->t_weather);
    cret_csm_bind(f->csm, p, 7);
    gfx_bind_tex(p, "t_misc", 8, GL_TEXTURE_2D, f->t_misc);
    gfx_set1f(p, "u_understory", 0.0f);
    (void)v;
}

// Ground cover instance: i_extra = (fade, 0, bend, height) for veg.vs.
static void push_cover(const CretVeg* v, InstList* l, const VegInstance* in, float fade) {
    push(l, in, fade, 0, v->grass_on ? BEND[in->kind] : 0.0f, v->mesh[in->kind][in->variant].height * in->scale * 0.85f);
}
// Wind and trample map for veg.vs, on every program that draws plant meshes,
// so the depth prepass, colour and shadow passes bend alike.
static void set_bend(const CretVeg* v, const GfxProgram* p, const VegFrameDesc* f) {
    float a = 0.6435f + 0.3f * sinf(f->time * 0.009f);   // downwind, veering slowly
    gfx_set3f(p, "u_wind", cosf(a), sinf(a), 0.75f + 0.9f * f->overcast);   // stronger under a stormy sky
    cret_trample_bind(v->grass_on ? v->trample : NULL, p, 9);
}

// The far impostors' static buffer (CretVeg.imp_block): every tree, block by
// block, as GpuInst with extra = (near-range factor, first impostor layer,
// radius, height). veg_imp.vs turns the factor and the distance into the fade.
static void build_impostors(CretVeg* v) {
    int T = v->tiles_n, B = (T + VEG_IMP_BLOCK - 1) / VEG_IMP_BLOCK, total = 0;
    for (int t = 0; t < T * T; t++) total += v->tiles[t].ntrees;
    GpuInst* buf = malloc(sizeof(GpuInst) * (total ? total : 1)); int n = 0;
    free(v->imp_block); v->imp_block = calloc((size_t)B * B, sizeof *v->imp_block); v->imp_blocks_n = B;
    free(v->imp_run); v->imp_run = malloc(sizeof *v->imp_run * (size_t)B * B);
    for (int bz = 0; bz < B; bz++) for (int bx = 0; bx < B; bx++) {
        struct VegImpBlock* b = &v->imp_block[bz * B + bx]; b->off = n; b->ymin = 1e9f; b->ymax = -1e9f;
        for (int tz = bz * VEG_IMP_BLOCK; tz < (bz + 1) * VEG_IMP_BLOCK && tz < T; tz++)
            for (int tx = bx * VEG_IMP_BLOCK; tx < (bx + 1) * VEG_IMP_BLOCK && tx < T; tx++) {
                const VegTile* tl = &v->tiles[tz * T + tx]; if (!tl->ntrees) continue;
                b->ymin = fminf(b->ymin, tl->ymin); b->ymax = fmaxf(b->ymax, tl->ymax);
                for (int i = 0; i < tl->ntrees; i++) {
                    const VegInstance* in = &tl->trees[i]; const VegMesh* vm = &v->mesh[in->kind][in->variant];
                    buf[n++] = (GpuInst){{in->x, in->y, in->z, in->scale}, {in->cos_yaw, in->sin_yaw, in->hue, in->phase},
                                         {(in->kind == VEG_CYCAD ? 0.6f : 1.0f) * tree_switch_scale(v, in), (float)v->imp_layer[in->kind][in->variant], vm->radius, vm->height}};
                }
            }
        b->n = n - b->off;
    }
    if (!v->imp_vbo) glGenBuffers(1, &v->imp_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, v->imp_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GpuInst) * n, buf, GL_STATIC_DRAW);
    free(buf); v->imp_built = true;
}
// The runs of adjacent impostor blocks within far_range and in the frustum.
static void select_impostors(CretVeg* v, const VFrustum* fr, float cx, float cz) {
    int B = v->imp_blocks_n; float S = VEG_TILE * VEG_IMP_BLOCK, R = v->far_range;
    v->nimp_run = 0; v->drawn_impostors = 0;
    int b0x = (int)floorf((cx - R) / S), b1x = (int)floorf((cx + R) / S), b0z = (int)floorf((cz - R) / S), b1z = (int)floorf((cz + R) / S);
    for (int bz = b0z < 0 ? 0 : b0z; bz <= b1z && bz < B; bz++) {
        struct VegImpRun* run = NULL;
        for (int bx = b0x < 0 ? 0 : b0x; bx <= b1x && bx < B; bx++) {
            const struct VegImpBlock* b = &v->imp_block[bz * B + bx];
            float x0 = bx * S, z0 = bz * S;
            float dx = fmaxf(fmaxf(x0 - cx, 0), cx - x0 - S), dz = fmaxf(fmaxf(z0 - cz, 0), cz - z0 - S);
            bool on = b->n && dx * dx + dz * dz < R * R && vbox(fr, x0 - 12, b->ymin, z0 - 12, x0 + S + 12, b->ymax, z0 + S + 12);
            if (!on) { run = NULL; continue; }
            if (run && run->off + run->n == b->off) run->n += b->n;
            else { run = &v->imp_run[v->nimp_run++]; run->off = b->off; run->n = b->n; }
            v->drawn_impostors += b->n;
        }
    }
}
// Fade inputs for veg_imp.vs: the mesh range (times the per-tree factor), the
// crossfade band and the far range, as cret_veg_prepare chooses meshes.
#define VEG_FAR_BAND 18.0f
static void imp_uniforms(const CretVeg* v, const GfxProgram* p) { gfx_set3f(p, "u_imp_range", v->near_range, VEG_FAR_BAND, v->far_range); gfx_set1i(p, "u_imp_blend", v->imp_blend); }
// Draws the impostor runs with p already in use.
static void draw_impostor_runs(const CretVeg* v) {
    for (int r = 0; r < v->nimp_run; r++) {
        bind_instances(v->quad_vao, v->imp_vbo, (size_t)v->imp_run[r].off * sizeof(GpuInst));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, v->imp_run[r].n);
    }
}

void cret_veg_prepare(CretVeg* v, const VegFrameDesc* f) {
    v->prepared = true; v->prepared_frame = f->frame;
    gprof_mark("veg-trample");
    if (v->grass_on && v->trample) cret_trample_update(v->trample, f->csm, f->cam, f->time);
    gprof_mark("veg-cull");
    VFrustum fr = vfrustum(f->viewproj);
    float cx = f->cam[0], cy = f->cam[1], cz = f->cam[2];
    InstList (*near)[2 * VEG_VARIANTS] = v->lists;   // q >= VEG_VARIANTS: LOD mesh
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) near[k][q].n = 0;
    if (!v->imp_built) build_impostors(v);
    select_impostors(v, &fr, cx, cz);
    // Tree meshes: only the tiles within the mesh range (impostors are static, above).
    float band = VEG_FAR_BAND, nr = v->near_range, nt = nr * TREE_SWITCH_MAX;   // nt: the furthest any tree keeps its mesh
    int T = v->tiles_n, t0x = (int)floorf((cx - nt) / VEG_TILE), t1x = (int)floorf((cx + nt) / VEG_TILE);
    int t0z = (int)floorf((cz - nt) / VEG_TILE), t1z = (int)floorf((cz + nt) / VEG_TILE);
    for (int tz = t0z < 0 ? 0 : t0z; tz <= t1z && tz < T; tz++) for (int tx = t0x < 0 ? 0 : t0x; tx <= t1x && tx < T; tx++) {
        VegTile* tl = &v->tiles[tz * T + tx]; if (!tl->ntrees) continue;
        float x0 = tx * VEG_TILE, z0 = tz * VEG_TILE;
        float dx = fmaxf(fmaxf(x0 - cx, 0), cx - x0 - VEG_TILE), dz = fmaxf(fmaxf(z0 - cz, 0), cz - z0 - VEG_TILE);
        if (dx * dx + dz * dz > nt * nt) continue;
        if (!vbox(&fr, x0 - 12, tl->ymin, z0 - 12, x0 + VEG_TILE + 12, tl->ymax, z0 + VEG_TILE + 12)) continue;
        for (int i = 0; i < tl->ntrees; i++) {
            const VegInstance* in = &tl->trees[i]; VegMesh* vm = &v->mesh[in->kind][in->variant];
            float ddx = in->x - cx, ddy = in->y - cy, ddz = in->z - cz, d = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            float sz = tree_switch_scale(v, in), n_eff = nr * (in->kind == VEG_CYCAD ? 0.6f : 1.0f) * sz;   // as build_impostors
            if (d < n_eff) {   // i_extra.w: the plant's height, for the trunk sway (veg.vs)
                // 8 m crossfade to the reduced mesh: the meshes share a skeleton, only leaf tone and detail trade.
                float out = 1.0f - sstep(n_eff - band, n_eff, d), L0 = v->tree_lod_dist * TREE_LOD[in->kind] * sz, lb = 4.0f, ht = vm->height * in->scale;
                if (!v->mesh_lo[in->kind][in->variant].tris || d < L0 - lb) push(&near[in->kind][in->variant], in, out, 0, 0, ht);
                else if (d > L0 + lb) push(&near[in->kind][VEG_VARIANTS + in->variant], in, out, 0, 0, ht);
                else {
                    float f = 1.0f - sstep(L0 - lb, L0 + lb, d);
                    push(&near[in->kind][in->variant], in, f, 0, 0, ht);
                    push(&near[in->kind][VEG_VARIANTS + in->variant], in, -1.0f - f, 0, 0, ht);
                }
            }
        }
    }
    gprof_mark("veg-refresh");
    refresh_ground(v, cx, cz);
    gprof_mark("veg-cull");
    for (int i = 0; i < v->nground + v->nfungi; i++) {
        const VegInstance* in = i < v->nground ? &v->ground[i] : &v->fungi[i - v->nground];
        float ddx = in->x - cx, ddz = in->z - cz, d2 = ddx * ddx + ddz * ddz, gr = v->ground_range * VEG_RANGE[in->kind];
        if (d2 > gr * gr) continue;
        if (!vbox(&fr, in->x - 2, in->y - 1, in->z - 2, in->x + 2, in->y + 3, in->z + 2)) continue;
        if (cleared(v, in->x, in->z, 0.4f * v->mesh[in->kind][in->variant].radius * in->scale)) continue;
        // Over the outer 20% of the range a plant grows in (from 70% of its
        // size) as it dithers in, so a fern at the edge rises out of the sward
        // instead of appearing whole.
        float d = sqrtf(d2), out = 1.0f - sstep(gr * 0.8f, gr, d), band = 3.0f, L0 = v->lod_dist;
        VegInstance g = *in; g.scale *= 0.7f + 0.3f * out;
        bool has_lo = v->mesh_lo[in->kind][in->variant].tris > 0;
        if (!has_lo || d < L0 - band) push_cover(v, &near[in->kind][in->variant], &g, out);
        else if (d > L0 + band) push_cover(v, &near[in->kind][VEG_VARIANTS + in->variant], &g, out);
        else {   // complementary dither crossfade: negative fade keeps the other half
            float f = 1.0f - sstep(L0 - band, L0 + band, d);
            push_cover(v, &near[in->kind][in->variant], &g, f);
            push_cover(v, &near[in->kind][VEG_VARIANTS + in->variant], &g, -1.0f - f);
        }
    }
    // Sward: shrinks into the ground over the outer 40% of its range, where
    // the terrain colour takes over; reduced tuft beyond 8-10 m.
    gprof_mark("veg-refresh");
    refresh_carpet(v, cx, cz);
    gprof_mark("veg-cull");
    float cr = v->carpet_range;
    for (int i = 0; i < v->ncarpet; i++) {
        VegInstance in = v->carpet[i];
        float ddx = in.x - cx, ddz = in.z - cz, d2 = ddx * ddx + ddz * ddz;
        if (d2 > cr * cr) continue;
        if (!vbox(&fr, in.x - 0.6f, in.y - 0.5f, in.z - 0.6f, in.x + 0.6f, in.y + 0.8f, in.z + 0.6f)) continue;
        if (cleared(v, in.x, in.z, 0.2f)) continue;
        float d = sqrtf(d2); in.scale *= 1.0f - sstep(cr * 0.6f, cr, d);
        if (in.scale < 0.08f) continue;
        // The reduced tuft takes over across 8-10 m with a complementary
        // dither (it switched at 9 m in one frame).
        InstList* full = &near[VEG_CARPET][in.variant], * lo = &near[VEG_CARPET][VEG_VARIANTS + in.variant];
        if (d < 8.0f) push_cover(v, full, &in, 1.0f);
        else if (d > 10.0f) push_cover(v, lo, &in, 1.0f);
        else { float f = 1.0f - sstep(8.0f, 10.0f, d); push_cover(v, full, &in, f); push_cover(v, lo, &in, -1.0f - f); }
    }
    // Tall grass: full mesh near, reduced past grass_lod; over the last 30% of
    // its range it shrinks a little and dithers out.
    gprof_mark("veg-refresh");
    refresh_grass(v, cx, cz);
    gprof_mark("veg-cull");
    float gr = grass_range(v), gl = grass_lod(v);
    for (int i = 0; i < v->ngrass; i++) {
        VegInstance in = v->grass[i];
        float ddx = in.x - cx, ddz = in.z - cz, d2 = ddx * ddx + ddz * ddz;
        if (d2 > gr * gr) continue;
        if (!vbox(&fr, in.x - 1.3f, in.y - 0.3f, in.z - 1.3f, in.x + 1.3f, in.y + 2.4f, in.z + 1.3f)) continue;
        if (cleared(v, in.x, in.z, 0.3f)) continue;
        float d = sqrtf(d2), out = 1.0f - sstep(gr * 0.7f, gr, d), band = 2.5f;
        in.scale *= 0.7f + 0.3f * out;
        InstList* full = &near[VEG_GRASS][in.variant], * lo = &near[VEG_GRASS][VEG_VARIANTS + in.variant];
        if (d < gl - band) push_cover(v, full, &in, out);
        else if (d > gl + band) push_cover(v, lo, &in, out);
        else { float f = 1.0f - sstep(gl - band, gl + band, d); push_cover(v, full, &in, f); push_cover(v, lo, &in, -1.0f - f); }
    }
    // Upload everything into one buffer: buckets back to back.
    gprof_mark("veg-upload");
    size_t total = 0; for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) total += near[k][q].n;
    v->drawn_meshes = v->drawn_ground = 0;
    if (!total) return;
    gfx_stream_begin();
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++)
        v->list_off[k][q] = gfx_stream_add(near[k][q].a, sizeof(GpuInst) * near[k][q].n) / sizeof(GpuInst);
    gfx_stream_upload(v->inst_vbo);
}

// Depth prepass: alpha-tested meshes and impostors write depth only.
void cret_veg_draw_depth(CretVeg* v, const VegFrameDesc* f) {
    if (!v->prepared || v->prepared_frame != f->frame) cret_veg_prepare(v, f);
    gprof_mark("veg-depth");
    glDisable(GL_CULL_FACE); glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    GfxProgram* p = &v->depth_prog; glUseProgram(p->id);
    gfx_setmat4(p, "u_viewproj", f->viewproj); gfx_set1f(p, "u_time", f->time); gfx_set1i(p, "u_bake", 0);
    gfx_set2f(p, "u_depth", f->depth_ab[0], f->depth_ab[1]);
    gfx_set3f(p, "u_cam", f->cam[0], f->cam[1], f->cam[2]);
    gfx_bind_tex(p, "t_atlas", 0, GL_TEXTURE_2D_ARRAY, v->t_atlas);
    set_bend(v, p, f);
    // Trees, then ground cover: nearest first within each, so early-z helps.
    for (int pass = 0; pass < 2; pass++)
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) {
        if (!v->lists[k][q].n || is_tree(k) != (pass == 0)) continue;
        VegMesh* vm = q < VEG_VARIANTS ? &v->mesh[k][q] : &v->mesh_lo[k][q - VEG_VARIANTS];
        bind_instances(vm->m.vao, v->inst_vbo, v->list_off[k][q] * sizeof(GpuInst));
        glDrawElementsInstanced(GL_TRIANGLES, vm->m.index_count, GL_UNSIGNED_INT, 0, v->lists[k][q].n);
    }
    if (v->nimp_run) {
        GfxProgram* ip = &v->imp_depth_prog; glUseProgram(ip->id);
        gfx_setmat4(ip, "u_viewproj", f->viewproj); gfx_set3f(ip, "u_cam", f->cam[0], f->cam[1], f->cam[2]);
        gfx_set1f(ip, "u_time", f->time); gfx_set2f(ip, "u_depth", f->depth_ab[0], f->depth_ab[1]);
        gfx_bind_tex(ip, "t_imp_albedo", 0, GL_TEXTURE_2D_ARRAY, v->t_imp_albedo);
        gfx_set1i(ip, "u_views", VEG_IMPOSTOR_VIEWS); imp_uniforms(v, ip);
        draw_impostor_runs(v);
    }
    glBindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void cret_veg_draw(CretVeg* v, const VegFrameDesc* f) {
    if (!v->prepared || v->prepared_frame != f->frame) cret_veg_prepare(v, f);
    v->prepared = false;
    InstList (*near)[2 * VEG_VARIANTS] = v->lists;
    glDisable(GL_CULL_FACE);
    // Near meshes.
    GfxProgram* p = &v->mesh_prog; glUseProgram(p->id); set_common(p, f, v);
    gfx_set1i(p, "u_bake", 0); gfx_set2f(p, "u_depth", f->depth_ab[0], f->depth_ab[1]);
    gfx_bind_tex(p, "t_atlas", 0, GL_TEXTURE_2D_ARRAY, v->t_atlas);
    set_bend(v, p, f);
    for (int pass = 0; pass < 2; pass++) {
    gprof_mark(pass == 0 ? "veg-trees" : "veg-ground");
    gfx_set1f(p, "u_understory", pass == 0 ? 0.0f : 1.0f);
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) {
        if (!near[k][q].n || is_tree(k) != (pass == 0)) continue;
        VegMesh* vm = q < VEG_VARIANTS ? &v->mesh[k][q] : &v->mesh_lo[k][q - VEG_VARIANTS];
        bind_instances(vm->m.vao, v->inst_vbo, v->list_off[k][q] * sizeof(GpuInst));
        glDrawElementsInstanced(GL_TRIANGLES, vm->m.index_count, GL_UNSIGNED_INT, 0, near[k][q].n);
        if (is_tree(k)) v->drawn_meshes += near[k][q].n; else v->drawn_ground += near[k][q].n;
    }
    }
    if (getenv("CRET_VEG_DEBUG") && (f->frame % 24 == 0 || getenv("CRET_GL_STATS"))) {   // instances and triangles per bucket (every frame with the GL stats)
        long tris[2] = {0, 0}; int inst[2][2] = {{0, 0}, {0, 0}};
        for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) {
            const VegMesh* vm = q < VEG_VARIANTS ? &v->mesh[k][q] : &v->mesh_lo[k][q - VEG_VARIANTS];
            int t = is_tree(k) ? 0 : 1; tris[t] += (long)vm->tris * near[k][q].n; inst[t][q >= VEG_VARIANTS] += near[k][q].n;
        }
        fprintf(stderr, "veg: trees %d+%d lod (%ld tris), ground %d+%d lod (%ld tris), impostors %d in %d draws\n", inst[0][0], inst[0][1], tris[0], inst[1][0], inst[1][1], tris[1], v->drawn_impostors, v->nimp_run);
        static const char* nm[VEG_KINDS] = {"conifer", "broadleaf", "cycad", "fern", "treefern", "horsetail", "palm", "shrub", "carpet", "grass", "snag", "lily", "drop", "fungus"};
        fprintf(stderr, "  by kind (k tris, instances full+reduced):");
        for (int k = 0; k < VEG_KINDS; k++) {
            long t = 0; int n0 = 0, n1 = 0;
            for (int q = 0; q < 2 * VEG_VARIANTS; q++) { const VegMesh* vm = q < VEG_VARIANTS ? &v->mesh[k][q] : &v->mesh_lo[k][q - VEG_VARIANTS]; t += (long)vm->tris * near[k][q].n; if (q < VEG_VARIANTS) n0 += near[k][q].n; else n1 += near[k][q].n; }
            if (n0 + n1) fprintf(stderr, " %s %ld (%d+%d)", nm[k], t / 1000, n0, n1);
        }
        fprintf(stderr, "\n");
    }
    // Far impostors.
    gprof_mark("veg-impostors");
    if (v->nimp_run) {
        GfxProgram* ip = &v->imp_prog; glUseProgram(ip->id); set_common(ip, f, v);
        gfx_set2f(ip, "u_depth", f->depth_ab[0], f->depth_ab[1]);
        gfx_bind_tex(ip, "t_imp_albedo", 0, GL_TEXTURE_2D_ARRAY, v->t_imp_albedo);
        gfx_bind_tex(ip, "t_imp_normal", 6, GL_TEXTURE_2D_ARRAY, v->t_imp_normal);
        gfx_set1i(ip, "u_views", VEG_IMPOSTOR_VIEWS); imp_uniforms(v, ip);
        draw_impostor_runs(v);
    }
    glBindVertexArray(0);
}

// ---------------------------------------------------------------- sun shadow casters
// Nearest to the sun first, so early-z rejects what lies behind. The order
// only speeds the GPU up (a depth map keeps the nearest depth whatever the
// order), so 256 depth buckets are close enough: a counting sort, linear,
// instead of qsort with a comparator call per comparison (an indirect call,
// which wasm cannot inline). Stable within a bucket.
static void sort_light_depth(InstList* l, const float fwd[3]) {
    static GpuInst* out; static uint8_t* key; static int cap;   // scratch, kept (the render thread only)
    int n = l->n; if (n < 2) return;
    GpuInst* a = l->a; float lo = 1e30f, hi = -1e30f;
    if (cap < n) { cap = n; out = realloc(out, sizeof(GpuInst) * n); key = realloc(key, n); }
    for (int i = 0; i < n; i++) {
        float d = a[i].pos_scale[0] * fwd[0] + a[i].pos_scale[1] * fwd[1] + a[i].pos_scale[2] * fwd[2];
        out[i].pos_scale[0] = d; lo = fminf(lo, d); hi = fmaxf(hi, d);   // out holds the depths until the scatter
    }
    if (!(hi > lo)) return;
    int count[257] = {0}; float k = 255.99f / (hi - lo);
    for (int i = 0; i < n; i++) { key[i] = (uint8_t)((out[i].pos_scale[0] - lo) * k); count[key[i] + 1]++; }
    for (int b = 0; b < 256; b++) count[b + 1] += count[b];
    for (int i = 0; i < n; i++) out[count[key[i]]++] = a[i];
    memcpy(a, out, sizeof(GpuInst) * n);
}
int cret_veg_draw_shadow(CretVeg* v, const CretCsm* c, int k, const VegFrameDesc* f) {
    float lo[3], hi[3]; cret_csm_bounds(c, k, lo, hi);
    float cx = f->cam[0], cy = f->cam[1], cz = f->cam[2];
    InstList (*near)[2 * VEG_VARIANTS] = v->shadow_lists[k]; InstList* cards_l = &v->shadow_cards[k];
    for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) near[a][q].n = 0;
    cards_l->n = 0;
    // Meshes near the camera, cards beyond the mesh range and everywhere in
    // the last cascade. With shadow_lod_cascade (high and ultra) the mesh
    // detail follows the cascade, not the distance: the full meshes in the
    // first, the reduced ones further out. A depth map cannot dither, so a
    // caster switching mesh at a distance (32 m for trees, 25 m for ground
    // cover) moved its shadow in one frame, a pop across the crowns and ground
    // it shaded; at a cascade edge receivers blend the two cascades, so the
    // change is spread out. Low and medium keep the distance switch: their
    // first cascade is larger (22 m), and full meshes over all of it cost a
    // third more shadow triangles.
    bool by_cascade = v->shadow_lod_cascade;
    float mesh_range = k >= 3 ? 0.0f : k == 2 ? v->near_range : 1e9f;
    int T = v->tiles_n;
    int t0x = (int)floorf((lo[0] - 12) / VEG_TILE), t1x = (int)floorf((hi[0] + 12) / VEG_TILE);
    int t0z = (int)floorf((lo[2] - 12) / VEG_TILE), t1z = (int)floorf((hi[2] + 12) / VEG_TILE);
    for (int tz = t0z < 0 ? 0 : t0z; tz <= t1z && tz < T; tz++) for (int tx = t0x < 0 ? 0 : t0x; tx <= t1x && tx < T; tx++) {
        VegTile* tl = &v->tiles[tz * T + tx]; if (!tl->ntrees) continue;
        if (tl->ymax < lo[1] || tl->ymin > hi[1]) continue;
        float tc[3] = {(tx + 0.5f) * VEG_TILE, 0.5f * (tl->ymin + tl->ymax), (tz + 0.5f) * VEG_TILE};
        float trad = sqrtf(2.0f * (VEG_TILE * 0.5f + 12) * (VEG_TILE * 0.5f + 12) + 0.25f * (tl->ymax - tl->ymin) * (tl->ymax - tl->ymin));
        if (!cret_csm_caster_visible(c, k, tc, trad, 40.0f)) continue;
        bool all = cret_csm_caster_inside(c, k, tc, trad);
        for (int i = 0; i < tl->ntrees; i++) {
            const VegInstance* in = &tl->trees[i]; const VegMesh* vm = &v->mesh[in->kind][in->variant];
            float H = vm->height * in->scale, R = vm->radius * in->scale;
            float pc[3] = {in->x, in->y + H * 0.5f, in->z};
            if (!all && !cret_csm_caster_visible(c, k, pc, fmaxf(R, H * 0.5f), H)) continue;
            float ddx = in->x - cx, ddy = in->y - cy, ddz = in->z - cz, d = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            if (d < mesh_range * (in->kind == VEG_CYCAD ? 0.6f : 1.0f)) {
                bool lo_mesh = v->mesh_lo[in->kind][in->variant].tris && (by_cascade ? k >= 1 : d > v->tree_lod_dist * TREE_LOD[in->kind] || k >= 2);
                push(&near[in->kind][(lo_mesh ? VEG_VARIANTS : 0) + in->variant], in, 1, 0, 0, H);
            } else if (v->imp_layer[in->kind][in->variant] >= 0)
                push(cards_l, in, 1, (float)v->imp_layer[in->kind][in->variant], vm->radius, vm->height);
        }
    }
    // Ground cover casts into the finest cascade within the view's ground
    // range. On high and ultra, whose first cascade is smaller, it also casts
    // into the second where the first does not reach, out to 44 m (as far as
    // the first cascade reached when it ended at 20 m) and tall grass to 16 m,
    // so the smaller cascade does not take shadows away.
    if (k == 0 || (k == 1 && by_cascade)) {
        float reach = k == 0 ? 1e9f : 44.0f;
        refresh_ground(v, cx, cz);
        for (int i = 0; i < v->nground; i++) {
            const VegInstance* in = &v->ground[i];
            if (in->kind == VEG_LILY || in->kind == VEG_DROP) continue;   // flat on the water or ground: nothing to cast
            float ddx = in->x - cx, ddz = in->z - cz, d2 = ddx * ddx + ddz * ddz, gr = v->ground_range * VEG_RANGE[in->kind];
            if (d2 > gr * gr || d2 > reach * reach) continue;
            const VegMesh* vm = &v->mesh[in->kind][in->variant];
            float H = vm->height * in->scale, R = vm->radius * in->scale, pc[3] = {in->x, in->y + H * 0.5f, in->z};
            if (!cret_csm_caster_visible(c, k, pc, fmaxf(R, H * 0.5f), H) || cleared(v, in->x, in->z, 0.4f * R)) continue;
            if (k == 1 && cret_csm_caster_inside(c, 0, pc, fmaxf(R, H * 0.5f))) continue;   // the first cascade has it
            bool lo_mesh = v->mesh_lo[in->kind][in->variant].tris && (by_cascade ? k >= 1 : sqrtf(d2) > v->lod_dist);
            push_cover(v, &near[in->kind][(lo_mesh ? VEG_VARIANTS : 0) + in->variant], in, 1);
        }
        // Tall grass near the camera: it shades itself and the sward.
        float gr = fminf(grass_range(v), 16.0f), gl = grass_lod(v);
        for (int i = 0; i < v->ngrass; i++) {
            const VegInstance* in = &v->grass[i];
            float ddx = in->x - cx, ddz = in->z - cz, d2 = ddx * ddx + ddz * ddz;
            if (d2 > gr * gr) continue;
            float pc[3] = {in->x, in->y + 0.8f, in->z};
            if (!cret_csm_caster_visible(c, k, pc, 1.2f, 1.6f) || cleared(v, in->x, in->z, 0.3f)) continue;
            if (k == 1 && cret_csm_caster_inside(c, 0, pc, 1.2f)) continue;
            push_cover(v, &near[VEG_GRASS][((by_cascade ? k >= 1 : d2 > gl * gl) ? VEG_VARIANTS : 0) + in->variant], in, 1);
        }
    }
    size_t total = cards_l->n; for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) total += near[a][q].n;
    if (!total) return 0;
    for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++)
        sort_light_depth(&near[a][q], c->fwd);
    GLuint vbo = v->shadow_vbo[k];
    size_t offs[VEG_KINDS][2 * VEG_VARIANTS];
    gfx_stream_begin();
    for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++)
        offs[a][q] = gfx_stream_add(near[a][q].a, sizeof(GpuInst) * near[a][q].n) / sizeof(GpuInst);
    size_t card_off = gfx_stream_add(cards_l->a, sizeof(GpuInst) * cards_l->n) / sizeof(GpuInst);
    gfx_stream_upload(vbo);
    glDisable(GL_CULL_FACE);   // leaf cards are two-sided
    GfxProgram* p = &v->shadow_prog; glUseProgram(p->id);
    gfx_setmat4(p, "u_viewproj", c->vp[k]); gfx_set1f(p, "u_time", f->time); gfx_set1i(p, "u_bake", 0);
    gfx_bind_tex(p, "t_atlas", 0, GL_TEXTURE_2D_ARRAY, v->t_atlas);
    set_bend(v, p, f);
    int drawn = 0;
    for (int pass = 0; pass < 2; pass++) for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) {
        if (!near[a][q].n || is_tree(a) != (pass == 0)) continue;   // trees (upper occluders) first
        const VegMesh* vm = q < VEG_VARIANTS ? &v->mesh[a][q] : &v->mesh_lo[a][q - VEG_VARIANTS];
        bind_instances(vm->m.vao, vbo, offs[a][q] * sizeof(GpuInst));
        glDrawElementsInstanced(GL_TRIANGLES, vm->shadow_count, GL_UNSIGNED_INT, 0, near[a][q].n);   // no twigs
        drawn += near[a][q].n;
    }
    if (cards_l->n) {
        GfxProgram* ip = &v->imp_shadow_prog; glUseProgram(ip->id);
        gfx_setmat4(ip, "u_viewproj", c->vp[k]); gfx_set3f(ip, "u_sun", f->sun[0], f->sun[1], f->sun[2]);
        gfx_set1i(ip, "u_views", VEG_IMPOSTOR_VIEWS);
        gfx_bind_tex(ip, "t_imp_albedo", 0, GL_TEXTURE_2D_ARRAY, v->t_imp_albedo);
        bind_instances(v->quad_vao, vbo, card_off * sizeof(GpuInst));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, cards_l->n);
        drawn += cards_l->n;
    }
    glBindVertexArray(0);
    return drawn;
}
