#pragma once

#include <math.h>

#include "ps_config.h"

// The storage adapters are intentionally different: CPU uses an AoS state and
// CUDA uses SoA device arrays. These small, side-effect-free geometry helpers
// are the common collision rules both adapters call.
#ifdef __CUDACC__
#define PS_GEOMETRY_FN static __host__ __device__ __forceinline__
#else
#define PS_GEOMETRY_FN static inline
#endif

PS_GEOMETRY_FN int ps_geometry_circle_overlaps(float dx, float dy, float radius) {
    if (dx >= radius || dx <= -radius) return 0;
    if (dy >= radius || dy <= -radius) return 0;
    return dx * dx + dy * dy < radius * radius;
}

PS_GEOMETRY_FN float ps_geometry_clamp(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

PS_GEOMETRY_FN int ps_geometry_push_out(float* x, float* y,
        float obstacle_x, float obstacle_y, float radius);

// dx/dy are the circle center relative to the shape center. The shape is
// either a circle with radius or an axis-aligned box with half extents.
PS_GEOMETRY_FN int ps_geometry_shape_overlaps_circle(int shape,
        float dx, float dy, float radius, float half_width, float half_height,
        float circle_radius) {
    if (shape == PS_SHAPE_CIRCLE)
        return ps_geometry_circle_overlaps(dx, dy, radius + circle_radius);

    float closest_x = ps_geometry_clamp(dx, -half_width, half_width);
    float closest_y = ps_geometry_clamp(dy, -half_height, half_height);
    float outside_x = dx - closest_x;
    float outside_y = dy - closest_y;
    return outside_x * outside_x + outside_y * outside_y
        < circle_radius * circle_radius;
}

PS_GEOMETRY_FN float ps_geometry_shape_bound_radius(int shape,
        float radius, float half_width, float half_height) {
    if (shape == PS_SHAPE_CIRCLE) return radius;
    return sqrtf(half_width * half_width + half_height * half_height);
}

// Push a circle obstacle out of a circle/AABB-shaped entity. This is used for
// the player/enemy-vs-static-obstacle movement constraint; hit tests use the
// exact shape function above.
PS_GEOMETRY_FN int ps_geometry_push_out_shape_circle(float* x, float* y,
        int shape, float radius, float half_width, float half_height,
        float obstacle_x, float obstacle_y, float obstacle_radius) {
    if (shape == PS_SHAPE_CIRCLE) {
        return ps_geometry_push_out(x, y, obstacle_x, obstacle_y,
            radius + obstacle_radius);
    }

    float rel_x = obstacle_x - *x;
    float rel_y = obstacle_y - *y;
    float closest_x = ps_geometry_clamp(rel_x, -half_width, half_width);
    float closest_y = ps_geometry_clamp(rel_y, -half_height, half_height);
    float outside_x = rel_x - closest_x;
    float outside_y = rel_y - closest_y;
    float d2 = outside_x * outside_x + outside_y * outside_y;
    float obstacle_r2 = obstacle_radius * obstacle_radius;
    if (d2 >= obstacle_r2) return 0;

    if (d2 > 0.000001f) {
        float d = sqrtf(d2);
        float push = obstacle_radius - d;
        // The obstacle is on the positive outside_x/outside_y side of the
        // box, so move the box center in the opposite direction.
        *x -= outside_x / d * push;
        *y -= outside_y / d * push;
        return 1;
    }

    // The circle center is inside the box. Move through the nearest face of
    // the box expanded by the circle radius.
    float push_x = half_width + obstacle_radius - fabsf(rel_x);
    float push_y = half_height + obstacle_radius - fabsf(rel_y);
    if (push_x < push_y) {
        *x += rel_x >= 0.0f ? -push_x : push_x;
    } else {
        *y += rel_y >= 0.0f ? -push_y : push_y;
    }
    return 1;
}

PS_GEOMETRY_FN int ps_geometry_push_out(float* x, float* y,
        float obstacle_x, float obstacle_y, float radius) {
    float dx = *x - obstacle_x;
    float dy = *y - obstacle_y;
    if (!ps_geometry_circle_overlaps(dx, dy, radius)) return 0;

    float d2 = dx * dx + dy * dy;
    float d = sqrtf(fmaxf(d2, 0.0001f));
    float push = radius - d;
    *x += dx / d * push;
    *y += dy / d * push;
    return 1;
}

PS_GEOMETRY_FN float ps_geometry_weapon_radius(const PSConfig* cfg,
        int weapon, int level_delta) {
    return cfg->weapon_base_radius[weapon]
        + cfg->weapon_radius_per_level[weapon] * (float)level_delta;
}

#undef PS_GEOMETRY_FN
