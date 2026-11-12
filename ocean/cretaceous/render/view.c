#include "view.h"
#include "critters.h"   // small animals; render/critters.c is compiled below, next to the agents
#include "scentfx.h"    // the scent view; render/scentfx.c is compiled below
#include "gprof.h"
#include "water_flow.h"
#include "cache.c"      // compiled here: build.sh lists the player sources outside ocean/cretaceous
#include "texcache.c"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define SHADER_DIR "ocean/cretaceous/shaders/"
#define TEX_DIR "ocean/cretaceous/assets/textures/"
#define CACHE_DIR "build/cretaceous/"
#define HORIZON_DIRS 8

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float fractf(float x) { return x - floorf(x); }
static inline float sstepf(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }

// ---------------------------------------------------------------- tiling noise (CPU, for textures)
static inline uint32_t hash3(int x, int y, int z, uint32_t s) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu ^ s * 0x165667b1u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h;
}
static inline float hf(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }
static inline int wrapi(int a, int p) { a %= p; return a < 0 ? a + p : a; }
// Periodic 3D gradient noise with period p (lattice units), ~[-1,1].
static float pnoise3(float x, float y, float z, int p, uint32_t s) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;
    float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10), w = fz * fz * fz * (fz * (fz * 6 - 15) + 10);
    float acc[8];
    for (int c = 0; c < 8; c++) {
        int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        uint32_t h = hash3(wrapi(ix + dx, p), wrapi(iy + dy, p), wrapi(iz + dz, p), s);
        float gx = hf(h) * 2 - 1, gy = hf(h * 747796405u + 1) * 2 - 1, gz = hf(h * 2891336453u + 7) * 2 - 1;
        acc[c] = gx * (fx - dx) + gy * (fy - dy) + gz * (fz - dz);
    }
    float x00 = lerpf(acc[0], acc[1], u), x10 = lerpf(acc[2], acc[3], u), x01 = lerpf(acc[4], acc[5], u), x11 = lerpf(acc[6], acc[7], u);
    return 1.6f * lerpf(lerpf(x00, x10, v), lerpf(x01, x11, v), w);
}
// Periodic Worley (F1 distance), returns 0..1 (1 at feature points after inversion by caller).
static float worley3(float x, float y, float z, int p, uint32_t s) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z); float best = 9;
    for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int cx = ix + dx, cy = iy + dy, cz = iz + dz; uint32_t h = hash3(wrapi(cx, p), wrapi(cy, p), wrapi(cz, p), s);
        float px = cx + hf(h), py = cy + hf(h * 747796405u + 3), pz = cz + hf(h * 2891336453u + 5);
        float d = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z); if (d < best) best = d;
    }
    return clampf(sqrtf(best), 0, 1);
}
static float worley_fbm(float x, float y, float z, int p, uint32_t s) {
    return 1 - (0.625f * worley3(x, y, z, p, s) + 0.25f * worley3(x * 2, y * 2, z * 2, p * 2, s + 1) + 0.125f * worley3(x * 4, y * 4, z * 4, p * 4, s + 2));
}
// Periodic 2D gradient noise with separate periods per axis (anisotropic
// patterns such as cloud streets still tile), ~[-1,1].
static float pnoise2a(float x, float y, int px, int py, uint32_t s) {
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy;
    float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    float acc[4];
    for (int c = 0; c < 4; c++) {
        int dx = c & 1, dy = c >> 1; uint32_t h = hash3(wrapi(ix + dx, px), wrapi(iy + dy, py), 7, s);
        float gx = hf(h) * 2 - 1, gy = hf(h * 747796405u + 1) * 2 - 1;
        acc[c] = gx * (fx - dx) + gy * (fy - dy);
    }
    return 1.4f * lerpf(lerpf(acc[0], acc[1], u), lerpf(acc[2], acc[3], u), v);
}
// fbm of pnoise2a over the unit tile: fx, fy base periods, n octaves, 0..1.
static float fbm2a(float u, float v, int fx, int fy, int n, uint32_t s) {
    float acc = 0, amp = 1, tot = 0;
    for (int o = 0; o < n; o++) { acc += amp * pnoise2a(u * fx, v * fy, fx, fy, s + o); tot += amp; amp *= 0.5f; fx *= 2; fy *= 2; }
    return clampf(acc / tot * 0.5f + 0.5f, 0, 1);
}
static float remapf(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }

static bool read_cache(const char* path, void* data, size_t n) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    bool ok = fread(data, 1, n, f) == n; fclose(f); return ok;
}
static void write_cache(const char* path, const void* data, size_t n) {
    mkdir("build", 0755); mkdir(CACHE_DIR, 0755);
    FILE* f = fopen(path, "wb"); if (!f) return; fwrite(data, 1, n, f); fclose(f);
}

// Stretch a channel so its 2nd..98th percentiles span 0..1 (raw Perlin and
// Worley sums are low-contrast, which leaves clouds as smooth blobs).
static void stretch_channel(float* f, size_t n, uint8_t* out, int stride) {
    float lo = 1e9f, hi = -1e9f; for (size_t i = 0; i < n; i++) { lo = fminf(lo, f[i]); hi = fmaxf(hi, f[i]); }
    enum { B = 1024 }; size_t hist[B] = {0};
    for (size_t i = 0; i < n; i++) hist[(int)fminf((f[i] - lo) / (hi - lo + 1e-9f) * B, B - 1)]++;
    size_t acc = 0; float p2 = lo, p98 = hi; bool got2 = false;
    for (int k = 0; k < B; k++) {
        acc += hist[k];
        if (!got2 && acc >= n / 50) { p2 = lo + (hi - lo) * k / B; got2 = true; }
        if (acc >= n - n / 50) { p98 = lo + (hi - lo) * (k + 1) / B; break; }
    }
    for (size_t i = 0; i < n; i++) out[i * stride] = (uint8_t)(clampf((f[i] - p2) / (p98 - p2 + 1e-9f), 0, 1) * 255 + 0.5f);
}

static void make_cloud_noise(CretView* v) {
    const int N = 128, D = 64; size_t ns = (size_t)N * N * N * 4, nd = (size_t)D * D * D * 4;
    uint8_t* shape = malloc(ns); uint8_t* detail = malloc(nd);
    if (!read_cache(CACHE_DIR "cloud_shape_v2.bin", shape, ns)) {
        fprintf(stderr, "cretaceous: generating cloud noise (one time)\n");
        size_t n3 = (size_t)N * N * N; float* ch[4]; for (int c = 0; c < 4; c++) ch[c] = malloc(sizeof(float) * n3);
        #pragma omp parallel for schedule(dynamic, 1)
        for (int z = 0; z < N; z++) for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
            float fx = (float)x / N, fy = (float)y / N, fz = (float)z / N;
            float perlin = 0, amp = 1, tot = 0;
            for (int o = 0; o < 4; o++) { int f = 4 << o; perlin += amp * pnoise3(fx * f, fy * f, fz * f, f, 11 + o); tot += amp; amp *= 0.5f; }
            perlin = perlin / tot * 0.5f + 0.5f;
            float w1 = worley_fbm(fx * 4, fy * 4, fz * 4, 4, 21), w2 = worley_fbm(fx * 8, fy * 8, fz * 8, 8, 31), w3 = worley_fbm(fx * 16, fy * 16, fz * 16, 16, 41);
            size_t i = ((size_t)z * N + y) * N + x;
            ch[0][i] = remapf(perlin, w1 - 1, 1, 0, 1);   // Perlin-Worley
            ch[1][i] = w1; ch[2][i] = w2; ch[3][i] = w3;
        }
        for (int c = 0; c < 4; c++) { stretch_channel(ch[c], n3, shape + c, 4); free(ch[c]); }
        write_cache(CACHE_DIR "cloud_shape_v2.bin", shape, ns);
    }
    if (!read_cache(CACHE_DIR "cloud_detail_v3.bin", detail, nd)) {
        // v3: 64^3 (was 32^3), so the erosion detail stays crisp when a
        // cloud fills the screen (texel 4.4 m at the 280 m repeat).
        size_t n3 = (size_t)D * D * D; float* ch[3]; for (int c = 0; c < 3; c++) ch[c] = malloc(sizeof(float) * n3);
        #pragma omp parallel for schedule(dynamic, 1)
        for (int z = 0; z < D; z++) for (int y = 0; y < D; y++) for (int x = 0; x < D; x++) {
            float fx = (float)x / D, fy = (float)y / D, fz = (float)z / D; size_t i = ((size_t)z * D + y) * D + x;
            ch[0][i] = worley_fbm(fx * 2, fy * 2, fz * 2, 2, 51);
            ch[1][i] = worley_fbm(fx * 4, fy * 4, fz * 4, 4, 61);
            ch[2][i] = worley_fbm(fx * 8, fy * 8, fz * 8, 8, 71);
        }
        for (int c = 0; c < 3; c++) { stretch_channel(ch[c], n3, detail + c, 4); free(ch[c]); }
        for (size_t i = 0; i < n3; i++) detail[i * 4 + 3] = 255;
        write_cache(CACHE_DIR "cloud_detail_v3.bin", detail, nd);
    }
    v->t_cloud_shape = gfx_tex3d(N, N, N, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, shape, true);
    v->t_cloud_detail = gfx_tex3d(D, D, D, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, detail, true);
    free(shape); free(detail);
    // Weather map (tiles over 30 km; terrain cloud shadows read R too).
    // R: low-cloud coverage field. Continuous merged noise, not one dome per
    //    cloud: cumulus clumps of every size (5-octave fbm plus a Worley
    //    billow term) inside broad fields with clear lanes (organisation),
    //    rowed along x by an anisotropic term (cloud streets). The slider
    //    remaps it in the shader, so more cover adds clouds and fattens them.
    // G: cloud type, 0 flat stratocumulus .. 0.5 cumulus .. 1 towering: the
    //    middle of big dense fields towers, stragglers stay flat.
    // B: high-cloud mask for the thin layers (altocumulus deck, cirrus wisps).
    const int W = 512; uint8_t* wm = malloc((size_t)W * W * 4);
    float* cov = malloc(sizeof(float) * W * W); float* typ = malloc(sizeof(float) * W * W); float* hi = malloc(sizeof(float) * W * W);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
        float u = (x + 0.5f) / W, v = (y + 0.5f) / W; size_t i = (size_t)y * W + x;
        float org = fbm2a(u, v, 2, 2, 3, 101);                                    // fields and gaps (~15 km)
        float cells = fbm2a(u, v, 6, 6, 5, 111);                                  // clumps, 5 km down to 300 m
        float bil = 1 - worley3(u * 14, v * 14, 0.37f, 14, 121);                  // round billow cores
        float street = fbm2a(u, v, 3, 18, 2, 131);                                // rows along x
        float c = (0.68f * cells + 0.32f * bil) * (0.5f + 0.5f * sstepf(0.25f, 0.75f, org)) + 0.16f * (street - 0.5f);
        cov[i] = c;
        float tn = fbm2a(u, v, 4, 4, 2, 141);
        typ[i] = 0.42f + 0.55f * (org - 0.5f) + 0.5f * (c - 0.5f) + 0.3f * (tn - 0.5f);
        float wisps = fbm2a(u, v, 2, 9, 3, 151), org2 = fbm2a(u, v, 2, 2, 2, 161);
        hi[i] = 0.6f * wisps + 0.4f * org2;
    }
    stretch_channel(cov, (size_t)W * W, wm + 0, 4);
    stretch_channel(hi, (size_t)W * W, wm + 2, 4);
    for (size_t i = 0; i < (size_t)W * W; i++) { wm[i * 4 + 1] = (uint8_t)(clampf(typ[i], 0, 1) * 255 + 0.5f); wm[i * 4 + 3] = 255; }
    free(cov); free(typ); free(hi);
    v->t_weather = gfx_tex2d(W, W, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, wm, true, true, true);
    free(wm);
}

// General tiling noise: R/G fbm at two scales, B/A wave normal (xy).
static void make_noise_texture(CretView* v) {
    uint64_t key = cret_hash_str(cret_hash_u64(CRET_HASH_SEED, CRET_TEX_CACHE_VERSION), "view noise v1 512");   // bump when the noise below changes
    if ((v->t_noise = cret_tex_cache_load("view_noise", key))) return;
    const int N = 512; float* hgt = malloc(sizeof(float) * N * N); uint8_t* t = malloc((size_t)N * N * 4);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float fx = (float)x / N, fy = (float)y / N, a = 0, b = 0, w = 0, amp = 1, tot = 0;
        for (int o = 0; o < 6; o++) { int f = 4 << o; a += amp * pnoise3(fx * f, fy * f, 0.25f, f, 101 + o); b += amp * pnoise3(fx * f, fy * f, 0.75f, f, 201 + o); tot += amp; amp *= 0.5f; }
        amp = 1; float wt = 0;
        for (int o = 0; o < 5; o++) { int f = 8 << o; w += amp * pnoise3(fx * f, fy * f, 0.4f, f, 301 + o); wt += amp; amp *= 0.6f; }
        hgt[y * N + x] = w / wt;
        uint8_t* o = t + ((size_t)y * N + x) * 4;
        o[0] = (uint8_t)(clampf(a / tot * 0.5f + 0.5f, 0, 1) * 255); o[1] = (uint8_t)(clampf(b / tot * 0.5f + 0.5f, 0, 1) * 255);
    }
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float dx = hgt[y * N + (x + 1) % N] - hgt[y * N + (x + N - 1) % N], dy = hgt[((y + 1) % N) * N + x] - hgt[((y + N - 1) % N) * N + x];
        float nx = -dx * 6, ny = -dy * 6, l = sqrtf(nx * nx + ny * ny + 1);
        uint8_t* o = t + ((size_t)y * N + x) * 4; o[2] = (uint8_t)((nx / l * 0.5f + 0.5f) * 255); o[3] = (uint8_t)((ny / l * 0.5f + 0.5f) * 255);
    }
    const void* lv[1] = {t};
    v->t_noise = cret_tex_build("view_noise", key, GL_TEXTURE_2D, GL_RGBA8, N, N, 1, lv, true, true, true, 0);
    free(hgt); free(t);
}

// ---------------------------------------------------------------- terrain data
// Horizon angles in 8 azimuths (half resolution), used for soft sun shadows at
// any time of day and for ambient occlusion. Cached beside the world.
static void bake_horizon(const TerraMap* m, uint8_t* hz, uint8_t* ao, int hn) {
    int n = m->n; float step0 = m->cell * (float)(n - 1) / (hn - 1);
    #pragma omp parallel for schedule(dynamic, 4)
    for (int z = 0; z < hn; z++) for (int x = 0; x < hn; x++) {
        float wx = x * step0, wz = z * step0; float h0 = terra_height(m, wx, wz) + 0.3f; float vis = 0;
        for (int k = 0; k < HORIZON_DIRS; k++) {
            float a = k * 6.2831853f / HORIZON_DIRS, dx = cosf(a), dz = sinf(a), best = 0, t = step0;
            for (int s = 0; s < 34 && t < 1500; s++, t *= 1.16f) {
                float h = terra_height(m, wx + dx * t, wz + dz * t), tn = (h - h0) / t; if (tn > best) best = tn;
            }
            float ang = atanf(best); hz[((size_t)z * hn + x) * HORIZON_DIRS + k] = (uint8_t)(clampf(ang / 1.5707963f, 0, 1) * 255);
            vis += 1 - sinf(ang);
        }
        ao[(size_t)z * hn + x] = (uint8_t)(clampf(vis / HORIZON_DIRS, 0, 1) * 255);
    }
}

// Terrain textures are built on the CPU and cached one by one (texcache.h).
static GLuint terrain_tex(const char* name, uint64_t key, int w, int h, GLenum ifmt, const void* data, bool mips) {
    const void* lv[1] = {data};
    return cret_tex_build(name, key, GL_TEXTURE_2D, ifmt, w, h, 1, lv, mips, true, false, 0);
}
static void build_terrain(CretView* v, uint64_t key) {
    const TerraMap* m = &v->world->map; int n = m->n; size_t N = (size_t)n * n;
    // The world's B-spline coefficients (terra_bspline_coefficients): the
    // sim stands agents on the same smooth surface (cret_height).
    v->t_height = terrain_tex("terrain_height", key, n, n, GL_R32F, v->world->hcoef, false);
    CRET_LOAD_MARK("terrain: b-spline height");
    // Horizon + AO at half resolution.
    int hn = n / 2 + 1; size_t HN = (size_t)hn * hn;
    uint8_t* hz = malloc(HN * HORIZON_DIRS + HN); uint8_t* ao = hz + HN * HORIZON_DIRS;
    char path[512]; snprintf(path, sizeof path, CACHE_DIR "worlds/horizon_s%u_n%d_%016llx.bin", m->seed, n, (unsigned long long)terra_params_hash(&v->world->params));
    if (!read_cache(path, hz, HN * HORIZON_DIRS + HN)) {
        fprintf(stderr, "cretaceous: baking terrain horizons (one time)\n");
        bake_horizon(m, hz, ao, hn); write_cache(path, hz, HN * HORIZON_DIRS + HN);
    }
    uint8_t* h0 = malloc(HN * 4); uint8_t* h1 = malloc(HN * 4);
    for (size_t i = 0; i < HN; i++) { memcpy(h0 + i * 4, hz + i * 8, 4); memcpy(h1 + i * 4, hz + i * 8 + 4, 4); }
    v->t_horizon0 = terrain_tex("terrain_horizon0", key, hn, hn, GL_RGBA8, h0, false);
    v->t_horizon1 = terrain_tex("terrain_horizon1", key, hn, hn, GL_RGBA8, h1, false);
    free(h0); free(h1);
    // Normal (xz) + AO + hardness.
    uint8_t* nt = malloc(N * 4);
    #pragma omp parallel for schedule(static)
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        int xl = x > 0 ? x - 1 : x, xr = x < n - 1 ? x + 1 : x, zl = z > 0 ? z - 1 : z, zr = z < n - 1 ? z + 1 : z;
        float hx = (m->height[(size_t)z * n + xr] - m->height[(size_t)z * n + xl]) / ((xr - xl) * m->cell);
        float hz2 = (m->height[(size_t)zr * n + x] - m->height[(size_t)zl * n + x]) / ((zr - zl) * m->cell);
        float nx = -hx, ny = 1, nz = -hz2, l = sqrtf(nx * nx + ny * ny + nz * nz);
        uint8_t* o = nt + ((size_t)z * n + x) * 4;
        o[0] = (uint8_t)((nx / l * 0.5f + 0.5f) * 255); o[1] = (uint8_t)((nz / l * 0.5f + 0.5f) * 255);
        o[2] = ao[(size_t)(z / 2) * hn + x / 2]; o[3] = m->hardness[(size_t)z * n + x];
    }
    v->t_normal = terrain_tex("terrain_normal", key, n, n, GL_RGBA8, nt, true);
    free(nt); free(hz); CRET_LOAD_MARK("terrain: horizons and normals");
    // Splat weights.
    uint8_t* s0 = malloc(N * 4); uint8_t* s1 = malloc(N * 4);
    for (size_t i = 0; i < N; i++) { memcpy(s0 + i * 4, m->splat + i * 8, 4); memcpy(s1 + i * 4, m->splat + i * 8 + 4, 4); }
    v->t_splat0 = terrain_tex("terrain_splat0", key, n, n, GL_RGBA8, s0, true);
    v->t_splat1 = terrain_tex("terrain_splat1", key, n, n, GL_RGBA8, s1, true);
    free(s0); free(s1);
    // Misc: water velocity (water_flow.h: direction x strength, smooth
    // through river mouths), surface speed (water_flow_speed: steepness,
    // smoothed), vegetation cover (ground tint).
    float* vel = malloc(N * 2 * sizeof(float)); float* spd = malloc(N * sizeof(float));
    {
        uint8_t* kind = malloc(N); float* dir = malloc(N * 2 * sizeof(float)); float* wq = malloc(N * sizeof(float));
        for (size_t i = 0; i < N; i++) {
            uint8_t k = m->water_kind[i];
            kind[i] = k == TERRA_WATER_RIVER ? WATER_FLOW_MOVING : k == TERRA_WATER_NONE ? WATER_FLOW_DRY : WATER_FLOW_STILL;
            dir[2 * i] = m->flow_dir[2 * i] / 127.0f; dir[2 * i + 1] = m->flow_dir[2 * i + 1] / 127.0f; wq[i] = sqrtf(fmaxf(m->flow[i], 0.0f));
        }
        // CRET_WATER_FLOW=0: the raw per-cell D8 directions instead (A/B of the river-mouth seam).
        bool raw = getenv("CRET_WATER_FLOW") && atoi(getenv("CRET_WATER_FLOW")) == 0;
        if (raw) memcpy(vel, dir, N * 2 * sizeof(float));
        else if (!water_flow_build(n, kind, dir, wq, NULL, vel)) memset(vel, 0, N * 2 * sizeof(float));
        for (size_t i = 0; i < N; i++) wq[i] = m->water[i] > TERRA_DRY ? m->water[i] : m->height[i];   // water surface
        water_flow_speed(n, kind, wq, m->cell, NULL, spd);
        free(kind); free(dir); free(wq);
    }
    uint8_t* mt = malloc(N * 4);
    #pragma omp parallel for schedule(static)
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        size_t i = (size_t)z * n + x; uint8_t* o = mt + i * 4; const uint8_t* vg = m->veg + i * TERRA_VEG_COUNT;
        o[0] = (uint8_t)lrintf(127.5f + 127.0f * clampf(vel[2 * i], -1, 1)); o[1] = (uint8_t)lrintf(127.5f + 127.0f * clampf(vel[2 * i + 1], -1, 1));
        o[2] = (uint8_t)lrintf(clampf(spd[i], 0, 1) * 255);
        int canopy = vg[TERRA_VEG_CONIFER] + vg[TERRA_VEG_BROADLEAF] + vg[TERRA_VEG_TREEFERN] + vg[TERRA_VEG_PALM];
        o[3] = (uint8_t)(canopy > 255 ? 255 : canopy);
    }
    v->t_misc = terrain_tex("terrain_misc", key, n, n, GL_RGBA8, mt, false);
    free(mt); free(vel); free(spd); CRET_LOAD_MARK("terrain: splats, water flow, misc");
    // Tree species cover at half resolution (2x2 box filter): beyond the
    // impostor range the terrain paints the canopy in each stand's colour.
    {
        int h = n / 2; uint8_t* vm = malloc((size_t)h * h * 4);
        static const int K[4] = {TERRA_VEG_CONIFER, TERRA_VEG_BROADLEAF, TERRA_VEG_TREEFERN, TERRA_VEG_PALM};
        #pragma omp parallel for schedule(static)
        for (int z = 0; z < h; z++) for (int x = 0; x < h; x++) for (int c = 0; c < 4; c++) {
            int acc = 0;
            for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++) acc += m->veg[((size_t)(2 * z + dz) * n + 2 * x + dx) * TERRA_VEG_COUNT + K[c]];
            vm[((size_t)z * h + x) * 4 + c] = (uint8_t)(acc / 4);
        }
        v->t_vegmix = terrain_tex("terrain_vegmix", key, h, h, GL_RGBA8, vm, false);
        free(vm);
    }
    // Water: R = surface level, G = wet mask. The level is extended outward
    // from real water by nearest-neighbour fill (a surface continuing under
    // the banks), so the rendered sheet is continuous and the terrain depth
    // test cuts the shoreline at the true intersection: any basin shape fills
    // correctly. Where the extended sheet would sit above dry ground the
    // shader discards it using the mask. Far from water: well below ground.
    float* wl = malloc(N * 2 * sizeof(float)); float* lvl = malloc(N * sizeof(float));
    int* q = malloc(sizeof(int) * N); uint8_t* dist = malloc(N); int qh = 0, qt = 0;
    for (size_t i = 0; i < N; i++) {
        bool has = m->water[i] > TERRA_DRY;
        lvl[i] = has ? m->water[i] : m->height[i] - 30.0f; dist[i] = has ? 0 : 255;
        if (has) q[qt++] = (int)i;
    }
    while (qh < qt) {
        int c = q[qh++], x = c % n, z = c / n; if (dist[c] >= 10) continue;
        static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int xx = x + DX[k], zz = z + DZ[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            int j = zz * n + xx; if (dist[j] != 255) continue;
            dist[j] = dist[c] + 1; lvl[j] = lvl[c]; q[qt++] = j;
        }
    }
    for (size_t i = 0; i < N; i++) { wl[2 * i] = lvl[i]; wl[2 * i + 1] = m->water_kind[i] != TERRA_WATER_NONE ? 1.0f : 0.0f; }
    v->t_water = terrain_tex("terrain_water", key, n, n, GL_RG32F, wl, false);
    free(wl); free(lvl); free(q); free(dist); CRET_LOAD_MARK("terrain: vegmix, water level");
}

// Terrain textures (build_terrain) are cached GPU-ready (texcache.h) under a
// key over the map's arrays themselves, the size, this file's and
// water_flow.h's source (the build time when the source is not beside the
// binary) and CRET_WATER_FLOW, so a new world or an edit to the derivation
// rebuilds them.
typedef struct { const void* p[9]; size_t n[9]; uint64_t h[9]; } TerrainHash;
static void terrain_hash_one(int i, void* c) { TerrainHash* t = c; t->h[i] = cret_hash(CRET_HASH_SEED, t->p[i], t->n[i]); }
static uint64_t terrain_key(const CretView* v) {
    const TerraMap* m = &v->world->map; size_t N = (size_t)m->n * m->n;
    TerrainHash t = {{m->height, m->water, m->flow, m->water_kind, m->flow_dir, m->splat, m->veg, m->hardness, NULL},
                     {N * 4, N * 4, N * 4, N, N * 2, N * TERRA_MAT_COUNT, N * TERRA_VEG_COUNT, N, 0}, {0}};
    cret_parallel_for(8, 0, terrain_hash_one, &t);
    uint64_t h = cret_hash_str(cret_hash_u64(CRET_HASH_SEED, CRET_TEX_CACHE_VERSION), "terrain textures v1");
    h = cret_hash_u64(cret_hash_u64(cret_hash(h, &m->cell, sizeof m->cell), (uint64_t)m->n), m->seed);
    for (int i = 0; i < 8; i++) h = cret_hash_u64(h, t.h[i]);
    uint64_t src = cret_hash_file(cret_hash_file(CRET_HASH_SEED, "ocean/cretaceous/render/view.c"), "ocean/cretaceous/render/water_flow.h");
    if (!FileExists("ocean/cretaceous/render/view.c")) src = cret_hash_str(src, __DATE__ " " __TIME__);
    const char* wf = getenv("CRET_WATER_FLOW");
    return cret_hash_str(cret_hash_u64(h, src), wf ? wf : "-");
}
static void upload_terrain(CretView* v) {
    static const char* NAME[9] = {"terrain_height", "terrain_horizon0", "terrain_horizon1", "terrain_normal", "terrain_splat0", "terrain_splat1", "terrain_misc", "terrain_vegmix", "terrain_water"};
    GLuint* T[9] = {&v->t_height, &v->t_horizon0, &v->t_horizon1, &v->t_normal, &v->t_splat0, &v->t_splat1, &v->t_misc, &v->t_vegmix, &v->t_water};
    uint64_t key = terrain_key(v); bool hit = true;
    for (int k = 0; k < 9 && hit; k++) hit = (*T[k] = cret_tex_cache_load(NAME[k], key)) != 0;
    if (hit) CRET_LOAD_MARK("terrain: textures from cache");
    else {
        for (int k = 0; k < 9; k++) if (*T[k]) { glDeleteTextures(1, T[k]); *T[k] = 0; }
        build_terrain(v, key);   // stores each texture as it is made
        CRET_LOAD_MARK("terrain: upload and store cache");
    }
    const TerraMap* m = &v->world->map; int n = m->n;
    // Min/max pyramid for CDLOD node bounds.
    for (int l = 0; l < CRET_LOD_LEVELS; l++) {
        float size = CRET_LEAF_SIZE * (float)(1 << l); int cnt = (int)ceilf(v->world->size / size);
        v->minmax_n[l] = cnt; v->minmax[l] = malloc(sizeof(float) * 2 * cnt * cnt);
        for (int j = 0; j < cnt; j++) for (int i = 0; i < cnt; i++) {
            int x0 = (int)(i * size / m->cell), x1 = (int)fminf(n - 1, ceilf((i + 1) * size / m->cell));
            int z0 = (int)(j * size / m->cell), z1 = (int)fminf(n - 1, ceilf((j + 1) * size / m->cell));
            float lo = 1e9f, hi = -1e9f;
            for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                float h = m->height[(size_t)z * n + x], w = m->water[(size_t)z * n + x];
                lo = fminf(lo, h); hi = fmaxf(hi, fmaxf(h, w));
            }
            v->minmax[l][2 * (j * cnt + i)] = lo - 2; v->minmax[l][2 * (j * cnt + i) + 1] = hi + 2;
        }
    }
}

// Reverse-Z scene depth: the rasterizer writes CRET_NEAR / w into a 32-bit
// float buffer (shaders/depth.glsl). No shader writes gl_FragDepth, so early-Z
// works and plants get a depth prepass. With ARB_clip_control (GL 4.5) the
// float keeps full precision to the horizon; without it, the -1..1 depth
// range costs some precision far away.
#define CRET_NEAR 0.3f   // = CRET_NEAR in shaders/common.glsl
#define GL_LOWER_LEFT_ 0x8CA1
#define GL_NEGATIVE_ONE_TO_ONE_ 0x935E
#define GL_ZERO_TO_ONE_ 0x935F
extern void* glfwGetProcAddress(const char* name);
static void (*p_clip_control)(GLenum origin, GLenum depth);
static void scene_depth_begin(CretView* v) {
    if (v->clip_control) p_clip_control(GL_LOWER_LEFT_, GL_ZERO_TO_ONE_);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL); glDepthMask(GL_TRUE);
}
static void scene_depth_end(CretView* v) {
    if (v->clip_control) p_clip_control(GL_LOWER_LEFT_, GL_NEGATIVE_ONE_TO_ONE_);
    glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glClearDepth(1.0);
}
static void set_scene_depth(const CretView* v, const GfxProgram* p) { gfx_set2f(p, "u_depth", v->depth_ab[0], v->depth_ab[1]); }


static const char* MAT_NAMES[TERRA_MAT_COUNT] = {"rock", "scree", "dirt", "forest", "fern", "mud", "sand", "moss"};
static const char* MAT_MAPS[5] = {"albedo", "normal", "rough", "height", "ao"};
typedef struct { int S; void** a; void** b; } MatJob;
// One material's five maps, decoded, resized and packed (a worker thread; no GL).
static void mat_decode(int k, void* ctx) {
    MatJob* J = ctx; int S = J->S; Image ims[5];
    for (int q = 0; q < 5; q++) {
        char p[512]; snprintf(p, sizeof p, TEX_DIR "%s_%s.jpg", MAT_NAMES[k], MAT_MAPS[q]); ims[q] = LoadImage(p);
        if (!ims[q].data) { if (q < 4) fprintf(stderr, "cretaceous: missing texture for %s (run tools/fetch_textures.py)\n", MAT_NAMES[k]); ims[q] = GenImageColor(S, S, q == 1 ? (Color){128, 128, 255, 255} : q == 4 ? WHITE : GRAY); }
        if (ims[q].width != S || ims[q].height != S) ImageResize(&ims[q], S, S);
        ImageFormat(&ims[q], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    uint8_t* pa = malloc((size_t)S * S * 4); uint8_t* pb = malloc((size_t)S * S * 4);
    const uint8_t *ca = ims[0].data, *cn = ims[1].data, *cr = ims[2].data, *ch = ims[3].data, *co = ims[4].data;
    for (size_t i = 0; i < (size_t)S * S; i++) {
        pa[i * 4] = ca[i * 4]; pa[i * 4 + 1] = ca[i * 4 + 1]; pa[i * 4 + 2] = ca[i * 4 + 2]; pa[i * 4 + 3] = ch[i * 4];
        // DXT5nm layout: normal x in alpha, y in green (the two best
        // channels of a BC3 block), roughness in red, AO in blue.
        pb[i * 4] = cr[i * 4]; pb[i * 4 + 1] = cn[i * 4 + 1]; pb[i * 4 + 2] = co[i * 4]; pb[i * 4 + 3] = cn[i * 4];
    }
    J->a[k] = pa; J->b[k] = pb;
    for (int q = 0; q < 5; q++) UnloadImage(ims[q]);
}

// Ground materials: two 2K texture arrays (albedo + height, roughness +
// normal + AO), cached GPU-ready (render/texcache.h) under a key over the
// 40 source images, the size and the compression. A miss decodes the
// images on threads, builds mips and BC3 blocks on threads, uploads and
// stores the result.
static void load_materials(CretView* v) {
    // 2K by default; CRET_MAT_SIZE=1024 (or 512) for low-memory/mobile.
    const char* ms = getenv("CRET_MAT_SIZE"); int S = ms ? atoi(ms) : 2048; if (S < 256 || S > 4096) S = 2048;
    bool bc = gfx_use_bc3();
    char paths[TERRA_MAT_COUNT * 5][256]; const char* files[TERRA_MAT_COUNT * 5];
    for (int k = 0; k < TERRA_MAT_COUNT; k++) for (int q = 0; q < 5; q++) {
        snprintf(paths[k * 5 + q], sizeof paths[0], TEX_DIR "%s_%s.jpg", MAT_NAMES[k], MAT_MAPS[q]); files[k * 5 + q] = paths[k * 5 + q];
    }
    uint64_t key = cret_hash_str(cret_hash_u64(CRET_HASH_SEED, CRET_TEX_CACHE_VERSION), "ground materials v1");
    key = cret_hash_u64(cret_hash_u64(key, (uint64_t)S), bc);
    key = cret_hash_files(key, files, TERRA_MAT_COUNT * 5);
    CRET_LOAD_MARK("materials: hash sources");
    v->t_mat_a = cret_tex_cache_load("ground_albedo", key); v->t_mat_b = v->t_mat_a ? cret_tex_cache_load("ground_detail", key) : 0;
    if (v->t_mat_a && v->t_mat_b) { CRET_LOAD_MARK("materials: from cache"); return; }
    if (v->t_mat_a) { glDeleteTextures(1, &v->t_mat_a); v->t_mat_a = 0; }
    void* a[TERRA_MAT_COUNT]; void* b[TERRA_MAT_COUNT]; MatJob job = {S, a, b};
    cret_parallel_for(TERRA_MAT_COUNT, 0, mat_decode, &job);
    CRET_LOAD_MARK("materials: decode and pack");
    // BC3 (DXT5) block compression: a quarter of the memory and bandwidth.
    // Uncompressed 2K arrays (357 MB) made terrain texture-bound (52 ms of a
    // 1080p ground view on a GTX 1060; 16 ms compressed). Mips and blocks are
    // made on threads (texbuild.h); CRET_TEX_COMPRESS=0 keeps RGBA8.
    v->t_mat_a = cret_tex_build("ground_albedo", key, GL_TEXTURE_2D_ARRAY, bc ? TB_BC3_SRGB : GL_SRGB8_ALPHA8, S, S, TERRA_MAT_COUNT, (const void* const*)a, true, true, true, 8.0f);
    v->t_mat_b = cret_tex_build("ground_detail", key, GL_TEXTURE_2D_ARRAY, bc ? TB_BC3 : GL_RGBA8, S, S, TERRA_MAT_COUNT, (const void* const*)b, true, true, true, 8.0f);
    for (int k = 0; k < TERRA_MAT_COUNT; k++) { free(a[k]); free(b[k]); }
    CRET_LOAD_MARK("materials: mips, compression, upload and store");
}

// ---------------------------------------------------------------- shaders & targets
static void load_shaders(CretView* v) {
#define P(field, vs, fs) gfx_program_load(&v->field, SHADER_DIR vs, SHADER_DIR fs)
    P(terrain, "terrain.vs", "terrain.fs");
    P(water, "water.vs", "water.fs");
    P(trans_lut, "fullscreen.vs", "transmittance.fs");
    P(sky_lut, "fullscreen.vs", "skyview.fs");
    P(sky, "fullscreen.vs", "sky.fs");
    P(clouds, "fullscreen.vs", "clouds.fs");
    P(cloud_resolve, "fullscreen.vs", "cloud_resolve.fs");
    P(post, "fullscreen.vs", "post.fs");
    P(lum, "fullscreen.vs", "lum.fs");
    P(adapt, "fullscreen.vs", "adapt.fs");
    P(object, "object.vs", "object.fs");
    P(object_shadow, "object.vs", "depth.fs");
    P(nest, "nest.vs", "nest.fs");
    P(bright, "fullscreen.vs", "bright.fs");
    P(blur, "fullscreen.vs", "blur.fs");
#undef P
}
// Water look (render/water_look.h): defaults, then config/water.ini or
// CRET_WATER_INI over them. Read at start and on every shader reload (F5).
static void load_water_look(CretView* v, bool verbose) {
    const char* path = getenv("CRET_WATER_INI") ? getenv("CRET_WATER_INI") : "ocean/cretaceous/config/water.ini";
    v->water_look = water_look_defaults();
    int n = water_look_load(path, &v->water_look);
    if (verbose || n < 0) fprintf(stderr, n < 0 ? "cretaceous: no %s, default water look\n" : "cretaceous: water look from %s (%d keys)\n", path, n);
}
void cret_view_reload_shaders(CretView* v) {
    load_water_look(v, true);
    GfxProgram* all[] = {&v->terrain, &v->water, &v->trans_lut, &v->sky_lut, &v->sky, &v->clouds, &v->cloud_resolve, &v->post, &v->object, &v->bright, &v->blur, &v->object_shadow, &v->nest};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) {
        all[i]->vs_mtime = 0; gfx_program_reload_if_changed(all[i]);
    }
    if (v->veg_ok) cret_veg_reload_shaders(&v->veg);
    if (v->props_ok) cret_props_reload_shaders(&v->props);   // props.c
    if (v->dinos_ok) cret_dino_reload_shaders(&v->dinos);     // dino.c
    cret_critters_reload_shaders();                            // critters.c
}

static float render_scale(const CretView* v) { float s = v->settings.render_scale; return s >= 0.3f && s <= 1.0f ? s : 1.0f; }
static void make_targets(CretView* v) {
    float sc = render_scale(v);
    v->rw = (int)(v->width * sc + 0.5f); v->rh = (int)(v->height * sc + 0.5f);
    int w = v->rw, h = v->rh;
    gfx_target(&v->hdr, w, h, GL_RGBA16F, true);
    gfx_target(&v->scene_copy, w, h, GL_RGBA16F, true);
    // Clouds: the history is full resolution; each frame marches one pixel
    // of every 4x4 block into cloud_q (cloud_rt, full size, only for the
    // CRET_CLOUD_CB=0 reference that marches every pixel).
    static int cb_all = -1; if (cb_all < 0) cb_all = getenv("CRET_CLOUD_CB") && atoi(getenv("CRET_CLOUD_CB")) == 0;
    if (cb_all) gfx_target(&v->cloud_rt, w, h, GL_RGBA16F, false); else memset(&v->cloud_rt, 0, sizeof v->cloud_rt);
    gfx_target(&v->cloud_q, (w + 3) / 4, (h + 3) / 4, GL_RGBA16F, false);
    for (int i = 0; i < 2; i++) gfx_target(&v->cloud_hist[i], w, h, GL_RGBA16F, false);
    v->hist_valid = false;
    gfx_target(&v->bloom[0], (w + 3) / 4, (h + 3) / 4, GL_RGBA16F, false);
    gfx_target(&v->bloom[1], (w + 3) / 4, (h + 3) / 4, GL_RGBA16F, false);
    // Auto-exposure: 64x32 log-luminance with a full mip chain, 1x1 adaptation ping-pong.
    gfx_target(&v->lum_rt, 64, 32, GL_RGBA16F, false);
    glBindTexture(GL_TEXTURE_2D, v->lum_rt.color); glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glBindTexture(GL_TEXTURE_2D, 0);
    for (int i = 0; i < 2; i++) gfx_target(&v->adapt_rt[i], 1, 1, GL_RGBA16F, false);
    v->adapt_valid = false;
}
static void free_targets(CretView* v) {
    gfx_target_free(&v->hdr); gfx_target_free(&v->scene_copy); if (v->cloud_rt.fbo) gfx_target_free(&v->cloud_rt); gfx_target_free(&v->cloud_q);
    gfx_target_free(&v->cloud_hist[0]); gfx_target_free(&v->cloud_hist[1]);
    gfx_target_free(&v->bloom[0]); gfx_target_free(&v->bloom[1]);
    gfx_target_free(&v->lum_rt); gfx_target_free(&v->adapt_rt[0]); gfx_target_free(&v->adapt_rt[1]);
}

// Loading screen between the slow startup stages (about 40 s on WSL), so the
// window is never just black. Skipped for hidden capture windows.
static void loading_screen(const char* what, float frac) {
    if (!IsWindowReady() || IsWindowState(FLAG_WINDOW_HIDDEN)) return;
    int w = GetScreenWidth(), h = GetScreenHeight(), bw = w / 3, bx = (w - bw) / 2, by = h / 2 + 30;
    BeginDrawing(); ClearBackground((Color){14, 16, 14, 255});
    const char* t = "CRETACEOUS"; DrawText(t, (w - MeasureText(t, 40)) / 2, h / 2 - 50, 40, (Color){225, 214, 180, 255});
    DrawText(what, (w - MeasureText(what, 20)) / 2, h / 2, 20, (Color){170, 165, 150, 255});
    DrawRectangle(bx, by, bw, 6, (Color){50, 54, 48, 255}); DrawRectangle(bx, by, (int)(bw * frac), 6, (Color){200, 170, 100, 255});
    EndDrawing();
}

#ifdef __EMSCRIPTEN__
// raylib's web build is its OpenGL ES 2 renderer, which uploads meshes without
// a vertex array object: GenMeshSphere sets the capsule's attributes on VAO 0,
// which raylib's 2D batch also draws from. So on web the capsule gets its own
// VAO (positions 0, uvs 1, normals 2, indices), and VAO 0 loses the normal
// attribute again; once UnloadMesh deleted that buffer, every 2D draw failed.
static GLuint web_capsule[5];   // vao, position, uv and normal buffers, index buffer
static void web_capsule_vao(Mesh* m) {
    glGenVertexArrays(1, &web_capsule[0]); glBindVertexArray(web_capsule[0]); glGenBuffers(4, &web_capsule[1]);
    const float* src[3] = {m->vertices, m->texcoords, m->normals}; const int comps[3] = {3, 2, 3};
    for (int a = 0; a < 3; a++) {
        glBindBuffer(GL_ARRAY_BUFFER, web_capsule[1 + a]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * comps[a] * m->vertexCount, src[a], GL_STATIC_DRAW);
        if (src[a]) { glEnableVertexAttribArray(a); glVertexAttribPointer(a, comps[a], GL_FLOAT, GL_FALSE, 0, 0); }
    }
    if (m->indices) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, web_capsule[4]); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned short) * 3 * m->triangleCount, m->indices, GL_STATIC_DRAW); }
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(2);
    m->vaoId = web_capsule[0];
}
static void web_capsule_free(Mesh* m) {
    if (web_capsule[0]) { glDeleteVertexArrays(1, &web_capsule[0]); glDeleteBuffers(4, &web_capsule[1]); }
    memset(web_capsule, 0, sizeof web_capsule); m->vaoId = 0;
}
#else
static void web_capsule_vao(Mesh* m) { (void)m; }
static void web_capsule_free(Mesh* m) { (void)m; }
#endif

static void patch_layout(const GfxMesh* m);
static void make_ocean(CretView* v);
bool cret_view_init(CretView* v, const CretWorld* w, int width, int height) {
    memset(v, 0, sizeof(*v)); v->world = w; v->width = width; v->height = height;
    load_water_look(v, false);
    if (getenv("CRET_TIME0")) v->time = (float)atof(getenv("CRET_TIME0"));   // animation clock start (s): long-session water and wind in a still
    v->settings = (CretViewSettings){.clouds = true, .water = true, .vegetation = true, .cloud_coverage = 0.5f, .exposure = 0.0f, .lod_scale = 1.0f};
    const char* pr = getenv("CRET_PRESET");
#ifdef __EMSCRIPTEN__
    const int default_preset = CRET_PRESET_LOW;   // web: targets start small; the saved settings may raise it
#else
    const int default_preset = CRET_PRESET_HIGH;
#endif
    cret_view_set_preset(v, !pr ? default_preset : (!strcmp(pr, "low") || !strcmp(pr, "0")) ? CRET_PRESET_LOW : (!strcmp(pr, "medium") || !strcmp(pr, "1")) ? CRET_PRESET_MEDIUM : CRET_PRESET_HIGH);
    v->far_plane = 60000.0f;
#ifdef __EMSCRIPTEN__
    p_clip_control = NULL;   // WebGL2 has no glClipControl: the standard [-1, 1] depth path below
#else
    p_clip_control = (gfx_has_ext("GL_ARB_clip_control") && !getenv("CRET_NO_CLIP_CONTROL")) ? (void (*)(GLenum, GLenum))glfwGetProcAddress("glClipControl") : NULL;
#endif
    v->clip_control = p_clip_control != NULL;
    v->depth_ab[0] = v->clip_control ? 0.0f : -1.0f; v->depth_ab[1] = v->clip_control ? CRET_NEAR : 2.0f * CRET_NEAR;
    cret_load_seconds(); CRET_LOAD_MARK("view: before init");
    loading_screen("Compiling shaders", 0.02f);
    load_shaders(v); CRET_LOAD_MARK("view: shaders");
    if (cret_load_profiling()) fprintf(stderr, "load: (KHR_parallel_shader_compile %s, ARB %s)\n", gfx_has_ext("GL_KHR_parallel_shader_compile") ? "yes" : "no", gfx_has_ext("GL_ARB_parallel_shader_compile") ? "yes" : "no");
    if (!v->terrain.id || !v->sky.id || !v->post.id) { fprintf(stderr, "cretaceous: shader load failed (run from the repository root)\n"); return false; }
    v->patch = gfx_grid_mesh(CRET_PATCH_QUADS);
    v->patch_half = gfx_grid_mesh(CRET_PATCH_QUADS / 2);
    v->tri = gfx_fullscreen_triangle();
    v->ocean_ring = gfx_grid_mesh(64);
    patch_layout(&v->patch); patch_layout(&v->patch_half); patch_layout(&v->ocean_ring);
    glGenBuffers(1, &v->node_vbo); make_ocean(v);
    loading_screen("Uploading terrain", 0.08f); upload_terrain(v); CRET_LOAD_MARK("view: terrain upload");
    loading_screen("Loading ground materials", 0.15f); load_materials(v); CRET_LOAD_MARK("view: ground materials");
    loading_screen("Building clouds", 0.25f); make_noise_texture(v); make_cloud_noise(v); CRET_LOAD_MARK("view: noise and clouds");
    loading_screen("Growing forests", 0.35f); v->veg_ok = cret_veg_init(&v->veg, &w->map, w->size);
    loading_screen("Placing rocks and logs", 0.6f); v->props_ok = cret_props_init(&v->props, &w->map, w->size);   // props.c
    loading_screen("Waking the dinosaurs", 0.8f);
    v->dinos_ok = !getenv("CRET_NO_DINOS") && cret_dino_init(&v->dinos);   // dino.c; CRET_NO_DINOS=1 draws capsules
    if (v->veg_ok) { v->veg_base[0] = v->veg.near_range; v->veg_base[1] = v->veg.ground_range; v->veg_base[2] = v->veg.lod_dist; v->veg_base[3] = v->veg.tree_lod_dist; }
    loading_screen("Almost there", 0.97f);
    cret_csm_init(&v->csm, v->settings.shadow_quality);
    gfx_target(&v->trans, 256, 64, GL_RGBA16F, false);
    gfx_target(&v->skyview, 192, 108, GL_RGBA16F, false);
    make_targets(v);
    v->capsule = GenMeshSphere(1.0f, 16, 24);
    web_capsule_vao(&v->capsule);
    cret_nest_draw_init(&v->nests);   // nest.c: egg meshes now, nest meshes as nests appear
    v->node_cap = 4096; v->nodes = malloc(sizeof(CretNode) * v->node_cap);
    CRET_LOAD_MARK("view: shadows and targets");
    v->initialized = true;
    return true;
}
// Preset table. Medium and low trade resolution first (the frame is fill
// bound), then shadow-map size, cloud steps, parallax and plant ranges.
void cret_view_set_preset(CretView* v, int preset) {
    static const struct { float scale; int shadows; float clouds, veg, props; int terrain; } T[3] = {
        [CRET_PRESET_LOW]    = {0.7f, 1, 0.35f, 0.6f, 0.5f, 0},
        [CRET_PRESET_MEDIUM] = {0.8f, 1, 0.6f, 0.8f, 0.65f, 1},
        [CRET_PRESET_HIGH]   = {1.0f, 2, 1.0f, 1.0f, 1.0f, 2},
    };
    if (preset < CRET_PRESET_LOW || preset > CRET_PRESET_HIGH) preset = CRET_PRESET_HIGH;
    CretViewSettings* s = &v->settings;
    s->preset = preset; s->render_scale = T[preset].scale; s->shadow_quality = T[preset].shadows;
    s->cloud_quality = T[preset].clouds; s->veg_range = T[preset].veg; s->prop_lod = T[preset].props; s->terrain_detail = T[preset].terrain;
    if (getenv("CRET_CLOUD_QUALITY")) s->cloud_quality = clampf((float)atof(getenv("CRET_CLOUD_QUALITY")), 0, 1);
    if (getenv("CRET_SHADOWS")) s->shadow_quality = atoi(getenv("CRET_SHADOWS"));
    if (getenv("CRET_RENDER_SCALE")) s->render_scale = clampf((float)atof(getenv("CRET_RENDER_SCALE")), 0.3f, 1.0f);
}
// Pushes settings that live in other modules or need new targets.
static void apply_settings(CretView* v) {
    const CretViewSettings* s = &v->settings;
    int rw = (int)(v->width * render_scale(v) + 0.5f), rh = (int)(v->height * render_scale(v) + 0.5f);
    if (rw != v->rw || rh != v->rh) { free_targets(v); make_targets(v); }
    if (v->veg_ok) {
        CretVeg* g = &v->veg; float r = s->veg_range > 0 ? s->veg_range : 1.0f;
        float want[4] = {v->veg_base[0] * (0.5f + 0.5f * r), v->veg_base[1] * r, v->veg_base[2] * r, v->veg_base[3] * r};
        if (want[1] > g->ground_range) g->ground_valid = false;   // regenerate the wider ring
        g->near_range = want[0]; g->ground_range = want[1]; g->lod_dist = want[2]; g->tree_lod_dist = want[3];
        static float carpet_base = -1; if (carpet_base < 0) carpet_base = g->carpet_range;   // sward ring scales with the preset too
        float cw = carpet_base * r; if (cw > g->carpet_range) g->carpet_valid = false; g->carpet_range = cw;
    }
    if (v->props_ok) v->props.lod_scale = s->prop_lod;
}
void cret_view_resize(CretView* v, int width, int height) {
    if (width == v->width && height == v->height) return;
    v->width = width; v->height = height; free_targets(v); make_targets(v);
}
void cret_view_close(CretView* v) {
    if (!v->initialized) return;
    GfxProgram* all[] = {&v->terrain, &v->water, &v->trans_lut, &v->sky_lut, &v->sky, &v->clouds, &v->cloud_resolve, &v->post, &v->object, &v->bright, &v->blur, &v->lum, &v->adapt, &v->object_shadow, &v->nest};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) gfx_program_free(all[i]);
    cret_nest_draw_close(&v->nests);
    cret_csm_close(&v->csm);
    GLuint tex[] = {v->t_height, v->t_normal, v->t_horizon0, v->t_horizon1, v->t_splat0, v->t_splat1, v->t_misc, v->t_vegmix, v->t_water, v->t_mat_a, v->t_mat_b, v->t_noise, v->t_cloud_shape, v->t_cloud_detail, v->t_weather};
    glDeleteTextures(sizeof tex / sizeof *tex, tex);
    free_targets(v); gfx_target_free(&v->trans); gfx_target_free(&v->skyview);
    gfx_mesh_free(&v->patch); gfx_mesh_free(&v->patch_half); gfx_mesh_free(&v->tri); gfx_mesh_free(&v->ocean_ring);
    glDeleteBuffers(1, &v->node_vbo); glDeleteBuffers(1, &v->ocean_vbo);
    web_capsule_free(&v->capsule);
    UnloadMesh(v->capsule);
    if (v->veg_ok) cret_veg_close(&v->veg);
    if (v->props_ok) cret_props_close(&v->props);   // props.c
    if (v->dinos_ok) cret_dino_close(&v->dinos);    // dino.c
    cret_critters_close();                          // critters.c
    cret_scent_fx_close();                          // scentfx.c
    for (int l = 0; l < CRET_LOD_LEVELS; l++) free(v->minmax[l]);
    free(v->nodes); v->initialized = false;
}

// ---------------------------------------------------------------- sun & camera
static float smoothstepf(float a, float b, float x) { float t = fminf(fmaxf((x - a) / (b - a), 0.0f), 1.0f); return t * t * (3.0f - 2.0f * t); }
void cret_sun_direction(float tod, float o[3]) {
    // Sun rises in the east (+x), peaks at noon with a tilted arc.
    float a = (tod - 0.25f) * 6.2831853f;           // 0 at sunrise
    float elev = sinf(a), across = cosf(a);
    float tilt = 0.45f;                               // latitude-ish tilt toward -z
    float x = across, y = elev * cosf(tilt), z = -elev * sinf(tilt) * 0.6f + 0.25f;
    float l = sqrtf(x * x + y * y + z * z); o[0] = x / l; o[1] = y / l; o[2] = z / l;
}

static void mat_to_floats(Matrix m, float* f) { float16 t = MatrixToFloatV(m); memcpy(f, t.v, sizeof(float) * 16); }

static void setup_camera(CretView* v, const CretCamera* c) {
    Vector3 pos = {c->pos[0], c->pos[1], c->pos[2]};
    Vector3 fwd = {cosf(c->pitch) * cosf(c->yaw), sinf(c->pitch), cosf(c->pitch) * sinf(c->yaw)};
    Matrix view = MatrixLookAt(pos, Vector3Add(pos, fwd), (Vector3){0, 1, 0});
    Matrix proj = MatrixPerspective(c->fov * DEG2RAD, (double)v->width / v->height, CRET_NEAR, v->far_plane);
    Matrix vp = MatrixMultiply(view, proj);
    memcpy(v->prev_viewproj, v->viewproj, sizeof v->viewproj);
    mat_to_floats(view, v->view); mat_to_floats(proj, v->proj); mat_to_floats(vp, v->viewproj);
    mat_to_floats(MatrixInvert(vp), v->inv_viewproj);
    memcpy(v->prev_cam, v->cam_pos, sizeof v->prev_cam);
    v->cam_pos[0] = c->pos[0]; v->cam_pos[1] = c->pos[1]; v->cam_pos[2] = c->pos[2];
    memcpy(v->prev_basis, v->basis, sizeof v->basis);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, (Vector3){0, 1, 0})), up = Vector3CrossProduct(right, fwd);
    float* b = v->basis; b[0] = right.x; b[1] = right.y; b[2] = right.z; b[3] = up.x; b[4] = up.y; b[5] = up.z; b[6] = fwd.x; b[7] = fwd.y; b[8] = fwd.z;
    v->tan_fov[1] = tanf(c->fov * DEG2RAD * 0.5f); v->tan_fov[0] = v->tan_fov[1] * (float)v->width / v->height;
    if (v->frame <= 1) memcpy(v->prev_basis, v->basis, sizeof v->basis);
}
static void basis_uniforms(const CretView* v, const GfxProgram* p) {
    const float* b = v->basis; const float* q = v->prev_basis;
    gfx_set3f(p, "u_right", b[0], b[1], b[2]); gfx_set3f(p, "u_up", b[3], b[4], b[5]); gfx_set3f(p, "u_fwd", b[6], b[7], b[8]);
    gfx_set3f(p, "u_prev_right", q[0], q[1], q[2]); gfx_set3f(p, "u_prev_up", q[3], q[4], q[5]); gfx_set3f(p, "u_prev_fwd", q[6], q[7], q[8]);
    gfx_set2f(p, "u_tan", v->tan_fov[0], v->tan_fov[1]);
}

// ---------------------------------------------------------------- CDLOD selection
typedef struct { float p[6][4]; } Frustum;
static Frustum frustum_from(const float* m) {
    // Column-major viewproj: rows r_i = (m[i], m[4+i], m[8+i], m[12+i]).
    Frustum f; float r[4][4];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r[i][j] = m[j * 4 + i];
    for (int j = 0; j < 4; j++) {
        f.p[0][j] = r[3][j] + r[0][j]; f.p[1][j] = r[3][j] - r[0][j];
        f.p[2][j] = r[3][j] + r[1][j]; f.p[3][j] = r[3][j] - r[1][j];
        f.p[4][j] = r[3][j] + r[2][j]; f.p[5][j] = r[3][j] - r[2][j];
    }
    return f;
}
static bool box_visible(const Frustum* f, float x0, float y0, float z0, float x1, float y1, float z1) {
    for (int i = 0; i < 6; i++) {
        const float* p = f->p[i];
        float x = p[0] > 0 ? x1 : x0, y = p[1] > 0 ? y1 : y0, z = p[2] > 0 ? z1 : z0;
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
    }
    return true;
}
static float lod_range(const CretView* v, int level) { return CRET_LEAF_SIZE * (float)(1 << level) * 2.6f * v->settings.lod_scale; }
static bool sphere_box(const float c[3], float r, float x0, float y0, float z0, float x1, float y1, float z1) {
    float dx = fmaxf(fmaxf(x0 - c[0], 0), c[0] - x1), dy = fmaxf(fmaxf(y0 - c[1], 0), c[1] - y1), dz = fmaxf(fmaxf(z0 - c[2], 0), c[2] - z1);
    return dx * dx + dy * dy + dz * dz <= r * r;
}
static void add_node(CretView* v, float x, float z, float size, int level, float y0, float y1, int quads) {
    if (v->node_count == v->node_cap) { v->node_cap *= 2; v->nodes = realloc(v->nodes, sizeof(CretNode) * v->node_cap); }
    v->nodes[v->node_count++] = (CretNode){x, z, size, y0, y1, level, quads};
}
static bool node_bounds(const CretView* v, int level, int i, int j, float* y0, float* y1) {
    int cnt = v->minmax_n[level]; if (i < 0 || j < 0 || i >= cnt || j >= cnt) return false;
    *y0 = v->minmax[level][2 * (j * cnt + i)]; *y1 = v->minmax[level][2 * (j * cnt + i) + 1]; return true;
}
static bool select_node(CretView* v, const Frustum* f, int level, int i, int j) {
    float size = CRET_LEAF_SIZE * (float)(1 << level), x0 = i * size, z0 = j * size, y0, y1;
    if (!node_bounds(v, level, i, j, &y0, &y1)) return true;
    if (!sphere_box(v->cam_pos, lod_range(v, level), x0, y0, z0, x0 + size, y1, z0 + size)) return false;
    if (!box_visible(f, x0, y0, z0, x0 + size, y1, z0 + size)) return true;
    if (level == 0 || !sphere_box(v->cam_pos, lod_range(v, level - 1), x0, y0, z0, x0 + size, y1, z0 + size)) {
        add_node(v, x0, z0, size, level, y0, y1, CRET_PATCH_QUADS); return true;
    }
    for (int c = 0; c < 4; c++) {
        int ci = i * 2 + (c & 1), cj = j * 2 + (c >> 1);
        if (!select_node(v, f, level - 1, ci, cj)) {
            float cs = size * 0.5f, cy0, cy1;
            if (node_bounds(v, level - 1, ci, cj, &cy0, &cy1) && box_visible(f, ci * cs, cy0, cj * cs, ci * cs + cs, cy1, cj * cs + cs))
                add_node(v, ci * cs, cj * cs, cs, level, cy0, cy1, CRET_PATCH_QUADS / 2);  // parent spacing
        }
    }
    return true;
}
static void select_terrain(CretView* v) {
    v->node_count = 0; Frustum f = frustum_from(v->viewproj);
    int top = CRET_LOD_LEVELS - 1, cnt = v->minmax_n[top];
    for (int j = 0; j < cnt; j++) for (int i = 0; i < cnt; i++) select_node(v, &f, top, i, j);
}
// Patch instances for terrain.vs and water.vs: (x, z, size, level) at
// location 4 and (morph start, morph end, quads per side) at location 5.
// Terrain and water draw one instanced call per patch mesh instead of a
// draw and three uniforms per node (about 100 nodes and 80 ocean tiles).
typedef struct { float node[4], morph[3]; } PatchInst;
static void patch_layout(const GfxMesh* m) {
    glBindVertexArray(m->vao);
    for (int a = 4; a < 6; a++) { glEnableVertexAttribArray(a); glVertexAttribDivisor(a, 1); }
    glBindVertexArray(0);
}
static void draw_patches(const GfxMesh* m, GLuint vbo, int first, int n) {
    if (n <= 0) return;
    glBindVertexArray(m->vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(PatchInst), (void*)(sizeof(PatchInst) * first));
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(PatchInst), (void*)(sizeof(PatchInst) * first + 16));
    glDrawElementsInstanced(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, 0, n);
    glBindVertexArray(0);
}
static void node_instances(CretView* v) {
    static PatchInst* inst; static int cap;
    if (cap < v->node_count) { cap = v->node_count * 2; inst = realloc(inst, sizeof(PatchInst) * cap); }
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < v->node_count; k++) {
            const CretNode* nd = &v->nodes[k];
            if ((nd->quads == CRET_PATCH_QUADS) != (pass == 0)) continue;
            float hi = lod_range(v, nd->level), lo = nd->level > 0 ? lod_range(v, nd->level - 1) : 0;
            inst[n++] = (PatchInst){{nd->x, nd->z, nd->size, (float)nd->level}, {lerpf(lo, hi, 0.65f), hi, (float)nd->quads}};
        }
        if (pass == 0) v->node_full = n;
    }
    v->node_half = n - v->node_full;
    gfx_stream_begin(); gfx_stream_add(inst, sizeof(PatchInst) * n); gfx_stream_upload(v->node_vbo);
}
// Open ocean beyond the map: a ring of world-sized tiles at sea level that
// overlap the map edge slightly (no seam).
static void make_ocean(CretView* v) {
    PatchInst t[80]; int n = 0; float S = v->world->size, ov = 24.0f;
    for (int j = -4; j < 5; j++) for (int i = -4; i < 5; i++)
        if (i || j) t[n++] = (PatchInst){{i * S - ov, j * S - ov, S + 2 * ov, 8.0f}, {1e9f, 2e9f, 64.0f}};
    glGenBuffers(1, &v->ocean_vbo); glBindBuffer(GL_ARRAY_BUFFER, v->ocean_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof t, t, GL_STATIC_DRAW); v->ocean_n = n;
}

// ---------------------------------------------------------------- frame
static void common_uniforms(const CretView* v, const GfxProgram* p, const CretSim* sim) {
    const TerraMap* m = &v->world->map;
    gfx_setmat4(p, "u_viewproj", v->viewproj);
    gfx_setmat4(p, "u_inv_viewproj", v->inv_viewproj);
    gfx_set3f(p, "u_cam", v->cam_pos[0], v->cam_pos[1], v->cam_pos[2]);
    gfx_set3f(p, "u_sun", v->sun_dir[0], v->sun_dir[1], v->sun_dir[2]);
    gfx_set1f(p, "u_time", v->time);
    gfx_set1f(p, "u_far", v->far_plane);
    gfx_set1f(p, "u_world_size", v->world->size);
    gfx_set2f(p, "u_map", (float)m->n, m->cell);
    gfx_set2f(p, "u_screen", (float)v->rw, (float)v->rh);
    gfx_set1f(p, "u_cloud_cover", v->cover);
    gfx_set1i(p, "u_frame", v->frame);
    gfx_set1f(p, "u_night", v->night);
    gfx_set1f(p, "u_overcast", v->overcast); gfx_set1f(p, "u_wet", v->wet); gfx_set1f(p, "u_rain", v->rain);
    (void)sim;
}

static void draw_terrain(CretView* v, const CretSim* sim) {
    GfxProgram* p = &v->terrain; glUseProgram(p->id); common_uniforms(v, p, sim); set_scene_depth(v, p);
    gfx_set1i(p, "u_detail", v->settings.terrain_detail);
    static int splat_dbg = -1; if (splat_dbg < 0) splat_dbg = getenv("CRET_SPLAT_DEBUG") ? atoi(getenv("CRET_SPLAT_DEBUG")) : 0;
    gfx_set1i(p, "u_splat_debug", splat_dbg);
    gfx_set1f(p, "u_veg_far", v->veg_ok ? v->veg.far_range : 0.0f);
    int u = 0;
    gfx_bind_tex(p, "t_vegmix", u++, GL_TEXTURE_2D, v->t_vegmix);
    gfx_bind_tex(p, "t_height", u++, GL_TEXTURE_2D, v->t_height);
    gfx_bind_tex(p, "t_normal", u++, GL_TEXTURE_2D, v->t_normal);
    gfx_bind_tex(p, "t_horizon0", u++, GL_TEXTURE_2D, v->t_horizon0);
    gfx_bind_tex(p, "t_horizon1", u++, GL_TEXTURE_2D, v->t_horizon1);
    gfx_bind_tex(p, "t_splat0", u++, GL_TEXTURE_2D, v->t_splat0);
    gfx_bind_tex(p, "t_splat1", u++, GL_TEXTURE_2D, v->t_splat1);
    gfx_bind_tex(p, "t_misc", u++, GL_TEXTURE_2D, v->t_misc);
    gfx_bind_tex(p, "t_water", u++, GL_TEXTURE_2D, v->t_water);
    gfx_bind_tex(p, "t_mat_a", u++, GL_TEXTURE_2D_ARRAY, v->t_mat_a);
    gfx_bind_tex(p, "t_mat_b", u++, GL_TEXTURE_2D_ARRAY, v->t_mat_b);
    gfx_bind_tex(p, "t_noise", u++, GL_TEXTURE_2D, v->t_noise);
    gfx_bind_tex(p, "t_trans", u++, GL_TEXTURE_2D, v->trans.color);
    gfx_bind_tex(p, "t_skyview", u++, GL_TEXTURE_2D, v->skyview.color);
    gfx_bind_tex(p, "t_weather", u++, GL_TEXTURE_2D, v->t_weather);
    cret_csm_bind(&v->csm, p, u++);
    if (v->settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    draw_patches(&v->patch, v->node_vbo, 0, v->node_full);
    draw_patches(&v->patch_half, v->node_vbo, v->node_full, v->node_half);
    if (v->settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void draw_water(CretView* v, const CretSim* sim) {
    GfxProgram* p = &v->water; glUseProgram(p->id); common_uniforms(v, p, sim); set_scene_depth(v, p);
    int u = 0;
    gfx_bind_tex(p, "t_height", u++, GL_TEXTURE_2D, v->t_height);
    gfx_bind_tex(p, "t_water", u++, GL_TEXTURE_2D, v->t_water);
    gfx_bind_tex(p, "t_misc", u++, GL_TEXTURE_2D, v->t_misc);
    gfx_bind_tex(p, "t_noise", u++, GL_TEXTURE_2D, v->t_noise);
    gfx_bind_tex(p, "t_scene", u++, GL_TEXTURE_2D, v->scene_copy.color);
    gfx_bind_tex(p, "t_depth", u++, GL_TEXTURE_2D, v->scene_copy.depth);
    gfx_bind_tex(p, "t_trans", u++, GL_TEXTURE_2D, v->trans.color);
    gfx_bind_tex(p, "t_skyview", u++, GL_TEXTURE_2D, v->skyview.color);
    gfx_bind_tex(p, "t_weather", u++, GL_TEXTURE_2D, v->t_weather);
    cret_csm_bind(&v->csm, p, u++);
    const WaterLook* wl = &v->water_look;
    gfx_set4f(p, "u_wflow", wl->flow_speed, fmaxf(wl->max_speed, 0.05f), wl->fast_gloss, wl->foam);
    gfx_set2f(p, "u_wripple", fmaxf(wl->ripple_scale, 0.05f), wl->ripple_strength);
    gfx_set3f(p, "u_wsparkle", wl->sparkle, fmaxf(wl->sparkle_size, 0.05f), wl->sparkle_peak);
    gfx_set3f(p, "u_wcolour", wl->colour[0], wl->colour[1], wl->colour[2]);
    gfx_set3f(p, "u_wsilt", wl->silt[0], wl->silt[1], wl->silt[2]);
    gfx_set2f(p, "u_wturbid", wl->turbidity, fmaxf(wl->clarity, 0.05f));
    GLint l_mode = gfx_loc(p, "u_mode");
    glUniform1i(l_mode, 0);
    draw_patches(&v->patch, v->node_vbo, 0, v->node_full);
    draw_patches(&v->patch_half, v->node_vbo, v->node_full, v->node_half);
    glUniform1i(l_mode, 1);   // the open ocean (make_ocean)
    draw_patches(&v->ocean_ring, v->ocean_vbo, 0, v->ocean_n);
}

static int draw_agent_meshes(CretView* v, const CretSim* sim, const GfxProgram* p, int shadow_k);
#include "critters.c"   // small animals (render/critters.h)
#include "nest.c"       // nests, eggs and carried food (render/nest.h), drawn after the animals
#include "scentfx.c"    // the scent view over the finished frame (render/scentfx.h)
// A carrier's head for nest.c: the model's head joint as last animated (capsules: none, the sim's mouth point is used).
static bool view_head(const void* ctx, int slot, float out[3]) { const CretView* v = ctx; return v->dinos_ok && cret_dino_joint(&v->dinos, slot, 0, out); }
static void draw_nests(CretView* v, const CretSim* sim) {
    GfxProgram* np = &v->nest; GfxProgram* op = &v->object;
    glUseProgram(np->id); common_uniforms(v, np, sim); set_scene_depth(v, np);
    gfx_bind_tex(np, "t_trans", 0, GL_TEXTURE_2D, v->trans.color);
    gfx_bind_tex(np, "t_skyview", 1, GL_TEXTURE_2D, v->skyview.color);
    gfx_bind_tex(np, "t_mat_a", 2, GL_TEXTURE_2D_ARRAY, v->t_mat_a);
    cret_csm_bind(&v->csm, np, 4);
    gfx_bind_tex(np, "t_horizon0", 5, GL_TEXTURE_2D, v->t_horizon0);   // the terrain's own shading inputs (nest.fs)
    gfx_bind_tex(np, "t_horizon1", 6, GL_TEXTURE_2D, v->t_horizon1);
    gfx_bind_tex(np, "t_weather", 7, GL_TEXTURE_2D, v->t_weather);
    gfx_bind_tex(np, "t_misc", 8, GL_TEXTURE_2D, v->t_misc);
    gfx_bind_tex(np, "t_normal", 9, GL_TEXTURE_2D, v->t_normal);
    glUseProgram(op->id);   // draw_agents left the object program's frame uniforms and textures set
    cret_nest_draw(&v->nests, sim, np, op, v->capsule.vaoId, v->capsule.triangleCount, v->capsule.indices != NULL, v->cam_pos, view_head, v);
}
static void draw_agents(CretView* v, const CretSim* sim) {
    GfxProgram* p = &v->object; glUseProgram(p->id); common_uniforms(v, p, sim); set_scene_depth(v, p);
    gfx_bind_tex(p, "t_trans", 0, GL_TEXTURE_2D, v->trans.color);
    gfx_bind_tex(p, "t_skyview", 1, GL_TEXTURE_2D, v->skyview.color);
    gfx_bind_tex(p, "t_horizon0", 2, GL_TEXTURE_2D, v->t_horizon0);
    gfx_bind_tex(p, "t_horizon1", 3, GL_TEXTURE_2D, v->t_horizon1);
    cret_csm_bind(&v->csm, p, 4);
    gfx_set2f(p, "u_map", (float)v->world->map.n, v->world->map.cell);
    draw_agent_meshes(v, sim, p, 0);
}

// Agent capsules for species without a model (dino.c draws the rest). Also the shadow-map caster pass for
// cascade k (k >= 0 binds that cascade's light matrix; the colour pass uses -1).
static void agent_model(const CretAgent* a, float* mf) {
    Matrix m = MatrixMultiply(MatrixScale(a->radius * 1.3f, a->height * 0.5f, a->radius),
                              MatrixMultiply(MatrixRotateY(-a->yaw), MatrixTranslate(a->x, a->y + a->height * 0.5f, a->z)));
    mat_to_floats(m, mf);
}
static int draw_agent_meshes(CretView* v, const CretSim* sim, const GfxProgram* p, int shadow_k) {
    GLint l_model = gfx_loc(p, "u_model"), l_color = gfx_loc(p, "u_color"); int n = 0;
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* a = &sim->agents[i]; if (!a->alive) continue;
        if (v->dinos_ok && cret_dino_has(&v->dinos, a->species)) continue;   // drawn by dino.c
        if (shadow_k >= 1) {
            float pc[3] = {a->x, a->y + a->height * 0.5f, a->z};
            if (!cret_csm_caster_visible(&v->csm, shadow_k - 1, pc, fmaxf(a->height * 0.5f, a->radius * 1.3f), a->height)) continue;
        }
        float mf[16]; agent_model(a, mf);
        glUniformMatrix4fv(l_model, 1, GL_FALSE, mf);
        if (l_color >= 0) glUniform3f(l_color, 0.55f + 0.1f * (i % 3), 0.5f, 0.42f);
        glBindVertexArray(v->capsule.vaoId);
        if (v->capsule.indices) glDrawElements(GL_TRIANGLES, v->capsule.triangleCount * 3, GL_UNSIGNED_SHORT, 0);
        else glDrawArrays(GL_TRIANGLES, 0, v->capsule.vertexCount);
        glBindVertexArray(0); n++;
    }
    return n;
}

// Sun shadow cascades: plants, props and agents into the depth array.
static void draw_shadows(CretView* v, const CretSim* sim, const CretCamera* cam, const VegFrameDesc* fd) {
    if (v->settings.shadow_quality != v->csm.requested) { cret_csm_close(&v->csm); cret_csm_init(&v->csm, v->settings.shadow_quality); }
    float fwd[3] = {cosf(cam->pitch) * cosf(cam->yaw), sinf(cam->pitch), cosf(cam->pitch) * sinf(cam->yaw)};
    // CRET_SHADOW_NUDGE="dx dy dz": moves only the cascade fit (stability test:
    // with texel snapping the image must not change).
    float fit[3] = {v->cam_pos[0], v->cam_pos[1], v->cam_pos[2]};
    if (getenv("CRET_SHADOW_NUDGE")) { float d[3] = {0}; sscanf(getenv("CRET_SHADOW_NUDGE"), "%f %f %f", &d[0], &d[1], &d[2]); for (int i = 0; i < 3; i++) fit[i] += d[i]; }
    if (!cret_csm_setup(&v->csm, fit, fwd, cam->fov, (float)v->width / v->height, v->sun_dir, v->viewproj)) return;
    double t0 = GetTime();
    for (int k = 0; k < v->csm.count; k++) {
        if (!v->csm.due[k]) continue;   // staggered: keeps last frame's map (csm.c)
        cret_csm_begin_cascade(&v->csm, k);
        int n = 0;
        if (v->veg_ok && v->settings.vegetation) {
            gprof_mark("shadow-veg");
            n += cret_veg_draw_shadow(&v->veg, &v->csm, k, fd);
            gprof_mark("shadow-props");
            if (v->props_ok) n += cret_props_draw_shadow(&v->props, &v->csm, k, fd);
        }
        gprof_mark("shadow-objects");
        if (k <= 1) {
            GfxProgram* p = &v->object_shadow; glUseProgram(p->id);
            gfx_setmat4(p, "u_viewproj", v->csm.vp[k]);
            n += draw_agent_meshes(v, sim, p, k + 1);
            n += cret_nest_draw_shadow(&v->nests, sim, p, &v->csm, k, v->cam_pos, view_head, v);   // nest.c: mounds and eggs
        }
        if (k <= 2 && v->dinos_ok) {
            n += cret_dino_draw_shadow(&v->dinos, sim, &v->csm, k);   // dino.c
        }
        n += cret_critters_draw_shadow(&v->csm, k);   // critters.c: the nearest cascade only
        v->csm.casters[k] = n;
        cret_csm_end_cascade(&v->csm, k);
    }
    cret_csm_end(&v->csm);
    v->csm.cpu_ms += (GetTime() - t0) * 1000.0;
}

// Contact shadows (csm.glsl): the nearest living animals within 45 m of the
// camera, then in the slots left the nearest small animals on the ground
// within 25 m. The same list stamps the trample map (render/trample.h), so a
// critter moving through ferns or grass parts them and gives itself away.
static void set_contacts(CretView* v, const CretSim* sim) {
    float b[CSM_CONTACTS][7], d2[CSM_CONTACTS]; int n = 0;
    for (int i = 0; i < sim->cfg.num_agents; i++) {
        const CretAgent* a = &sim->agents[i]; if (!a->alive) continue;
        float dx = a->x - v->cam_pos[0], dz = a->z - v->cam_pos[2], d = dx * dx + dz * dz;
        if (d > 45.0f * 45.0f) continue;
        int k = n < CSM_CONTACTS ? n++ : -1;
        if (k < 0) { k = 0; for (int j = 1; j < n; j++) if (d2[j] > d2[k]) k = j; if (d >= d2[k]) continue; }
        float len = CRET_SPECIES_TABLE[a->species].length * cret_growth_scale(a->growth);
        float body[7] = {a->x, a->y, a->z, a->yaw, len * 0.42f, fmaxf(a->radius, len * 0.1f), a->height};
        memcpy(b[k], body, sizeof body); d2[k] = d;
    }
    int animals = n;
    for (int q = 0; q < CRET_MAX_CRITTERS && sim->ncrit; q++) {
        const CretCritter* c = &sim->critters[q];
        if (!c->active || c->alt > 0.05f || c->kind == CRET_CR_FISH) continue;
        float dx = c->x - v->cam_pos[0], dz = c->z - v->cam_pos[2], d = dx * dx + dz * dz;
        if (d > 25.0f * 25.0f) continue;
        int k = n < CSM_CONTACTS ? n++ : -1;
        if (k < 0) { k = animals; for (int j = animals + 1; j < n; j++) if (d2[j] > d2[k]) k = j; if (k >= n || d >= d2[k]) continue; }
        float len = cret_critter_length(c->kind, c->kg);
        float body[7] = {c->x, c->y, c->z, c->yaw, len * 0.42f, len * 0.16f, len * 0.35f};
        memcpy(b[k], body, sizeof body); d2[k] = d;
    }
    cret_csm_set_contacts(&v->csm, &b[0][0], n);
}

static void fullscreen(CretView* v, GfxProgram* p, const GfxTarget* t) {
    gfx_bind_target(t); glUseProgram(p->id); gfx_draw(&v->tri); (void)p;
}

void cret_view_render(CretView* v, const CretSim* sim, const CretCamera* cam, float dt) {
    if (!v->initialized) return;
    rlDrawRenderBatchActive();
    gprof_frame_begin(); gprof_mark("setup");
    v->time += dt; v->frame++;
    apply_settings(v);
    if (v->props_ok && sim->world) {   // movable props the sim moved (dynprops.c) draw at their pose
        PropOverride o[CRET_MAX_DYN]; int n = 0;
        for (int k = 0; k < sim->ndyn; k++) {
            const CretDynProp* r = &sim->dyn[k];
            if (r->id < 0 || r->id >= sim->world->nprops) continue;
            o[n] = (PropOverride){sim->world->props[r->id].key, {r->p[0], r->p[1], r->p[2]}, {r->q[0], r->q[1], r->q[2], r->q[3]}}; n++;
        }
        cret_props_set_moved(&v->props, o, n);
    }
    cret_sun_direction(sim->time_of_day, v->sun_dir);
    // Weather follows the cloud cover (the one knob the sim drives): above
    // ~0.7 the sky greys over, above ~0.9 it rains. CRET_WEATHER=1 forces
    // overcast, 2 rain. Ground wetness lags the rain by about half a minute
    // (it snaps on teleports, so captures start wet).
    {
        static int wenv = -2; if (wenv == -2) wenv = getenv("CRET_WEATHER") ? atoi(getenv("CRET_WEATHER")) : -1;
        float cover = v->settings.cloud_coverage;
        if (wenv == 0) cover = fminf(cover, 0.6f); else if (wenv == 1) cover = fmaxf(cover, 0.86f); else if (wenv >= 2) cover = fmaxf(cover, 0.97f);
        v->cover = cover;
        v->overcast = smoothstepf(0.68f, 0.92f, cover); v->rain = smoothstepf(0.88f, 0.97f, cover);
        float jump = fabsf(cam->pos[0] - v->cam_pos[0]) + fabsf(cam->pos[1] - v->cam_pos[1]) + fabsf(cam->pos[2] - v->cam_pos[2]);
        if (v->frame <= 1 || jump > 60.0f) v->wet = v->rain;
        else v->wet += (v->rain - v->wet) * fminf(dt / 30.0f, 1.0f);
    }
    // Night is shot day-for-night: once the sun is well below the horizon the
    // full moon (opposite the sun) takes over as the key light, so shadows,
    // clouds and water all keep working, and post.fs grades the frame dark,
    // blue and desaturated by v->night. The switch happens in deep twilight,
    // when the moon is low and its light dim and red.
    v->true_sun_y = v->sun_dir[1];
    v->night = smoothstepf(-0.02f, -0.13f, v->sun_dir[1]);
    // The moon rises from below the horizon as the sun sinks, so at the switch
    // both lights sit just under the horizon and the key light fades through
    // zero instead of popping on (moon height = -sun height - 0.12).
    if (v->sun_dir[1] < -0.06f) {
        float my = -v->sun_dir[1] - 0.12f, hx = -v->sun_dir[0], hz = -v->sun_dir[2], hl = sqrtf(hx * hx + hz * hz), k = sqrtf(fmaxf(1 - my * my, 0)) / fmaxf(hl, 1e-6f);
        v->sun_dir[0] = hx * k; v->sun_dir[1] = my; v->sun_dir[2] = hz * k;
    }
    setup_camera(v, cam);
    gprof_mark("dino-anim");
    if (v->dinos_ok) cret_dino_update(&v->dinos, sim, v->world, v->cam_pos, dt);   // dino.c: animation and joint upload
    gprof_mark("nest+critters");
    cret_nest_draw_update(&v->nests, sim, v->world, v->cam_pos, v->time);          // nest.c: build timers, changed nest meshes
    if (v->veg_ok) {   // no ferns or grass growing through a nest
        float clr[VEG_CLEARINGS * 3]; cret_veg_set_clearings(&v->veg, clr, cret_nest_clearings(&v->nests, sim, clr, VEG_CLEARINGS));
    }
    cret_nest_queue_critters(sim, v->cam_pos, view_head, v);                        // nest.c: caught critters, lying or carried
    cret_critters_update(sim, v->cam_pos, v->viewproj, v->time);                     // critters.c: poses, culling, LODs, upload
    gprof_mark("terrain-select");
    select_terrain(v); node_instances(v);
    GLint prev_fbo; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    cret_csm_frame_timer(&v->csm, true);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
    VegFrameDesc fd = {.viewproj = v->viewproj, .cam = v->cam_pos, .sun = v->sun_dir, .far_plane = v->far_plane, .time = v->time, .frame = v->frame,
                       .t_trans = v->trans.color, .t_skyview = v->skyview.color, .t_horizon0 = v->t_horizon0, .t_horizon1 = v->t_horizon1,
                       .t_weather = v->t_weather, .cloud_cover = v->cover, .overcast = v->overcast, .wet = v->wet, .t_misc = v->t_misc,
                       .map_n = v->world->map.n, .map_cell = v->world->map.cell, .csm = &v->csm, .depth_ab = {v->depth_ab[0], v->depth_ab[1]}};
    // 0. Sun shadow maps (plants, props, agents).
    gprof_mark("shadows");
    draw_shadows(v, sim, cam, &fd);
    set_contacts(v, sim);
    gprof_mark("flush");
    // Submit work in chunks: the driver otherwise holds the whole frame until
    // the swap, and the GPU idles while the CPU culls and builds draw lists
    // (sp_conifer 27.9 -> 23.8 ms per frame on WSL d3d12).
    static int flush_mask = -1; if (flush_mask < 0) flush_mask = getenv("CRET_FLUSH") ? atoi(getenv("CRET_FLUSH")) : 3;
    if (flush_mask & 1) glFlush();
    glDisable(GL_DEPTH_TEST);
    // 1. Atmosphere LUTs.
    gprof_mark("luts+sky");
    if (v->frame == 1 || gfx_program_reload_if_changed(&v->trans_lut) || 1) {
        GfxProgram* p = &v->trans_lut; gfx_bind_target(&v->trans); glUseProgram(p->id); gfx_set1f(p, "u_night", v->night);
        gfx_set2f(p, "u_screen", 256, 64); gfx_draw(&v->tri);
    }
    {
        GfxProgram* p = &v->sky_lut; gfx_bind_target(&v->skyview); glUseProgram(p->id); common_uniforms(v, p, sim);
        gfx_set2f(p, "u_screen", 192, 108);
        gfx_bind_tex(p, "t_trans", 0, GL_TEXTURE_2D, v->trans.color);
        gfx_draw(&v->tri);
    }
    // 2. Opaque HDR scene.
    gfx_bind_target(&v->hdr);
    glClearColor(0, 0, 0, 1); glClearDepth(0.0); glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    {
        GfxProgram* p = &v->sky; glUseProgram(p->id); common_uniforms(v, p, sim);
        gfx_bind_tex(p, "t_trans", 0, GL_TEXTURE_2D, v->trans.color);
        gfx_bind_tex(p, "t_skyview", 1, GL_TEXTURE_2D, v->skyview.color);
        glDepthMask(GL_FALSE); gfx_draw(&v->tri); glDepthMask(GL_TRUE);
    }
    scene_depth_begin(v);
    // Plant depth prepass first: terrain and plants then shade each pixel once.
    bool veg = v->veg_ok && v->settings.vegetation;
    if (veg) { cret_veg_prepare(&v->veg, &fd); cret_veg_draw_depth(&v->veg, &fd); }
    // Props before the terrain: rocks and logs then hide the ground under
    // them from the (expensive) terrain shader through early-Z.
    gprof_mark("props");
    if (veg && v->props_ok) cret_props_draw(&v->props, &fd);   // props.c: same frame inputs as vegetation
    gprof_mark("terrain");
    draw_terrain(v, sim);
    if (flush_mask & 2) glFlush();
    if (veg) {
        glDepthMask(GL_FALSE);
        cret_veg_draw(&v->veg, &fd);
        glDepthMask(GL_TRUE);
    }
    gprof_mark("agents");
    draw_agents(v, sim);
    draw_nests(v, sim);
    if (v->dinos_ok) cret_dino_draw(&v->dinos, sim, &fd);   // dino.c
    gprof_mark("critters");
    cret_critters_draw(&fd);                                 // critters.c
    gprof_mark("fx");
    if (v->fx_draw) v->fx_draw(v->fx_ctx, v, &fd);           // fx.c
    gprof_mark("water");
    // 3. Water reads a copy of the opaque scene for refraction and depth.
    if (v->settings.water) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, v->hdr.fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, v->scene_copy.fbo);
        glBlitFramebuffer(0, 0, v->rw, v->rh, 0, 0, v->rw, v->rh, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        gfx_bind_target(&v->hdr);
        draw_water(v, sim);
    }
    scene_depth_end(v);
    glDisable(GL_DEPTH_TEST);
    gprof_mark("clouds");
    // 4. Volumetric clouds: a full-resolution sky image kept across frames.
    if (v->settings.clouds) {
        // CRET_PROFILE=1 prints the cloud pass GPU time (march + resolve).
        static GLuint q_cloud; static double q_sum; static int q_n; static int q_on = -1;
        if (q_on < 0) { q_on = getenv("CRET_PROFILE") != NULL; if (q_on) glGenQueries(1, &q_cloud); }
        if (q_on) glBeginQuery(GL_TIME_ELAPSED, q_cloud);
        // Sparse march (CRET_CLOUD_CB, default on): one full-res pixel of
        // every 4x4 block per frame, in a 16-frame Bayer order; the rest is
        // reprojected history (cloud_resolve.fs). CRET_CLOUD_CB=0 marches
        // every pixel every frame (reference, about 16x the march cost).
        static int cb_env = -2; if (cb_env == -2) cb_env = getenv("CRET_CLOUD_CB") ? atoi(getenv("CRET_CLOUD_CB")) : 1;
        // Teleports (capture shots, respawns) restart the history.
        float jump = fabsf(v->cam_pos[0] - v->prev_cam[0]) + fabsf(v->cam_pos[1] - v->prev_cam[1]) + fabsf(v->cam_pos[2] - v->prev_cam[2]);
        if (jump > 60.0f) v->hist_valid = false;
        bool cb = cb_env != 0;
        static const int BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};   // frame -> block index
        int bi = 0; for (int k = 0; k < 16; k++) if (BAYER4[k] == (v->frame & 15)) bi = k;
        int cbx = cb ? (bi & 3) : -1, cby = cb ? (bi >> 2) : -1;
        GfxTarget* mt = cb ? &v->cloud_q : &v->cloud_rt;
        GfxProgram* p = &v->clouds; gfx_bind_target(mt); glUseProgram(p->id); common_uniforms(v, p, sim); basis_uniforms(v, p);
        gfx_set2f(p, "u_screen", (float)mt->w, (float)mt->h);
        gfx_set2f(p, "u_full", (float)v->rw, (float)v->rh);
        glUniform2i(gfx_loc(p, "u_cb"), cbx, cby);
        gfx_set1f(p, "u_quality", v->settings.cloud_quality);
        gfx_bind_tex(p, "t_depth", 0, GL_TEXTURE_2D, v->hdr.depth);
        gfx_bind_tex(p, "t_shape", 1, GL_TEXTURE_3D, v->t_cloud_shape);
        gfx_bind_tex(p, "t_detail", 2, GL_TEXTURE_3D, v->t_cloud_detail);
        gfx_bind_tex(p, "t_weather", 3, GL_TEXTURE_2D, v->t_weather);
        gfx_bind_tex(p, "t_trans", 4, GL_TEXTURE_2D, v->trans.color);
        gfx_bind_tex(p, "t_skyview", 5, GL_TEXTURE_2D, v->skyview.color);
        gfx_bind_tex(p, "t_noise", 6, GL_TEXTURE_2D, v->t_noise);
        gfx_draw(&v->tri);
        // Temporal accumulation: the march jitters every frame; history is
        // reprojected by view direction (clouds are far) and, while the view
        // moves, clamped to the current neighbourhood against ghosting.
        gprof_mark("cloud-resolve");
        GfxProgram* r = &v->cloud_resolve; int cur = v->hist_index ^ 1;
        gfx_bind_target(&v->cloud_hist[cur]); glUseProgram(r->id); common_uniforms(v, r, sim); basis_uniforms(v, r);
        gfx_set2f(r, "u_screen", (float)v->rw, (float)v->rh);
        gfx_set1i(r, "u_hist_valid", v->hist_valid);
        gfx_bind_tex(r, "t_cur", 0, GL_TEXTURE_2D, mt->color);
        gfx_bind_tex(r, "t_hist", 1, GL_TEXTURE_2D, v->cloud_hist[v->hist_index].color);
        gfx_bind_tex(r, "t_depth", 2, GL_TEXTURE_2D, v->hdr.depth);
        glUniform2i(gfx_loc(r, "u_cb"), cbx, cby);
        gfx_draw(&v->tri);
        v->hist_index = cur; v->hist_valid = true;
        if (q_on) {
            glEndQuery(GL_TIME_ELAPSED); GLuint64 ns = 0; glGetQueryObjectui64v(q_cloud, GL_QUERY_RESULT, &ns);
            if (v->frame > 4) { q_sum += ns * 1e-6; q_n++; }
            if (q_n == 20) { fprintf(stderr, "cretaceous: clouds %.2f ms (%dx%d history, %dx%d marched)\n", q_sum / q_n, v->rw, v->rh, mt->w, mt->h); q_sum = 0; q_n = 0; }
        }
    }
    GLuint clouds_tex = v->cloud_hist[v->hist_index].color;
    // 5. Bloom.
    gprof_mark("bloom+exposure");
    {
        GfxProgram* p = &v->bright; gfx_bind_target(&v->bloom[0]); glUseProgram(p->id); common_uniforms(v, p, sim);
        gfx_bind_tex(p, "t_src", 0, GL_TEXTURE_2D, v->hdr.color);
        gfx_bind_tex(p, "t_clouds", 1, GL_TEXTURE_2D, clouds_tex);
        gfx_bind_tex(p, "t_depth", 2, GL_TEXTURE_2D, v->hdr.depth);
        gfx_set1i(p, "u_has_clouds", v->settings.clouds);
        gfx_draw(&v->tri);
        GfxProgram* b = &v->blur;
        for (int it = 0; it < 3; it++) {
            gfx_bind_target(&v->bloom[1]); glUseProgram(b->id); gfx_bind_tex(b, "t_src", 0, GL_TEXTURE_2D, v->bloom[0].color);
            gfx_set2f(b, "u_dir", 1.0f / v->bloom[0].w * (1 + it), 0); gfx_draw(&v->tri);
            gfx_bind_target(&v->bloom[0]); glUseProgram(b->id); gfx_bind_tex(b, "t_src", 0, GL_TEXTURE_2D, v->bloom[1].color);
            gfx_set2f(b, "u_dir", 0, 1.0f / v->bloom[0].h * (1 + it)); gfx_draw(&v->tri);
        }
    }
    // 5b. Auto-exposure: log luminance -> mip average -> temporal adaptation.
    {
        GfxProgram* p = &v->lum; gfx_bind_target(&v->lum_rt); glUseProgram(p->id); common_uniforms(v, p, sim);
        gfx_bind_tex(p, "t_src", 0, GL_TEXTURE_2D, v->hdr.color);
        gfx_bind_tex(p, "t_clouds", 1, GL_TEXTURE_2D, clouds_tex);
        gfx_bind_tex(p, "t_depth", 2, GL_TEXTURE_2D, v->hdr.depth);
        gfx_set1i(p, "u_has_clouds", v->settings.clouds); gfx_draw(&v->tri);
        glBindTexture(GL_TEXTURE_2D, v->lum_rt.color); glGenerateMipmap(GL_TEXTURE_2D);
        int src = v->adapt_i, dst = 1 - src;
        GfxProgram* a = &v->adapt; gfx_bind_target(&v->adapt_rt[dst]); glUseProgram(a->id);
        gfx_bind_tex(a, "t_lum", 0, GL_TEXTURE_2D, v->lum_rt.color);
        gfx_bind_tex(a, "t_prev", 1, GL_TEXTURE_2D, v->adapt_rt[src].color);
        float rate = v->adapt_valid ? 1.0f - expf(-fmaxf(dt, 0.0f) * 2.5f) : 1.0f;
        gfx_set1f(a, "u_rate", rate); gfx_draw(&v->tri);
        v->adapt_i = dst; v->adapt_valid = true;
    }
    // 6. Composite: clouds, bloom, exposure, tonemap -> backbuffer.
    gprof_mark("post");
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(0, 0, v->width, v->height);
    {
        GfxProgram* p = &v->post; glUseProgram(p->id); common_uniforms(v, p, sim);
        gfx_bind_tex(p, "t_src", 0, GL_TEXTURE_2D, v->hdr.color);
        gfx_bind_tex(p, "t_clouds", 1, GL_TEXTURE_2D, clouds_tex);
        gfx_bind_tex(p, "t_bloom", 2, GL_TEXTURE_2D, v->bloom[0].color);
        gfx_bind_tex(p, "t_depth", 3, GL_TEXTURE_2D, v->hdr.depth);
        gfx_bind_tex(p, "t_adapt", 4, GL_TEXTURE_2D, v->adapt_rt[v->adapt_i].color);
        gfx_set1i(p, "u_has_clouds", v->settings.clouds);
        gfx_set1f(p, "u_exposure", v->settings.exposure);
        gfx_draw(&v->tri);
    }
    glUseProgram(0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    cret_csm_frame_timer(&v->csm, false);
    gprof_frame_end();
}
