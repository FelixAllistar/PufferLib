// The plant atlas layers and the small helpers that the atlas painter
// (render/veg.c) and the mesh generator (render/veg_gen.h) share. GL-free, so
// tests/test_veg.c builds every plant mesh without a context.
#ifndef CRET_VEG_LAYERS_H
#define CRET_VEG_LAYERS_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define VPI 3.14159265f
#define AT 512                 // foliage atlas layer size (painted at this size)

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline uint32_t hashu(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }
typedef struct { uint32_t s; } VRng;
static inline float vr(VRng* r) { r->s = hashu(r->s + 0x9e3779b9u); return (r->s >> 8) * (1.0f / 16777216.0f); }
static inline float vrr(VRng* r, float a, float b) { return a + (b - a) * vr(r); }

// Layers 0..P_COUNT-1 have painted versions; the rest fall back to a painted
// sibling (FALLBACK in veg.c) when the photo sources in assets/foliage are missing.
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
static inline bool is_cluster_layer(int k) { return k >= L_CL_MAGN && k <= L_CL_ROPE; }
static inline int layer_columns(int k) { return k == L_LEAFLET || k == L_PSEG || k == L_ROPE || k == L_PETAL || k == L_FRUIT ? 8 : k == L_FPINNA || k == L_FUNGUS || k == L_CLUB ? 4 : 0; }
static inline bool is_bark_layer(int k) { return k == L_BARK || k == L_BARK_CON || k == L_BARK_PALM || k == L_BARK_FERN; }
// A texel of a layer while it is painted: linear colour and coverage.
typedef struct { float r, g, b, a; } Px;

// Leaf relief (render/veg.c build_relief): one layer's normal map, RGBA8 with
// the slope across (u) in A and along (v) in G.
static void blur_wrap(const float* src, float* dst, float* tmp, int r) {
    // Box blur of radius r along x, then y, wrapping at the layer's edges.
    float w = 1.0f / (2 * r + 1);
    for (int y = 0; y < AT; y++) {
        float acc = 0; for (int i = -r; i <= r; i++) acc += src[y * AT + (i & (AT - 1))];
        for (int x = 0; x < AT; x++) { tmp[y * AT + x] = acc * w; acc += src[y * AT + ((x + r + 1) & (AT - 1))] - src[y * AT + ((x - r) & (AT - 1))]; }
    }
    for (int x = 0; x < AT; x++) {
        float acc = 0; for (int i = -r; i <= r; i++) acc += tmp[(i & (AT - 1)) * AT + x];
        for (int y = 0; y < AT; y++) { dst[y * AT + x] = acc * w; acc += tmp[((y + r + 1) & (AT - 1)) * AT + x] - tmp[((y - r) & (AT - 1)) * AT + x]; }
    }
}
#define RELIEF_DOME_R 4        // texels: the blur radius of the alpha dome (twice over)
#define RELIEF_DETAIL 0.18f    // luminance detail's share of the height on leaves (on bark three times that)
#define RELIEF_SLOPE 7.0f      // height slope per texel to normal tilt
static void relief_layer(const Px* src, bool bark, uint8_t* out) {
    float* a = malloc(sizeof(float) * AT * AT * 5); float *dome = a + AT * AT, *lum = dome + AT * AT, *lb = lum + AT * AT, *tmp = lb + AT * AT;
    for (int i = 0; i < AT * AT; i++) { a[i] = src[i].a; lum[i] = logf(fmaxf(src[i].r * 0.3f + src[i].g * 0.59f + src[i].b * 0.11f, 1e-4f)); }
    blur_wrap(a, dome, tmp, RELIEF_DOME_R); blur_wrap(dome, dome, tmp, RELIEF_DOME_R);
    blur_wrap(lum, lb, tmp, 6);
    float det = bark ? 3 * RELIEF_DETAIL : RELIEF_DETAIL;
    // Height: the dome on leaves (bark is opaque: no dome), plus the log-luminance above its surround.
    for (int i = 0; i < AT * AT; i++) lum[i] = (bark ? 0 : dome[i]) + det * clampf(lum[i] - lb[i], -1.5f, 1.5f) * a[i];
    for (int y = 0; y < AT; y++) for (int x = 0; x < AT; x++) {
        const float* h = lum;
        float gx = 0.5f * (h[y * AT + ((x + 1) & (AT - 1))] - h[y * AT + ((x - 1) & (AT - 1))]), gy = 0.5f * (h[((y + 1) & (AT - 1)) * AT + x] - h[((y - 1) & (AT - 1)) * AT + x]);
        float nx = -RELIEF_SLOPE * gx, ny = -RELIEF_SLOPE * gy, il = 1.0f / sqrtf(nx * nx + ny * ny + 1);
        uint8_t* o = out + 4 * (y * AT + x);
        o[0] = 128; o[1] = (uint8_t)(clampf(ny * il, -1, 1) * 127 + 128); o[2] = 128; o[3] = (uint8_t)(clampf(nx * il, -1, 1) * 127 + 128);
    }
    free(a);
}
#endif
