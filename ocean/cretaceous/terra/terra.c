// terra: see terra.h for the pipeline overview.
#include "terra.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define TPI 6.28318530718f
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// ---------------------------------------------------------------- random/noise
static inline uint32_t mix32(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h;
}
static inline uint32_t hash2i(int x, int y, uint32_t s) {
    return mix32((uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ s * 0xcb1ab31fu);
}
typedef struct { uint64_t s; } Rng;
static inline uint32_t rng_u32(Rng* r) {
    r->s += 0x9e3779b97f4a7c15ull; uint64_t z = r->s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
static inline float rng_f(Rng* r) { return (rng_u32(r) >> 8) * (1.0f / 16777216.0f); }

static const float GRAD[16][2] = {
    {1,0},{.9239f,.3827f},{.7071f,.7071f},{.3827f,.9239f},{0,1},{-.3827f,.9239f},{-.7071f,.7071f},{-.9239f,.3827f},
    {-1,0},{-.9239f,-.3827f},{-.7071f,-.7071f},{-.3827f,-.9239f},{0,-1},{.3827f,-.9239f},{.7071f,-.7071f},{.9239f,-.3827f}};
static inline float gdot(int ix, int iy, float fx, float fy, uint32_t s) {
    const float* g = GRAD[hash2i(ix, iy, s) & 15]; return g[0] * fx + g[1] * fy;
}
// Gradient noise in about [-1, 1].
static float gnoise(float x, float y, uint32_t s) {
    float fx0 = floorf(x), fy0 = floorf(y);
    int ix = (int)fx0, iy = (int)fy0; float fx = x - fx0, fy = y - fy0;
    float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    float a = gdot(ix, iy, fx, fy, s), b = gdot(ix + 1, iy, fx - 1, fy, s);
    float c = gdot(ix, iy + 1, fx, fy - 1, s), d = gdot(ix + 1, iy + 1, fx - 1, fy - 1, s);
    return 1.4f * lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}
static float fbm(float x, float y, uint32_t s, int oct, float lac, float gain) {
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < oct; i++) {
        // Rotate each octave to hide the lattice.
        float rx = 0.8f * x - 0.6f * y, ry = 0.6f * x + 0.8f * y;
        sum += amp * gnoise(rx, ry, s + i * 1013u); norm += amp;
        x = rx * lac + 17.3f; y = ry * lac - 9.1f; amp *= gain;
    }
    return sum / norm;
}
// Musgrave ridged multifractal, 0..~1.
static float ridged(float x, float y, uint32_t s, int oct) {
    float sum = 0, amp = 0.5f, w = 1, norm = 0;
    for (int i = 0; i < oct; i++) {
        float rx = 0.8f * x - 0.6f * y, ry = 0.6f * x + 0.8f * y;
        float n = 1 - fabsf(gnoise(rx, ry, s + i * 7919u)); n *= n;
        sum += n * amp * w; norm += amp; w = clampf(n * 1.6f, 0, 1);
        x = rx * 2.03f + 3.1f; y = ry * 2.03f - 1.7f; amp *= 0.5f;
    }
    return sum / norm;
}

// ---------------------------------------------------------------- grid helpers
static float* fgrid(int n) { return (float*)calloc((size_t)n * n, sizeof(float)); }
static float sample_bilinear(const float* g, int n, float x, float z) {
    x = clampf(x, 0, n - 1.001f); z = clampf(z, 0, n - 1.001f);
    int ix = (int)x, iz = (int)z; float fx = x - ix, fz = z - iz;
    const float* r0 = g + (size_t)iz * n + ix; const float* r1 = r0 + n;
    return lerpf(lerpf(r0[0], r0[1], fx), lerpf(r1[0], r1[1], fx), fz);
}
static inline float cubic(float a, float b, float c, float d, float t) {
    return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}
static float sample_bicubic(const float* g, int n, float x, float z) {
    int ix = (int)floorf(x), iz = (int)floorf(z); float fx = x - ix, fz = z - iz; float col[4];
    for (int j = -1; j <= 2; j++) {
        int zz = imin(imax(iz + j, 0), n - 1); const float* row = g + (size_t)zz * n; float v[4];
        for (int i = -1; i <= 2; i++) v[i + 1] = row[imin(imax(ix + i, 0), n - 1)];
        col[j + 1] = cubic(v[0], v[1], v[2], v[3], fx);
    }
    return cubic(col[0], col[1], col[2], col[3], fz);
}
// Resample a grid of size a to size b (both sample-aligned at corners).
static float* resample(const float* g, int a, int b) {
    float* o = fgrid(b); float s = (float)(a - 1) / (b - 1);
    #pragma omp parallel for schedule(static)
    for (int z = 0; z < b; z++) for (int x = 0; x < b; x++) o[(size_t)z * b + x] = sample_bicubic(g, a, x * s, z * s);
    return o;
}
static void box_blur(float* g, int n, int r, int passes) {
    float* tmp = fgrid(n);
    for (int p = 0; p < passes; p++) {
        #pragma omp parallel for schedule(static)
        for (int z = 0; z < n; z++) {
            float* row = g + (size_t)z * n; float acc = 0; int cnt = 0;
            for (int x = -r; x <= r; x++) if (x >= 0 && x < n) { acc += row[x]; cnt++; }
            for (int x = 0; x < n; x++) {
                tmp[(size_t)z * n + x] = acc / cnt;
                int xo = x - r, xi = x + r + 1;
                if (xo >= 0) { acc -= row[xo]; cnt--; }
                if (xi < n) { acc += row[xi]; cnt++; }
            }
        }
        #pragma omp parallel for schedule(static)
        for (int x = 0; x < n; x++) {
            float acc = 0; int cnt = 0;
            for (int z = -r; z <= r; z++) if (z >= 0 && z < n) { acc += tmp[(size_t)z * n + x]; cnt++; }
            for (int z = 0; z < n; z++) {
                g[(size_t)z * n + x] = acc / cnt;
                int zo = z - r, zi = z + r + 1;
                if (zo >= 0) { acc -= tmp[(size_t)zo * n + x]; cnt--; }
                if (zi < n) { acc += tmp[(size_t)zi * n + x]; cnt++; }
            }
        }
    }
    free(tmp);
}
static const int DX8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
static const int DZ8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const float DL8[8] = {1, 1.41421356f, 1, 1.41421356f, 1, 1.41421356f, 1, 1.41421356f};

// ---------------------------------------------------------------- min-heap
typedef struct { float k; int i; } HeapItem;
typedef struct { HeapItem* a; int n, cap; } Heap;
static void heap_push(Heap* h, float k, int i) {
    if (h->n == h->cap) { h->cap = h->cap ? h->cap * 2 : 1024; h->a = realloc(h->a, sizeof(HeapItem) * h->cap); }
    int c = h->n++;
    while (c > 0) { int p = (c - 1) >> 1; if (h->a[p].k <= k) break; h->a[c] = h->a[p]; c = p; }
    h->a[c] = (HeapItem){k, i};
}
static HeapItem heap_pop(Heap* h) {
    HeapItem top = h->a[0], last = h->a[--h->n]; int c = 0;
    for (;;) {
        int l = 2 * c + 1; if (l >= h->n) break;
        int r = l + 1, m = (r < h->n && h->a[r].k < h->a[l].k) ? r : l;
        if (h->a[m].k >= last.k) break;
        h->a[c] = h->a[m]; c = m;
    }
    if (h->n > 0) h->a[c] = last;
    return top;
}

// ---------------------------------------------------------------- ocean & priority flood
// Marks cells connected to the map edge through h < sea. Returns count.
static int ocean_mask(const float* h, int n, uint8_t* sea) {
    memset(sea, 0, (size_t)n * n);
    int* q = (int*)malloc(sizeof(int) * (size_t)n * n); int qh = 0, qt = 0;
    for (int i = 0; i < n; i++) {
        int e[4] = {i, (n - 1) * n + i, i * n, i * n + n - 1};
        for (int k = 0; k < 4; k++) if (!sea[e[k]] && h[e[k]] < 0) { sea[e[k]] = 1; q[qt++] = e[k]; }
    }
    while (qh < qt) {
        int c = q[qh++], x = c % n, z = c / n;
        for (int k = 0; k < 8; k += 2) {
            int xx = x + DX8[k], zz = z + DZ8[k];
            if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            int j = zz * n + xx; if (!sea[j] && h[j] < 0) { sea[j] = 1; q[qt++] = j; }
        }
    }
    free(q); return qt;
}

// Priority flood (Barnes 2014). Seeds: sea cells (at sea level) and map edges.
// Writes filled surface `f` (eps > 0 makes every cell drain), the pop order
// (ascending f, a valid downstream-first topological order) and returns the
// number of ordered cells. Sea cells are not in the order.
// Optional f0: the same flood without epsilon, so lake depth f0 - h is not
// polluted by epsilon accumulated along long flats.
static int priority_flood_ex(const float* h, const uint8_t* sea, int n, float eps, float* f, int* order, uint8_t* seen, float* f0);
static int priority_flood(const float* h, const uint8_t* sea, int n, float eps, float* f, int* order, uint8_t* seen) {
    return priority_flood_ex(h, sea, n, eps, f, order, seen, NULL);
}
static int priority_flood_ex(const float* h, const uint8_t* sea, int n, float eps, float* f, int* order, uint8_t* seen, float* f0) {
    Heap hp = {0}; int cnt = 0; size_t N = (size_t)n * n;
    memset(seen, 0, N);
    for (size_t i = 0; i < N; i++) {
        if (sea[i]) { f[i] = h[i]; if (f0) f0[i] = h[i]; seen[i] = 1; continue; }
        int x = (int)(i % n), z = (int)(i / n);
        int edge = x == 0 || z == 0 || x == n - 1 || z == n - 1, coast = 0;
        if (!edge) for (int k = 0; k < 8; k++) if (sea[(z + DZ8[k]) * n + x + DX8[k]]) { coast = 1; break; }
        if (edge || coast) { seen[i] = 1; f[i] = h[i] < 0 ? 0 : h[i]; if (f0) f0[i] = f[i]; heap_push(&hp, f[i], (int)i); }
    }
    while (hp.n) {
        HeapItem it = heap_pop(&hp); int c = it.i, x = c % n, z = c / n;
        order[cnt++] = c;
        for (int k = 0; k < 8; k++) {
            int xx = x + DX8[k], zz = z + DZ8[k];
            if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            int j = zz * n + xx; if (seen[j]) continue; seen[j] = 1;
            float v = h[j] > f[c] + eps ? h[j] : f[c] + eps; f[j] = v; heap_push(&hp, v, j);
            if (f0) f0[j] = h[j] > f0[c] ? h[j] : f0[c];
        }
    }
    free(hp.a); return cnt;
}
// Steepest descent receiver on the filled surface; -1 for base-level cells.
static void receivers(const float* f, const uint8_t* sea, int n, int* rec, float* rdist, float cell) {
    #pragma omp parallel for schedule(static)
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        int i = z * n + x; rec[i] = -1; rdist[i] = cell;
        if (sea[i]) continue;
        float best = 0; int bi = -1;
        for (int k = 0; k < 8; k++) {
            int xx = x + DX8[k], zz = z + DZ8[k];
            if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            int j = zz * n + xx; float s = (f[i] - f[j]) / DL8[k];
            if (s > best) { best = s; bi = k; }
        }
        if (bi >= 0) { rec[i] = (z + DZ8[bi]) * n + x + DX8[bi]; rdist[i] = DL8[bi] * cell; }
    }
}

// ---------------------------------------------------------------- tectonics
#define MAX_PLATES 24
typedef struct {
    float x, z, vx, vz; int continental; float lith;
} Plate;
typedef struct {
    const TerraParams* p; float size; int np; Plate pl[MAX_PLATES];
    int nhot; float hx[4], hz[4], hr[4], ha[4];
    int nbas; float bx[6], bz[6], br[6], ba[6];
    uint32_t s;
} Tecto;

static void tecto_init(Tecto* t, const TerraParams* p, float size) {
    memset(t, 0, sizeof(*t)); t->p = p; t->size = size; t->s = mix32(p->seed * 2654435761u + 77);
    Rng r = {p->seed * 0x9E3779B97F4A7C15ull + 1};
    t->np = imin(imax(p->plates, 3), MAX_PLATES);
    for (int i = 0; i < t->np; i++) {
        Plate* q = &t->pl[i];
        // Relaxed placement: best of a few candidates by distance to others.
        float bx = 0, bz = 0, bd = -1;
        for (int c = 0; c < 12; c++) {
            float x = (-0.15f + 1.3f * rng_f(&r)) * size, z = (-0.15f + 1.3f * rng_f(&r)) * size, d = 1e30f;
            for (int j = 0; j < i; j++) { float dx = x - t->pl[j].x, dz = z - t->pl[j].z; d = fminf(d, dx * dx + dz * dz); }
            if (d > bd) { bd = d; bx = x; bz = z; }
        }
        q->x = bx; q->z = bz;
        float a = TPI * rng_f(&r), m = 0.4f + 0.6f * rng_f(&r);
        q->vx = cosf(a) * m; q->vz = sinf(a) * m;
        float cx = bx / size - 0.5f, cz = bz / size - 0.5f, centre = 1 - sqrtf(cx * cx + cz * cz) * 1.6f;
        q->continental = rng_f(&r) < p->land_fraction + 0.6f * centre - 0.2f;
        q->lith = 0.7f + 0.6f * rng_f(&r);
    }
    // Make sure the plate nearest the centre is continental.
    int best = 0; float bd = 1e30f;
    for (int i = 0; i < t->np; i++) { float dx = t->pl[i].x - size / 2, dz = t->pl[i].z - size / 2, d = dx * dx + dz * dz; if (d < bd) { bd = d; best = i; } }
    t->pl[best].continental = 1;
    t->nhot = 1 + (int)(rng_f(&r) * 2.5f);
    for (int i = 0; i < t->nhot; i++) {
        t->hx[i] = (0.25f + 0.5f * rng_f(&r)) * size; t->hz[i] = (0.25f + 0.5f * rng_f(&r)) * size;
        t->hr[i] = (0.025f + 0.025f * rng_f(&r)) * size; t->ha[i] = 0.5f + 0.7f * rng_f(&r);
    }
    // Sag basins: slow subsidence that keeps lakes alive against infill.
    t->nbas = 2 + (int)(rng_f(&r) * 3.5f);
    for (int i = 0; i < t->nbas; i++) {
        t->bx[i] = (0.25f + 0.5f * rng_f(&r)) * size; t->bz[i] = (0.25f + 0.5f * rng_f(&r)) * size;
        t->br[i] = (0.02f + 0.03f * rng_f(&r)) * size; t->ba[i] = 0.04f + 0.05f * rng_f(&r);
    }
}

// Uplift (m/yr), initial elevation (m) and lithology hardness at a world point.
static void tecto_sample(const Tecto* t, float wx, float wz, float* uplift, float* base, float* lith) {
    const TerraParams* p = t->p; float S = t->size; uint32_t s = t->s;
    float u = wx / S, v = wz / S;
    // Domain warp: two layers so plate boundaries meander at several scales.
    float w1x = fbm(u * 2.1f + 5.2f, v * 2.1f + 1.3f, s + 1, 4, 2.0f, 0.5f);
    float w1z = fbm(u * 2.1f - 3.7f, v * 2.1f + 8.1f, s + 2, 4, 2.0f, 0.5f);
    float qx = u + 0.16f * w1x, qz = v + 0.16f * w1z;
    float w2x = fbm(qx * 6.0f + 1.7f, qz * 6.0f - 4.2f, s + 3, 3, 2.0f, 0.5f);
    float w2z = fbm(qx * 6.0f - 7.3f, qz * 6.0f + 2.9f, s + 4, 3, 2.0f, 0.5f);
    qx = (qx + 0.035f * w2x) * S; qz = (qz + 0.035f * w2z) * S;
    int a = -1, b = -1; float da = 1e30f, db = 1e30f;
    for (int i = 0; i < t->np; i++) {
        float dx = qx - t->pl[i].x, dz = qz - t->pl[i].z, d = dx * dx + dz * dz;
        if (d < da) { db = da; b = a; da = d; a = i; } else if (d < db) { db = d; b = i; }
    }
    const Plate *A = &t->pl[a], *B = &t->pl[b];
    float nx = B->x - A->x, nz = B->z - A->z, nl = sqrtf(nx * nx + nz * nz) + 1e-6f; nx /= nl; nz /= nl;
    float bd = (db - da) / (2 * nl);                     // distance to the bisector (m)
    float conv = (A->vx - B->vx) * nx + (A->vz - B->vz) * nz; // >0 converging
    float shear = fabsf((A->vx - B->vx) * -nz + (A->vz - B->vz) * nx);
    // Along-boundary modulation breaks orogens into segments and saddles.
    float seg = 0.5f + 0.5f * fbm(u * 7.0f + 11.0f, v * 7.0f - 5.0f, s + 9, 4, 2.0f, 0.55f);
    seg = clampf(seg * 1.7f - 0.25f, 0.05f, 1.4f);
    float W = S * (0.045f + 0.03f * (0.5f + 0.5f * fbm(u * 3 + 2, v * 3 - 1, s + 10, 2, 2, 0.5f)));
    float U = 0; float pk = p->uplift;
    int ca = A->continental, cb = B->continental;
    if (conv > 0) {
        if (ca && cb) {                      // continental collision: broad range
            float g = expf(-(bd * bd) / (W * W * 1.8f));
            float g2 = expf(-((bd - W * 0.9f) * (bd - W * 0.9f)) / (W * W * 0.5f));
            U += pk * conv * seg * (g + 0.35f * g2);
        } else if (ca && !cb) {              // A overrides oceanic B: coastal arc inland
            float off = W * 1.1f, g = expf(-((bd - off) * (bd - off)) / (W * W * 0.6f));
            U += pk * conv * seg * 0.9f * g;
        } else if (!ca && cb) {              // A subducts: trench
            U -= pk * conv * 0.2f * expf(-(bd * bd) / (W * W * 0.3f));
        } else {                             // ocean-ocean: island arc on the higher index
            if (a > b) { float off = W * 0.9f; U += pk * conv * seg * 0.7f * expf(-((bd - off) * (bd - off)) / (W * W * 0.35f)); }
        }
    } else if (ca) {                         // continental rift: graben with shoulders
        float wr = W * 0.55f, g = expf(-(bd * bd) / (wr * wr));
        float sh = expf(-((bd - wr * 1.6f) * (bd - wr * 1.6f)) / (wr * wr * 0.6f));
        U += pk * (-conv) * seg * (-0.55f * g + 0.25f * sh);
    }
    (void)shear;
    // Interior: gentle warping keeps continents rolling, not flat.
    float interior = fbm(u * 3.5f + 40, v * 3.5f - 13, s + 21, 5, 2.0f, 0.5f);
    float hillsn = ridged(u * 9.0f + 3, v * 9.0f + 7, s + 22, 4);
    if (ca) U += pk * (0.06f + 0.10f * interior + 0.10f * hillsn * seg);
    else U -= pk * 0.05f;
    // Hotspot volcanoes with a summit crater.
    for (int i = 0; i < t->nhot; i++) {
        float dx = wx - t->hx[i], dz = wz - t->hz[i], r2 = (dx * dx + dz * dz) / (t->hr[i] * t->hr[i]);
        U += pk * t->ha[i] * (1.3f * expf(-r2) - 0.9f * expf(-r2 * 9.0f));
    }
    for (int i = 0; i < t->nbas; i++) {
        // Warped, stretched lobes so lakes take the shape of the valleys.
        float ox = 0.6f * t->br[i] * fbm(u * 6 + i * 3.1f, v * 6, s + 60 + i, 4, 2, 0.55f);
        float oz = 0.6f * t->br[i] * fbm(u * 6, v * 6 + i * 2.3f, s + 70 + i, 4, 2, 0.55f);
        float dx = wx + ox - t->bx[i], dz = wz + oz - t->bz[i];
        float ang = i * 1.7f, ca2 = cosf(ang), sa2 = sinf(ang);
        float ax = (dx * ca2 + dz * sa2) / 1.8f, az = -dx * sa2 + dz * ca2;
        float r2 = (ax * ax + az * az) / (t->br[i] * t->br[i]);
        U -= pk * t->ba[i] * expf(-r2);
    }
    // Continental shelf: crust type blends across the boundary (0.5 on it).
    float cont = (float)ca;
    if (ca && !cb) cont = 0.5f + 0.5f * sstep(0.0f, 0.12f * S, bd);
    else if (!ca && cb) cont = 0.5f * (1 - sstep(0.0f, 0.12f * S, bd));
    float b0 = lerpf(-420.0f, 60.0f + 120.0f * interior, cont);
    // Bounded world: an organic, warped radial falloff guarantees open ocean
    // at the map edge without boxy coastlines.
    float cx = u - 0.5f + 0.14f * fbm(u * 2.3f + 9, v * 2.3f + 4, s + 30, 4, 2, 0.5f);
    float cz = v - 0.5f + 0.14f * fbm(u * 2.3f - 3, v * 2.3f + 7, s + 31, 4, 2, 0.5f);
    float rr = sqrtf(cx * cx + cz * cz) * 2 + 0.12f * fbm(u * 6 + 1, v * 6 + 2, s + 32, 4, 2, 0.55f);
    float e = fminf(fminf(u, 1 - u), fminf(v, 1 - v));
    float edge = (1 - sstep(0.62f, 0.98f, rr)) * sstep(0.015f, 0.07f, e);
    b0 = lerpf(-30.0f - 900.0f * (1 - edge) * (1 - edge), b0, edge);
    U = lerpf(-pk * 0.1f, U, edge);
    *uplift = U; *base = b0 + 30.0f * fbm(u * 20, v * 20, s + 40, 3, 2, 0.5f);
    *lith = (bd < W * 1.5f && conv > 0 ? 1.25f : 1.0f) * A->lith;
}

// ---------------------------------------------------------------- climate
// Orographic precipitation: moisture advected along the wind, rained out on
// upslopes and replenished over the sea. Result normalised to mean `rain`.
static void orographic_rain(const float* h, const uint8_t* sea, int n, float cell, float wind, float mean_rain, float* rain) {
    size_t N = (size_t)n * n; float wx = cosf(wind), wz = sinf(wind);
    float* hb = fgrid(n); memcpy(hb, h, N * sizeof(float));
    for (size_t i = 0; i < N; i++) if (hb[i] < 0) hb[i] = 0;
    box_blur(hb, n, imax(1, (int)(250.0f / cell)), 2);
    float* M = fgrid(n);
    // Sweep order by projection on the wind; counting sort by bucket.
    float pmin = 1e30f, pmax = -1e30f;
    float corners[4] = {0, (n - 1) * wx, (n - 1) * wz, (n - 1) * (wx + wz)};
    for (int k = 0; k < 4; k++) { pmin = fminf(pmin, corners[k]); pmax = fmaxf(pmax, corners[k]); }
    int nb = (int)(pmax - pmin) + 2; int* cnt = (int*)calloc(nb + 1, sizeof(int)); int* ord = (int*)malloc(sizeof(int) * N);
    for (size_t i = 0; i < N; i++) { int x = (int)(i % n), z = (int)(i / n); cnt[(int)(x * wx + z * wz - pmin) + 1]++; }
    for (int k = 0; k < nb; k++) cnt[k + 1] += cnt[k];
    for (size_t i = 0; i < N; i++) { int x = (int)(i % n), z = (int)(i / n); ord[cnt[(int)(x * wx + z * wz - pmin)]++] = (int)i; }
    float step = 1.0f, decay = expf(-cell / 150000.0f);
    for (size_t k = 0; k < N; k++) {
        int i = ord[k], x = i % n, z = i / n;
        float ux = x - wx * step, uz = z - wz * step; float m, hu;
        if (ux < 0 || uz < 0 || ux > n - 1 || uz > n - 1) { m = 1.0f; hu = hb[i]; }
        else { m = sample_bilinear(M, n, ux, uz); hu = sample_bilinear(hb, n, ux, uz); }
        m *= decay;
        if (sea[i]) m += (1.0f - m) * 0.08f * cell / 100.0f;
        float up = fmaxf(0, (hb[i] - hu) / cell);
        float r = m * (0.0012f * cell / 100.0f + 1.0f * up * fminf(1.0f, cell / 50.0f));
        r = fminf(r, m * 0.5f);
        rain[i] = r * 100.0f / cell; M[i] = m - r;
    }
    box_blur(rain, n, imax(1, (int)(400.0f / cell)), 2);
    double acc = 0; int land = 0;
    for (size_t i = 0; i < N; i++) if (!sea[i]) { acc += rain[i]; land++; }
    float scale = land ? mean_rain / (float)(acc / land + 1e-9) : 1;
    for (size_t i = 0; i < N; i++) {
        float alt = clampf(h[i], 0, 3000);
        rain[i] = clampf(rain[i] * scale * (1 + alt / 2500.0f) * 0.5f + mean_rain * 0.5f, 0.2f * mean_rain, 4 * mean_rain);
    }
    free(cnt); free(ord); free(M); free(hb);
}

// ---------------------------------------------------------------- landscape evolution
typedef struct {
    int n; float cell;
    float *h, *U, *lith, *rain, *f, *A, *rd, *tmp, *h0, *qs, *lk; int *order, *rec; uint8_t *sea, *seen;
    uint32_t s;
} Level;

static void level_alloc(Level* L, int n, float cell, uint32_t s) {
    size_t N = (size_t)n * n; L->n = n; L->cell = cell; L->s = s;
    L->h = fgrid(n); L->U = fgrid(n); L->lith = fgrid(n); L->rain = fgrid(n); L->f = fgrid(n);
    L->A = fgrid(n); L->rd = fgrid(n); L->tmp = fgrid(n); L->h0 = fgrid(n); L->qs = fgrid(n); L->lk = fgrid(n);
    L->order = (int*)malloc(sizeof(int) * N); L->rec = (int*)malloc(sizeof(int) * N);
    L->sea = (uint8_t*)calloc(N, 1); L->seen = (uint8_t*)calloc(N, 1);
}
static void level_free(Level* L) {
    free(L->h); free(L->U); free(L->lith); free(L->rain); free(L->f); free(L->A); free(L->rd); free(L->tmp); free(L->h0); free(L->qs); free(L->lk);
    free(L->order); free(L->rec); free(L->sea); free(L->seen);
}
static void level_tectonics(Level* L, const Tecto* t, int init_height) {
    int n = L->n;
    #pragma omp parallel for schedule(dynamic, 8)
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        float u, b, li; tecto_sample(t, x * L->cell, z * L->cell, &u, &b, &li);
        size_t i = (size_t)z * n + x; L->U[i] = u; L->lith[i] = li; if (init_height) L->h[i] = b;
    }
}
// Strata: alternating hard and soft beds, tilted and warped.
static inline float strata(float h, float x, float z, uint32_t s) {
    float w = h + 70.0f * gnoise(x * 0.0007f, z * 0.0007f, s + 50) + 0.012f * x - 0.007f * z;
    float ph = w / 55.0f; float fr = ph - floorf(ph);
    float hard = sstep(0.55f, 0.7f, fr) * (1 - sstep(0.88f, 0.98f, fr));
    float bed = 0.5f + 0.5f * gnoise(floorf(ph) * 1.37f, 0.5f, s + 51);
    return 0.65f + 0.9f * hard * bed;
}

static void thermal_ex(float* h, float* soil, int n, float cell, const float* hard, int iters, float ta, float tb, float tlo, float thi, float rate);
static void evolve(Level* L, float years, int steps, const TerraParams* p, int rain_every, int final_level) {
    int n = L->n; size_t N = (size_t)n * n; float dt = years / steps, cell = L->cell;
    float K = p->erodibility, D = p->diffusion;
    float acrit = getenv("TERRA_ACRIT") ? (float)atof(getenv("TERRA_ACRIT")) : 4.0e3f;
    for (int st = 0; st < steps; st++) {
        for (size_t i = 0; i < N; i++) L->h[i] += L->U[i] * dt;
        ocean_mask(L->h, n, L->sea);
        if (st % rain_every == 0) orographic_rain(L->h, L->sea, n, cell, p->wind_angle, p->rain, L->rain);
        int cnt = priority_flood_ex(L->h, L->sea, n, 1e-3f, L->f, L->order, L->seen, L->lk);
        receivers(L->f, L->sea, n, L->rec, L->rd, cell);
        float area = cell * cell, rmean = p->rain;
        for (size_t i = 0; i < N; i++) L->A[i] = area * L->rain[i] / rmean;
        for (int k = cnt - 1; k >= 0; k--) { int i = L->order[k], r = L->rec[i]; if (r >= 0) L->A[r] += L->A[i]; }
        // Implicit stream-power incision, downstream first.
        memcpy(L->h0, L->h, N * sizeof(float));
        for (int k = 0; k < cnt; k++) {
            int i = L->order[k], r = L->rec[i];
            if (r < 0) continue;
            float depth = L->lk[i] - L->h[i];
            if (depth > 0.05f) continue;  // lake floor: filled by sediment below
            if (L->h[i] <= L->h[r]) continue;
            float hard = L->lith[i] * strata(L->h[i], (i % n) * cell, (i / n) * cell, L->s);
            // Channel threshold: unchannelled hillslopes creep instead of
            // incising, which sets a valley spacing instead of grid-scale rills.
            float chan = L->A[i] / (L->A[i] + acrit);
            float fK = K / hard * chan * dt * sqrtf(L->A[i]) / L->rd[i];
            L->h[i] = (L->h[i] + fK * L->h[r]) / (1 + fK);
        }
        if (getenv("TERRA_DEBUG") && st == steps - 1) {
            float mx = -1e9f; int mi = 0; for (size_t q = 0; q < N; q++) if (L->h[q] > mx) { mx = L->h[q]; mi = (int)q; }
            int r = L->rec[mi]; int pos = -1; for (int k = 0; k < cnt; k++) if (L->order[k] == mi) { pos = k; break; }
            fprintf(stderr, "   dbg i=%d rec=%d h=%.1f h0=%.1f hr=%.1f f=%.1f fr=%.1f pos=%d/%d sea=%d\n", mi, r, L->h[mi], L->h0[mi], r >= 0 ? L->h[r] : 0, L->f[mi], r >= 0 ? L->f[r] : 0, pos, cnt, L->sea[mi]);
        }
        // Sediment routing with deposition rate G*Qs/A (Davy & Lague xi-q):
        // builds flat alluvial valley floors, fans and lake infill. Sediment
        // reaching the sea is dropped on the shoreface.
        {
            float G = 0.35f;
            memset(L->qs, 0, N * sizeof(float));
            for (int k = cnt - 1; k >= 0; k--) {
                int i = L->order[k], r = L->rec[i];
                float qin = L->qs[i] + fmaxf(0, L->h0[i] - L->h[i]) * area / dt;
                float dep = fminf(G * qin / L->A[i] * dt, qin * dt / area);
                float lake = L->lk[i] - L->h0[i];
                if (lake > 0.05f) dep = fminf(qin * dt / area, lake);
                // Explicit step limit: refill what was cut plus modest aggradation.
                else dep = fminf(dep, fmaxf(0, L->h0[i] - L->h[i]) + 0.25f);
                dep = fmaxf(dep, 0);
                L->h[i] += dep; qin -= dep * area / dt;
                if (r >= 0) L->qs[r] += qin;
                else if (L->h[i] > -40.0f) {
                    // Shoreface: spread into the adjacent sea cell below.
                    int x = i % n, z = i / n; float best = 1e9f; int bj = -1;
                    for (int q2 = 0; q2 < 8; q2++) {
                        int xx = x + DX8[q2], zz = z + DZ8[q2]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
                        int j = zz * n + xx; if (L->sea[j] && L->h[j] < best) { best = L->h[j]; bj = j; }
                    }
                    if (bj >= 0) L->h[bj] += fminf(qin * dt / area * 0.5f, fmaxf(0, -2.0f - L->h[bj]));
                }
            }
        }
        // Implicit hillslope diffusion, red-black Gauss-Seidel (damps the
        // checkerboard mode that plain Jacobi leaves at large r); stronger
        // under water so shelves stay smooth.
        float r0 = D * dt / (cell * cell);
        (void)final_level;
        memcpy(L->tmp, L->h, N * sizeof(float));
        for (int it = 0; it < 8; it++) for (int colour = 0; colour < 2; colour++) {
            #pragma omp parallel for schedule(static)
            for (int z = 1; z < n - 1; z++) for (int x = 1 + ((z + colour) & 1); x < n - 1; x += 2) {
                size_t i = (size_t)z * n + x; float r = L->tmp[i] < 0 ? r0 * 1.5f : r0;
                float nb = L->h[i - 1] + L->h[i + 1] + L->h[i - n] + L->h[i + n];
                L->h[i] = (L->tmp[i] + r * nb) / (1 + 4 * r);
            }
        }
        // Threshold hillslopes: mass-conserving talus failure toward the
        // critical angle of the local rock (soft ~30 deg, hard beds ~50).
        if (!getenv("TERRA_NO_SLIDE")) {
            #pragma omp parallel for schedule(static)
            for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
                size_t i = (size_t)z * n + x; L->rd[i] = L->lith[i] * strata(L->h[i], x * cell, z * cell, L->s);
            }
            thermal_ex(L->h, NULL, n, cell, L->rd, 6, 0.42f, 0.2f, 0.7f, 2.4f, 0.3f);
        }
    }
}

// The Jacobi loop above reads neighbours from L->f; seed it with h first.
static void evolve_level(Level* L, float years, int steps, const TerraParams* p, int rain_every, int final_level, TerraProgress cb, void* user, const char* name, float f0, float f1) {
    int chunk = imax(1, steps / 10);
    for (int s = 0; s < steps; s += chunk) {
        int k = imin(chunk, steps - s);
        evolve(L, years * k / steps, k, p, rain_every, final_level);
        if (getenv("TERRA_DEBUG")) {
            float mx = -1e9f; int mi = 0; for (int i = 0; i < L->n * L->n; i++) if (L->h[i] > mx) { mx = L->h[i]; mi = i; }
            fprintf(stderr, "  %s step %d max %.1f at %d,%d A=%.3g f=%.1f\n", name, s + k, mx, mi % L->n, mi / L->n, L->A[mi], L->f[mi]);
        }
        if (cb) cb(name, f0 + (f1 - f0) * (s + k) / (float)steps, user);
    }
}

// ---------------------------------------------------------------- fine detail
static void add_detail(float* h, int n, float cell, uint32_t s, const float* hard) {
    float* g = fgrid(n); memcpy(g, h, sizeof(float) * (size_t)n * n);
    #pragma omp parallel for schedule(static)
    for (int z = 1; z < n - 1; z++) for (int x = 1; x < n - 1; x++) {
        size_t i = (size_t)z * n + x;
        float gx = (g[i + 1] - g[i - 1]) / (2 * cell), gz = (g[i + n] - g[i - n]) / (2 * cell);
        float slope = sqrtf(gx * gx + gz * gz);
        float wx = x * cell, wz = z * cell;
        float r = ridged(wx / 260.0f, wz / 260.0f, s + 60, 4);
        float f = fbm(wx / 90.0f, wz / 90.0f, s + 61, 4, 2.1f, 0.5f);
        float amp = (h[i] > 0 ? 1.0f : 0.3f) * (0.5f + 9.0f * clampf(slope, 0, 1.2f));
        float rock = hard ? hard[i] : 1;
        h[i] += amp * (0.55f * f + 0.6f * (r - 0.35f) * rock * sstep(0.15f, 0.6f, slope));
        // Irregular coastlines: coves, points and skerries near sea level.
        float near = 1 - sstep(4.0f, 25.0f, fabsf(g[i]));
        h[i] += near * 5.0f * fbm(wx / 210.0f + 3, wz / 210.0f - 8, s + 62, 5, 2.1f, 0.55f);
    }
    free(g);
}

typedef struct { float erode, deposit, capacity, inertia, evaporate, gravity, min_slope; int radius, life; } DropParams;
static void hydraulic(float* h, float* soil, int n, float cell, long drops, uint32_t seed, const float* rain, float mean_rain) {
    DropParams P = {0.35f, 0.25f, 5.0f, 0.06f, 0.015f, 4.0f, 0.008f, 2, 72};
    // Erosion brush weights.
    int R = P.radius; int bw = 2 * R + 1; float* brush = (float*)malloc(sizeof(float) * bw * bw); float bs = 0;
    for (int j = -R; j <= R; j++) for (int i = -R; i <= R; i++) { float d = sqrtf((float)(i * i + j * j)); float w = fmaxf(0, R + 0.5f - d); brush[(j + R) * bw + i + R] = w; bs += w; }
    for (int k = 0; k < bw * bw; k++) brush[k] /= bs;
    Rng r = {seed * 0x2545F4914F6CDD1Dull + 3};
    float hs = cell; // heights in metres, positions in cells: scale gradient
    for (long d = 0; d < drops; d++) {
        float px = 1 + rng_f(&r) * (n - 3), pz = 1 + rng_f(&r) * (n - 3);
        // Rain-weighted acceptance so wet regions erode more.
        if (rain) { float rr = sample_bilinear(rain, n, px, pz) / (mean_rain * 2.0f); if (rng_f(&r) > rr) continue; }
        float dx = 0, dz = 0, speed = 1, water = 1, sed = 0;
        for (int life = 0; life < P.life; life++) {
            int ix = (int)px, iz = (int)pz; float fx = px - ix, fz = pz - iz; size_t i = (size_t)iz * n + ix;
            float h00 = h[i], h10 = h[i + 1], h01 = h[i + n], h11 = h[i + n + 1];
            if (h00 < -2.0f) break; // reached the sea
            float gx = (h10 - h00) * (1 - fz) + (h11 - h01) * fz, gz = (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
            float hcur = h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) + h01 * (1 - fx) * fz + h11 * fx * fz;
            dx = dx * P.inertia - gx * (1 - P.inertia); dz = dz * P.inertia - gz * (1 - P.inertia);
            float len = sqrtf(dx * dx + dz * dz); if (len < 1e-6f) break;
            dx /= len; dz /= len; px += dx; pz += dz;
            if (px < 1 || pz < 1 || px >= n - 2 || pz >= n - 2) break;
            float hn = sample_bilinear(h, n, px, pz), dh = hn - hcur;
            float cap = fmaxf(-dh / hs, P.min_slope) * speed * water * P.capacity * hs * 0.25f;
            if (sed > cap || dh > 0) {
                float amt = dh > 0 ? fminf(dh, sed) : (sed - cap) * P.deposit;
                sed -= amt;
                h[i] += amt * (1 - fx) * (1 - fz); h[i + 1] += amt * fx * (1 - fz); h[i + n] += amt * (1 - fx) * fz; h[i + n + 1] += amt * fx * fz;
                if (soil) { soil[i] += amt * (1 - fx) * (1 - fz); soil[i + 1] += amt * fx * (1 - fz); soil[i + n] += amt * (1 - fx) * fz; soil[i + n + 1] += amt * fx * fz; }
            } else {
                float amt = fminf((cap - sed) * P.erode, -dh);
                if (ix - R >= 0 && iz - R >= 0 && ix + R < n && iz + R < n) {
                    for (int j = -R; j <= R; j++) for (int q = -R; q <= R; q++) {
                        size_t k = (size_t)(iz + j) * n + ix + q; float w = amt * brush[(j + R) * bw + q + R];
                        float take = fminf(w, fmaxf(0, h[k] - hn + 0.5f)); h[k] -= take; sed += take;
                        if (soil) soil[k] = fmaxf(0, soil[k] - take);
                    }
                }
            }
            speed = sqrtf(fmaxf(0.01f, speed * speed + dh / hs * -P.gravity));
            water *= 1 - P.evaporate;
        }
    }
    free(brush);
}

// Thermal relaxation: material above the talus slope slides downhill. Hard
// rock holds steeper faces, which leaves cliffs where strata are resistant.
static void thermal_ex(float* h, float* soil, int n, float cell, const float* hard, int iters, float ta, float tb, float tlo, float thi, float rate);
static void thermal(float* h, float* soil, int n, float cell, const float* hard, int iters) {
    thermal_ex(h, soil, n, cell, hard, iters, 0.75f, 0.25f, 0.6f, 2.2f, 0.25f);
}
// Talus slope = ta * clamp(hard, tlo, thi) + tb; loose soil fails at 0.72.
static void thermal_ex(float* h, float* soil, int n, float cell, const float* hard, int iters, float ta, float tb, float tlo, float thi, float rate) {
    float* d = fgrid(n);
    for (int it = 0; it < iters; it++) {
        memset(d, 0, sizeof(float) * (size_t)n * n);
        for (int z = 1; z < n - 1; z++) for (int x = 1; x < n - 1; x++) {
            size_t i = (size_t)z * n + x; float hi = h[i];
            float loose = soil ? clampf(soil[i] / 1.5f, 0, 1) : 0;
            float tal = lerpf(ta * (hard ? clampf(hard[i], tlo, thi) : 1.0f) + tb, 0.72f, loose) * cell;
            float tot = 0, mx = 0, diffs[8];
            for (int k = 0; k < 8; k++) {
                float dd = hi - h[(z + DZ8[k]) * n + x + DX8[k]] - tal * DL8[k];
                diffs[k] = dd > 0 ? dd : 0; tot += diffs[k]; if (diffs[k] > mx) mx = diffs[k];
            }
            if (tot <= 0) continue;
            float move = rate * mx;
            for (int k = 0; k < 8; k++) if (diffs[k] > 0) d[(z + DZ8[k]) * n + x + DX8[k]] += move * diffs[k] / tot;
            d[i] -= move;
        }
        for (size_t i = 0; i < (size_t)n * n; i++) { h[i] += d[i]; if (soil && d[i] > 0) soil[i] += d[i]; }
    }
    free(d);
}

// ---------------------------------------------------------------- final hydrology
// Beaches and coastal flats. Stream-power uplift leaves steep shores, so the
// sand berm alone is only 10-25 m wide. On stretches picked by low-frequency
// noise, land near the sea is lowered onto a gentle beach profile (about 3 %
// up to a berm, 35-110 m wide), and the seabed just offshore is raised into
// a shallow sandy shelf. Where the land behind is high the profile fades out
// instead of cutting a scarp, so cliffs stay cliffs.
static void coastal_flats(float* h, int n, float cell, uint32_t s) {
    size_t N = (size_t)n * n; uint8_t* sea = (uint8_t*)calloc(N, 1);
    ocean_mask(h, n, sea);
    // Chamfer distance (metres) to the sea for land, and to land for sea.
    float* dl = fgrid(n); float* ds = fgrid(n);
    for (size_t i = 0; i < N; i++) { dl[i] = sea[i] ? 0 : 1e9f; ds[i] = sea[i] ? 1e9f : 0; }
    const float a = cell, b = cell * 1.41421356f;
    for (int pass = 0; pass < 2; pass++) {
        float* d = pass ? ds : dl;
        for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
            size_t i = (size_t)z * n + x; float v = d[i];
            if (x > 0) v = fminf(v, d[i - 1] + a);
            if (z > 0) { v = fminf(v, d[i - n] + a); if (x > 0) v = fminf(v, d[i - n - 1] + b); if (x < n - 1) v = fminf(v, d[i - n + 1] + b); }
            d[i] = v;
        }
        for (int z = n - 1; z >= 0; z--) for (int x = n - 1; x >= 0; x--) {
            size_t i = (size_t)z * n + x; float v = d[i];
            if (x < n - 1) v = fminf(v, d[i + 1] + a);
            if (z < n - 1) { v = fminf(v, d[i + n] + a); if (x < n - 1) v = fminf(v, d[i + n + 1] + b); if (x > 0) v = fminf(v, d[i + n - 1] + b); }
            d[i] = v;
        }
    }
    #pragma omp parallel for schedule(static)
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        size_t i = (size_t)z * n + x; float wx = x * cell, wz = z * cell;
        float pick = fbm(wx / 1400.0f, wz / 1400.0f, s + 71, 3, 2, 0.5f);
        float mask = sstep(-0.1f, 0.2f, pick);            // about half the coastline
        if (mask <= 0) continue;
        float wn = fbm(wx / 300.0f, wz / 300.0f, s + 72, 3, 2, 0.5f);
        float width = 35.0f + 75.0f * clampf(0.5f + wn, 0, 1);
        if (!sea[i]) {
            float d = dl[i]; if (d > width * 1.6f) continue;
            float prof = 0.35f + d * 0.03f + 0.4f * fbm(wx / 25.0f, wz / 25.0f, s + 73, 2, 2, 0.5f) * sstep(0, 20, d);
            // Fade where the land behind is high (no cut scarps) and toward the inland edge.
            float wgt = mask * (1 - sstep(width * 0.7f, width * 1.6f, d)) * (1 - sstep(14.0f, 32.0f, h[i]));
            if (h[i] > prof) h[i] = lerpf(h[i], prof, wgt);
        } else {
            float d = ds[i]; if (d > 120.0f) continue;
            float shelf = -0.3f - d * 0.035f;             // ~4 m deep at 110 m out
            float wgt = mask * (1 - sstep(60.0f, 120.0f, d));
            if (h[i] < shelf) h[i] = lerpf(h[i], shelf, wgt);
        }
    }
    free(dl); free(ds); free(sea);
}

static void hydrology(TerraMap* m, const TerraParams* p, uint8_t* sea) {
    int n = m->n; size_t N = (size_t)n * n; float cell = m->cell;
    float* f = fgrid(n); float* fe = fgrid(n); float* rd = fgrid(n);
    int* order = (int*)malloc(sizeof(int) * N); int* rec = (int*)malloc(sizeof(int) * N);
    uint8_t* seen = (uint8_t*)malloc(N);
    ocean_mask(m->height, n, sea);
    // Lakes: depressions of the true surface at their spill level.
    priority_flood(m->height, sea, n, 0, f, order, seen);
    // Label depression components; tiny ones are filled in as flats.
    int* lab = (int*)malloc(sizeof(int) * N); for (size_t i = 0; i < N; i++) lab[i] = -1;
    int* q = (int*)malloc(sizeof(int) * N);
    float min_area = 2500.0f; // m^2
    for (size_t s0 = 0; s0 < N; s0++) {
        if (lab[s0] >= 0 || sea[s0] || f[s0] - m->height[s0] < 0.15f) continue;
        int qh = 0, qt = 0; q[qt++] = (int)s0; lab[s0] = (int)s0;
        while (qh < qt) {
            int c = q[qh++], x = c % n, z = c / n;
            for (int k = 0; k < 8; k++) {
                int xx = x + DX8[k], zz = z + DZ8[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
                int j = zz * n + xx; if (lab[j] >= 0 || sea[j] || f[j] - m->height[j] < 0.15f) continue;
                lab[j] = (int)s0; q[qt++] = j;
            }
        }
        int keep = qt * cell * cell >= min_area;
        for (int k = 0; k < qt; k++) {
            int c = q[k];
            if (keep) { m->water[c] = f[c]; m->water_kind[c] = TERRA_WATER_LAKE; }
            else { m->soil[c] += f[c] - m->height[c]; m->height[c] = f[c]; lab[c] = -2; }
        }
    }
    free(q);
    // Drainage on an epsilon-filled surface, rain weighted.
    int cnt = priority_flood(m->height, sea, n, 1e-4f, fe, order, seen);
    receivers(fe, sea, n, rec, rd, cell);
    for (size_t i = 0; i < N; i++) m->flow[i] = m->rain[i] * cell * cell;
    for (int k = cnt - 1; k >= 0; k--) { int i = order[k], r = rec[i]; if (r >= 0) m->flow[r] += m->flow[i]; }
    for (size_t i = 0; i < N; i++) {
        int r = rec[i]; m->flow_dir[2 * i] = m->flow_dir[2 * i + 1] = 0;
        if (r >= 0 && m->water_kind[i] != TERRA_WATER_LAKE) {
            float dx = (float)(r % n) - (float)(i % n), dz = (float)(r / n) - (float)(i / n), l = sqrtf(dx * dx + dz * dz);
            m->flow_dir[2 * i] = (int8_t)(127 * dx / l); m->flow_dir[2 * i + 1] = (int8_t)(127 * dz / l);
        }
    }
    // Rivers: hydraulic geometry from discharge, exaggerated for play.
    float qmin = p->rain * 0.15e6f; // ~0.35 km^2 of mean-rain catchment
    float* bed = fgrid(n); memcpy(bed, m->height, N * sizeof(float));
    float* surf = fgrid(n); for (size_t i = 0; i < N; i++) surf[i] = TERRA_DRY;
    // Each bank cell takes the surface of its nearest centreline cell, so the
    // river surface is continuous instead of a max over overlapping stamps.
    float* sdist = fgrid(n); for (size_t i = 0; i < N; i++) sdist[i] = 1e30f;
    for (int k = cnt - 1; k >= 0; k--) {          // upstream first
        int i = order[k];
        if (m->flow[i] < qmin || m->water_kind[i] == TERRA_WATER_LAKE) continue;
        float qs = m->flow[i] / 3.15e7f; // m^3/s
        float w = fmaxf(3.0f, 7.0f * sqrtf(qs)), d = 0.6f + 0.9f * powf(qs, 0.4f);
        float S = fe[i] + 0.05f; int x0 = i % n, z0 = i / n;
        int R = (int)ceilf((w * 0.5f + 2 * cell) / cell);
        for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
            int xx = x0 + dx, zz = z0 + dz; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            size_t j = (size_t)zz * n + xx; if (sea[j]) continue;
            float r = sqrtf((float)(dx * dx + dz * dz)) * cell, half = w * 0.5f;
            float target;
            if (r <= half) target = S - d * (1 - (r / half) * (r / half));
            else target = S + (r - half) * 0.35f;                 // bank ramps up
            if (target < bed[j]) bed[j] = target;
            if (r <= half + cell && r < sdist[j]) { sdist[j] = r; surf[j] = S; }
        }
        m->water_kind[i] = TERRA_WATER_RIVER;
    }
    for (size_t i = 0; i < N; i++) {
        float lowered = m->height[i] - bed[i]; if (lowered > 0) m->height[i] = bed[i];
        if (surf[i] > TERRA_DRY && m->water_kind[i] != TERRA_WATER_LAKE && surf[i] > m->height[i]) {
            m->water[i] = surf[i]; if (m->water_kind[i] == TERRA_WATER_NONE) m->water_kind[i] = TERRA_WATER_RIVER;
        }
        if (sea[i]) { m->water[i] = 0; m->water_kind[i] = TERRA_WATER_SEA; }
    }
    // Extend water surfaces a few samples under the banks so the rendered
    // surface meets the terrain without gaps; terrain occludes the rest.
    float* w2 = fgrid(n);
    for (int pass = 0; pass < 3; pass++) {
        memcpy(w2, m->water, N * sizeof(float));
        for (int z = 1; z < n - 1; z++) for (int x = 1; x < n - 1; x++) {
            size_t i = (size_t)z * n + x; if (m->water[i] > TERRA_DRY) continue;
            float best = TERRA_DRY;
            for (int k = 0; k < 8; k++) { float v = m->water[(z + DZ8[k]) * n + x + DX8[k]]; if (v > best) best = v; }
            if (best > TERRA_DRY && m->height[i] > best - 0.5f) w2[i] = best;
        }
        memcpy(m->water, w2, N * sizeof(float));
    }
    free(w2); free(bed); free(surf); free(sdist); free(lab);
    free(f); free(fe); free(rd); free(order); free(rec); free(seen);
}

// ---------------------------------------------------------------- surface & vegetation
static void surface(TerraMap* m, const TerraParams* p, const uint8_t* sea, const float* hard) {
    int n = m->n; size_t N = (size_t)n * n; float cell = m->cell; uint32_t s = mix32(p->seed + 999);
    // Distance-like wetness: blurred water masks.
    float* wet = fgrid(n); float* coast = fgrid(n); float* curv = fgrid(n); float* hb = fgrid(n);
    for (size_t i = 0; i < N; i++) {
        wet[i] = (m->water_kind[i] == TERRA_WATER_RIVER || m->water_kind[i] == TERRA_WATER_LAKE) ? 1.0f : 0.0f;
        coast[i] = sea[i] ? 1.0f : 0.0f; hb[i] = m->height[i];
    }
    box_blur(wet, n, imax(1, (int)(24.0f / cell)), 3);
    box_blur(coast, n, imax(1, (int)(60.0f / cell)), 3);
    box_blur(hb, n, imax(1, (int)(30.0f / cell)), 2);
    for (size_t i = 0; i < N; i++) curv[i] = hb[i] - m->height[i]; // >0 concave (valley)
    float rmax = 0; for (size_t i = 0; i < N; i++) rmax = fmaxf(rmax, m->rain[i]);
    #pragma omp parallel for schedule(dynamic, 16)
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        size_t i = (size_t)z * n + x; float h = m->height[i];
        int xl = imax(x - 1, 0), xr = imin(x + 1, n - 1), zl = imax(z - 1, 0), zr = imin(z + 1, n - 1);
        float gx = (m->height[(size_t)z * n + xr] - m->height[(size_t)z * n + xl]) / ((xr - xl) * cell);
        float gz = (m->height[(size_t)zr * n + x] - m->height[(size_t)zl * n + x]) / ((zr - zl) * cell);
        float slope = sqrtf(gx * gx + gz * gz);
        float wx = x * cell, wz = z * cell;
        float n1 = fbm(wx / 180.0f, wz / 180.0f, s + 1, 4, 2, 0.5f), n2 = fbm(wx / 40.0f, wz / 40.0f, s + 2, 3, 2, 0.5f);
        float patch = fbm(wx / 650.0f, wz / 650.0f, s + 3, 4, 2, 0.55f);
        float temp = p->temperature - 6.5f * fmaxf(h, 0) / 1000.0f + 2.0f * n1;
        m->temp[i] = temp;
        float moist = clampf(m->rain[i] / (p->rain * 1.6f), 0, 1);
        float riparian = clampf(wet[i] * 3.0f, 0, 1);
        float flowlog = clampf(log10f(m->flow[i] / (p->rain * cell * cell) + 1) / 4.0f, 0, 1);
        moist = clampf(moist * 0.7f + 0.5f * riparian + 0.25f * flowlog + 0.15f * clampf(curv[i] / 4.0f, -1, 1) + 0.1f * n1, 0, 1);
        float soil = m->soil[i], hd = hard ? hard[i] : 1;
        // Beaches: a sand berm up to 3.5-10 m along any sea shore that is not
        // a cliff (coastal slopes here are mostly 0.1-0.5, so the berm sets the
        // width: 10-25 m); steeper shores get a wave-cut apron of rock and
        // scree at the waterline instead.
        float berm = 3.5f + 3.5f * n2 + 3.0f * sstep(0.3f, 0.7f, patch);
        float shore = sstep(0.04f, 0.2f, coast[i]);
        float beach = (1 - sstep(berm * 0.6f, berm, h)) * shore * (1 - sstep(0.45f + 0.12f * n2, 0.75f + 0.12f * n2, slope));
        float wavecut = (1 - sstep(2.5f, 6.0f + 3.0f * n2, h)) * shore * sstep(0.45f, 0.85f, slope);
        float w[TERRA_MAT_COUNT] = {0};
        // Rock follows the landform: steep faces and convex ridges, with only a
        // weak strata term (a strong one paints continuous horizontal bands).
        float cv = clampf(curv[i] / 3.0f, -1, 1);   // >0 hollow, <0 ridge
        float n3 = fbm(wx / 14.0f, wz / 14.0f, s + 6, 2, 2, 0.5f);
        float rockiness = sstep(1.15f, 1.75f, slope + 0.08f * (hd - 1) - 0.12f * cv + 0.18f * n3 + 0.12f * n2 - 0.15f * clampf(soil, 0, 2));
        w[TERRA_MAT_ROCK] = rockiness * 1.2f;
        // Scree collects in hollows below faces, not as bands across them.
        w[TERRA_MAT_SCREE] = sstep(1.0f, 1.45f, slope + 0.15f * n3) * (1 - rockiness) * (0.6f + 0.6f * clampf(soil, 0, 1)) * (0.35f + 0.65f * sstep(-0.3f, 0.6f, cv)) + wavecut * 1.3f;
        w[TERRA_MAT_SAND] = beach * 2.0f + (sea[i] ? 1.5f : 0) + sstep(0.6f, 0.9f, riparian) * sstep(0.0f, 0.3f, soil - 0.3f) * 0.6f;
        float open = (1 - rockiness) * (1 - clampf(beach, 0, 1));
        float flat = 1 - sstep(0.1f, 0.35f, slope);
        float canopy = sstep(0.25f, 0.55f, moist + 0.25f * patch) * (1 - sstep(0.7f, 1.1f, slope)) * sstep(-2.0f, 6.0f, temp);
        // Mud only in a narrow strip at the waterline; banks beyond are vegetated.
        float waterline = sstep(0.0f, -1.2f, h - (m->water[i] > TERRA_DRY ? m->water[i] : h - 10.0f));
        w[TERRA_MAT_MUD] = open * flat * (sstep(0.85f, 1.0f, riparian) * 0.5f + waterline * 1.4f + sstep(0.9f, 1.0f, moist) * 0.4f);
        w[TERRA_MAT_FOREST] = open * canopy * 1.1f;
        // Fern prairie is the default open ground (no grasses yet); bare soil only
        // where it is very dry or steep.
        w[TERRA_MAT_FERN] = open * (0.5f + 0.5f * sstep(0.05f, 0.4f, moist)) * (1 - canopy * 0.35f) * (0.9f + 0.2f * n2) * (1 - sstep(1.05f, 1.5f, slope));
        // Bare soil where the landform strips it: dry ground, sharp convex
        // ridge crests and shoulders, and erosion gullies (channelled flow
        // without standing water). Edges are broken by fine noise, not blobs.
        float gully = sstep(0.3f, 0.55f, flowlog) * (1 - riparian) * sstep(0.12f, 0.4f, slope);
        float crest = sstep(0.6f, 1.4f, -cv + 0.4f * n3) * sstep(0.35f, 0.8f, slope);
        w[TERRA_MAT_DIRT] = open * (0.03f + sstep(0.12f, 0.0f, moist) * 0.5f + 0.35f * crest + 0.35f * gully) * (0.75f + 0.5f * n3);
        // Moss on damp, shaded hollows and around rock, not on every steep slope.
        w[TERRA_MAT_MOSS] = sstep(0.4f, 0.85f, moist) * sstep(0.2f, 0.6f, slope) * (0.4f + 0.6f * rockiness) * (0.6f + 0.8f * n3) * (0.5f + 0.5f * sstep(-0.2f, 0.6f, cv))
                          + open * sstep(0.6f, 1.1f, slope) * (0.3f + 0.6f * moist) * sstep(0.0f, 0.8f, cv) * (0.8f + 0.4f * n3);
        float tot = 0; for (int k = 0; k < TERRA_MAT_COUNT; k++) { w[k] = fmaxf(w[k], 0); w[k] *= w[k]; tot += w[k]; }
        if (tot < 1e-6f) { w[TERRA_MAT_DIRT] = 1; tot = 1; }
        int acc = 0, big = 0;
        for (int k = 0; k < TERRA_MAT_COUNT; k++) { int v = (int)(255 * w[k] / tot); m->splat[i * TERRA_MAT_COUNT + k] = (uint8_t)v; acc += v; if (w[k] > w[big]) big = k; }
        m->splat[i * TERRA_MAT_COUNT + big] += (uint8_t)(255 - acc);
        // Vegetation.
        float v[TERRA_VEG_COUNT] = {0};
        int wetcell = m->water_kind[i] != TERRA_WATER_NONE && m->water[i] > h + 0.05f;
        float grow = wetcell ? 0 : (1 - clampf(beach, 0, 1)) * (1 - sstep(0.75f, 1.0f, rockiness)) * (1 - sstep(1.0f, 1.35f, slope));
        float clump = sstep(-0.15f, 0.35f, patch), clump2 = sstep(-0.2f, 0.3f, fbm(wx / 300.0f + 7, wz / 300.0f, s + 5, 3, 2, 0.5f));
        // Greenhouse Cretaceous: forest is the default cover; conifer stands
        // and angiosperm groves form a mosaic, cycads fill drier openings.
        // Forest covers everything that is not rock, beach or water; moisture
        // and patch noise only shift the mix between conifers and angiosperms.
        float forest = grow * (0.6f + 0.4f * sstep(0.05f, 0.45f, moist)) * (1 - sstep(0.95f, 1.25f, slope));
        float conshare = clampf(0.55f + 0.5f * (clump - clump2) - 0.35f * riparian + 0.2f * sstep(0.3f, 0.7f, slope), 0.1f, 0.9f);
        v[TERRA_VEG_CONIFER] = forest * conshare * sstep(0.0f, 8.0f, temp);
        v[TERRA_VEG_BROADLEAF] = forest * (1 - conshare) * sstep(12.0f, 18.0f, temp);
        v[TERRA_VEG_CYCAD] = grow * sstep(0.55f, 0.2f, moist) * sstep(16.0f, 22.0f, temp) * (0.3f + 0.4f * n2);
        v[TERRA_VEG_FERN] = grow * (0.55f + 0.45f * sstep(0.1f, 0.5f, moist)) * (0.65f + 0.35f * n2);
        v[TERRA_VEG_TREEFERN] = grow * sstep(0.7f, 0.95f, moist) * sstep(0.0f, 3.0f, curv[i]) * sstep(15.0f, 20.0f, temp);
        v[TERRA_VEG_HORSETAIL] = (1 - sstep(0.1f, 0.3f, slope)) * sstep(0.4f, 0.9f, riparian) * (wetcell ? 0.3f : 1.0f);
        v[TERRA_VEG_PALM] = grow * sstep(0.2f, 0.6f, coast[i] * 2) * (1 - sstep(8.0f, 30.0f, h)) * sstep(20.0f, 24.0f, temp) * (0.3f + 0.7f * clump2);
        v[TERRA_VEG_SHRUB] = grow * (0.25f + 0.4f * sstep(0.6f, 0.3f, moist)) * (0.5f + 0.5f * n2);
        if (sea[i] || h < 0.3f) for (int k = 0; k < TERRA_VEG_COUNT; k++) v[k] = 0;
        for (int k = 0; k < TERRA_VEG_COUNT; k++) m->veg[i * TERRA_VEG_COUNT + k] = (uint8_t)(255 * clampf(v[k], 0, 1));
        m->hardness[i] = (uint8_t)(clampf(hd / 2.2f, 0, 1) * 255);
    }
    (void)rmax;
    free(wet); free(coast); free(curv); free(hb);
}

// ---------------------------------------------------------------- public API
TerraParams terra_default_params(uint32_t seed) {
    TerraParams p;
    memset(&p, 0, sizeof(p));
    p.seed = seed; p.n = 2048; p.cell = 4.0f; p.plates = 9; p.land_fraction = 0.45f;
    p.uplift = 1.0e-3f; p.erodibility = 8.0e-5f; p.diffusion = 0.006f; p.evolve_years = 6.0e6f;
    p.droplets_per_cell_x100 = 35; p.wind_angle = 0.6f; p.rain = 1.4f; p.temperature = 24.0f;
    p.threads = 0; p.verbose = 0;
    return p;
}
int terra_params_apply(TerraParams* p, const char* spec) {
    if (!spec) return 0;
    int applied = 0; const char* c = spec;
    while (*c) {
        while (*c == ' ' || *c == ',') c++;
        if (!*c) break;
        char key[32]; int k = 0;
        while (*c && *c != '=' && *c != ' ' && *c != ',' && k < 31) key[k++] = *c++;
        key[k] = 0; if (*c != '=') return -1;
        c++; char* end; double v = strtod(c, &end); if (end == c) return -1; c = end;
        if (!strcmp(key, "n")) p->n = (int)v; else if (!strcmp(key, "cell")) p->cell = (float)v;
        else if (!strcmp(key, "plates")) p->plates = (int)v; else if (!strcmp(key, "land")) p->land_fraction = (float)v;
        else if (!strcmp(key, "uplift")) p->uplift = (float)v; else if (!strcmp(key, "k")) p->erodibility = (float)v;
        else if (!strcmp(key, "diffusion")) p->diffusion = (float)v; else if (!strcmp(key, "years")) p->evolve_years = (float)v;
        else if (!strcmp(key, "drops")) p->droplets_per_cell_x100 = (int)v; else if (!strcmp(key, "wind")) p->wind_angle = (float)v;
        else if (!strcmp(key, "rain")) p->rain = (float)v; else if (!strcmp(key, "temp")) p->temperature = (float)v;
        else return -1;
        applied++;
    }
    return applied;
}
uint64_t terra_params_hash(const TerraParams* p) {
    TerraParams q = *p; q.threads = 0; q.verbose = 0;
    const unsigned char* b = (const unsigned char*)&q; uint64_t h = 1469598103934665603ull ^ TERRA_VERSION;
    for (size_t i = 0; i < sizeof(q); i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
static int map_alloc(TerraMap* m, int n, float cell, uint32_t seed) {
    size_t N = (size_t)n * n; memset(m, 0, sizeof(*m)); m->n = n; m->cell = cell; m->seed = seed;
    m->height = fgrid(n); m->water = fgrid(n); m->flow = fgrid(n); m->rain = fgrid(n); m->soil = fgrid(n); m->temp = fgrid(n);
    m->water_kind = calloc(N, 1); m->flow_dir = calloc(N * 2, 1); m->splat = calloc(N * TERRA_MAT_COUNT, 1);
    m->veg = calloc(N * TERRA_VEG_COUNT, 1); m->hardness = calloc(N, 1);
    return m->height && m->veg ? 0 : -1;
}
void terra_free(TerraMap* m) {
    free(m->height); free(m->water); free(m->flow); free(m->rain); free(m->soil); free(m->temp);
    free(m->water_kind); free(m->flow_dir); free(m->splat); free(m->veg); free(m->hardness);
    memset(m, 0, sizeof(*m));
}

int terra_generate(const TerraParams* p, TerraMap* out, TerraProgress cb, void* user) {
#ifdef _OPENMP
    if (p->threads > 0) omp_set_num_threads(p->threads);
#endif
    int n = p->n; if (n < 256 || (n & (n - 1))) return -1;
    float size = (n - 1) * p->cell;
    Tecto t; tecto_init(&t, p, size);
    uint32_t s = mix32(p->seed ^ 0xA5A5A5u);
    // Coarse to fine: n/8, n/4, n/2 evolve; n gets detail and short evolution.
    int n0 = n / 8; Level L; level_alloc(&L, n0, size / (n0 - 1), s);
    if (cb) cb("tectonics", 0, user);
    level_tectonics(&L, &t, 1);
    float Y = p->evolve_years;
    evolve_level(&L, Y * 0.70f, 160, p, 8, 0, cb, user, "evolve 1/3", 0.02f, 0.30f);
    for (int lv = 1; lv <= 2; lv++) {
        int nn = n0 << lv; Level M; level_alloc(&M, nn, size / (nn - 1), s);
        float* up = resample(L.h, L.n, nn); memcpy(M.h, up, sizeof(float) * (size_t)nn * nn); free(up);
        float* rr = resample(L.rain, L.n, nn); memcpy(M.rain, rr, sizeof(float) * (size_t)nn * nn); free(rr);
        level_tectonics(&M, &t, 0);
        level_free(&L); L = M;
        if (lv == 1) evolve_level(&L, Y * 0.20f, 50, p, 10, 0, cb, user, "evolve 2/3", 0.30f, 0.50f);
        else evolve_level(&L, Y * 0.10f, 24, p, 12, 0, cb, user, "evolve 3/3", 0.50f, 0.66f);
    }
    if (map_alloc(out, n, p->cell, p->seed)) { level_free(&L); return -2; }
    float* h = resample(L.h, L.n, n); memcpy(out->height, h, sizeof(float) * (size_t)n * n); free(h);
    float* rr = resample(L.rain, L.n, n); memcpy(out->rain, rr, sizeof(float) * (size_t)n * n); free(rr);
    level_free(&L);
    // Hardness at full resolution for detail, droplets and talus.
    size_t N = (size_t)n * n; float* hard = fgrid(n);
    {
        Level F; memset(&F, 0, sizeof(F)); F.n = n; F.cell = p->cell; F.U = fgrid(n); F.lith = fgrid(n); F.h = out->height;
        level_tectonics(&F, &t, 0);
        #pragma omp parallel for schedule(static)
        for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
            size_t i = (size_t)z * n + x; hard[i] = F.lith[i] * strata(out->height[i], x * p->cell, z * p->cell, s);
        }
        free(F.U); free(F.lith);
    }
    if (cb) cb("detail", 0.68f, user);
    if (!getenv("TERRA_NO_DETAIL")) add_detail(out->height, n, p->cell, s, hard);
    if (cb) cb("hydraulic erosion", 0.72f, user);
    long drops = (long)((double)N * p->droplets_per_cell_x100 / 100.0);
    hydraulic(out->height, out->soil, n, p->cell, drops, p->seed, out->rain, p->rain);
    if (cb) cb("thermal", 0.86f, user);
    if (!getenv("TERRA_NO_THERMAL")) thermal(out->height, out->soil, n, p->cell, hard, 24);
    if (!getenv("TERRA_NO_BEACH")) coastal_flats(out->height, n, p->cell, s);
    if (cb) cb("hydrology", 0.90f, user);
    uint8_t* sea = (uint8_t*)calloc(N, 1);
    for (size_t i = 0; i < N; i++) out->water[i] = TERRA_DRY;
    hydrology(out, p, sea);
    if (cb) cb("surface", 0.96f, user);
    surface(out, p, sea, hard);
    free(sea); free(hard);
    if (cb) cb("done", 1, user);
    return 0;
}

// ---------------------------------------------------------------- io
typedef struct { char magic[4]; uint32_t version; uint64_t hash; int32_t n; float cell; uint32_t seed; uint32_t pad; } TerraHeader;
#define TERRA_FIELDS(X) \
    X(height, sizeof(float)) X(water, sizeof(float)) X(flow, sizeof(float)) X(rain, sizeof(float)) \
    X(soil, sizeof(float)) X(temp, sizeof(float)) X(water_kind, 1) X(flow_dir, 2) \
    X(splat, TERRA_MAT_COUNT) X(veg, TERRA_VEG_COUNT) X(hardness, 1)
int terra_save(const TerraMap* m, const TerraParams* p, const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return -1;
    TerraHeader h = {{'T', 'E', 'R', 'A'}, TERRA_VERSION, p ? terra_params_hash(p) : 0, m->n, m->cell, m->seed, 0};
    fwrite(&h, sizeof(h), 1, f); size_t N = (size_t)m->n * m->n;
#define W(name, sz) fwrite(m->name, sz, N, f);
    TERRA_FIELDS(W)
#undef W
    return fclose(f);
}
int terra_load(TerraMap* m, const TerraParams* p, const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return -1;
    TerraHeader h; if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, "TERA", 4) || h.version != TERRA_VERSION ||
        (p && h.hash != terra_params_hash(p))) { fclose(f); return -2; }
    map_alloc(m, h.n, h.cell, h.seed); size_t N = (size_t)h.n * h.n; int ok = 1;
#define R(name, sz) ok &= fread(m->name, sz, N, f) == N;
    TERRA_FIELDS(R)
#undef R
    fclose(f); if (!ok) { terra_free(m); return -3; }
    return 0;
}

// ---------------------------------------------------------------- queries
float terra_height(const TerraMap* m, float x, float z) { return sample_bilinear(m->height, m->n, x / m->cell, z / m->cell); }
float terra_water(const TerraMap* m, float x, float z) {
    float gx = clampf(x / m->cell, 0, m->n - 1), gz = clampf(z / m->cell, 0, m->n - 1);
    return m->water[(size_t)(int)(gz + 0.5f) * m->n + (int)(gx + 0.5f)];
}
void terra_normal(const TerraMap* m, float x, float z, float o[3]) {
    float e = m->cell;
    float hx = terra_height(m, x + e, z) - terra_height(m, x - e, z), hz = terra_height(m, x, z + e) - terra_height(m, x, z - e);
    float nx = -hx, ny = 2 * e, nz = -hz, l = sqrtf(nx * nx + ny * ny + nz * nz);
    o[0] = nx / l; o[1] = ny / l; o[2] = nz / l;
}
