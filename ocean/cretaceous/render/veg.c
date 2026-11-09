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
#define AT 512                 // foliage atlas layer size
#define IMP 256                // impostor view size

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
enum { L_BARK, L_FERN, L_CYCAD, L_ARAU, L_LEAF, L_PALM, L_HORSE, L_SHRUB, P_COUNT,
       L_FERN2 = P_COUNT, L_TFERN, L_SPRAY, L_PODO, L_LEAF2, L_LEAF3, L_BARK_CON, L_BARK_PALM, L_BARK_FERN, L_GRASS, L_SEDGE, L_REED, L_COUNT };
static const int FALLBACK[L_COUNT] = {L_BARK, L_FERN, L_CYCAD, L_ARAU, L_LEAF, L_PALM, L_HORSE, L_SHRUB,
                                      L_FERN, L_FERN, L_ARAU, L_ARAU, L_LEAF, L_LEAF, L_BARK, L_BARK, L_BARK, L_HORSE, L_HORSE, L_HORSE};
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
    SpriteSet fern, blade, spray, strap, magn, laurel, heart, round, sprig, small;
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

// Fills `img` from photos; false when the sources are missing (painted fallback).
static bool photo_layer(int layer, Px* img, const Photos* P) {
    Canvas c = {img}; VRng r = {(uint32_t)layer * 104729u + 7};
    static const float T[L_COUNT][3] = {   // mean linear albedo per layer
        [L_FERN] = {0.045f, 0.10f, 0.028f}, [L_FERN2] = {0.05f, 0.105f, 0.03f}, [L_TFERN] = {0.038f, 0.086f, 0.027f},
        [L_CYCAD] = {0.026f, 0.066f, 0.028f}, [L_ARAU] = {0.03f, 0.072f, 0.034f}, [L_SPRAY] = {0.045f, 0.098f, 0.036f},
        [L_PODO] = {0.03f, 0.07f, 0.028f}, [L_LEAF] = {0.045f, 0.095f, 0.03f}, [L_LEAF2] = {0.032f, 0.075f, 0.026f},
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
    case L_PODO: {    // podocarp twig: spiralled strap leaves
        if (!P->strap.n) return false;
        paint_stem(&c, 256, 0, 262, 480, 7, 2, 0.1f, 0.08f, 0.05f);
        for (int i = 0; i < 18; i++) {
            float v = 0.06f + i * 0.05f, side = (i & 1) ? 1 : -1, Lp = vrr(&r, 120, 160) * (1 - 0.3f * v); const Sprite* s = pick(&P->strap, &r);
            stamp(&c, s, 256 + 6 * v, v * AT, side * vrr(&r, 0.45f, 0.85f), Lp, side * Lp * s->aspect, t, vrr(&r, 0.8f, 1.15f));
        }
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

// Albedo grade for the leaf layers. Photo leaves are evenly lit and pale, so
// a frond read as one flat mint shape in sun. Per layer: stretch the
// leaf-to-leaf value contrast, darken toward the rachis and the frond base
// (deep blue-green) and warm the tips (yellow-green), with a few dry,
// yellow-brown tips in patches; then settle the layer's mean a little deeper
// than its photo target. Layers keep their base at y = 0 and the rachis at
// x = AT/2, except the palm fan, which radiates from its hastula.
static void grade_layer(int layer, Px* img) {
    if (is_bark_layer(layer) || layer == L_HORSE) return;
    bool fern = layer == L_FERN || layer == L_FERN2 || layer == L_TFERN;
    float gamma = fern ? 1.7f : 1.4f, core = fern ? 0.5f : 0.75f, base = fern ? 0.65f : 0.85f, dry_amt = fern ? 0.6f : 0.3f;
    float deep = fern ? 0.72f : 0.9f, warm = fern ? 0.82f : 0.92f;   // mean value; blue kept (less mint, more olive)
    double m0 = 0, m1 = 0, n = 0;
    for (int i = 0; i < AT * AT; i++) if (img[i].a > 0.5f) { m0 += img[i].r * 0.3f + img[i].g * 0.59f + img[i].b * 0.11f; n++; }
    if (n < 1) return;
    float M = (float)(m0 / n);
    for (int y = 0; y < AT; y++) for (int x = 0; x < AT; x++) {
        Px* q = &img[y * AT + x]; if (q->a <= 0.02f) continue;
        float t, d;   // along the frond (base 0 .. tip 1), out from its axis (0 .. 1)
        if (layer == L_PALM) { float dx = x - 256.0f, dy = y - 190.0f; t = d = clampf(sqrtf(dx * dx + dy * dy) / 300.0f, 0, 1); }
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
    int levels = 0; for (int s = AT; s >= 1; s >>= 1) levels++;
    size_t psize; uint8_t* pay = cret_tex_payload_alloc(TB_TEX_2D_ARRAY, GL_SRGB8_ALPHA8, AT, AT, count, levels, true, true, 4.0f, &psize);
    if (!pay) return 0;
    for (int k = 0; k < count; k++) {
        Px* cur = layers[k]; int s = AT; float cov0 = 0;
        for (int i = 0; i < AT * AT; i++) cov0 += cur[i].a > 0.5f;
        cov0 /= (float)(AT * AT);
        Px* owned = NULL;
        for (int l = 0; l < levels; l++) {
            uint8_t* bytes = cret_tex_payload_level(pay, l) + (size_t)k * s * s * 4;
            for (int i = 0; i < s * s; i++) {
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
typedef struct { float p[3]; float uv[2]; int8_t n[4]; uint8_t a[4]; } VVert;   // n.w = ao; a = layer, wind, leaf, unused
typedef struct { VVert* v; int nv, cv; uint32_t* i; int ni, ci; float rad, height; } MB;
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { return (V3){x, y, z}; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)); return l > 1e-8f ? vmul(a, 1 / l) : v3(0, 1, 0); }

static int g_leaf_dry;    // 0..255 frond dryness written into attr.w of leaf vertices
static int mb_vert(MB* m, V3 p, float u, float v, V3 n, float ao, int layer, float wind, int leaf) {
    if (m->nv == m->cv) { m->cv = m->cv ? m->cv * 2 : 1024; m->v = realloc(m->v, sizeof(VVert) * m->cv); }
    n = vnorm(n);
    m->v[m->nv] = (VVert){{p.x, p.y, p.z}, {u, v}, {(int8_t)(n.x * 127), (int8_t)(n.y * 127), (int8_t)(n.z * 127), (int8_t)(clampf(ao, 0, 1) * 127)},
                          {(uint8_t)layer, (uint8_t)(clampf(wind, 0, 1) * 255), (uint8_t)leaf, (uint8_t)(leaf ? g_leaf_dry : 0)}};
    float r = sqrtf(p.x * p.x + p.z * p.z); if (r > m->rad) m->rad = r; if (p.y > m->height) m->height = p.y;
    return m->nv++;
}
static void mb_tri(MB* m, int a, int b, int c) {
    if (m->ni + 3 > m->ci) { m->ci = m->ci ? m->ci * 2 : 4096; m->i = realloc(m->i, sizeof(uint32_t) * m->ci); }
    m->i[m->ni++] = a; m->i[m->ni++] = b; m->i[m->ni++] = c;
}
// Tube along a polyline (bark). wind grows from w0 to w1 along it.
static int g_bark_tone;   // per-species bark pattern index written into attr.w
static int g_bark_layer = L_BARK;   // atlas layer for tubes
static void mb_tube(MB* m, const V3* pts, const float* rad, int n, int sides, float ao0, float ao1, float w0, float w1, float vscale) {
    int base = m->nv; float len = 0;
    for (int k = 0; k < n; k++) {
        V3 t = vnorm(vsub(pts[k < n - 1 ? k + 1 : k], pts[k > 0 ? k - 1 : k]));
        V3 ref = fabsf(t.y) < 0.95f ? v3(0, 1, 0) : v3(1, 0, 0);
        V3 b = vnorm(vcross(t, ref)), nn = vcross(b, t);
        if (k > 0) len += sqrtf(vdot(vsub(pts[k], pts[k - 1]), vsub(pts[k], pts[k - 1])));
        float f = n > 1 ? (float)k / (n - 1) : 0;
        for (int s = 0; s <= sides; s++) {
            float a = 2 * VPI * s / sides; V3 d = vadd(vmul(b, cosf(a)), vmul(nn, sinf(a)));
            int vi = mb_vert(m, vadd(pts[k], vmul(d, rad[k])), (float)s / sides * fmaxf(1, roundf(rad[k] * 6)), len / vscale, d, lerpf(ao0, ao1, f), g_bark_layer, lerpf(w0, w1, f), 0);
            m->v[vi].a[3] = (uint8_t)g_bark_tone;
        }
    }
    for (int k = 0; k < n - 1; k++) for (int s = 0; s < sides; s++) {
        int a = base + k * (sides + 1) + s, b = a + 1, c = a + sides + 1, d = c + 1;
        mb_tri(m, a, c, b); mb_tri(m, b, c, d);
    }
}
// Arching frond ribbon from `base`: direction `dir` (horizontal unit),
// initial elevation e0, droop (radians over the length), width W.
static void mb_frond(MB* m, V3 base, V3 dir, float e0, float droop, float L, float W, int layer, V3 crown, float ao0, float wind0, float twist, int segs) {
    V3 side0 = vnorm(vcross(v3(0, 1, 0), dir)); V3 p = base; int first = m->nv;
    for (int k = 0; k <= segs; k++) {
        float t = (float)k / segs, e = e0 - droop * t * t * 1.3f - droop * t * 0.2f;
        V3 fwd = vnorm(vadd(vmul(dir, cosf(e)), v3(0, sinf(e), 0)));
        V3 side = vnorm(vadd(vmul(side0, cosf(twist * t)), vmul(vcross(fwd, side0), sinf(twist * t))));
        V3 up = vnorm(vcross(side, fwd));
        if (up.y < 0) up = vmul(up, -1);
        V3 bent = vnorm(vadd(vmul(up, 0.55f), vmul(vnorm(vsub(p, crown)), 0.45f)));
        float w = W * (k == 0 ? 0.35f : 1.0f);
        float ao = lerpf(ao0, 1.0f, sstep(0.0f, 0.7f, t)), wind = lerpf(wind0, 1.0f, t * t);
        mb_vert(m, vadd(p, vmul(side, -0.5f * w)), 0, t, bent, ao, layer, wind, 1);
        mb_vert(m, vadd(p, vmul(side, 0.5f * w)), 1, t, bent, ao, layer, wind, 1);
        p = vadd(p, vmul(fwd, L / segs));
    }
    for (int k = 0; k < segs; k++) { int a = first + 2 * k; mb_tri(m, a, a + 2, a + 1); mb_tri(m, a + 1, a + 2, a + 3); }
}
// Flat card: centre c, axes right/up (unit), sizes; texture v runs along up.
static void mb_card(MB* m, V3 c, V3 right, V3 up, float w, float h, int layer, V3 crown, float ao, float wind) {
    V3 n = vnorm(vcross(right, up)), out = vnorm(vsub(c, crown)); if (vdot(n, out) < 0) n = vmul(n, -1);
    V3 bent = vnorm(vadd(vmul(n, 0.4f), vmul(out, 0.6f)));
    int a = m->nv;
    V3 r = vmul(right, w * 0.5f), u = vmul(up, h * 0.5f);
    float aol = ao * 0.85f;
    mb_vert(m, vsub(vsub(c, r), u), 0, 0, bent, aol, layer, wind * 0.7f, 1);
    mb_vert(m, vsub(vadd(c, r), u), 1, 0, bent, aol, layer, wind * 0.7f, 1);
    mb_vert(m, vadd(vsub(c, r), u), 0, 1, bent, ao, layer, wind, 1);
    mb_vert(m, vadd(vadd(c, r), u), 1, 1, bent, ao, layer, wind, 1);
    mb_tri(m, a, a + 1, a + 2); mb_tri(m, a + 1, a + 3, a + 2);
}
static V3 dir_az(float az) { return v3(cosf(az), 0, sinf(az)); }
// Branch spray cards crossed around the branch axis.
static void mb_spray(MB* m, V3 c, V3 axis, float len, float wid, int layer, V3 crown, float ao, float wind, float roll) {
    V3 side = vnorm(vcross(axis, v3(0, 1, 0)));
    if (vdot(side, side) < 0.5f) side = v3(1, 0, 0);
    V3 nrm = vcross(side, axis);
    for (int k = 0; k < 2; k++) {
        float a = roll + k * VPI * 0.5f; V3 rgt = vnorm(vadd(vmul(side, cosf(a)), vmul(nrm, sinf(a))));
        mb_card(m, c, rgt, axis, wid, len, layer, crown, ao, wind);
    }
}

static int g_lod;          // 1: reduced ground-cover mesh for the mid-distance band
// Angiosperm-style crown: trunk to a fork, ascending limbs, secondary
// branches carrying leaf-twig cards, and a shell of clusters that rounds
// the crown. spread scales crown width, elong its height, card the twig size.
static void broad_tree(MB* m, VRng* r, float H, int layer, float trunk_frac, float spread, float elong, float card, float twig_scale) {
    int lod = g_lod; V3 trunk[4]; float tr[4];
    for (int k = 0; k < 4; k++) { float t = k / 3.0f; trunk[k] = v3(0.2f * sinf(t * 2 + H), H * trunk_frac * t, 0.2f * cosf(t * 3)); tr[k] = lerpf(0.32f, 0.22f, t) * (H / 10); }
    mb_tube(m, trunk, tr, 4, lod ? 6 : 8, 0.45f, 0.7f, 0, 0.05f, 1.5f);
    V3 fork = trunk[3], crown = vadd(fork, v3(0, H * 0.3f * elong, 0));
    float cs = card * (lod ? 1.35f : 1.0f) * twig_scale;
    int np = 4 + (int)vrr(r, 0, 2);
    for (int p = 0; p < np; p++) {
        float az = p * (2 * VPI / np) + vrr(r, -0.3f, 0.3f), e = vrr(r, 0.8f, 1.1f) + (elong - 1) * 0.4f, L = H * vrr(r, 0.3f, 0.4f) * sqrtf(spread * elong);
        V3 d = vnorm(vadd(vmul(dir_az(az), cosf(e) * spread), v3(0, sinf(e), 0))), end = vadd(fork, vmul(d, L));
        V3 bp[2] = {fork, end}; float br[2] = {0.18f * H / 10, 0.1f * H / 10};
        mb_tube(m, bp, br, 2, lod ? 4 : 6, 0.6f, 0.8f, 0.05f, 0.3f, 1.5f);
        for (int s = 0; s < 4; s++) {
            float az2 = az + vrr(r, -1.1f, 1.1f), e2 = vrr(r, 0.2f, 0.8f) + (elong - 1) * 0.5f, L2 = H * vrr(r, 0.24f, 0.34f) * spread;
            V3 d2 = vnorm(vadd(vmul(dir_az(az2), cosf(e2)), v3(0, sinf(e2), 0))), end2 = vadd(end, vmul(d2, L2));
            V3 sp[2] = {end, end2}; float sr[2] = {0.09f, 0.03f};
            if (!lod) mb_tube(m, sp, sr, 2, 4, 0.7f, 0.9f, 0.3f, 0.7f, 1.0f);
            int nc = lod ? 7 : 16;
            for (int c = 0; c < nc; c++) {
                float t = vrr(r, 0.15f, 1.15f); V3 cc = vadd(end, vmul(d2, L2 * t));
                cc = vadd(cc, v3(vrr(r, -1.2f, 1.2f) * spread, vrr(r, -0.5f, 0.9f) * elong, vrr(r, -1.2f, 1.2f) * spread));
                V3 out = vnorm(vsub(cc, crown));
                V3 up = vnorm(vadd(out, v3(vrr(r, -0.5f, 0.5f), 0.5f, vrr(r, -0.5f, 0.5f))));
                float ao = clampf(0.5f + 0.5f * vdot(out, v3(0, 1, 0)) * 0.5f + 0.25f * t, 0.4f, 1);
                mb_spray(m, cc, up, vrr(r, 1.4f, 2.0f) * cs / 1.7f, vrr(r, 1.2f, 1.7f) * cs / 1.7f, layer, crown, ao, 0.5f + 0.5f * t, vrr(r, 0, VPI));
            }
        }
    }
    // Shell clusters round the crown into a dome and hide the card stack from below.
    int ns = lod ? 14 : 26;
    for (int k = 0; k < ns; k++) {
        float a = vrr(r, 0, 2 * VPI), e = vrr(r, -0.35f, 1.2f), R = H * vrr(r, 0.3f, 0.42f);
        V3 dd = v3(cosf(a) * cosf(e) * spread, sinf(e) * 0.7f * elong, sinf(a) * cosf(e) * spread), cc = vadd(crown, vmul(dd, R));
        V3 up = vnorm(vadd(vnorm(dd), v3(vrr(r, -0.4f, 0.4f), 0.6f, vrr(r, -0.4f, 0.4f))));
        mb_spray(m, cc, up, vrr(r, 1.5f, 2.1f) * cs / 1.7f, vrr(r, 1.3f, 1.8f) * cs / 1.7f, layer, crown, clampf(0.55f + 0.4f * dd.y, 0.35f, 1), 0.8f, vrr(r, 0, VPI));
        if (e < 0.35f) {   // lower shell: a near-horizontal card so the crown reads solid from below
            float ha = vrr(r, 0, 2 * VPI); V3 hx = v3(cosf(ha), vrr(r, -0.15f, 0.15f), sinf(ha)), hz = vnorm(vcross(hx, v3(0, 1, 0)));
            mb_card(m, vadd(cc, v3(0, -0.2f, 0)), vnorm(hx), hz, vrr(r, 1.5f, 2.0f) * cs / 1.7f, vrr(r, 1.5f, 2.0f) * cs / 1.7f, layer, crown, clampf(0.5f + 0.4f * dd.y, 0.35f, 1), 0.8f);
        }
    }
}

static void build_species(MB* m, int kind, int variant) {
    VRng r = {(uint32_t)(kind * 131 + variant * 17 + 5)}; memset(m, 0, sizeof(*m));
    g_bark_tone = kind == VEG_TREEFERN ? 1 : kind == VEG_PALM ? 2 : kind == VEG_CONIFER ? 3 : kind == VEG_CYCAD ? 4 : kind == VEG_BROADLEAF ? 5 : 0;
    g_bark_layer = kind == VEG_TREEFERN ? L_BARK_FERN : (kind == VEG_PALM || kind == VEG_CYCAD) ? L_BARK_PALM : kind == VEG_CONIFER ? L_BARK_CON : L_BARK;
    switch (kind) {
    case VEG_TREEFERN: {   // Dicksonia/Cyathea-like: slim fibrous trunk, lacy arching crown, dead skirt
        float H = vrr(&r, 3.0f, 5.8f), lx = vrr(&r, -0.5f, 0.5f), lz = vrr(&r, -0.5f, 0.5f);
        V3 pts[7]; float rad[7];
        for (int k = 0; k < 7; k++) { float t = k / 6.0f; pts[k] = v3(lx * t * t, H * t, lz * t * t); rad[k] = lerpf(0.26f, 0.15f, sqrtf(t)) + (k == 6 ? 0.06f : 0); }
        mb_tube(m, pts, rad, 7, 8, 0.45f, 0.8f, 0, 0.15f, 1.0f);
        V3 top = pts[6], crown = vadd(top, v3(0, -0.4f, 0));
        // Dead fronds hang against the trunk below the crown.
        int nd = 5 + (int)vrr(&r, 0, 4);
        for (int f = 0; f < nd; f++) {
            float az = f * 2.39996f + vrr(&r, -0.3f, 0.3f); g_leaf_dry = (int)vrr(&r, 190, 255);
            mb_frond(m, vadd(top, v3(0, -0.1f, 0)), dir_az(az), vrr(&r, -1.35f, -1.0f), vrr(&r, 0.1f, 0.35f), vrr(&r, 1.3f, 2.0f), vrr(&r, 0.45f, 0.6f), L_TFERN, crown, 0.4f, 0.05f, vrr(&r, -0.6f, 0.6f), 5);
        }
        int nf = 20 + (int)vrr(&r, 0, 7);
        for (int f = 0; f < nf; f++) {
            float az = f * 2.39996f + vrr(&r, -0.2f, 0.2f); int young = f >= nf - 3; float age = (float)f / nf;   // early = old, low
            g_leaf_dry = age < 0.25f ? (int)vrr(&r, 30, 110) : 0;
            float L = young ? vrr(&r, 0.9f, 1.4f) : vrr(&r, 2.3f, 3.2f);
            mb_frond(m, top, dir_az(az), young ? vrr(&r, 1.3f, 1.45f) : lerpf(0.55f, 1.05f, age) + vrr(&r, -0.1f, 0.1f),
                     young ? 0.6f : lerpf(2.2f, 1.5f, age) + vrr(&r, -0.2f, 0.2f), L, L * vrr(&r, 0.3f, 0.36f), L_TFERN, crown, 0.5f, 0.15f, vrr(&r, -0.4f, 0.4f), 9);
        }
        g_leaf_dry = 0;
        break; }
    case VEG_CYCAD: {      // stout armoured trunk, stiff glossy pinnate rosette
        if (variant == 3) {   // Williamsonia sewardiana (bennettitalean): a slim trunk that forks into a
                              // few spreading branches, each tipped by a cycad-like crown whose stiff
                              // pinnate leaves arch outward and down, with a flower cone in the centre.
            float H0 = vrr(&r, 1.0f, 1.7f);
            V3 b0[3] = {v3(0, 0, 0), v3(0.05f, H0 * 0.5f, -0.03f), v3(0, H0, 0)}; float r0[3] = {0.19f, 0.16f, 0.15f};
            mb_tube(m, b0, r0, 3, 8, 0.5f, 0.75f, 0, 0.02f, 0.6f);
            int ns = 2 + (int)vrr(&r, 0, 1.99f);
            float az0 = vrr(&r, 0, 2 * VPI);
            for (int k = 0; k < ns; k++) {
                float az = az0 + k * (2 * VPI / ns) + vrr(&r, -0.35f, 0.35f), lean = vrr(&r, 0.55f, 0.85f), h = vrr(&r, 1.0f, 1.7f);
                V3 d = dir_az(az);
                // Branches leave the fork at an angle, then turn upward toward the crown.
                V3 p[3] = {b0[2], vadd(b0[2], vadd(vmul(d, h * 0.45f * sinf(lean)), v3(0, h * 0.45f * cosf(lean), 0))),
                           vadd(b0[2], vadd(vmul(d, h * 0.65f * sinf(lean)), v3(0, h * cosf(lean * 0.6f), 0)))};
                float rr[3] = {0.12f, 0.1f, 0.11f};
                mb_tube(m, p, rr, 3, 6, 0.55f, 0.8f, 0.02f, 0.2f, 0.6f);
                V3 tip = p[2];
                V3 cone[2] = {vadd(tip, v3(0, -0.05f, 0)), vadd(tip, v3(0, 0.28f, 0))}; float cr[2] = {0.1f, 0.03f};
                mb_tube(m, cone, cr, 2, 6, 0.6f, 0.9f, 0.1f, 0.2f, 0.4f);
                // Dead leaves hang against the branch below the crown.
                for (int f = 0; f < 2; f++) {
                    float a2 = az + f * 2.6f + vrr(&r, -0.4f, 0.4f); g_leaf_dry = (int)vrr(&r, 180, 255);
                    mb_frond(m, vadd(tip, v3(0, -0.08f, 0)), dir_az(a2), vrr(&r, -1.2f, -0.8f), 0.2f, vrr(&r, 0.9f, 1.3f), 0.38f, L_CYCAD, tip, 0.4f, 0.05f, vrr(&r, -0.3f, 0.3f), 3);
                }
                int nf = g_lod ? 9 : 16;
                for (int f = 0; f < nf; f++) {
                    float a2 = f * 2.39996f + vrr(&r, -0.15f, 0.15f), age = (float)f / nf;   // early = old, low
                    float L = vrr(&r, 1.3f, 1.8f) * lerpf(1.0f, 0.72f, age);
                    g_leaf_dry = age < 0.12f ? (int)vrr(&r, 40, 120) : 0;
                    mb_frond(m, tip, dir_az(a2), lerpf(-0.2f, 0.7f, age) + vrr(&r, -0.1f, 0.1f), lerpf(0.95f, 0.5f, age) + vrr(&r, -0.1f, 0.1f),
                             L, L * vrr(&r, 0.34f, 0.4f), L_CYCAD, tip, 0.5f, 0.2f, vrr(&r, -0.2f, 0.2f), g_lod ? 3 : 5);
                }
                g_leaf_dry = 0;
            }
            break;
        }
        float H = vrr(&r, 0.3f, 1.6f); V3 pts[4]; float rad[4];
        for (int k = 0; k < 4; k++) { float t = k / 3.0f; pts[k] = v3(0, H * t, 0); rad[k] = lerpf(0.32f, 0.26f, t) + 0.04f * sinf(VPI * t); }
        mb_tube(m, pts, rad, 4, 10, 0.5f, 0.75f, 0, 0.03f, 0.6f);
        V3 top = pts[3];
        int nd = 3 + (int)vrr(&r, 0, 4);
        for (int f = 0; f < nd; f++) {
            float az = f * 2.39996f + 0.5f; g_leaf_dry = (int)vrr(&r, 170, 255);
            mb_frond(m, vadd(top, vmul(dir_az(az), 0.2f)), dir_az(az), vrr(&r, -0.35f, 0.05f), vrr(&r, 0.5f, 0.9f), vrr(&r, 1.0f, 1.5f), 0.5f, L_CYCAD, top, 0.4f, 0.05f, vrr(&r, -0.3f, 0.3f), 5);
        }
        int nf = 20 + (int)vrr(&r, 0, 8);
        for (int f = 0; f < nf; f++) {
            float az = f * 2.39996f + vrr(&r, -0.1f, 0.1f), age = (float)f / nf;
            g_leaf_dry = age < 0.15f ? (int)vrr(&r, 20, 90) : 0;
            float L = vrr(&r, 1.4f, 2.1f) * lerpf(1.0f, 0.75f, age);
            mb_frond(m, vadd(top, vmul(dir_az(az), 0.15f)), dir_az(az), lerpf(0.15f, 0.95f, age) + vrr(&r, -0.08f, 0.08f), vrr(&r, 0.3f, 0.6f),
                     L, L * vrr(&r, 0.4f, 0.46f), L_CYCAD, top, 0.45f, 0.08f, vrr(&r, -0.15f, 0.15f), 5);
        }
        g_leaf_dry = 0;
        break; }
    case VEG_FERN: {       // clump of lanceolate bipinnate fronds; a few outer fronds dying back
        int nf = g_lod ? 10 + (int)vrr(&r, 0, 3) : 16 + (int)vrr(&r, 0, 8); V3 c = v3(0, 0.05f, 0);
        for (int f = 0; f < nf; f++) {
            float az = f * 2.39996f + vrr(&r, -0.3f, 0.3f); bool inner = f >= nf - 6;
            float L = inner ? vrr(&r, 0.6f, 1.0f) : vrr(&r, 0.9f, 1.5f);
            g_leaf_dry = (!inner && vr(&r) < 0.1f) ? (int)vrr(&r, 110, 220) : 0;
            mb_frond(m, vadd(c, vmul(dir_az(az), 0.05f)), dir_az(az), inner ? vrr(&r, 1.2f, 1.45f) : vrr(&r, 0.85f, 1.2f),
                     inner ? vrr(&r, 1.0f, 1.5f) : vrr(&r, 1.6f, 2.3f), L, L * vrr(&r, 0.26f, 0.32f) * (g_lod ? 1.25f : 1.0f), (f & 1) ? L_FERN2 : L_FERN, v3(0, 0, 0), g_lod ? 0.22f : 0.3f, 0.2f, vrr(&r, -0.5f, 0.5f), g_lod ? 3 : 6);
        }
        g_leaf_dry = 0;
        break; }
    case VEG_HORSETAIL: {  // colony of separate jointed stalks, one atlas stalk per crossed card pair
        int ns = g_lod ? 10 + (int)vrr(&r, 0, 4) : 22 + (int)vrr(&r, 0, 10);
        for (int k = 0; k < ns; k++) {
            float a = vrr(&r, 0, 2 * VPI), d = 1.6f * sqrtf(vr(&r)), h = vrr(&r, 0.9f, 2.2f), yaw = vrr(&r, 0, VPI);
            int st = (int)vrr(&r, 0, 3.99f); float u0 = (120 + st * 90 - 56) / 512.0f, u1 = (120 + st * 90 + 56) / 512.0f;
            V3 c = v3(cosf(a) * d, h * 0.5f, sinf(a) * d);
            int first = m->nv;
            mb_spray(m, c, vnorm(v3(vrr(&r, -0.08f, 0.08f), 1, vrr(&r, -0.08f, 0.08f))), h, 0.22f, L_HORSE, v3(0, 0.6f, 0), 0.8f, 0.6f, yaw);
            for (int q = first; q < m->nv; q++) m->v[q].uv[0] = lerpf(u0, u1, m->v[q].uv[0]);
        }
        break; }
    case VEG_PALM: {
        float H = vrr(&r, 4.0f, 8.5f), bend = vrr(&r, 0.3f, 1.2f), az0 = vrr(&r, 0, 2 * VPI); V3 pts[7]; float rad[7];
        for (int k = 0; k < 7; k++) { float t = k / 6.0f; pts[k] = vadd(v3(0, H * t, 0), vmul(dir_az(az0), bend * t * t)); rad[k] = lerpf(0.27f, 0.2f, t); }
        mb_tube(m, pts, rad, 7, 8, 0.5f, 0.85f, 0, 0.2f, 0.5f);
        // Open crown: a dozen fans on long petioles (young ones upright, old
        // ones level), plus a few dead fans hanging against the trunk.
        V3 top = pts[6]; int nf = 11 + (int)vrr(&r, 0, 4);
        for (int f = 0; f < 3; f++) {
            float az = f * 2.1f + vrr(&r, 0, 1); g_leaf_dry = (int)vrr(&r, 200, 255);
            mb_frond(m, vadd(top, v3(0, -0.25f, 0)), dir_az(az), vrr(&r, -1.3f, -0.9f), 0.2f, vrr(&r, 2.2f, 2.8f), vrr(&r, 1.4f, 1.8f), L_PALM, top, 0.4f, 0.05f, vrr(&r, -0.5f, 0.5f), 4);
        }
        g_leaf_dry = 0;
        for (int f = 0; f < nf; f++) {
            float az = f * 2.39996f + vrr(&r, -0.2f, 0.2f), age = (float)f / nf;   // early = old, low
            g_leaf_dry = age < 0.12f ? (int)vrr(&r, 40, 120) : 0;
            mb_frond(m, top, dir_az(az), lerpf(-0.1f, 1.15f, age) + vrr(&r, -0.12f, 0.12f), lerpf(1.1f, 0.4f, age), vrr(&r, 3.0f, 3.8f), vrr(&r, 2.0f, 2.4f), L_PALM, top, 0.5f, 0.2f, vrr(&r, -0.3f, 0.3f), 5);
        }
        g_leaf_dry = 0;
        break; }
    case VEG_CARPET: {     // low sward tuft: 0/1 young ferns, 2 broad-leaved forb, 3 fern and forb mix
        int nf = g_lod ? 4 : 7 + (int)vrr(&r, 0, 3); V3 c = v3(0, 0.02f, 0);
        for (int f = 0; f < nf; f++) {
            float az = f * 2.39996f + vrr(&r, -0.4f, 0.4f);
            V3 base = vadd(c, vmul(dir_az(az + 1.3f), vrr(&r, 0.0f, 0.18f)));
            bool forb = variant == 2 || (variant == 3 && (f & 1));
            if (forb) {   // a few rounded leaves on a short stalk, held low
                float L = vrr(&r, 0.16f, 0.26f); V3 d = dir_az(az); V3 up = vnorm(vadd(vmul(d, 0.6f), v3(0, vrr(&r, 0.3f, 0.8f), 0)));
                V3 cc = vadd(base, vadd(vmul(d, L * 0.6f), v3(0, vrr(&r, 0.06f, 0.16f), 0)));
                mb_card(m, cc, vnorm(vcross(up, v3(0, 1, 0.01f))), up, L, L * 1.2f, L_SHRUB, c, 0.55f, 0.4f);
            } else {
                float L = vrr(&r, 0.28f, 0.5f);
                g_leaf_dry = vr(&r) < 0.08f ? (int)vrr(&r, 90, 200) : 0;
                mb_frond(m, base, dir_az(az), vrr(&r, 0.6f, 1.15f), vrr(&r, 1.0f, 1.6f), L, L * 0.34f, (f & 1) ? L_FERN2 : L_FERN, v3(0, -0.1f, 0), 0.35f, 0.25f, vrr(&r, -0.4f, 0.4f), g_lod ? 2 : 3);
            }
        }
        g_leaf_dry = 0;
        break; }
    case VEG_GRASS: {      // 0 meadow grass, 1 tall grass, 2 sedge fountain, 3 reeds with a sedge skirt
        // Arching ribbons of the blade-strip layers, pinched at the base into a tuft.
        int lod = g_lod, segs = lod ? 2 : 4;
        int nr = variant == 0 ? (lod ? 4 : 8) : variant == 1 ? (lod ? 5 : 10) : variant == 2 ? (lod ? 4 : 9) : (lod ? 4 : 8);
        for (int f = 0; f < nr; f++) {
            float az = f * 2.39996f + vrr(&r, -0.35f, 0.35f), L, W, e0, droop, tw = vrr(&r, -0.5f, 0.5f); int layer = L_GRASS;
            V3 base = vmul(dir_az(az + 1.7f), vrr(&r, 0.0f, variant == 3 ? 0.25f : 0.12f));
            switch (variant) {
            case 0: L = vrr(&r, 0.6f, 0.9f); W = vrr(&r, 0.45f, 0.62f); e0 = vrr(&r, 1.18f, 1.45f); droop = vrr(&r, 0.35f, 0.8f); break;
            case 1: L = vrr(&r, 0.9f, 1.2f); W = vrr(&r, 0.5f, 0.7f); e0 = vrr(&r, 1.25f, 1.48f); droop = vrr(&r, 0.4f, 0.9f); break;
            case 2: layer = L_SEDGE; L = vrr(&r, 0.7f, 1.0f); W = vrr(&r, 0.5f, 0.62f); e0 = vrr(&r, 0.95f, 1.3f); droop = vrr(&r, 1.3f, 2.0f); break;
            default:
                if (f < 3) { layer = L_SEDGE; L = vrr(&r, 0.6f, 0.85f); W = vrr(&r, 0.5f, 0.6f); e0 = vrr(&r, 1.0f, 1.25f); droop = vrr(&r, 1.2f, 1.8f); }
                else { layer = L_REED; L = vrr(&r, 1.2f, 1.6f); W = vrr(&r, 0.55f, 0.7f); e0 = vrr(&r, 1.38f, 1.52f); droop = vrr(&r, 0.1f, 0.35f); tw *= 0.4f; }
                break;
            }
            g_leaf_dry = vr(&r) < 0.1f ? (int)vrr(&r, 50, 140) : 0;
            mb_frond(m, base, dir_az(az), e0, droop, L, W * (lod ? 1.3f : 1.0f), layer, v3(0, -0.5f, 0), 0.35f, 0.04f, tw, segs);
        }
        g_leaf_dry = 0;
        break; }
    case VEG_SHRUB: {
        float R = vrr(&r, 0.8f, 1.3f); V3 c = v3(0, R * 0.65f, 0);
        for (int s = 0; s < 3; s++) {
            float a = vrr(&r, 0, 2 * VPI); V3 pts[2] = {v3(0, 0, 0), v3(cosf(a) * 0.4f, R * 0.9f, sinf(a) * 0.4f)}; float rad[2] = {0.05f, 0.02f};
            mb_tube(m, pts, rad, 2, 4, 0.4f, 0.6f, 0, 0.4f, 0.5f);
        }
        for (int k = 0; k < (g_lod ? 16 : 40); k++) {
            float a = vrr(&r, 0, 2 * VPI), e = vrr(&r, -0.2f, 1.3f);
            V3 d = v3(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e));
            V3 cc = vadd(c, vmul(d, R * vrr(&r, 0.35f, 0.75f)));
            V3 up = vnorm(vadd(d, v3(0, 0.6f, 0))), rgt = vnorm(vcross(up, v3(vrr(&r, -1, 1), vrr(&r, -1, 1), vrr(&r, -1, 1))));
            mb_card(m, cc, rgt, vnorm(vcross(rgt, vcross(up, rgt))), vrr(&r, 0.6f, 0.9f), vrr(&r, 0.6f, 0.9f), L_SHRUB, c, lerpf(0.5f, 1.0f, (d.y + 0.2f) / 1.5f), 0.5f);
        }
        break; }
    case VEG_CONIFER: {
        // 0 Araucaria (heterophylla-like), 1 podocarp, 2 Araucaria (araucana-like
        // umbrella), 3 Metasequoia (dawn redwood: wet ground, feathery sprays).
        if (variant == 1) { broad_tree(m, &r, vrr(&r, 12, 18), L_PODO, 0.32f, 0.62f, 1.25f, 1.55f, 1.25f); break; }
        bool umbrella = variant == 2, redwood = variant == 3; int lod = g_lod;
        float H = umbrella ? vrr(&r, 18, 24) : redwood ? vrr(&r, 22, 30) : vrr(&r, 15, 23); V3 pts[10]; float rad[10];
        for (int k = 0; k < 10; k++) {
            float t = k / 9.0f; pts[k] = v3(0.15f * sinf(t * 3), H * t, 0.1f * cosf(t * 2));
            rad[k] = redwood ? lerpf(0.62f, 0.05f, powf(t, 0.7f)) + 0.45f * expf(-t * 14) : lerpf(0.42f, 0.05f, powf(t, 0.8f));
        }
        mb_tube(m, pts, rad, 10, lod ? 6 : (redwood ? 12 : 8), 0.45f, 0.9f, 0, 0.1f, 2.0f);
        V3 crown = v3(0, umbrella ? H * 0.88f : H * 0.6f, 0);
        float h0 = umbrella ? 0.62f : redwood ? 0.2f : 0.28f; int whorl = 0, nb = redwood ? 5 : 6;
        float spray_len = (redwood ? 1.7f : 2.0f) * (lod ? 1.45f : 1.0f), spray_w = (redwood ? 1.35f : 1.8f) * (lod ? 1.45f : 1.0f);
        int layer = redwood ? L_SPRAY : L_ARAU;
        bool arau = !redwood && !umbrella;
        if (arau) { nb = 5; spray_len *= 1.1f; spray_w *= 1.45f; }
        // Umbrella: a dome. Branch length follows a quarter circle down the
        // crown (long to the top, short only in the last whorls), so the tips
        // trace a dome; branches run out nearly level and turn their tips up,
        // and the whorls go on to the leader under a cap of rising sprays. (It used to taper linearly from 4.5 m to 1.2 m with arched,
        // drooping branches: a flat-topped crown that read as cut off.)
        for (float h = h0 * H; h < H * (umbrella ? 0.985f : 0.97f); h += redwood ? vrr(&r, 0.6f, 0.95f) : umbrella ? vrr(&r, 0.85f, 1.15f) : vrr(&r, 1.25f, 1.6f) * lerpf(1.0f, 0.75f, (h / H - h0) / (1 - h0)), whorl++) {
            float hr = (h / H - h0) / (1 - h0);
            for (int b = 0; b < nb; b++) {
                float az = b * (2 * VPI / nb) + whorl * (redwood ? 1.9f : 0.7f) + vrr(&r, -0.15f, redwood ? 0.6f : 0.15f);
                float bl = umbrella ? lerpf(1.5f, 4.6f, sqrtf(fmaxf(1 - hr * hr, 0))) * vrr(&r, 0.9f, 1.08f) : redwood ? 0.6f + powf(1 - hr, 1.1f) * 0.22f * H * vrr(&r, 0.8f, 1.15f)
                                                                       : 0.5f + powf(1 - hr, 0.9f) * 0.3f * H;
                float elev = umbrella ? lerpf(0.08f, 0.5f, hr) : redwood ? lerpf(-0.15f, 0.5f, hr) : lerpf(-0.05f, 0.45f, hr);
                V3 d = dir_az(az), bp[4]; float br[4];
                for (int k = 0; k < 4; k++) {
                    float t = k / 3.0f, e = umbrella ? elev : elev - (redwood ? 0.5f : 0.35f) * t * t * (bl / 6);
                    bp[k] = vadd(v3(0, h, 0), vadd(vmul(d, bl * t * cosf(e)), v3(0, bl * t * sinf(e) + (umbrella ? bl * 0.2f * t * t : 0), 0)));
                    br[k] = lerpf(0.09f * fminf(bl / 5, 1.5f) + 0.02f, 0.015f, t);
                }
                if (lod) { V3 b2[2] = {bp[0], bp[2]}; float r2[2] = {br[0], br[2]}; if (hr < 0.7f) mb_tube(m, b2, r2, 2, 3, lerpf(0.5f, 0.8f, hr), 0.9f, 0.15f, 0.7f, 1.0f); }
                else mb_tube(m, bp, br, 4, 4, lerpf(0.5f, 0.8f, hr), 0.9f, 0.15f, 0.7f, 1.0f);
                float step = (redwood ? 0.42f : 0.4f) / fmaxf(bl, 1.0f) * (lod ? (redwood || arau ? 2.6f : 2.0f) : 1.0f);
                // Norfolk-pine tiers carry their ropes on the outer part of
                // each branch, so the tiers read as flat plates with gaps.
                for (float s = umbrella ? 0.22f : arau ? 0.18f + 0.2f * (1 - hr) * (bl > 2.5f) : 0.15f; s <= 1.0f; s += arau ? step * 0.85f : step) {
                    int k = (int)fminf(s * 3, 2.0f); float f = s * 3 - k;
                    V3 c = vadd(bp[k], vmul(vsub(bp[k + 1], bp[k]), f)), ax = vnorm(vsub(bp[k + 1], bp[k]));
                    if (redwood) ax = vnorm(vadd(ax, v3(0, -0.35f, 0)));   // sprays hang
                    float ao = lerpf(0.45f, 1.0f, s * 0.6f + hr * 0.4f), wnd = lerpf(0.3f, 0.9f, s);
                    if (!redwood) {   // two cards in a shallow V about the branch: a plate of ropes, not a fern fan
                        V3 sd = vnorm(vcross(ax, v3(0, 1, 0))), nr = vcross(sd, ax);
                        for (int q = -1; q <= 1; q += 2) {
                            float a = q * 0.42f + vrr(&r, -0.1f, 0.1f);
                            mb_card(m, vadd(c, vmul(ax, 0.45f)), vnorm(vadd(vmul(sd, cosf(a)), vmul(nr, sinf(a)))), ax, spray_w, spray_len, layer, crown, ao, wnd);
                        }
                    } else mb_spray(m, vadd(c, vmul(ax, 0.4f)), ax, spray_len, spray_w, layer, crown, ao, wnd, vrr(&r, -0.5f, 0.5f));
                }
            }
        }
        if (umbrella) for (int b = 0; b < 5; b++) {   // the dome's cap: sprays fanning up and out from the leader
            V3 ax = vnorm(vadd(vmul(dir_az(b * (2 * VPI / 5) + 0.4f), 0.75f), v3(0, 1, 0)));
            mb_spray(m, vadd(v3(0, H * 0.96f, 0), vmul(ax, 0.7f)), ax, spray_len, spray_w * 0.8f, layer, crown, 1.0f, 0.6f, vrr(&r, -0.5f, 0.5f));
        }
        mb_spray(m, v3(0, H * 0.98f, 0), v3(0, 1, 0), umbrella ? 2.0f : 1.6f, 0.9f, layer, crown, 1.0f, 0.5f, 0);   // leader tuft
        break; }
    case VEG_BROADLEAF:   // 0 magnolia-like, 1 laurel-like, 2 Cercidiphyllum-like, 3 laurel understory
        if (variant == 3) broad_tree(m, &r, vrr(&r, 4.5f, 6.5f), L_LEAF2, 0.38f, 0.75f, 1.1f, 1.3f, 1.05f);
        else broad_tree(m, &r, vrr(&r, 7, 12), variant == 0 ? L_LEAF : variant == 1 ? L_LEAF2 : L_LEAF3, 0.45f, 1.0f, 1.0f,
                        variant == 0 ? 1.85f : 1.6f, 1.0f);
        break;
    }
}

static GfxMesh upload_mesh(const MB* m) {
    GfxMesh g = {0};
    glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(VVert) * m->nv, m->v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VVert), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_BYTE, GL_TRUE, sizeof(VVert), (void*)20);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(VVert), (void*)24);
    glGenBuffers(1, &g.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * m->ni, m->i, GL_STATIC_DRAW);
    glBindVertexArray(0); g.index_count = m->ni;
    return g;
}

// ---------------------------------------------------------------- instances
typedef struct { float pos_scale[4], rot[4], extra[4]; } GpuInst;   // extra: fade, impostor layer, radius, height

static void bind_instances(GLuint vao, GLuint vbo, size_t offset) {
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (int k = 0; k < 3; k++) {
        glEnableVertexAttribArray(4 + k);
        glVertexAttribPointer(4 + k, 4, GL_FLOAT, GL_FALSE, sizeof(GpuInst), (void*)(offset + 16 * k));
        glVertexAttribDivisor(4 + k, 1);
    }
}

static bool is_tree(int kind) { return kind == VEG_CONIFER || kind == VEG_BROADLEAF || kind == VEG_TREEFERN || kind == VEG_PALM || kind == VEG_CYCAD; }
// Spacing (m) of the scatter lattice and size range per kind.
static const float SPACING[VEG_KINDS] = {7.5f, 8.0f, 4.0f, 1.2f, 4.5f, 2.4f, 7.0f, 2.6f, 0.5f, 0.8f};
static const float SCALE_LO[VEG_KINDS] = {0.7f, 0.75f, 0.7f, 0.9f, 0.75f, 0.9f, 0.8f, 0.7f, 0.7f, 0.8f};
static const float SCALE_HI[VEG_KINDS] = {1.25f, 1.25f, 1.2f, 1.65f, 1.25f, 1.5f, 1.2f, 1.3f, 1.55f, 1.2f};
static const float DENSITY[VEG_KINDS] = {0.9f, 0.85f, 0.6f, 0.85f, 0.75f, 1.0f, 0.7f, 0.65f, 1.0f, 1.0f};
// How far ground cover bends under wind and animals (veg.vs): 0 rigid .. 1 grass.
static const float BEND[VEG_KINDS] = {0, 0, 0, 0.75f, 0, 0.6f, 0, 0.3f, 1.0f, 1.0f};

static float veg_density(const TerraMap* m, int kind, float x, float z) {
    float gx = clampf(x / m->cell, 0, m->n - 1.001f), gz = clampf(z / m->cell, 0, m->n - 1.001f);
    int ix = (int)gx, iz = (int)gz; float fx = gx - ix, fz = gz - iz; size_t i = (size_t)iz * m->n + ix;
    const uint8_t* v = m->veg;
    float a = v[i * TERRA_VEG_COUNT + kind], b = v[(i + 1) * TERRA_VEG_COUNT + kind];
    float c = v[(i + m->n) * TERRA_VEG_COUNT + kind], d = v[(i + m->n + 1) * TERRA_VEG_COUNT + kind];
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz) / 255.0f;
}
// Only cells terra marked as water count as wet: the water level is also
// extended a few cells under dry banks (so the surface meets the shore), and
// the renderer hides it there. Emergent plants may stand in shallow water.
static bool placeable(const TerraMap* m, float x, float z, float max_slope, float wade, float* y) {
    float n[3]; terra_normal(m, x, z, n);
    if (n[1] < 1 / sqrtf(1 + max_slope * max_slope)) return false;
    float h = terra_height(m, x, z), w = terra_water(m, x, z);
    int gx = (int)fminf(fmaxf(x / m->cell + 0.5f, 0), m->n - 1), gz = (int)fminf(fmaxf(z / m->cell + 0.5f, 0), m->n - 1);
    size_t i = (size_t)gz * m->n + gx;
    bool wet = m->water_kind[i] != TERRA_WATER_NONE && w > h + 0.05f;
    if (h < 0.4f || m->water_kind[i] == TERRA_WATER_SEA) return false;
    if (wet && w - h > wade) return false;
    *y = h - 0.15f; return true;
}
// Variants grow in stands: a coarse cell picks the dominant one, so groves
// cluster instead of mixing evenly. Metasequoia takes wet ground.
static int pick_variant(const TerraMap* m, int kind, float x, float z, VRng* r) {
    uint32_t cell = hashu((uint32_t)(int)floorf(x / 140.0f) * 92837111u ^ (uint32_t)(int)floorf(z / 140.0f) * 689287499u ^ (uint32_t)kind * 31u);
    int dom = (int)(cell % VEG_VARIANTS);
    if (kind == VEG_CONIFER) {
        float wet = veg_density(m, VEG_HORSETAIL, x, z) + veg_density(m, VEG_TREEFERN, x, z);
        if (wet > 0.3f) return vr(r) < 0.85f ? 3 : 1;
        if (dom == 3) dom = (int)(cell / 7 % 3);
    }
    if (kind == VEG_CYCAD && dom == 3 && (cell >> 9) % 3) dom = (int)(cell / 5 % 3);   // Williamsonia is rarer
    return vr(r) < 0.75f ? dom : (int)(vr(r) * VEG_VARIANTS) % VEG_VARIANTS;
}
// Scatter one kind over a tile on a jittered lattice; deterministic per tile.
static int scatter(const TerraMap* m, int kind, int tx, int tz, VegInstance* out, int cap) {
    float sp = SPACING[kind]; int cnt = 0, steps = (int)(VEG_TILE / sp);
    for (int j = 0; j < steps; j++) for (int i = 0; i < steps; i++) {
        uint32_t h = hashu((uint32_t)(tx * 4099 + i) * 2654435761u ^ (uint32_t)(tz * 3571 + j) * 40503u ^ (uint32_t)kind * 977u);
        VRng r = {h};
        float x = tx * VEG_TILE + (i + vr(&r)) * sp, z = tz * VEG_TILE + (j + vr(&r)) * sp;
        float d = veg_density(m, kind, x, z) * DENSITY[kind];
        if (vr(&r) > d * d * 1.6f) continue;
        float y; if (!placeable(m, x, z, is_tree(kind) ? 0.95f : 1.1f, kind == VEG_HORSETAIL ? 0.5f : -1.0f, &y)) continue;
        if (cnt == cap) return cnt;
        out[cnt++] = (VegInstance){x, y, z, vrr(&r, SCALE_LO[kind], SCALE_HI[kind]), vrr(&r, 0, 2 * VPI), vrr(&r, -1, 1), vrr(&r, 0, 6.283f),
                                   (uint16_t)kind, (uint16_t)pick_variant(m, kind, x, z, &r), 0, 0};
        out[cnt - 1].cos_yaw = cosf(out[cnt - 1].yaw); out[cnt - 1].sin_yaw = sinf(out[cnt - 1].yaw);
    }
    return cnt;
}

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

static void bake_impostors(CretVeg* v) {
    int layers = 0;
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < VEG_VARIANTS; q++) {
        v->imp_layer[k][q] = is_tree(k) ? layers : -1; if (is_tree(k)) layers += VEG_IMPOSTOR_VIEWS;
    }
    GLuint tex[2]; glGenTextures(2, tex);
    for (int t = 0; t < 2; t++) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex[t]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, t == 0 ? GL_SRGB8_ALPHA8 : GL_RGBA8, IMP, IMP, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    GLuint fbo, depth; glGenFramebuffers(1, &fbo); glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, IMP, IMP);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
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
        glBindVertexArray(vm->m.vao);
        for (int a = 4; a < 7; a++) glDisableVertexAttribArray(a);
        glVertexAttrib4f(4, 0, 0, 0, 1); glVertexAttrib4f(5, 1, 0, 0, 0); glVertexAttrib4f(6, 1, -1, 0, 0);
        for (int view = 0; view < VEG_IMPOSTOR_VIEWS; view++) {
            int layer = v->imp_layer[k][q] + view;
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex[0], 0, layer);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, tex[1], 0, layer);
            // Normal/AO clears to an up-facing normal with open AO: mips average
            // the background in, and a zero clear darkened distant trees.
            static const float c_alb[4] = {0.1f, 0.2f, 0.07f, 0.0f}, c_nrm[4] = {0.5f, 0.9f, 0.5f, 0.85f};
            glClearBufferfv(GL_COLOR, 0, c_alb); glClearBufferfv(GL_COLOR, 1, c_nrm); glClear(GL_DEPTH_BUFFER_BIT);
            float vp[16]; ortho_view(view * 2 * VPI / VEG_IMPOSTOR_VIEWS, vm->radius, vm->height, vp);
            gfx_setmat4(p, "u_viewproj", vp);
            glDrawElements(GL_TRIANGLES, vm->m.index_count, GL_UNSIGNED_INT, 0);
        }
    }
    glBindVertexArray(0);
    gfx_set1i(p, "u_bake", 0);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fbo); glDeleteRenderbuffers(1, &depth);
    GLenum one = GL_COLOR_ATTACHMENT0; (void)one;
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
    v->near_range = 150.0f; v->far_range = 4500.0f; v->ground_range = 90.0f;
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
    // Meshes.
    int tris = 0;
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < VEG_VARIANTS; q++) {
        MB mb; build_species(&mb, k, q);
        v->mesh[k][q] = (VegMesh){upload_mesh(&mb), fmaxf(mb.rad, 0.5f) * 1.08f, mb.height * 1.04f + 0.2f, mb.ni / 3};
        tris += mb.ni / 3; free(mb.v); free(mb.i);
        if (k == VEG_FERN || k == VEG_HORSETAIL || k == VEG_SHRUB || k == VEG_CONIFER || k == VEG_BROADLEAF || k == VEG_CARPET || k == VEG_GRASS) {
            g_lod = 1; build_species(&mb, k, q); g_lod = 0;
            v->mesh_lo[k][q] = (VegMesh){upload_mesh(&mb), fmaxf(mb.rad, 0.5f) * 1.08f, mb.height * 1.04f + 0.2f, mb.ni / 3};
            free(mb.v); free(mb.i);
        }
    }
    v->lod_dist = getenv("CRET_VEG_LOD") ? (float)atof(getenv("CRET_VEG_LOD")) : 25.0f;
    v->tree_lod_dist = getenv("CRET_VEG_TREE_LOD") ? (float)atof(getenv("CRET_VEG_TREE_LOD")) : 55.0f;
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
        for (int i = 0; i < n; i++) { tl->ymin = fminf(tl->ymin, buf[i].y); tl->ymax = fmaxf(tl->ymax, buf[i].y + 30.0f); }
        total += n;
    }
    CRET_LOAD_MARK("veg: tree scatter");
    fprintf(stderr, "cretaceous: vegetation %ld trees, %d mesh tris over %d species variants\n", total, tris, VEG_KINDS * VEG_VARIANTS);
    if (getenv("CRET_VEG_DEBUG")) {
        static const char* nm[VEG_KINDS] = {"conifer", "broadleaf", "cycad", "fern", "treefern", "horsetail", "palm", "shrub", "carpet", "grass"};
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
    free(v->far_list.a);
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
        fprintf(stderr, "veg ground: fern %d horsetail %d shrub %d\n", cnt[VEG_FERN], cnt[VEG_HORSETAIL], cnt[VEG_SHRUB]);
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
static void refresh_carpet(CretVeg* v, float cx, float cz) {
    if (v->carpet_range <= 0) { v->ncarpet = 0; return; }
    int gx = (int)floorf(cx / 6.0f), gz = (int)floorf(cz / 6.0f);
    if (v->carpet_valid && gx == v->carpet_cx && gz == v->carpet_cz) return;
    v->carpet_cx = gx; v->carpet_cz = gz; v->carpet_valid = true; v->ncarpet = 0;
    const TerraMap* m = v->map; const float sp = SPACING[VEG_CARPET], R = v->carpet_range + 8.0f;
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
        // Stands: a 9 m cell picks fern- or forb-rich sward.
        uint32_t cell = hashu((uint32_t)(int)floorf(x / 9.0f) * 73856093u ^ (uint32_t)(int)floorf(z / 9.0f) * 19349663u);
        int var = vr(&r) < 0.7f ? (int)(cell % 4) : (int)(vr(&r) * 3.99f);
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
    const TerraMap* m = v->map; const float sp = SPACING[VEG_GRASS];
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
    v->ground_valid = false; v->grass_valid = false;
    return n;
}

// Trunks as vertical cylinders: radius from the mesh height (thick conifers
// and broadleaves, slim tree ferns and palms), up to the crown.
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
            float th = m->height * p->scale, tr;
            switch (p->kind) {
            case VEG_CONIFER: case VEG_BROADLEAF: tr = fminf(fmaxf(0.03f * m->height, 0.15f), 1.0f) * p->scale; th *= 0.8f; break;
            case VEG_CYCAD: tr = 0.3f * p->scale; th *= 0.5f; break;
            default: tr = 0.25f * p->scale; th *= 0.85f; break;   // tree fern, palm
            }
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

void cret_veg_prepare(CretVeg* v, const VegFrameDesc* f) {
    v->prepared = true; v->prepared_frame = f->frame;
    gprof_mark("veg-trample");
    if (v->grass_on && v->trample) cret_trample_update(v->trample, f->csm, f->cam, f->time);
    gprof_mark("veg-cull");
    VFrustum fr = vfrustum(f->viewproj);
    float cx = f->cam[0], cy = f->cam[1], cz = f->cam[2];
    InstList (*near)[2 * VEG_VARIANTS] = v->lists; InstList* far_l = &v->far_list;   // q >= VEG_VARIANTS: LOD mesh
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) near[k][q].n = 0;
    far_l->n = 0;
    float band = 18.0f, nr = v->near_range, fr_ = v->far_range;
    int T = v->tiles_n, t0x = (int)floorf((cx - fr_) / VEG_TILE), t1x = (int)floorf((cx + fr_) / VEG_TILE);
    int t0z = (int)floorf((cz - fr_) / VEG_TILE), t1z = (int)floorf((cz + fr_) / VEG_TILE);
    for (int tz = t0z < 0 ? 0 : t0z; tz <= t1z && tz < T; tz++) for (int tx = t0x < 0 ? 0 : t0x; tx <= t1x && tx < T; tx++) {
        VegTile* tl = &v->tiles[tz * T + tx]; if (!tl->ntrees) continue;
        float x0 = tx * VEG_TILE, z0 = tz * VEG_TILE;
        float dx = fmaxf(fmaxf(x0 - cx, 0), cx - x0 - VEG_TILE), dz = fmaxf(fmaxf(z0 - cz, 0), cz - z0 - VEG_TILE);
        if (dx * dx + dz * dz > fr_ * fr_) continue;
        if (!vbox(&fr, x0 - 12, tl->ymin, z0 - 12, x0 + VEG_TILE + 12, tl->ymax, z0 + VEG_TILE + 12)) continue;
        for (int i = 0; i < tl->ntrees; i++) {
            const VegInstance* in = &tl->trees[i]; VegMesh* vm = &v->mesh[in->kind][in->variant];
            float ddx = in->x - cx, ddy = in->y - cy, ddz = in->z - cz, d = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
            float n_eff = nr * (in->kind == VEG_CYCAD ? 0.6f : 1.0f);
            if (d < n_eff) {
                float out = 1.0f - sstep(n_eff - band, n_eff, d), L0 = v->tree_lod_dist, lb = 4.0f;
                if (!v->mesh_lo[in->kind][in->variant].tris || d < L0 - lb) push(&near[in->kind][in->variant], in, out, 0, 0, 0);
                else if (d > L0 + lb) push(&near[in->kind][VEG_VARIANTS + in->variant], in, out, 0, 0, 0);
                else {
                    float f = 1.0f - sstep(L0 - lb, L0 + lb, d);
                    push(&near[in->kind][in->variant], in, f, 0, 0, 0);
                    push(&near[in->kind][VEG_VARIANTS + in->variant], in, -1.0f - f, 0, 0, 0);
                }
            }
            if (d > n_eff - band && d < fr_) {
                float fade = sstep(n_eff - band, n_eff, d) * (1.0f - sstep(fr_ * 0.85f, fr_, d));
                push(far_l, in, fade, (float)v->imp_layer[in->kind][in->variant], vm->radius, vm->height);
            }
        }
    }
    refresh_ground(v, cx, cz);
    for (int i = 0; i < v->nground; i++) {
        const VegInstance* in = &v->ground[i];
        float ddx = in->x - cx, ddz = in->z - cz, d2 = ddx * ddx + ddz * ddz, gr = v->ground_range * (in->kind == VEG_SHRUB ? 1.4f : 1.0f);
        if (d2 > gr * gr) continue;
        if (!vbox(&fr, in->x - 2, in->y - 1, in->z - 2, in->x + 2, in->y + 3, in->z + 2)) continue;
        float d = sqrtf(d2), out = 1.0f - sstep(gr * 0.8f, gr, d), band = 3.0f, L0 = v->lod_dist;
        bool has_lo = v->mesh_lo[in->kind][in->variant].tris > 0;
        if (!has_lo || d < L0 - band) push_cover(v, &near[in->kind][in->variant], in, out);
        else if (d > L0 + band) push_cover(v, &near[in->kind][VEG_VARIANTS + in->variant], in, out);
        else {   // complementary dither crossfade: negative fade keeps the other half
            float f = 1.0f - sstep(L0 - band, L0 + band, d);
            push_cover(v, &near[in->kind][in->variant], in, f);
            push_cover(v, &near[in->kind][VEG_VARIANTS + in->variant], in, -1.0f - f);
        }
    }
    // Sward: shrinks into the ground over the outer 40% of its range, where
    // the terrain colour takes over; reduced tuft beyond 9 m.
    refresh_carpet(v, cx, cz);
    float cr = v->carpet_range;
    for (int i = 0; i < v->ncarpet; i++) {
        VegInstance in = v->carpet[i];
        float ddx = in.x - cx, ddz = in.z - cz, d2 = ddx * ddx + ddz * ddz;
        if (d2 > cr * cr) continue;
        if (!vbox(&fr, in.x - 0.6f, in.y - 0.5f, in.z - 0.6f, in.x + 0.6f, in.y + 0.8f, in.z + 0.6f)) continue;
        float d = sqrtf(d2); in.scale *= 1.0f - sstep(cr * 0.6f, cr, d);
        if (in.scale < 0.08f) continue;
        push_cover(v, &near[VEG_CARPET][(d < 9.0f ? 0 : VEG_VARIANTS) + in.variant], &in, 1.0f);
    }
    // Tall grass: full mesh near, reduced past grass_lod; over the last 30% of
    // its range it shrinks a little and dithers out.
    refresh_grass(v, cx, cz);
    float gr = grass_range(v), gl = grass_lod(v);
    for (int i = 0; i < v->ngrass; i++) {
        VegInstance in = v->grass[i];
        float ddx = in.x - cx, ddz = in.z - cz, d2 = ddx * ddx + ddz * ddz;
        if (d2 > gr * gr) continue;
        if (!vbox(&fr, in.x - 1.3f, in.y - 0.3f, in.z - 1.3f, in.x + 1.3f, in.y + 2.4f, in.z + 1.3f)) continue;
        float d = sqrtf(d2), out = 1.0f - sstep(gr * 0.7f, gr, d), band = 2.5f;
        in.scale *= 0.7f + 0.3f * out;
        InstList* full = &near[VEG_GRASS][in.variant], * lo = &near[VEG_GRASS][VEG_VARIANTS + in.variant];
        if (d < gl - band) push_cover(v, full, &in, out);
        else if (d > gl + band) push_cover(v, lo, &in, out);
        else { float f = 1.0f - sstep(gl - band, gl + band, d); push_cover(v, full, &in, f); push_cover(v, lo, &in, -1.0f - f); }
    }
    // Upload everything into one buffer: buckets back to back.
    gprof_mark("veg-upload");
    size_t total = far_l->n; for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) total += near[k][q].n;
    v->drawn_meshes = v->drawn_impostors = v->drawn_ground = 0;
    if (!total) return;
    glBindBuffer(GL_ARRAY_BUFFER, v->inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GpuInst) * total, NULL, GL_STREAM_DRAW);
    size_t off = 0;
    for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) {
        v->list_off[k][q] = off; if (near[k][q].n) glBufferSubData(GL_ARRAY_BUFFER, off * sizeof(GpuInst), sizeof(GpuInst) * near[k][q].n, near[k][q].a);
        off += near[k][q].n;
    }
    v->far_off = off; if (far_l->n) glBufferSubData(GL_ARRAY_BUFFER, off * sizeof(GpuInst), sizeof(GpuInst) * far_l->n, far_l->a);
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
    if (v->far_list.n) {
        GfxProgram* ip = &v->imp_depth_prog; glUseProgram(ip->id);
        gfx_setmat4(ip, "u_viewproj", f->viewproj); gfx_set3f(ip, "u_cam", f->cam[0], f->cam[1], f->cam[2]);
        gfx_set1f(ip, "u_time", f->time); gfx_set2f(ip, "u_depth", f->depth_ab[0], f->depth_ab[1]);
        gfx_bind_tex(ip, "t_imp_albedo", 0, GL_TEXTURE_2D_ARRAY, v->t_imp_albedo);
        gfx_set1i(ip, "u_views", VEG_IMPOSTOR_VIEWS);
        bind_instances(v->quad_vao, v->inst_vbo, v->far_off * sizeof(GpuInst));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, v->far_list.n);
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
    if (getenv("CRET_VEG_DEBUG") && f->frame % 24 == 0) {   // instances and triangles per bucket
        long tris[2] = {0, 0}; int inst[2][2] = {{0, 0}, {0, 0}};
        for (int k = 0; k < VEG_KINDS; k++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) {
            const VegMesh* vm = q < VEG_VARIANTS ? &v->mesh[k][q] : &v->mesh_lo[k][q - VEG_VARIANTS];
            int t = is_tree(k) ? 0 : 1; tris[t] += (long)vm->tris * near[k][q].n; inst[t][q >= VEG_VARIANTS] += near[k][q].n;
        }
        fprintf(stderr, "veg: trees %d+%d lod (%ld tris), ground %d+%d lod (%ld tris), impostors %d\n", inst[0][0], inst[0][1], tris[0], inst[1][0], inst[1][1], tris[1], v->far_list.n);
    }
    // Far impostors.
    gprof_mark("veg-impostors");
    if (v->far_list.n) {
        GfxProgram* ip = &v->imp_prog; glUseProgram(ip->id); set_common(ip, f, v);
        gfx_set2f(ip, "u_depth", f->depth_ab[0], f->depth_ab[1]);
        gfx_bind_tex(ip, "t_imp_albedo", 0, GL_TEXTURE_2D_ARRAY, v->t_imp_albedo);
        gfx_bind_tex(ip, "t_imp_normal", 6, GL_TEXTURE_2D_ARRAY, v->t_imp_normal);
        gfx_set1i(ip, "u_views", VEG_IMPOSTOR_VIEWS);
        bind_instances(v->quad_vao, v->inst_vbo, v->far_off * sizeof(GpuInst));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, v->far_list.n);
        v->drawn_impostors = v->far_list.n;
    }
    glBindVertexArray(0);
}

// ---------------------------------------------------------------- sun shadow casters
static float g_light_fwd[3];
static int cmp_light_depth(const void* a, const void* b) {   // nearest to the sun first: early-z rejects the rest
    const float* p = ((const GpuInst*)a)->pos_scale, * q = ((const GpuInst*)b)->pos_scale;
    float da = p[0] * g_light_fwd[0] + p[1] * g_light_fwd[1] + p[2] * g_light_fwd[2];
    float db = q[0] * g_light_fwd[0] + q[1] * g_light_fwd[1] + q[2] * g_light_fwd[2];
    return (da > db) - (da < db);
}
int cret_veg_draw_shadow(CretVeg* v, const CretCsm* c, int k, const VegFrameDesc* f) {
    float lo[3], hi[3]; cret_csm_bounds(c, k, lo, hi);
    float cx = f->cam[0], cy = f->cam[1], cz = f->cam[2];
    InstList (*near)[2 * VEG_VARIANTS] = v->shadow_lists[k]; InstList* cards_l = &v->shadow_cards[k];
    for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) near[a][q].n = 0;
    cards_l->n = 0;
    // Meshes near the camera (same LOD choice as the view, no dither), cards
    // beyond the mesh range and everywhere in the last cascade.
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
                bool lo_mesh = v->mesh_lo[in->kind][in->variant].tris && (d > v->tree_lod_dist || k >= 2);
                push(&near[in->kind][(lo_mesh ? VEG_VARIANTS : 0) + in->variant], in, 1, 0, 0, 0);
            } else if (v->imp_layer[in->kind][in->variant] >= 0)
                push(cards_l, in, 1, (float)v->imp_layer[in->kind][in->variant], vm->radius, vm->height);
        }
    }
    if (k == 0) {   // ground cover: only the finest cascade, only within the view's ground range
        refresh_ground(v, cx, cz);
        for (int i = 0; i < v->nground; i++) {
            const VegInstance* in = &v->ground[i];
            float ddx = in->x - cx, ddz = in->z - cz, d2 = ddx * ddx + ddz * ddz, gr = v->ground_range * (in->kind == VEG_SHRUB ? 1.4f : 1.0f);
            if (d2 > gr * gr) continue;
            const VegMesh* vm = &v->mesh[in->kind][in->variant];
            float H = vm->height * in->scale, R = vm->radius * in->scale, pc[3] = {in->x, in->y + H * 0.5f, in->z};
            if (!cret_csm_caster_visible(c, k, pc, fmaxf(R, H * 0.5f), H)) continue;
            bool lo_mesh = v->mesh_lo[in->kind][in->variant].tris && sqrtf(d2) > v->lod_dist;
            push_cover(v, &near[in->kind][(lo_mesh ? VEG_VARIANTS : 0) + in->variant], in, 1);
        }
        // Tall grass near the camera (reduced mesh): it shades itself and the sward.
        float gr = fminf(grass_range(v), 16.0f), gl = grass_lod(v);
        for (int i = 0; i < v->ngrass; i++) {
            const VegInstance* in = &v->grass[i];
            float ddx = in->x - cx, ddz = in->z - cz, d2 = ddx * ddx + ddz * ddz;
            if (d2 > gr * gr) continue;
            float pc[3] = {in->x, in->y + 0.8f, in->z};
            if (!cret_csm_caster_visible(c, k, pc, 1.2f, 1.6f)) continue;
            push_cover(v, &near[VEG_GRASS][(d2 > gl * gl ? VEG_VARIANTS : 0) + in->variant], in, 1);
        }
    }
    size_t total = cards_l->n; for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) total += near[a][q].n;
    if (!total) return 0;
    memcpy(g_light_fwd, c->fwd, sizeof g_light_fwd);
    for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++)
        if (near[a][q].n > 1) qsort(near[a][q].a, near[a][q].n, sizeof(GpuInst), cmp_light_depth);
    GLuint vbo = v->shadow_vbo[k];
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GpuInst) * total, NULL, GL_STREAM_DRAW);
    size_t off = 0, offs[VEG_KINDS][2 * VEG_VARIANTS];
    for (int a = 0; a < VEG_KINDS; a++) for (int q = 0; q < 2 * VEG_VARIANTS; q++) {
        offs[a][q] = off; if (near[a][q].n) glBufferSubData(GL_ARRAY_BUFFER, off * sizeof(GpuInst), sizeof(GpuInst) * near[a][q].n, near[a][q].a);
        off += near[a][q].n;
    }
    size_t card_off = off; if (cards_l->n) glBufferSubData(GL_ARRAY_BUFFER, off * sizeof(GpuInst), sizeof(GpuInst) * cards_l->n, cards_l->a);
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
        glDrawElementsInstanced(GL_TRIANGLES, vm->m.index_count, GL_UNSIGNED_INT, 0, near[a][q].n);
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
