// CPU texture payloads (texbuild.h). Compiled by including it from
// texcache.c, and by tests/test_cache.c.
#include "texbuild.h"
#include "cache.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TB_LINEAR 0x2601u
#define TB_NEAREST 0x2600u
#define TB_LINEAR_MIPMAP_LINEAR 0x2703u
#define TB_REPEAT 0x2901u
#define TB_CLAMP 0x812Fu

static bool tb_is_bc3(uint32_t f) { return f == TB_BC3 || f == TB_BC3_SRGB; }
static bool tb_is_rgba(uint32_t f) { return f == TB_RGBA8 || f == TB_SRGB8_A8 || tb_is_bc3(f); }
static bool tb_is_srgb(uint32_t f) { return f == TB_SRGB8_A8 || f == TB_BC3_SRGB; }
static int tb_dim(int s, int l) { s >>= l; return s > 0 ? s : 1; }

size_t cret_tex_level_bytes(uint32_t ifmt, int w, int h, int d) {
    switch (ifmt) {
    case 0x83F0: case 0x83F1: case 0x8C4C: case 0x8C4D: return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 8 * d;    // BC1
    case 0x83F2: case 0x83F3: case 0x8C4E: case 0x8C4F: return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16 * d;   // BC2, BC3
    case TB_RGBA8: case TB_SRGB8_A8: case TB_R32F: return (size_t)w * h * d * 4;
    case TB_RG32F: case 0x881A /*RGBA16F*/: return (size_t)w * h * d * 8;
    default: return 0;
    }
}

uint8_t* cret_tex_payload_alloc(uint32_t target, uint32_t ifmt, int w, int h, int d, int levels, bool linear, bool repeat, float aniso, size_t* size) {
    *size = 0;
    if (w < 1 || h < 1 || d < 1 || !cret_tex_level_bytes(ifmt, 1, 1, 1)) return NULL;
    int full = 1; while ((w > h ? w : h) >> full) full++;
    if (levels <= 0 || levels > full) levels = full;
    size_t total = sizeof(TcHeader);
    for (int l = 0; l < levels; l++) total += cret_tex_level_bytes(ifmt, tb_dim(w, l), tb_dim(h, l), d);
    uint8_t* p = calloc(1, total); if (!p) return NULL;
    TcHeader hd = {TC_MAGIC, target, ifmt, (uint32_t)w, (uint32_t)h, (uint32_t)d, (uint32_t)levels,
                   repeat ? TB_REPEAT : TB_CLAMP, repeat ? TB_REPEAT : TB_CLAMP,
                   levels > 1 ? TB_LINEAR_MIPMAP_LINEAR : (linear ? TB_LINEAR : TB_NEAREST), linear ? TB_LINEAR : TB_NEAREST, aniso};
    memcpy(p, &hd, sizeof hd);
    *size = total; return p;
}

uint8_t* cret_tex_payload_level(uint8_t* payload, int l) {
    TcHeader h; memcpy(&h, payload, sizeof h);
    size_t at = sizeof h;
    for (int k = 0; k < l; k++) at += cret_tex_level_bytes(h.ifmt, tb_dim((int)h.w, k), tb_dim((int)h.h, k), (int)h.d);
    return payload + at;
}

// ---------------------------------------------------------------- BC3
static uint16_t tb_565(const float c[3]) {
    int r = (int)(c[0] * 31.0f / 255.0f + 0.5f), g = (int)(c[1] * 63.0f / 255.0f + 0.5f), b = (int)(c[2] * 31.0f / 255.0f + 0.5f);
    r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 63 ? 63 : g; b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static void tb_from565(uint16_t c, int o[3]) {
    int r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
    o[0] = r << 3 | r >> 2; o[1] = g << 2 | g >> 4; o[2] = b << 3 | b >> 2;
}

static void bc3_alpha(const uint8_t* px, uint8_t out[8]) {
    int mn = 255, mx = 0;
    for (int i = 0; i < 16; i++) { int a = px[4 * i + 3]; if (a < mn) mn = a; if (a > mx) mx = a; }
    memset(out, 0, 8); out[0] = (uint8_t)mx; out[1] = (uint8_t)mn;   // a0 > a1: eight levels
    if (mx == mn) return;
    uint64_t bits = 0;
    for (int i = 0; i < 16; i++) {
        int t = ((px[4 * i + 3] - mn) * 14 + (mx - mn)) / (2 * (mx - mn));   // round(7 (a - min) / (max - min))
        int code = t == 7 ? 0 : t == 0 ? 1 : 8 - t;                          // codes 2..7 run from a0 toward a1
        bits |= (uint64_t)code << (3 * i);
    }
    for (int k = 0; k < 6; k++) out[2 + k] = (uint8_t)(bits >> (8 * k));
}

static void bc3_color(const uint8_t* px, uint8_t out[8]) {
    float mean[3] = {0, 0, 0};
    for (int i = 0; i < 16; i++) for (int c = 0; c < 3; c++) mean[c] += px[4 * i + c];
    for (int c = 0; c < 3; c++) mean[c] /= 16;
    float cov[6] = {0};   // xx xy xz yy yz zz
    for (int i = 0; i < 16; i++) {
        float r = px[4 * i] - mean[0], g = px[4 * i + 1] - mean[1], b = px[4 * i + 2] - mean[2];
        cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
    }
    // Principal axis by power iteration (as stb_dxt), from the widest channel.
    float ax[3] = {cov[0] >= cov[3] && cov[0] >= cov[5] ? 1.0f : 0.3f, cov[3] >= cov[0] && cov[3] >= cov[5] ? 1.0f : 0.3f, cov[5] >= cov[0] && cov[5] >= cov[3] ? 1.0f : 0.3f};
    for (int it = 0; it < 6; it++) {
        float x = ax[0] * cov[0] + ax[1] * cov[1] + ax[2] * cov[2];
        float y = ax[0] * cov[1] + ax[1] * cov[3] + ax[2] * cov[4];
        float z = ax[0] * cov[2] + ax[1] * cov[4] + ax[2] * cov[5];
        float m = fmaxf(fabsf(x), fmaxf(fabsf(y), fabsf(z)));
        if (m < 1e-6f) { ax[0] = ax[1] = ax[2] = 0.57735f; break; }
        ax[0] = x / m; ax[1] = y / m; ax[2] = z / m;
    }
    float len = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); for (int c = 0; c < 3; c++) ax[c] /= len;
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 16; i++) {
        float d = (px[4 * i] - mean[0]) * ax[0] + (px[4 * i + 1] - mean[1]) * ax[1] + (px[4 * i + 2] - mean[2]) * ax[2];
        if (d < lo) lo = d; if (d > hi) hi = d;
    }
    float inset = (hi - lo) / 32.0f; hi -= inset; lo += inset;   // the palette ends sit a little inside the extremes
    float e0[3], e1[3];
    for (int c = 0; c < 3; c++) { e0[c] = mean[c] + ax[c] * hi; e1[c] = mean[c] + ax[c] * lo; }
    uint16_t c0 = tb_565(e0), c1 = tb_565(e1);
    if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; }   // BC3 is always four-colour; keep the BC1 order anyway
    out[0] = (uint8_t)c0; out[1] = (uint8_t)(c0 >> 8); out[2] = (uint8_t)c1; out[3] = (uint8_t)(c1 >> 8);
    uint32_t idx = 0;
    if (c0 != c1) {
        int p[4][3]; tb_from565(c0, p[0]); tb_from565(c1, p[1]);
        for (int c = 0; c < 3; c++) { p[2][c] = (2 * p[0][c] + p[1][c]) / 3; p[3][c] = (p[0][c] + 2 * p[1][c]) / 3; }
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 4; k++) {
                int dr = px[4 * i] - p[k][0], dg = px[4 * i + 1] - p[k][1], db = px[4 * i + 2] - p[k][2], d = dr * dr + dg * dg + db * db;
                if (d < bd) { bd = d; best = k; }
            }
            idx |= (uint32_t)best << (2 * i);
        }
    }
    out[4] = (uint8_t)idx; out[5] = (uint8_t)(idx >> 8); out[6] = (uint8_t)(idx >> 16); out[7] = (uint8_t)(idx >> 24);
}

void cret_bc3_encode(const uint8_t rgba[64], uint8_t out[16]) { bc3_alpha(rgba, out); bc3_color(rgba, out + 8); }

void cret_bc3_decode(const uint8_t in[16], uint8_t rgba[64]) {
    int a0 = in[0], a1 = in[1], ap[8] = {a0, a1};
    if (a0 > a1) for (int k = 1; k < 7; k++) ap[k + 1] = ((7 - k) * a0 + k * a1) / 7;
    else { for (int k = 1; k < 5; k++) ap[k + 1] = ((5 - k) * a0 + k * a1) / 5; ap[6] = 0; ap[7] = 255; }
    uint64_t ab = 0; for (int k = 0; k < 6; k++) ab |= (uint64_t)in[2 + k] << (8 * k);
    uint16_t c0 = (uint16_t)(in[8] | in[9] << 8), c1 = (uint16_t)(in[10] | in[11] << 8);
    int p[4][3]; tb_from565(c0, p[0]); tb_from565(c1, p[1]);
    for (int c = 0; c < 3; c++) { p[2][c] = (2 * p[0][c] + p[1][c]) / 3; p[3][c] = (p[0][c] + 2 * p[1][c]) / 3; }
    uint32_t ci = (uint32_t)in[12] | (uint32_t)in[13] << 8 | (uint32_t)in[14] << 16 | (uint32_t)in[15] << 24;
    for (int i = 0; i < 16; i++) {
        int k = ci >> (2 * i) & 3;
        for (int c = 0; c < 3; c++) rgba[4 * i + c] = (uint8_t)p[k][c];
        rgba[4 * i + 3] = (uint8_t)ap[ab >> (3 * i) & 7];
    }
}

// ---------------------------------------------------------------- mips
static float g_s2l[256]; static uint8_t g_l2s[4097]; static bool g_lut;
static void tb_luts(void) {
    if (g_lut) return;
    for (int i = 0; i < 256; i++) { float c = i / 255.0f; g_s2l[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
    for (int i = 0; i <= 4096; i++) {
        float l = i / 4096.0f, s = l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f;
        g_l2s[i] = (uint8_t)(s * 255.0f + 0.5f);
    }
    g_lut = true;
}

typedef struct {
    uint32_t ifmt; int w, h, layers, levels;
    const uint8_t** src;   // per layer: the current level's texels (RGBA8)
    uint8_t** next;        // per layer: the next level (owned)
    uint8_t* dst;          // the payload level being written
    int lw, lh, chunk_rows, chunks;
} TbJob;

// Level texels into the payload: BC3 blocks over a band of block rows, or a copy.
static void tb_emit(int i, void* ctx) {
    TbJob* J = ctx; int layer = i / J->chunks, band = i % J->chunks, w = J->lw, h = J->lh;
    const uint8_t* s = J->src[layer];
    if (!tb_is_bc3(J->ifmt)) {
        if (band == 0) memcpy(J->dst + (size_t)layer * w * h * 4, s, (size_t)w * h * 4);
        return;
    }
    int bw = (w + 3) / 4, bh = (h + 3) / 4, y0 = band * J->chunk_rows, y1 = y0 + J->chunk_rows < bh ? y0 + J->chunk_rows : bh;
    uint8_t* out = J->dst + (size_t)layer * bw * bh * 16;
    for (int by = y0; by < y1; by++) for (int bx = 0; bx < bw; bx++) {
        uint8_t blk[64];
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {   // edge texels repeat in blocks past a small level's edge
            int sx = bx * 4 + x, sy = by * 4 + y; if (sx >= w) sx = w - 1; if (sy >= h) sy = h - 1;
            memcpy(blk + 4 * (y * 4 + x), s + 4 * ((size_t)sy * w + sx), 4);
        }
        cret_bc3_encode(blk, out + 16 * ((size_t)by * bw + bx));
    }
}

// The next level of one layer: 2x2 box filter (linear light for sRGB colour).
static void tb_down(int layer, void* ctx) {
    TbJob* J = ctx; int w = J->lw, h = J->lh, nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
    const uint8_t* s = J->src[layer]; uint8_t* d = malloc((size_t)nw * nh * 4); J->next[layer] = d;
    if (!d) return;
    bool srgb = tb_is_srgb(J->ifmt);
    for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) {
        int x0 = 2 * x < w ? 2 * x : w - 1, x1 = 2 * x + 1 < w ? 2 * x + 1 : w - 1, y0 = 2 * y < h ? 2 * y : h - 1, y1 = 2 * y + 1 < h ? 2 * y + 1 : h - 1;
        const uint8_t* q[4] = {s + 4 * ((size_t)y0 * w + x0), s + 4 * ((size_t)y0 * w + x1), s + 4 * ((size_t)y1 * w + x0), s + 4 * ((size_t)y1 * w + x1)};
        uint8_t* o = d + 4 * ((size_t)y * nw + x);
        for (int c = 0; c < 4; c++) {
            if (srgb && c < 3) {
                float l = 0.25f * (g_s2l[q[0][c]] + g_s2l[q[1][c]] + g_s2l[q[2][c]] + g_s2l[q[3][c]]);
                o[c] = g_l2s[(int)(l * 4096.0f + 0.5f)];
            } else o[c] = (uint8_t)((q[0][c] + q[1][c] + q[2][c] + q[3][c] + 2) / 4);
        }
    }
}

uint8_t* cret_tex_payload_build(uint32_t target, uint32_t ifmt, int w, int h, int layers, const void* const* texels,
                                bool mips, bool linear, bool repeat, float aniso, size_t* size) {
    bool rgba = tb_is_rgba(ifmt);
    if (!rgba && ifmt != TB_R32F && ifmt != TB_RG32F) { *size = 0; return NULL; }
    uint8_t* p = cret_tex_payload_alloc(target, ifmt, w, h, layers, rgba && mips ? 0 : 1, linear, repeat, aniso, size);
    if (!p) return NULL;
    TcHeader hd; memcpy(&hd, p, sizeof hd);
    if (!rgba) {   // floats: level 0 as given
        size_t lb = cret_tex_level_bytes(ifmt, w, h, 1);
        for (int k = 0; k < layers; k++) memcpy(p + sizeof hd + lb * k, texels[k], lb);
        return p;
    }
    tb_luts();
    const uint8_t** src = malloc(sizeof(void*) * layers); uint8_t** next = calloc(layers, sizeof(void*)); uint8_t** owned = calloc(layers, sizeof(void*));
    bool ok = src && next && owned;
    for (int k = 0; ok && k < layers; k++) src[k] = texels[k];
    for (int l = 0; ok && l < (int)hd.levels; l++) {
        TbJob J = {ifmt, w, h, layers, (int)hd.levels, src, next, cret_tex_payload_level(p, l), tb_dim(w, l), tb_dim(h, l), 16, 1};
        int bh = (J.lh + 3) / 4; J.chunks = tb_is_bc3(ifmt) ? (bh + J.chunk_rows - 1) / J.chunk_rows : 1;
        cret_parallel_for(layers * J.chunks, 0, tb_emit, &J);
        if (l + 1 < (int)hd.levels) {
            cret_parallel_for(layers, 0, tb_down, &J);
            for (int k = 0; k < layers; k++) { if (!next[k]) ok = false; free(owned[k]); owned[k] = next[k]; src[k] = next[k]; next[k] = NULL; }
        }
    }
    if (owned) for (int k = 0; k < layers; k++) free(owned[k]);
    free(src); free(next); free(owned);
    if (!ok) { free(p); *size = 0; return NULL; }
    return p;
}
