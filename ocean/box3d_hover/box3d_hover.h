#pragma once

#ifdef B3H_PROFILE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#endif

#include "box3d/box3d.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef B3H_MAX_AGENTS
#define B3H_MAX_AGENTS 64
#endif

#define B3H_OBS_SIZE 24
#define B3H_NUM_ATNS 2
#define B3H_OBSTACLES 4
#define B3H_WALLS 4
#define B3H_DIRS 9
#define B3H_THROTTLES 3

static const float B3H_DT = 1.0f / 60.0f;
static const float B3H_OBSTACLE_POS[B3H_OBSTACLES][2] = {
    {-4.0f, -3.2f}, {3.4f, -2.1f}, {-2.5f, 3.7f}, {4.4f, 3.2f}
};

// Required Puffer log: floats only, n last.
typedef struct {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float captures;
    float timeouts;
    float wall_hits;
    float obstacle_hits;
    float speed;
    float distance;
    float box3d_step_ms;
    float body_count;
    float contact_count;
    float n;
} Log;

typedef struct {
    Log log;
    int num_agents;
    unsigned int rng;
    float* observations;
    float* actions;
    float* rewards;
    float* terminals;

    b3WorldId world;
    b3BodyId balls[B3H_MAX_AGENTS];
    b3BodyId statics[B3H_MAX_AGENTS];

    float center_x[B3H_MAX_AGENTS];
    float center_z[B3H_MAX_AGENTS];
    float target_x[B3H_MAX_AGENTS];
    float target_z[B3H_MAX_AGENTS];
    float prev_dist[B3H_MAX_AGENTS];
    float episode_return_accum[B3H_MAX_AGENTS];
    int age[B3H_MAX_AGENTS];
    int captures[B3H_MAX_AGENTS];

    float arena_half;
    float arena_stride;
    float ball_radius;
    float target_radius;
    float obstacle_radius;
    float force;
    float boost_mult;
    float brake_mult;
    float max_speed;
    int max_steps;
    int substeps;

    uint64_t prof_apply_ns;
    uint64_t prof_step_ns;
    uint64_t prof_post_ns;
} Box3DHover;

static inline float b3h_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float b3h_len2(float x, float z) {
    return x * x + z * z;
}

static inline uint32_t b3h_rand_u32(Box3DHover* env) {
    env->rng = env->rng * 1664525u + 1013904223u;
    return env->rng;
}

static inline float b3h_rand01(Box3DHover* env) {
    return (float)(b3h_rand_u32(env) >> 8) * (1.0f / 16777216.0f);
}

static inline float b3h_rand_range(Box3DHover* env, float lo, float hi) {
    return lo + (hi - lo) * b3h_rand01(env);
}

#ifdef B3H_PROFILE
#include <time.h>
static inline uint64_t b3h_ns_now(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#define B3H_PROF_START(name) uint64_t name = b3h_ns_now()
#define B3H_PROF_ADD(field, start) env->field += b3h_ns_now() - (start)
#else
#define B3H_PROF_START(name) ((void)0)
#define B3H_PROF_ADD(field, start) ((void)0)
#endif

static inline void b3h_dir(int action, float* x, float* z) {
    static const float dirs[B3H_DIRS][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
        {0.70710678f, 0.70710678f}, {0.70710678f, -0.70710678f},
        {-0.70710678f, 0.70710678f}, {-0.70710678f, -0.70710678f}
    };
    int a = action < 0 ? 0 : (action >= B3H_DIRS ? 0 : action);
    *x = dirs[a][0];
    *z = dirs[a][1];
}

static inline void b3h_set_body(Box3DHover* env, int i, float lx, float lz) {
    b3Pos p = { env->center_x[i] + lx, 0.0f, env->center_z[i] + lz };
    b3Body_SetTransform(env->balls[i], p, b3Quat_identity);
    b3Body_SetLinearVelocity(env->balls[i], b3Vec3_zero);
    b3Body_SetAngularVelocity(env->balls[i], b3Vec3_zero);
    b3Body_SetAwake(env->balls[i], true);
}

static inline void b3h_pick_target(Box3DHover* env, int i) {
    float margin = env->ball_radius + env->target_radius + 0.8f;
    env->target_x[i] = b3h_rand_range(env, -env->arena_half + margin, env->arena_half - margin);
    env->target_z[i] = b3h_rand_range(env, -env->arena_half + margin, env->arena_half - margin);
}

static inline void b3h_reset_agent(Box3DHover* env, int i) {
    float span = env->arena_half * 0.35f;
    float lx = b3h_rand_range(env, -span, span);
    float lz = b3h_rand_range(env, -span, span);
    b3h_set_body(env, i, lx, lz);
    b3h_pick_target(env, i);
    float dx = env->target_x[i] - lx;
    float dz = env->target_z[i] - lz;
    env->prev_dist[i] = sqrtf(b3h_len2(dx, dz));
    env->age[i] = 0;
    env->episode_return_accum[i] = 0.0f;
    env->captures[i] = 0;
}

static inline void b3h_attach_static_box(b3BodyId body, float ox, float oz, float hx, float hz) {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.baseMaterial.friction = 0.35f;
    shape_def.invokeContactCreation = false;
    b3BoxHull hull = b3MakeOffsetBoxHull(hx, 0.5f, hz, (b3Vec3){ ox, 0.0f, oz });
    b3CreateHullShape(body, &shape_def, &hull.base);
}

static inline void b3h_attach_static_sphere(b3BodyId body, float ox, float oz, float radius) {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.baseMaterial.friction = 0.35f;
    shape_def.invokeContactCreation = false;
    b3Sphere sphere = { { ox, 0.0f, oz }, radius };
    b3CreateSphereShape(body, &shape_def, &sphere);
}

static inline void b3h_create_world(Box3DHover* env) {
    b3WorldDef world_def = b3DefaultWorldDef();
    world_def.gravity = b3Vec3_zero;
    world_def.enableSleep = false;
    world_def.enableContinuous = false;
    world_def.workerCount = 1;
    world_def.capacity.staticBodyCount = env->num_agents;
    world_def.capacity.dynamicBodyCount = env->num_agents;
    world_def.capacity.staticShapeCount = env->num_agents * (B3H_WALLS + B3H_OBSTACLES);
    world_def.capacity.dynamicShapeCount = env->num_agents;
    world_def.capacity.contactCount = env->num_agents * 12;
    env->world = b3CreateWorld(&world_def);
    b3World_SetMaximumLinearSpeed(env->world, env->max_speed * 2.5f);

    b3ShapeDef ball_shape = b3DefaultShapeDef();
    ball_shape.density = 1.0f;
    ball_shape.baseMaterial.friction = 0.08f;
    ball_shape.baseMaterial.restitution = 0.15f;
    ball_shape.baseMaterial.rollingResistance = 0.05f;

    for (int i = 0; i < env->num_agents; i++) {
        env->center_x[i] = (float)i * env->arena_stride;
        env->center_z[i] = 0.0f;
        float cx = env->center_x[i];
        float cz = env->center_z[i];
        float h = env->arena_half;
        float t = 0.35f;

        b3BodyDef static_def = b3DefaultBodyDef();
        static_def.type = b3_staticBody;
        static_def.position = (b3Pos){ cx, 0.0f, cz };
        env->statics[i] = b3CreateBody(env->world, &static_def);
        b3h_attach_static_box(env->statics[i], -h - t, 0.0f, t, h + t);
        b3h_attach_static_box(env->statics[i], h + t, 0.0f, t, h + t);
        b3h_attach_static_box(env->statics[i], 0.0f, -h - t, h + t, t);
        b3h_attach_static_box(env->statics[i], 0.0f, h + t, h + t, t);
        for (int o = 0; o < B3H_OBSTACLES; o++) {
            b3h_attach_static_sphere(env->statics[i], B3H_OBSTACLE_POS[o][0], B3H_OBSTACLE_POS[o][1], env->obstacle_radius);
        }

        b3BodyDef ball_def = b3DefaultBodyDef();
        ball_def.type = b3_dynamicBody;
        ball_def.position = (b3Pos){ cx, 0.0f, cz };
        ball_def.linearDamping = 1.0f;
        ball_def.angularDamping = 2.0f;
        ball_def.gravityScale = 0.0f;
        ball_def.enableSleep = false;
        ball_def.motionLocks.linearY = true;
        env->balls[i] = b3CreateBody(env->world, &ball_def);
        b3Sphere ball = { {0.0f, 0.0f, 0.0f}, env->ball_radius };
        b3CreateSphereShape(env->balls[i], &ball_shape, &ball);
    }
}

static inline void box3d_hover_init(Box3DHover* env, int num_agents) {
    if (num_agents < 1) num_agents = 1;
    if (num_agents > B3H_MAX_AGENTS) num_agents = B3H_MAX_AGENTS;
    env->num_agents = num_agents;
    env->arena_half = env->arena_half > 0.0f ? env->arena_half : 8.0f;
    env->arena_stride = env->arena_stride > 0.0f ? env->arena_stride : 22.0f;
    env->ball_radius = env->ball_radius > 0.0f ? env->ball_radius : 0.35f;
    env->target_radius = env->target_radius > 0.0f ? env->target_radius : 0.55f;
    env->obstacle_radius = env->obstacle_radius > 0.0f ? env->obstacle_radius : 0.85f;
    env->force = env->force > 0.0f ? env->force : 18.0f;
    env->boost_mult = env->boost_mult > 0.0f ? env->boost_mult : 1.65f;
    env->brake_mult = env->brake_mult > 0.0f ? env->brake_mult : 0.55f;
    env->max_speed = env->max_speed > 0.0f ? env->max_speed : 8.0f;
    env->max_steps = env->max_steps > 0 ? env->max_steps : 360;
    env->substeps = env->substeps > 0 ? env->substeps : 2;
    b3h_create_world(env);
}

static inline void b3h_compute_observations(Box3DHover* env) {
    float inv_half = 1.0f / fmaxf(env->arena_half, 0.001f);
    float inv_speed = 1.0f / fmaxf(env->max_speed, 0.001f);
    float inv_diag = 1.0f / fmaxf(1.41421356f * env->arena_half, 0.001f);

    for (int i = 0; i < env->num_agents; i++) {
        float* obs = env->observations + i * B3H_OBS_SIZE;
        b3Pos p = b3Body_GetPosition(env->balls[i]);
        b3Vec3 v = b3Body_GetLinearVelocity(env->balls[i]);
        float lx = p.x - env->center_x[i];
        float lz = p.z - env->center_z[i];
        float tx = env->target_x[i];
        float tz = env->target_z[i];
        float dx = tx - lx;
        float dz = tz - lz;
        float d = sqrtf(b3h_len2(dx, dz));

        int nearest = 0;
        float nearest_d2 = 1.0e20f;
        for (int o = 0; o < B3H_OBSTACLES; o++) {
            float odx = B3H_OBSTACLE_POS[o][0] - lx;
            float odz = B3H_OBSTACLE_POS[o][1] - lz;
            float od2 = b3h_len2(odx, odz);
            if (od2 < nearest_d2) {
                nearest_d2 = od2;
                nearest = o;
            }
        }
        float odx = B3H_OBSTACLE_POS[nearest][0] - lx;
        float odz = B3H_OBSTACLE_POS[nearest][1] - lz;
        float wall_x = env->arena_half - fabsf(lx);
        float wall_z = env->arena_half - fabsf(lz);
        float speed = sqrtf(b3h_len2(v.x, v.z));

        int k = 0;
        obs[k++] = lx * inv_half;
        obs[k++] = lz * inv_half;
        obs[k++] = v.x * inv_speed;
        obs[k++] = v.z * inv_speed;
        obs[k++] = speed * inv_speed;
        obs[k++] = dx * inv_half;
        obs[k++] = dz * inv_half;
        obs[k++] = d * inv_diag;
        obs[k++] = tx * inv_half;
        obs[k++] = tz * inv_half;
        obs[k++] = odx * inv_half;
        obs[k++] = odz * inv_half;
        obs[k++] = sqrtf(nearest_d2) * inv_diag;
        obs[k++] = wall_x * inv_half;
        obs[k++] = wall_z * inv_half;
        obs[k++] = (float)env->age[i] / (float)env->max_steps;
        obs[k++] = (float)env->captures[i] * 0.1f;
        obs[k++] = b3h_clamp(env->prev_dist[i] * inv_diag, 0.0f, 1.0f);
        obs[k++] = b3h_clamp((float)i / (float)B3H_MAX_AGENTS, 0.0f, 1.0f);
        obs[k++] = b3h_clamp((env->arena_half - fmaxf(fabsf(lx), fabsf(lz))) * inv_half, 0.0f, 1.0f);
        obs[k++] = b3h_clamp(v.x * dx * inv_speed * inv_half, -1.0f, 1.0f);
        obs[k++] = b3h_clamp(v.z * dz * inv_speed * inv_half, -1.0f, 1.0f);
        obs[k++] = 0.0f;
        obs[k++] = 1.0f;
    }
}

static inline void c_reset(Box3DHover* env) {
    memset(&env->log, 0, sizeof(env->log));
    for (int i = 0; i < env->num_agents; i++) {
        env->rewards[i] = 0.0f;
        env->terminals[i] = 0.0f;
        b3h_reset_agent(env, i);
    }
    b3h_compute_observations(env);
}

static inline void c_step(Box3DHover* env) {
    B3H_PROF_START(t_apply);
    for (int i = 0; i < env->num_agents; i++) {
        env->rewards[i] = -0.001f;
        env->terminals[i] = 0.0f;
        float dx, dz;
        b3h_dir((int)env->actions[i * B3H_NUM_ATNS], &dx, &dz);
        int throttle = (int)env->actions[i * B3H_NUM_ATNS + 1];
        float mult = throttle == 2 ? env->boost_mult : (throttle == 0 ? env->brake_mult : 1.0f);
        b3Vec3 force = { dx * env->force * mult, 0.0f, dz * env->force * mult };
        b3Body_ApplyForceToCenter(env->balls[i], force, true);
    }
    B3H_PROF_ADD(prof_apply_ns, t_apply);

    B3H_PROF_START(t_step);
    b3World_Step(env->world, B3H_DT, env->substeps);
    B3H_PROF_ADD(prof_step_ns, t_step);

    B3H_PROF_START(t_post);
    b3Profile profile = {0};
    b3Counters counters = {0};
    int have_world_stats = 0;

    for (int i = 0; i < env->num_agents; i++) {
        b3Pos p = b3Body_GetPosition(env->balls[i]);
        b3Vec3 v = b3Body_GetLinearVelocity(env->balls[i]);
        float lx = p.x - env->center_x[i];
        float lz = p.z - env->center_z[i];
        float ddx = env->target_x[i] - lx;
        float ddz = env->target_z[i] - lz;
        float d = sqrtf(b3h_len2(ddx, ddz));
        float progress = env->prev_dist[i] - d;
        float speed = sqrtf(b3h_len2(v.x, v.z));
        float reward = -0.001f + 0.12f * progress - 0.0005f * speed;

        float wall_margin = env->arena_half - fmaxf(fabsf(lx), fabsf(lz));
        if (wall_margin < env->ball_radius + 0.08f) {
            reward -= 0.01f;
            env->log.wall_hits += 1.0f;
        }

        for (int o = 0; o < B3H_OBSTACLES; o++) {
            float ox = env->center_x[i] + B3H_OBSTACLE_POS[o][0] - p.x;
            float oz = env->center_z[i] + B3H_OBSTACLE_POS[o][1] - p.z;
            float hit = env->obstacle_radius + env->ball_radius + 0.05f;
            if (b3h_len2(ox, oz) < hit * hit) {
                reward -= 0.015f;
                env->log.obstacle_hits += 1.0f;
                break;
            }
        }

        env->age[i]++;
        env->prev_dist[i] = d;
        int captured = d < env->target_radius;
        int timed_out = env->age[i] >= env->max_steps;
        if (captured) {
            reward += 1.0f;
            env->captures[i]++;
        }

        env->rewards[i] = reward;
        env->episode_return_accum[i] += reward;
        if (captured || timed_out) {
            if (!have_world_stats) {
                profile = b3World_GetProfile(env->world);
                counters = b3World_GetCounters(env->world);
                have_world_stats = 1;
            }
            float perf = captured ? b3h_clamp(1.0f - (float)env->age[i] / (float)env->max_steps, 0.0f, 1.0f) : 0.0f;
            env->log.perf += perf;
            env->log.score += captured ? 100.0f * perf : 0.0f;
            env->log.episode_return += env->episode_return_accum[i];
            env->log.episode_length += (float)env->age[i];
            env->log.captures += captured ? 1.0f : 0.0f;
            env->log.timeouts += timed_out ? 1.0f : 0.0f;
            env->log.speed += speed;
            env->log.distance += d;
            env->log.box3d_step_ms += profile.step;
            env->log.body_count += (float)counters.bodyCount;
            env->log.contact_count += (float)counters.contactCount;
            env->log.n += 1.0f;
            env->terminals[i] = 1.0f;
            b3h_reset_agent(env, i);
        }
    }

    b3h_compute_observations(env);
    B3H_PROF_ADD(prof_post_ns, t_post);
}

static inline void c_close(Box3DHover* env) {
    if (b3World_IsValid(env->world)) {
        b3DestroyWorld(env->world);
    }
}
