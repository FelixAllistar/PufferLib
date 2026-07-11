#pragma once

#ifdef B3P_PROFILE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#endif

#include "box3d/box3d.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef B3P_MAX_AGENTS
#define B3P_MAX_AGENTS 64
#endif

#define B3P_PUCKS 3
#define B3P_MAX_PUCKS (B3P_MAX_AGENTS * B3P_PUCKS)
#define B3P_OBS_SIZE 48
#define B3P_NUM_ATNS 2
#define B3P_BUMPERS 4
#define B3P_WALLS 4
#define B3P_DIRS 9
#define B3P_THROTTLES 3

static const float B3P_DT = 1.0f / 60.0f;
static const float B3P_BUMPER_POS[B3P_BUMPERS][2] = {
    {-3.8f, -3.0f}, {-2.0f, 3.6f}, {2.5f, -2.5f}, {3.8f, 2.6f}
};
static const float B3P_PUCK_START[B3P_PUCKS][2] = {
    {-4.7f, -2.2f}, {-5.2f, 0.8f}, {-3.7f, 2.8f}
};

typedef struct {
    float perf;
    float score;
    float episode_return;
    float episode_length;
    float goals;
    float timeouts;
    float wall_hits;
    float bumper_hits;
    float player_contacts;
    float puck_speed;
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
    b3BodyId players[B3P_MAX_AGENTS];
    b3BodyId statics[B3P_MAX_AGENTS];
    b3BodyId pucks[B3P_MAX_PUCKS];

    float center_x[B3P_MAX_AGENTS];
    float center_z[B3P_MAX_AGENTS];
    float goal_x[B3P_MAX_AGENTS];
    float goal_z[B3P_MAX_AGENTS];
    float prev_nearest_dist[B3P_MAX_AGENTS];
    float prev_nearest_player_puck_dist[B3P_MAX_AGENTS];
    float episode_return_accum[B3P_MAX_AGENTS];
    int age[B3P_MAX_AGENTS];
    int goals[B3P_MAX_AGENTS];

    float puck_prev_goal_dist[B3P_MAX_PUCKS];

    float arena_half;
    float arena_stride;
    float player_radius;
    float puck_radius;
    float goal_radius;
    float bumper_radius;
    float goal_local_x;
    float goal_z_range;
    float player_start_x;
    float puck_start_shift;
    float puck_start_scale;
    float force;
    float boost_mult;
    float brake_mult;
    float max_speed;
    float approach_reward;
    float push_reward;
    float progress_reward;
    float contact_reward;
    float goal_reward;
    int target_goals;
    int max_steps;
    int substeps;

    uint64_t prof_apply_ns;
    uint64_t prof_step_ns;
    uint64_t prof_post_ns;
} Box3DPusher;

static inline float b3p_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float b3p_len2(float x, float z) {
    return x * x + z * z;
}

static inline uint32_t b3p_rand_u32(Box3DPusher* env) {
    env->rng = env->rng * 1664525u + 1013904223u;
    return env->rng;
}

static inline float b3p_rand01(Box3DPusher* env) {
    return (float)(b3p_rand_u32(env) >> 8) * (1.0f / 16777216.0f);
}

static inline float b3p_rand_range(Box3DPusher* env, float lo, float hi) {
    return lo + (hi - lo) * b3p_rand01(env);
}

#ifdef B3P_PROFILE
#include <time.h>
static inline uint64_t b3p_ns_now(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#define B3P_PROF_START(name) uint64_t name = b3p_ns_now()
#define B3P_PROF_ADD(field, start) env->field += b3p_ns_now() - (start)
#else
#define B3P_PROF_START(name) ((void)0)
#define B3P_PROF_ADD(field, start) ((void)0)
#endif

static inline int b3p_puck_index(int agent, int puck) {
    return agent * B3P_PUCKS + puck;
}

static inline void b3p_dir(int action, float* x, float* z) {
    static const float dirs[B3P_DIRS][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
        {0.70710678f, 0.70710678f}, {0.70710678f, -0.70710678f},
        {-0.70710678f, 0.70710678f}, {-0.70710678f, -0.70710678f}
    };
    int a = action < 0 ? 0 : (action >= B3P_DIRS ? 0 : action);
    *x = dirs[a][0];
    *z = dirs[a][1];
}

static inline void b3p_set_body(b3BodyId body, float x, float z) {
    b3Body_SetTransform(body, (b3Pos){x, 0.0f, z}, b3Quat_identity);
    b3Body_SetLinearVelocity(body, b3Vec3_zero);
    b3Body_SetAngularVelocity(body, b3Vec3_zero);
    b3Body_SetAwake(body, true);
}

static inline void b3p_pick_goal(Box3DPusher* env, int i) {
    env->goal_x[i] = env->goal_local_x;
    env->goal_z[i] = b3p_rand_range(env, -env->goal_z_range, env->goal_z_range);
}

static inline void b3p_reset_puck(Box3DPusher* env, int i, int p) {
    int idx = b3p_puck_index(i, p);
    float jitter_x = b3p_rand_range(env, -0.55f, 0.55f);
    float jitter_z = b3p_rand_range(env, -0.45f, 0.45f);
    float lx = B3P_PUCK_START[p][0] * env->puck_start_scale + env->puck_start_shift + jitter_x;
    float lz = B3P_PUCK_START[p][1] * env->puck_start_scale + jitter_z;
    b3p_set_body(env->pucks[idx], env->center_x[i] + lx, env->center_z[i] + lz);
    float gx = env->goal_x[i] - lx;
    float gz = env->goal_z[i] - lz;
    env->puck_prev_goal_dist[idx] = sqrtf(b3p_len2(gx, gz));
}

static inline float b3p_nearest_player_puck_dist(Box3DPusher* env, int i) {
    b3Pos pp = b3Body_GetPosition(env->players[i]);
    float plx = pp.x - env->center_x[i];
    float plz = pp.z - env->center_z[i];
    float best_d2 = 1.0e20f;

    for (int p = 0; p < B3P_PUCKS; p++) {
        int idx = b3p_puck_index(i, p);
        b3Pos kp = b3Body_GetPosition(env->pucks[idx]);
        float dx = (kp.x - env->center_x[i]) - plx;
        float dz = (kp.z - env->center_z[i]) - plz;
        float d2 = b3p_len2(dx, dz);
        if (d2 < best_d2) best_d2 = d2;
    }

    return sqrtf(best_d2);
}

static inline float b3p_nearest_puck_goal_dist(Box3DPusher* env, int i) {
    float best = 1.0e20f;
    for (int p = 0; p < B3P_PUCKS; p++) {
        int idx = b3p_puck_index(i, p);
        if (env->puck_prev_goal_dist[idx] < best) best = env->puck_prev_goal_dist[idx];
    }
    return best;
}

static inline void b3p_reset_agent(Box3DPusher* env, int i) {
    b3p_pick_goal(env, i);
    b3p_set_body(env->players[i], env->center_x[i] + env->player_start_x, env->center_z[i]);
    for (int p = 0; p < B3P_PUCKS; p++) {
        b3p_reset_puck(env, i, p);
    }
    env->prev_nearest_dist[i] = b3p_nearest_puck_goal_dist(env, i);
    env->prev_nearest_player_puck_dist[i] = b3p_nearest_player_puck_dist(env, i);
    env->age[i] = 0;
    env->goals[i] = 0;
    env->episode_return_accum[i] = 0.0f;
}

static inline void b3p_attach_static_box(b3BodyId body, float ox, float oz, float hx, float hz) {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.baseMaterial.friction = 0.25f;
    shape_def.baseMaterial.restitution = 0.75f;
    shape_def.invokeContactCreation = false;
    b3BoxHull hull = b3MakeOffsetBoxHull(hx, 0.5f, hz, (b3Vec3){ox, 0.0f, oz});
    b3CreateHullShape(body, &shape_def, &hull.base);
}

static inline void b3p_attach_static_sphere(b3BodyId body, float ox, float oz, float radius) {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.baseMaterial.friction = 0.08f;
    shape_def.baseMaterial.restitution = 1.05f;
    shape_def.invokeContactCreation = false;
    b3Sphere sphere = {{ox, 0.0f, oz}, radius};
    b3CreateSphereShape(body, &shape_def, &sphere);
}

static inline void b3p_create_world(Box3DPusher* env) {
    b3WorldDef world_def = b3DefaultWorldDef();
    world_def.gravity = b3Vec3_zero;
    world_def.enableSleep = false;
    world_def.enableContinuous = false;
    world_def.workerCount = 1;
    world_def.capacity.staticBodyCount = env->num_agents;
    world_def.capacity.dynamicBodyCount = env->num_agents * (1 + B3P_PUCKS);
    world_def.capacity.staticShapeCount = env->num_agents * (B3P_WALLS + B3P_BUMPERS);
    world_def.capacity.dynamicShapeCount = env->num_agents * (1 + B3P_PUCKS);
    world_def.capacity.contactCount = env->num_agents * 32;
    env->world = b3CreateWorld(&world_def);
    b3World_SetMaximumLinearSpeed(env->world, env->max_speed * 3.0f);

    b3ShapeDef player_shape = b3DefaultShapeDef();
    player_shape.density = 1.5f;
    player_shape.baseMaterial.friction = 0.05f;
    player_shape.baseMaterial.restitution = 0.45f;
    player_shape.baseMaterial.rollingResistance = 0.02f;

    b3ShapeDef puck_shape = b3DefaultShapeDef();
    puck_shape.density = 0.8f;
    puck_shape.baseMaterial.friction = 0.03f;
    puck_shape.baseMaterial.restitution = 0.82f;
    puck_shape.baseMaterial.rollingResistance = 0.01f;

    for (int i = 0; i < env->num_agents; i++) {
        env->center_x[i] = (float)i * env->arena_stride;
        env->center_z[i] = 0.0f;
        float cx = env->center_x[i];
        float cz = env->center_z[i];
        float h = env->arena_half;
        float t = 0.35f;

        b3BodyDef static_def = b3DefaultBodyDef();
        static_def.type = b3_staticBody;
        static_def.position = (b3Pos){cx, 0.0f, cz};
        env->statics[i] = b3CreateBody(env->world, &static_def);
        b3p_attach_static_box(env->statics[i], -h - t, 0.0f, t, h + t);
        b3p_attach_static_box(env->statics[i], h + t, 0.0f, t, h + t);
        b3p_attach_static_box(env->statics[i], 0.0f, -h - t, h + t, t);
        b3p_attach_static_box(env->statics[i], 0.0f, h + t, h + t, t);
        for (int b = 0; b < B3P_BUMPERS; b++) {
            b3p_attach_static_sphere(env->statics[i], B3P_BUMPER_POS[b][0], B3P_BUMPER_POS[b][1], env->bumper_radius);
        }

        b3BodyDef player_def = b3DefaultBodyDef();
        player_def.type = b3_dynamicBody;
        player_def.position = (b3Pos){cx, 0.0f, cz};
        player_def.linearDamping = 0.65f;
        player_def.angularDamping = 1.6f;
        player_def.gravityScale = 0.0f;
        player_def.enableSleep = false;
        player_def.motionLocks.linearY = true;
        env->players[i] = b3CreateBody(env->world, &player_def);
        b3Sphere player = {{0.0f, 0.0f, 0.0f}, env->player_radius};
        b3CreateSphereShape(env->players[i], &player_shape, &player);

        for (int p = 0; p < B3P_PUCKS; p++) {
            int idx = b3p_puck_index(i, p);
            b3BodyDef puck_def = b3DefaultBodyDef();
            puck_def.type = b3_dynamicBody;
            puck_def.position = (b3Pos){cx, 0.0f, cz};
            puck_def.linearDamping = 0.18f;
            puck_def.angularDamping = 0.35f;
            puck_def.gravityScale = 0.0f;
            puck_def.enableSleep = false;
            puck_def.motionLocks.linearY = true;
            env->pucks[idx] = b3CreateBody(env->world, &puck_def);
            b3Sphere puck = {{0.0f, 0.0f, 0.0f}, env->puck_radius};
            b3CreateSphereShape(env->pucks[idx], &puck_shape, &puck);
        }
    }
}

static inline void box3d_pusher_init(Box3DPusher* env, int num_agents) {
    if (num_agents < 1) num_agents = 1;
    if (num_agents > B3P_MAX_AGENTS) num_agents = B3P_MAX_AGENTS;
    env->num_agents = num_agents;
    env->arena_half = env->arena_half > 0.0f ? env->arena_half : 8.0f;
    env->arena_stride = env->arena_stride > 0.0f ? env->arena_stride : 22.0f;
    env->player_radius = env->player_radius > 0.0f ? env->player_radius : 0.38f;
    env->puck_radius = env->puck_radius > 0.0f ? env->puck_radius : 0.32f;
    env->goal_radius = env->goal_radius > 0.0f ? env->goal_radius : 0.85f;
    env->bumper_radius = env->bumper_radius > 0.0f ? env->bumper_radius : 0.72f;
    env->goal_local_x = env->goal_local_x != 0.0f ? env->goal_local_x : env->arena_half - 1.35f;
    env->goal_z_range = env->goal_z_range > 0.0f ? env->goal_z_range : env->arena_half * 0.55f;
    env->player_start_x = env->player_start_x != 0.0f ? env->player_start_x : -env->arena_half * 0.15f;
    env->puck_start_scale = env->puck_start_scale > 0.0f ? env->puck_start_scale : 1.0f;
    env->force = env->force > 0.0f ? env->force : 23.0f;
    env->boost_mult = env->boost_mult > 0.0f ? env->boost_mult : 1.65f;
    env->brake_mult = env->brake_mult > 0.0f ? env->brake_mult : 0.45f;
    env->max_speed = env->max_speed > 0.0f ? env->max_speed : 9.0f;
    env->approach_reward = env->approach_reward > 0.0f ? env->approach_reward : 0.04f;
    env->push_reward = env->push_reward > 0.0f ? env->push_reward : 0.05f;
    env->progress_reward = env->progress_reward > 0.0f ? env->progress_reward : 0.08f;
    env->contact_reward = env->contact_reward > 0.0f ? env->contact_reward : 0.004f;
    env->goal_reward = env->goal_reward > 0.0f ? env->goal_reward : 2.25f;
    env->target_goals = env->target_goals > 0 ? env->target_goals : 5;
    env->max_steps = env->max_steps > 0 ? env->max_steps : 720;
    env->substeps = env->substeps > 0 ? env->substeps : 1;
    b3p_create_world(env);
}

static inline void b3p_compute_observations(Box3DPusher* env) {
    float inv_half = 1.0f / fmaxf(env->arena_half, 0.001f);
    float inv_speed = 1.0f / fmaxf(env->max_speed, 0.001f);
    float inv_diag = 1.0f / fmaxf(1.41421356f * env->arena_half, 0.001f);

    for (int i = 0; i < env->num_agents; i++) {
        float* obs = env->observations + i * B3P_OBS_SIZE;
        b3Pos pp = b3Body_GetPosition(env->players[i]);
        b3Vec3 pv = b3Body_GetLinearVelocity(env->players[i]);
        float plx = pp.x - env->center_x[i];
        float plz = pp.z - env->center_z[i];
        float speed = sqrtf(b3p_len2(pv.x, pv.z));

        int k = 0;
        obs[k++] = plx * inv_half;
        obs[k++] = plz * inv_half;
        obs[k++] = pv.x * inv_speed;
        obs[k++] = pv.z * inv_speed;
        obs[k++] = speed * inv_speed;
        obs[k++] = env->goal_x[i] * inv_half;
        obs[k++] = env->goal_z[i] * inv_half;
        obs[k++] = (env->goal_x[i] - plx) * inv_half;
        obs[k++] = (env->goal_z[i] - plz) * inv_half;

        float nearest_puck_d2 = 1.0e20f;
        int nearest_puck = 0;
        for (int p = 0; p < B3P_PUCKS; p++) {
            int idx = b3p_puck_index(i, p);
            b3Pos kp = b3Body_GetPosition(env->pucks[idx]);
            float klx = kp.x - env->center_x[i];
            float klz = kp.z - env->center_z[i];
            float dx = klx - plx;
            float dz = klz - plz;
            float d2 = b3p_len2(dx, dz);
            if (d2 < nearest_puck_d2) {
                nearest_puck_d2 = d2;
                nearest_puck = p;
            }
        }

        for (int p = 0; p < B3P_PUCKS; p++) {
            int idx = b3p_puck_index(i, p);
            b3Pos kp = b3Body_GetPosition(env->pucks[idx]);
            b3Vec3 kv = b3Body_GetLinearVelocity(env->pucks[idx]);
            float klx = kp.x - env->center_x[i];
            float klz = kp.z - env->center_z[i];
            float pdx = klx - plx;
            float pdz = klz - plz;
            float gdx = env->goal_x[i] - klx;
            float gdz = env->goal_z[i] - klz;
            float gd = sqrtf(b3p_len2(gdx, gdz));
            obs[k++] = pdx * inv_half;
            obs[k++] = pdz * inv_half;
            obs[k++] = kv.x * inv_speed;
            obs[k++] = kv.z * inv_speed;
            obs[k++] = gdx * inv_half;
            obs[k++] = gdz * inv_half;
            obs[k++] = gd * inv_diag;
        }

        int nearest_bumper = 0;
        float nearest_bumper_d2 = 1.0e20f;
        for (int b = 0; b < B3P_BUMPERS; b++) {
            float dx = B3P_BUMPER_POS[b][0] - plx;
            float dz = B3P_BUMPER_POS[b][1] - plz;
            float d2 = b3p_len2(dx, dz);
            if (d2 < nearest_bumper_d2) {
                nearest_bumper_d2 = d2;
                nearest_bumper = b;
            }
        }
        obs[k++] = (float)nearest_puck * (1.0f / (float)B3P_PUCKS);
        obs[k++] = sqrtf(nearest_puck_d2) * inv_diag;
        obs[k++] = (B3P_BUMPER_POS[nearest_bumper][0] - plx) * inv_half;
        obs[k++] = (B3P_BUMPER_POS[nearest_bumper][1] - plz) * inv_half;
        obs[k++] = sqrtf(nearest_bumper_d2) * inv_diag;
        obs[k++] = (env->arena_half - fabsf(plx)) * inv_half;
        obs[k++] = (env->arena_half - fabsf(plz)) * inv_half;
        obs[k++] = (float)env->age[i] / (float)env->max_steps;
        obs[k++] = (float)env->goals[i] / (float)env->target_goals;
        obs[k++] = b3p_clamp(env->prev_nearest_dist[i] * inv_diag, 0.0f, 1.0f);
        obs[k++] = b3p_clamp((float)i / (float)B3P_MAX_AGENTS, 0.0f, 1.0f);
        obs[k++] = b3p_clamp((env->arena_half - fmaxf(fabsf(plx), fabsf(plz))) * inv_half, 0.0f, 1.0f);
        obs[k++] = 1.0f;
        while (k < B3P_OBS_SIZE) obs[k++] = 0.0f;
    }
}

static inline void c_reset(Box3DPusher* env) {
    memset(&env->log, 0, sizeof(env->log));
    for (int i = 0; i < env->num_agents; i++) {
        env->rewards[i] = 0.0f;
        env->terminals[i] = 0.0f;
        b3p_reset_agent(env, i);
    }
    b3p_compute_observations(env);
}

static inline void c_step(Box3DPusher* env) {
    B3P_PROF_START(t_apply);
    for (int i = 0; i < env->num_agents; i++) {
        env->rewards[i] = -0.001f;
        env->terminals[i] = 0.0f;
        float dx, dz;
        b3p_dir((int)env->actions[i * B3P_NUM_ATNS], &dx, &dz);
        int throttle = (int)env->actions[i * B3P_NUM_ATNS + 1];
        if (throttle == 0) {
            b3Vec3 v = b3Body_GetLinearVelocity(env->players[i]);
            b3Body_SetLinearVelocity(env->players[i], (b3Vec3){v.x * env->brake_mult, 0.0f, v.z * env->brake_mult});
        } else {
            float mult = throttle == 2 ? env->boost_mult : 1.0f;
            b3Body_ApplyForceToCenter(env->players[i], (b3Vec3){dx * env->force * mult, 0.0f, dz * env->force * mult}, true);
        }
    }
    B3P_PROF_ADD(prof_apply_ns, t_apply);

    B3P_PROF_START(t_step);
    b3World_Step(env->world, B3P_DT, env->substeps);
    B3P_PROF_ADD(prof_step_ns, t_step);

    B3P_PROF_START(t_post);
    b3Profile profile = {0};
    b3Counters counters = {0};
    int have_world_stats = 0;

    for (int i = 0; i < env->num_agents; i++) {
        b3Pos pp = b3Body_GetPosition(env->players[i]);
        b3Vec3 pv = b3Body_GetLinearVelocity(env->players[i]);
        float plx = pp.x - env->center_x[i];
        float plz = pp.z - env->center_z[i];
        float player_speed = sqrtf(b3p_len2(pv.x, pv.z));
        float reward = -0.001f - 0.00025f * player_speed;
        float nearest_goal_dist = 1.0e20f;
        float nearest_player_puck_d2 = 1.0e20f;
        float puck_speed_sum = 0.0f;
        int goals_this_step = 0;

        float wall_margin = env->arena_half - fmaxf(fabsf(plx), fabsf(plz));
        if (wall_margin < env->player_radius + 0.08f) {
            reward -= 0.006f;
            env->log.wall_hits += 1.0f;
        }

        for (int b = 0; b < B3P_BUMPERS; b++) {
            float bx = B3P_BUMPER_POS[b][0] - plx;
            float bz = B3P_BUMPER_POS[b][1] - plz;
            float hit = env->bumper_radius + env->player_radius + 0.05f;
            if (b3p_len2(bx, bz) < hit * hit) {
                reward -= 0.004f;
                env->log.bumper_hits += 1.0f;
                break;
            }
        }

        for (int p = 0; p < B3P_PUCKS; p++) {
            int idx = b3p_puck_index(i, p);
            b3Pos kp = b3Body_GetPosition(env->pucks[idx]);
            b3Vec3 kv = b3Body_GetLinearVelocity(env->pucks[idx]);
            float klx = kp.x - env->center_x[i];
            float klz = kp.z - env->center_z[i];
            float gdx = env->goal_x[i] - klx;
            float gdz = env->goal_z[i] - klz;
            float gd = sqrtf(b3p_len2(gdx, gdz));
            float progress = env->puck_prev_goal_dist[idx] - gd;
            reward += env->progress_reward * progress;
            env->puck_prev_goal_dist[idx] = gd;
            if (gd < nearest_goal_dist) nearest_goal_dist = gd;

            if (gd > 0.001f) {
                float toward_goal_speed = (kv.x * gdx + kv.z * gdz) / gd;
                if (toward_goal_speed > 0.0f) {
                    reward += env->push_reward * toward_goal_speed * B3P_DT;
                }
            }

            float cvx = klx - plx;
            float cvz = klz - plz;
            float player_puck_d2 = b3p_len2(cvx, cvz);
            if (player_puck_d2 < nearest_player_puck_d2) nearest_player_puck_d2 = player_puck_d2;

            float contact = env->player_radius + env->puck_radius + 0.08f;
            if (player_puck_d2 < contact * contact) {
                reward += env->contact_reward;
                env->log.player_contacts += 1.0f;
            }

            float puck_speed = sqrtf(b3p_len2(kv.x, kv.z));
            puck_speed_sum += puck_speed;
            if (gd < env->goal_radius) {
                reward += env->goal_reward;
                env->goals[i]++;
                goals_this_step++;
                b3p_reset_puck(env, i, p);
            }
        }

        env->age[i]++;
        if (goals_this_step > 0) {
            nearest_goal_dist = b3p_nearest_puck_goal_dist(env, i);
        }
        float nearest_player_puck_dist = goals_this_step > 0
            ? b3p_nearest_player_puck_dist(env, i)
            : sqrtf(nearest_player_puck_d2);
        if (goals_this_step == 0) {
            float approach_progress = env->prev_nearest_player_puck_dist[i] - nearest_player_puck_dist;
            if (approach_progress > 0.0f) {
                reward += env->approach_reward * approach_progress;
            }
        }
        env->prev_nearest_dist[i] = nearest_goal_dist;
        env->prev_nearest_player_puck_dist[i] = nearest_player_puck_dist;
        int solved = env->goals[i] >= env->target_goals;
        int timed_out = env->age[i] >= env->max_steps;
        env->rewards[i] = reward;
        env->episode_return_accum[i] += reward;

        if (solved || timed_out) {
            if (!have_world_stats) {
                profile = b3World_GetProfile(env->world);
                counters = b3World_GetCounters(env->world);
                have_world_stats = 1;
            }
            float goal_frac = b3p_clamp((float)env->goals[i] / (float)env->target_goals, 0.0f, 1.0f);
            float speed_bonus = solved ? b3p_clamp(1.0f - (float)env->age[i] / (float)env->max_steps, 0.0f, 1.0f) : 0.0f;
            float perf = b3p_clamp(0.75f * goal_frac + 0.25f * speed_bonus, 0.0f, 1.0f);
            env->log.perf += perf;
            env->log.score += (float)env->goals[i] * 100.0f + speed_bonus * 100.0f;
            env->log.episode_return += env->episode_return_accum[i];
            env->log.episode_length += (float)env->age[i];
            env->log.goals += (float)env->goals[i];
            env->log.timeouts += timed_out ? 1.0f : 0.0f;
            env->log.puck_speed += puck_speed_sum / (float)B3P_PUCKS;
            env->log.distance += nearest_goal_dist;
            env->log.box3d_step_ms += profile.step;
            env->log.body_count += (float)counters.bodyCount;
            env->log.contact_count += (float)counters.contactCount;
            env->log.n += 1.0f;
            env->terminals[i] = 1.0f;
            b3p_reset_agent(env, i);
        } else if (goals_this_step > 0) {
            env->log.goals += (float)goals_this_step;
        }
    }

    b3p_compute_observations(env);
    B3P_PROF_ADD(prof_post_ns, t_post);
}

static inline void c_close(Box3DPusher* env) {
    if (b3World_IsValid(env->world)) {
        b3DestroyWorld(env->world);
    }
}
