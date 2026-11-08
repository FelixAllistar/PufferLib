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
// Cost: about 0.1 s for a 2048^2 map with a few hundred river mouths
// (single thread; the work is proportional to river and mouth area).
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
    int spread;          // cells: how far a river's flow reaches into still water (default 16)
    int dilate;          // cells: copy of the field into dry ground beside water (default 2)
} WaterFlowParams;

static WaterFlowParams water_flow_defaults(void) { WaterFlowParams p = {4, 16, 2}; return p; }

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
    int S = P.spread < 1 ? 0 : P.spread > 4000 ? 4000 : P.spread;
    while (qh < qt) {
        int c = q[qh++], x = c % n, z = c / n; if (dist[c] >= S) continue;
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
    for (int it = 0; it < S; it++) {
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
        float u = (float)dist[c] / (S + 1), a = 0.6f * u;
        float dx = vx * (1 - a) + ex * a, dz = vz * (1 - a) + ez * a, dl = sqrtf(dx * dx + dz * dz);
        float str = (1 - u) * (1 - u);
        out[2 * c] = dl > 1e-6f ? dx / dl * str : 0; out[2 * c + 1] = dl > 1e-6f ? dz / dl * str : 0;
    }
    // 3. 3x3 blur over water cells (quantisation steps, the river/still edge) ...
    memcpy(tmp, out, N * 2 * sizeof(float));
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        size_t i = (size_t)z * n + x; if (kind[i] == WATER_FLOW_DRY) continue;
        float sx = tmp[2 * i] * 2, sz = tmp[2 * i + 1] * 2, w = 2;
        for (int k = 0; k < 8; k++) {
            int xx = x + DX[k], zz = z + DZ[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            size_t j = (size_t)zz * n + xx; if (kind[j] == WATER_FLOW_DRY) continue;
            sx += tmp[2 * j]; sz += tmp[2 * j + 1]; w += 1;
        }
        out[2 * i] = sx / w; out[2 * i + 1] = sz / w;
    }
    // ... and a short dilation into dry cells, so filtering at the banks does
    // not pull the flow toward zero.
    qh = qt = 0;
    for (size_t i = 0; i < N; i++) { dist[i] = kind[i] != WATER_FLOW_DRY ? 0 : UINT16_MAX; if (!dist[i]) q[qt++] = (int)i; }
    while (qh < qt) {
        int c = q[qh++], x = c % n, z = c / n; if (dist[c] >= P.dilate) continue;
        for (int k = 0; k < 4; k++) {
            int xx = x + DX[k], zz = z + DZ[k]; if (xx < 0 || zz < 0 || xx >= n || zz >= n) continue;
            int j = zz * n + xx; if (dist[j] != UINT16_MAX) continue;
            dist[j] = dist[c] + 1; out[2 * j] = out[2 * c]; out[2 * j + 1] = out[2 * c + 1]; q[qt++] = j;
        }
    }
    free(q); free(src); free(dist); free(tmp);
    return true;
}
#endif
