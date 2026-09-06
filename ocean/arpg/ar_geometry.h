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

// Dungeon floor query. floor[] holds ARTile values; rock and deep water
// are solid. Out of bounds is solid.
AR_GEOMETRY_FN int ar_geometry_floor(const uint8_t* floor, float arena_size,
        float x, float y) {
    float half = 0.5f * arena_size;
    int gx = (int)(((x + half) / arena_size) * (float)AR_DUN_W);
    int gy = (int)(((y + half) / arena_size) * (float)AR_DUN_H);
    if (gx < 0 || gx >= AR_DUN_W || gy < 0 || gy >= AR_DUN_H) return 0;
    uint8_t t = floor[gy * AR_DUN_W + gx];
    return (t != AR_TILE_ROCK && t != AR_TILE_DEEP) ? 1 : 0;
}

// Movement speed multiplier for a terrain tile.
AR_GEOMETRY_FN float ar_tile_speed(uint8_t tile) {
    if (tile == AR_TILE_FOREST) return 0.85f;
    if (tile == AR_TILE_SHALLOW) return 0.6f;
    return 1.0f;
}

// Raw tile at a world point (AR_TILE_ROCK when out of bounds).
AR_GEOMETRY_FN uint8_t ar_tile_at(const uint8_t* floor, float arena_size,
        float x, float y) {
    float half = 0.5f * arena_size;
    int gx = (int)(((x + half) / arena_size) * (float)AR_DUN_W);
    int gy = (int)(((y + half) / arena_size) * (float)AR_DUN_H);
    if (gx < 0 || gx >= AR_DUN_W || gy < 0 || gy >= AR_DUN_H) {
        return AR_TILE_ROCK;
    }
    return floor[gy * AR_DUN_W + gx];
}

// Push a circle out of solid dungeon cells. Returns 1 if corrected.
// Checks the 3x3 neighborhood around the circle center and resolves along
// the least-penetration axis per solid cell.
AR_GEOMETRY_FN int ar_geometry_collide_dungeon(const uint8_t* floor,
        float arena_size, float* x, float* y, float radius) {
    float half = 0.5f * arena_size;
    float cell = arena_size / (float)AR_DUN_W;
    int gx = (int)(((*x + half) / arena_size) * (float)AR_DUN_W);
    int gy = (int)(((*y + half) / arena_size) * (float)AR_DUN_H);
    int moved = 0;
    for (int cy = gy - 1; cy <= gy + 1; cy++) {
        for (int cx = gx - 1; cx <= gx + 1; cx++) {
            int solid = 1;
            if (cx >= 0 && cx < AR_DUN_W && cy >= 0 && cy < AR_DUN_H) {
                solid = floor[cy * AR_DUN_W + cx] ? 0 : 1;
            }
            if (!solid) continue;
            float minx = -half + cx * cell;
            float maxx = minx + cell;
            float miny = -half + cy * cell;
            float maxy = miny + cell;
            float nx = *x < minx ? minx : (*x > maxx ? maxx : *x);
            float ny = *y < miny ? miny : (*y > maxy ? maxy : *y);
            float dx = *x - nx, dy = *y - ny;
            float d2 = dx * dx + dy * dy;
            if (d2 >= radius * radius) continue;
            if (d2 > 1e-8f) {
                float d = sqrtf(d2);
                *x = nx + dx / d * radius;
                *y = ny + dy / d * radius;
            } else {
                // Center inside the cell: push along least penetration.
                float pl = *x - minx, pr = maxx - *x;
                float pb = *y - miny, pt = maxy - *y;
                float m = pl;
                if (pr < m) m = pr;
                if (pb < m) m = pb;
                if (pt < m) m = pt;
                if (m == pl) *x = minx - radius;
                else if (m == pr) *x = maxx + radius;
                else if (m == pb) *y = miny - radius;
                else *y = maxy + radius;
            }
            moved = 1;
        }
    }
    return moved;
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
