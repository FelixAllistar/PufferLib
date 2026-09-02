#pragma once

#include <math.h>

#include "ar_constants.h"

// CPU packs one environment's state in a single Env struct (AoS across envs);
// CUDA spreads each field across environments (SoA) so one warp touches
// contiguous memory. ar_sim.h hides that difference; these helpers are the
// shared collision rules both backends call.
#ifdef AR_GPU_SIM
#define AR_GEOMETRY_FN static __host__ __device__ __forceinline__
#else
#define AR_GEOMETRY_FN static inline
#endif

AR_GEOMETRY_FN float ar_geometry_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

AR_GEOMETRY_FN float ar_geometry_dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

// Push a point out of a circle. radius is the combined collision radius.
AR_GEOMETRY_FN int ar_geometry_push_out_circle(float* x, float* y,
        float other_x, float other_y, float radius) {
    float dx = *x - other_x;
    float dy = *y - other_y;
    if (dx >= radius || dx <= -radius || dy >= radius || dy <= -radius) return 0;
    float d2 = dx * dx + dy * dy;
    if (d2 >= radius * radius) return 0;

    float d = sqrtf(fmaxf(d2, 0.0001f));
    float push = radius - d;
    *x += dx / d * push;
    *y += dy / d * push;
    return 1;
}

// Uniform grid cell over the whole arena. Matches ar_sim.h grid insertion.
AR_GEOMETRY_FN int ar_geometry_cell(const ARConfig* cfg, float x, float y) {
    float half = 0.5f * cfg->arena_size;
    int gx = (int)(((x + half) / cfg->arena_size) * (float)AR_GRID_W);
    int gy = (int)(((y + half) / cfg->arena_size) * (float)AR_GRID_H);
    gx = gx < 0 ? 0 : (gx >= AR_GRID_W ? AR_GRID_W - 1 : gx);
    gy = gy < 0 ? 0 : (gy >= AR_GRID_H ? AR_GRID_H - 1 : gy);
    return gy * AR_GRID_W + gx;
}

#undef AR_GEOMETRY_FN
