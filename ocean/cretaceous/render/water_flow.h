// water_flow.h: a smooth flow field for flow-mapped water shading.
// Single header, C99, libc only; every function is static, so include it
// from the C file that builds the water textures.
//
// Height-field worlds usually store rivers as one D8 receiver direction per
// cell plus a water kind (river, lake, sea). Shading water straight from that
// has three faults:
//   - bank cells drain sideways into the channel, so the two halves of a
//     river appear to flow toward each other;
//   - the direction is quantised to 45 degrees;
//   - the flow stops dead where a river meets a lake or the sea: a hard seam
//     at every river mouth, with ripples running left on one side and right
//     on the other.
// water_flow_build() turns that data into one smooth velocity field:
//   1. Moving water (rivers): each cell takes the discharge-weighted mean of
//      the directions around it, so the thalweg wins and the banks' sideways
//      components cancel. Strength 1.
//   2. Standing water (lakes, sea) within `spread` cells of a river: the
//      velocity diffuses outward from the river cells and fans out from the
//      mouth (or converges on an outlet), and its strength falls smoothly to
//      zero at `spread` cells.
//   3. A light blur, then a short dilation into dry cells, so bilinear
//      filtering along the banks stays smooth.
// Output: two floats per cell, direction times strength (|v| <= 1, 0 for
// still water and dry ground). Shade with a two-phase flow map advected by v,
// and blend between flowing and still-water ripples by |v| (see
// shaders/water.fs): there is then no seam anywhere.
//
// water_flow_speed() adds the matching surface speed (0..1): steeper water
// runs faster, smoothed over a few cells so it never steps at a cell edge.
// A flow map advected by a speed that jumps from one cell to the next
// stretches its ripples into hard bands along the jump, so the speed must be
// as smooth as the direction.
//
// Cost: work is proportional to the river and mouth area plus a few O(n^2)
// passes (about 0.2 s single-threaded for a 2048^2 island).
#ifndef WATER_FLOW_H
#define WATER_FLOW_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { WATER_FLOW_DRY = 0, WATER_FLOW_MOVING = 1, WATER_FLOW_STILL = 2 };

typedef struct WaterFlowParams {
    int smooth_radius;   // cells: window for the river direction (default 4)
    int spread;          // cells: how far a river's flow reaches into still water (default 12)
    int dilate;          // cells: copy of the field into dry ground beside water (default 2)
    int speed_radius;    // cells: box radius of the speed blur, applied twice (default 3)
    float speed_slope;   // speed gained per unit of surface slope (default 6)
    float speed_min;     // speed of flat moving water, and of still water (default 0.25)
} WaterFlowParams;

static WaterFlowParams water_flow_defaults(void) { WaterFlowParams p = {4, 12, 2, 3, 6.0f, 0.25f}; return p; }

// n x n cells, row-major (index z * n + x).
// kind:   WATER_FLOW_* per cell.
// dir:    2 floats per cell, the downstream unit vector (x, z); read for MOVING cells.
// weight: per cell, how much the cell's direction counts (discharge or its
//         square root works well); read for MOVING cells. NULL: all equal.
// out:    2 floats per cell, written for every cell.
// Returns false if out of memory (out is then all zero).
static bool water_flow_build(int n, const uint8_t* kind, const float* dir, const float* weight, const WaterFlowParams* prm, float* out) {
    WaterFlowParams P = prm ? *prm : water_flow_defaults();
    size_t N = (size_t)n * n;
    memset(out, 0, N * 2 * sizeof(float));
    int* q = malloc(N * sizeof(int)); int* src = malloc(N * sizeof(int)); uint16_t* dist = malloc(N * sizeof(uint16_t));
    float* tmp = malloc(N * 2 * sizeof(float));
    if (!q || !src || !dist || !tmp) { free(q); free(src); free(dist); free(tmp); return false; }
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    // 1. Rivers: weighted mean direction over the window, unit length.
    int R = P.smooth_radius < 0 ? 0 : P.smooth_radius;
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        size_t i = (size_t)z * n + x; if (kind[i] != WATER_FLOW_MOVING) continue;
        float sx = 0, sz = 0;
        for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
            int xx = x + dx, zz = z + dz; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            size_t j = (size_t)zz * n + xx; if (kind[j] != WATER_FLOW_MOVING) continue;
            float w = weight ? weight[j] : 1.0f;
            sx += dir[2 * j] * w; sz += dir[2 * j + 1] * w;
        }
        float l = sqrtf(sx * sx + sz * sz);
        if (l > 1e-6f) { out[2 * i] = sx / l; out[2 * i + 1] = sz / l; }
        else { out[2 * i] = dir[2 * i]; out[2 * i + 1] = dir[2 * i + 1]; }
    }
    // 2. Still water near rivers: distance and nearest river cell (BFS) ...
    int qh = 0, qt = 0;
    for (size_t i = 0; i < N; i++) {
        dist[i] = UINT16_MAX; src[i] = -1;
        if (kind[i] == WATER_FLOW_MOVING) { dist[i] = 0; src[i] = (int)i; q[qt++] = (int)i; }
    }
    int S = P.spread < 1 ? 0 : P.spread > 4000 ? 4000 : P.spread, reach = S * 3 / 2 + 1;   // BFS steps are Manhattan; the falloff is Euclidean
    while (qh < qt) {
        int c = q[qh++], x = c % n, z = c / n; if (dist[c] >= reach) continue;
        for (int k = 0; k < 4; k++) {
            int xx = x + DX[k], zz = z + DZ[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            int j = zz * n + xx; if (kind[j] != WATER_FLOW_STILL || dist[j] != UINT16_MAX) continue;
            dist[j] = dist[c] + 1; src[j] = src[c]; q[qt++] = j;
        }
    }
    int first_still = 0; while (first_still < qt && kind[q[first_still]] == WATER_FLOW_MOVING) first_still++;
    // ... then the river's velocity diffuses outward (Jacobi over the reached
    // cells, rivers held fixed), so a mouth's direction carries on into the
    // still water instead of stopping at its edge.
    for (int it = 0; it < reach; it++) {
        for (int t = first_still; t < qt; t++) {
            int c = q[t], x = c % n, z = c / n; float sx = 0, sz = 0; int cnt = 0;
            for (int k = 0; k < 8; k++) {
                int xx = x + DX[k], zz = z + DZ[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
                int j = zz * n + xx; if (kind[j] == WATER_FLOW_DRY) continue;
                sx += out[2 * j]; sz += out[2 * j + 1]; cnt++;
            }
            tmp[2 * c] = cnt ? sx / cnt : 0; tmp[2 * c + 1] = cnt ? sz / cnt : 0;
        }
        for (int t = first_still; t < qt; t++) { int c = q[t]; out[2 * c] = tmp[2 * c]; out[2 * c + 1] = tmp[2 * c + 1]; }
    }
    // Direction: the diffused one, turned toward the fan away from the mouth
    // (or toward an outlet the water leaves by) with distance; strength falls
    // smoothly to zero at `spread` cells.
    for (int t = first_still; t < qt; t++) {
        int c = q[t], s = src[c]; float ex = (float)(c % n - s % n), ez = (float)(c / n - s / n), el = sqrtf(ex * ex + ez * ez);
        float vx = out[2 * c], vz = out[2 * c + 1], vl = sqrtf(vx * vx + vz * vz);
        if (vl < 1e-6f || el < 1e-6f) { out[2 * c] = out[2 * c + 1] = 0; continue; }
        vx /= vl; vz /= vl; ex /= el; ez /= el;
        if (ex * out[2 * s] + ez * out[2 * s + 1] < 0) { ex = -ex; ez = -ez; }   // outflow: converge on the outlet
        float u = fminf(el / (S + 1), 1.0f), a = 0.6f * u;
        float dx = vx * (1 - a) + ex * a, dz = vz * (1 - a) + ez * a, dl = sqrtf(dx * dx + dz * dz);
        float str = (1 - u) * (1 - u);
        out[2 * c] = dl > 1e-6f ? dx / dl * str : 0; out[2 * c + 1] = dl > 1e-6f ? dz / dl * str : 0;
    }
    // 3. 3x3 blur over the cells that carry flow (everything else is zero) ...
    int act = qt;
    for (int t = 0; t < act; t++) {
        int c = q[t], x = c % n, z = c / n;
        float sx = out[2 * c] * 2, sz = out[2 * c + 1] * 2, w = 2;
        for (int k = 0; k < 8; k++) {
            int xx = x + DX[k], zz = z + DZ[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            size_t j = (size_t)zz * n + xx; if (kind[j] == WATER_FLOW_DRY) continue;
            sx += out[2 * j]; sz += out[2 * j + 1]; w += 1;
        }
        tmp[2 * c] = sx / w; tmp[2 * c + 1] = sz / w;
    }
    for (int t = 0; t < act; t++) { int c = q[t]; out[2 * c] = tmp[2 * c]; out[2 * c + 1] = tmp[2 * c + 1]; }
    // ... and a short dilation into dry cells, so filtering at the banks does
    // not pull the flow toward zero.
    for (size_t i = 0; i < N; i++) dist[i] = UINT16_MAX;
    for (int t = 0; t < act; t++) dist[q[t]] = 0;
    qh = 0; qt = act;
    while (qh < qt) {
        int c = q[qh++], x = c % n, z = c / n; if (dist[c] >= P.dilate) continue;
        for (int k = 0; k < 4; k++) {
            int xx = x + DX[k], zz = z + DZ[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            int j = zz * n + xx; if (dist[j] != UINT16_MAX || kind[j] != WATER_FLOW_DRY) continue;
            dist[j] = dist[c] + 1; out[2 * j] = out[2 * c]; out[2 * j + 1] = out[2 * c + 1]; q[qt++] = j;
        }
    }
    free(q); free(src); free(dist); free(tmp);
    return true;
}

// Surface speed, 0..1 per cell. surface: n x n water surface heights (m;
// read for wet cells), cell: metres per sample. Moving water takes
// speed_min + speed_slope * |surface slope| (central differences over wet
// neighbours), clamped to 1; still water takes speed_min. A separable box
// blur of radius speed_radius over wet cells, applied twice (a tent about
// 4 * radius cells wide), removes every step, and dry cells within `dilate`
// cells take the mean of the wet cells around them, so bilinear filtering at
// the banks stays smooth. Returns false if out of memory (out is then all
// speed_min).
static bool water_flow_speed(int n, const uint8_t* kind, const float* surface, float cell, const WaterFlowParams* prm, float* out) {
    WaterFlowParams P = prm ? *prm : water_flow_defaults();
    size_t N = (size_t)n * n;
    float* tmp = malloc(N * sizeof(float));
    for (size_t i = 0; i < N; i++) out[i] = P.speed_min;
    if (!tmp) return false;
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        size_t i = (size_t)z * n + x;
        if (kind[i] != WATER_FLOW_MOVING) continue;
        float g[2] = {0, 0};
        for (int a = 0; a < 2; a++) {   // a = 0: x, a = 1: z; one-sided where a neighbour is dry
            int lo = a ? z : x, x0 = a ? x : x - 1, z0 = a ? z - 1 : z, x1 = a ? x : x + 1, z1 = a ? z + 1 : z;
            bool w0 = lo > 0 && kind[(size_t)z0 * n + x0] != WATER_FLOW_DRY, w1 = lo < n - 1 && kind[(size_t)z1 * n + x1] != WATER_FLOW_DRY;
            float h0 = w0 ? surface[(size_t)z0 * n + x0] : surface[i], h1 = w1 ? surface[(size_t)z1 * n + x1] : surface[i];
            int span = (int)w0 + (int)w1; g[a] = span ? (h1 - h0) / (span * cell) : 0;
        }
        float v = P.speed_min + P.speed_slope * sqrtf(g[0] * g[0] + g[1] * g[1]);
        out[i] = v > 1 ? 1 : v;
    }
    int R = P.speed_radius < 0 ? 0 : P.speed_radius;
    for (int pass = 0; pass < 4 && R > 0; pass++) {   // x, z, x, z
        bool along_z = pass & 1; const float* src = (pass & 1) ? tmp : out; float* dst = (pass & 1) ? out : tmp;
        for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
            size_t i = (size_t)z * n + x;
            if (kind[i] == WATER_FLOW_DRY) { dst[i] = src[i]; continue; }
            float sum = 0; int cnt = 0;
            for (int d = -R; d <= R; d++) {
                int xx = along_z ? x : x + d, zz = along_z ? z + d : z; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
                size_t j = (size_t)zz * n + xx; if (kind[j] == WATER_FLOW_DRY) continue;
                sum += src[j]; cnt++;
            }
            dst[i] = sum / cnt;
        }
    }
    // Dry cells beside water: the mean of the wet cells within `dilate`.
    int D = P.dilate < 0 ? 0 : P.dilate;
    memcpy(tmp, out, N * sizeof(float));
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        size_t i = (size_t)z * n + x; if (kind[i] != WATER_FLOW_DRY) continue;
        float sum = 0; int cnt = 0;
        for (int dz = -D; dz <= D; dz++) for (int dx = -D; dx <= D; dx++) {
            int xx = x + dx, zz = z + dz; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            size_t j = (size_t)zz * n + xx; if (kind[j] == WATER_FLOW_DRY) continue;
            sum += tmp[j]; cnt++;
        }
        if (cnt) out[i] = sum / cnt;
    }
    free(tmp);
    return true;
}
#endif
