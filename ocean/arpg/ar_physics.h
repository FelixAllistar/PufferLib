#pragma once

// box3d physics glue. CPU path only: box3d has no device kernels, so the CUDA
// backend swaps this file for the analytic movement in ar_sim.h (see README,
// "Physics backends"). Everything here is plain box3d; no Env dependency.
#ifndef AR_GPU_SIM

#include "box3d/box3d.h"

#include "ar_constants.h"

// Wall thickness/height for the arena border boxes.
#define AR_WALL_THICKNESS 0.5f
#define AR_WALL_HEIGHT 1.0f

static inline b3WorldId ar_phys_create_world(const ARConfig* cfg) {
    b3WorldDef def = b3DefaultWorldDef();
    // Top-down: no gravity, single worker so OpenMP env workers own one world
    // each without an internal task scheduler.
    def.gravity = (b3Vec3){0.0f, 0.0f, 0.0f};
    def.workerCount = 1;
    def.enableSleep = false;
    def.maximumLinearSpeed = cfg->player_speed * 16.0f;
    b3WorldId world = b3CreateWorld(&def);
    if (B3_IS_NULL(world)) return world;

    // Arena border walls. The playfield is the square [-half, half]^2.
    float half = 0.5f * cfg->arena_size;
    float t = AR_WALL_THICKNESS;
    float length = cfg->arena_size + 2.0f * t;
    b3Vec3 wall_axes[4][2] = {
        {{length, 2.0f * t, 2.0f * AR_WALL_HEIGHT}},  // north/south spans x
        {{2.0f * t, length, 2.0f * AR_WALL_HEIGHT}},  // east/west spans y
    };
    float wall_pos[4][2] = {
        {0.0f, half + t},
        {0.0f, -half - t},
        {half + t, 0.0f},
        {-half - t, 0.0f},
    };
    for (int i = 0; i < 4; i++) {
        b3BodyDef bd = b3DefaultBodyDef();
        bd.position = (b3Pos){wall_pos[i][0], wall_pos[i][1], 0.0};
        b3BodyId body = b3CreateBody(world, &bd);
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.baseMaterial.friction = 0.0f;
        sd.baseMaterial.restitution = 0.0f;
        int axis = i < 2 ? 0 : 1;
        b3BoxHull hull = b3MakeBoxHull(
            wall_axes[axis][0].x, wall_axes[axis][0].y, wall_axes[axis][0].z);
        b3CreateHullShape(body, &sd, &hull.base);
    }
    return world;
}

// Dynamic sphere body locked to the ground plane (no z, no rotation). Used for
// the player, pets, and enemies; velocities are set every tick by the sim.
static inline b3BodyId ar_phys_dynamic_body(b3WorldId world, float x, float y,
        float radius) {
    b3BodyDef bd = b3DefaultBodyDef();
    bd.type = b3_dynamicBody;
    bd.position = (b3Pos){x, y, radius};
    bd.enableSleep = false;
    bd.motionLocks.linearZ = true;
    bd.motionLocks.angularX = true;
    bd.motionLocks.angularY = true;
    bd.motionLocks.angularZ = true;
    b3BodyId body = b3CreateBody(world, &bd);
    if (B3_IS_NULL(body)) return body;

    b3ShapeDef sd = b3DefaultShapeDef();
    sd.density = 1.0f;
    sd.baseMaterial.friction = 0.0f;
    sd.baseMaterial.restitution = 0.0f;
    b3Sphere sphere;
    sphere.center.x = 0.0f;
    sphere.center.y = 0.0f;
    sphere.center.z = 0.0f;
    sphere.radius = radius;
    b3CreateSphereShape(body, &sd, &sphere);
    return body;
}

static inline b3BodyId ar_phys_static_circle(b3WorldId world, float x, float y,
        float radius) {
    b3BodyDef bd = b3DefaultBodyDef();
    bd.position = (b3Pos){x, y, radius};
    b3BodyId body = b3CreateBody(world, &bd);
    if (B3_IS_NULL(body)) return body;

    b3ShapeDef sd = b3DefaultShapeDef();
    sd.baseMaterial.friction = 0.0f;
    sd.baseMaterial.restitution = 0.0f;
    b3Sphere sphere;
    sphere.center.x = 0.0f;
    sphere.center.y = 0.0f;
    sphere.center.z = 0.0f;
    sphere.radius = radius;
    b3CreateSphereShape(body, &sd, &sphere);
    return body;
}

static inline void ar_phys_teleport(b3BodyId body, float x, float y) {
    b3Body_SetTransform(body, (b3Pos){x, y, 0.0}, b3Quat_identity);
}

static inline void ar_phys_set_velocity(b3BodyId body, float vx, float vy) {
    b3Body_SetLinearVelocity(body, (b3Vec3){vx, vy, 0.0f});
}

static inline void ar_phys_position(b3BodyId body, float* x, float* y) {
    b3Pos p = b3Body_GetPosition(body);
    *x = (float)p.x;
    *y = (float)p.y;
}

static inline void ar_phys_step(b3WorldId world) {
    b3World_Step(world, AR_DT, 1);
}

#endif  // !AR_GPU_SIM
