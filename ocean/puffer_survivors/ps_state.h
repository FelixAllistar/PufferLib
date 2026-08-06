#pragma once

#include "ps_defs.h"

typedef struct {
    uint8_t active[PS_MAX_ENEMIES];
    uint8_t type[PS_MAX_ENEMIES];
    float x[PS_MAX_ENEMIES];
    float y[PS_MAX_ENEMIES];
    float vx[PS_MAX_ENEMIES];
    float vy[PS_MAX_ENEMIES];
    float hp[PS_MAX_ENEMIES];
    float max_hp[PS_MAX_ENEMIES];
    float radius[PS_MAX_ENEMIES];
    float bound_radius[PS_MAX_ENEMIES];
    float half_width[PS_MAX_ENEMIES];
    float half_height[PS_MAX_ENEMIES];
    float speed[PS_MAX_ENEMIES];
    float damage[PS_MAX_ENEMIES];
    uint8_t shape[PS_MAX_ENEMIES];
    int next[PS_MAX_ENEMIES];
    int dense[PS_MAX_ENEMIES];
    int dense_pos[PS_MAX_ENEMIES];
} PSEnemyPool;

typedef struct {
    uint8_t active[PS_MAX_PROJECTILES];
    uint8_t type[PS_MAX_PROJECTILES];
    float x[PS_MAX_PROJECTILES];
    float y[PS_MAX_PROJECTILES];
    float vx[PS_MAX_PROJECTILES];
    float vy[PS_MAX_PROJECTILES];
    float damage[PS_MAX_PROJECTILES];
    float radius[PS_MAX_PROJECTILES];
    int ttl[PS_MAX_PROJECTILES];
    int pierce[PS_MAX_PROJECTILES];
    int dense[PS_MAX_PROJECTILES];
    int dense_pos[PS_MAX_PROJECTILES];
} PSProjectilePool;

typedef struct {
    uint8_t active[PS_MAX_DROPS];
    uint8_t type[PS_MAX_DROPS];
    float x[PS_MAX_DROPS];
    float y[PS_MAX_DROPS];
    float value[PS_MAX_DROPS];
    int dense[PS_MAX_DROPS];
    int dense_pos[PS_MAX_DROPS];
} PSDropPool;

typedef struct {
    uint8_t active[PS_MAX_AREAS];
    uint8_t type[PS_MAX_AREAS];
    float x[PS_MAX_AREAS];
    float y[PS_MAX_AREAS];
    float radius[PS_MAX_AREAS];
    float damage[PS_MAX_AREAS];
    int ttl[PS_MAX_AREAS];
    int tick_rate[PS_MAX_AREAS];
    int tick_timer[PS_MAX_AREAS];
    int dense[PS_MAX_AREAS];
    int dense_pos[PS_MAX_AREAS];
} PSAreaPool;

typedef struct {
    uint8_t type[PS_MAX_OBSTACLES];
    float x[PS_MAX_OBSTACLES];
    float y[PS_MAX_OBSTACLES];
    float radius[PS_MAX_OBSTACLES];
} PSObstaclePool;

typedef struct {
    uint8_t active[PS_MAX_MOVING_OBSTACLES];
    uint8_t type[PS_MAX_MOVING_OBSTACLES];
    uint8_t shape[PS_MAX_MOVING_OBSTACLES];
    float x[PS_MAX_MOVING_OBSTACLES];
    float y[PS_MAX_MOVING_OBSTACLES];
    float vx[PS_MAX_MOVING_OBSTACLES];
    float vy[PS_MAX_MOVING_OBSTACLES];
    float bound_radius[PS_MAX_MOVING_OBSTACLES];
    float half_width[PS_MAX_MOVING_OBSTACLES];
    float half_height[PS_MAX_MOVING_OBSTACLES];
    int ttl[PS_MAX_MOVING_OBSTACLES];
    int dense[PS_MAX_MOVING_OBSTACLES];
    int dense_pos[PS_MAX_MOVING_OBSTACLES];
} PSMovingObstaclePool;

struct Env {
    Log log;
    Agent agents[1];
    int tag, boundary_reached;
    void* client;
    int num_agents;
    uint32_t rng;

    PSConfig cfg;
    int show_hitboxes;

    float px, py, pvx, pvy, hp, max_hp, xp;
    int player_facing_left;
    float speed_bonus, damage_bonus, cooldown_mult, projectile_speed_bonus;
    float magnet_bonus, area_bonus;
    int level, pierce_bonus, pending_upgrade, queued_upgrades, last_boss_tick;
    int offered[PS_UPGRADE_SLOTS];
    float weapon_cd[PS_WEAPON_COUNT];
    float weapon_active[PS_WEAPON_COUNT];
    int weapon_level[PS_WEAPON_COUNT];
    float orbit_phase;
    int tick, invuln_timer;

    PSEnemyPool enemies;
    PSProjectilePool projectiles;
    PSDropPool drops;
    PSAreaPool areas;
    PSObstaclePool obstacles;
    PSMovingObstaclePool moving_obstacles;
    int grid_head[PS_GRID_CELLS];
    int grid_touched[PS_MAX_ENEMIES];
    int grid_touched_count;
    int aabb_indices[PS_MAX_ENEMIES];
    int aabb_count;
    int nearest_enemy;
    float nearest_enemy_d2;
    int enemy_count;
    int projectile_count;
    int drop_count;
    int area_count;
    int moving_obstacle_count;
    int active_ink_count;
    int next_enemy_slot;
    int next_projectile_slot;
    int next_drop_slot;
    int next_area_slot;
    int next_moving_obstacle_slot;

    float episode_return;
    float episode_reward_survival;
    float episode_reward_damage;
    float episode_reward_kill;
    float episode_reward_hurt;
    float episode_reward_pickup;
    float episode_reward_xp;
    float episode_reward_levelup;
    float episode_reward_obstacle;
    float episode_reward_terminal;
    float episode_score;
    float episode_kills;
    float episode_xp;
    float episode_damage_dealt;
    float episode_damage_taken;
    float episode_pickups;
    float episode_levelups;
    float episode_obstacle_hits;
    float episode_peak_enemies;
    float episode_peak_projectiles;
    float episode_min_hp;
};

typedef Env PufferSurvivors;

static inline uint32_t ps_rand_u32(PufferSurvivors* env) {
    uint32_t x = env->rng ? env->rng : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    env->rng = x ? x : 1u;
    return env->rng;
}

static inline float ps_randf(PufferSurvivors* env) {
    return (float)(ps_rand_u32(env) & 0x00ffffffu) / 16777216.0f;
}

static inline int ps_cell(PufferSurvivors* env, float x, float y) {
    float half = 0.5f * env->cfg.arena_size;
    int gx = (int)((((x - env->px) + half) / env->cfg.arena_size) * (float)PS_GRID_W);
    int gy = (int)((((y - env->py) + half) / env->cfg.arena_size) * (float)PS_GRID_H);
    gx = gx < 0 ? 0 : (gx >= PS_GRID_W ? PS_GRID_W - 1 : gx);
    gy = gy < 0 ? 0 : (gy >= PS_GRID_H ? PS_GRID_H - 1 : gy);
    return gy * PS_GRID_W + gx;
}

static inline int ps_count_enemies(PufferSurvivors* env) {
    return env->enemy_count;
}

static inline int ps_count_projectiles(PufferSurvivors* env) {
    return env->projectile_count;
}

static inline int ps_count_drops(PufferSurvivors* env) {
    return env->drop_count;
}

static inline int ps_count_areas(PufferSurvivors* env) {
    return env->area_count;
}

static inline void ps_count_entities(PufferSurvivors* env, int* enemies, int* projectiles, int* drops) {
    *enemies = ps_count_enemies(env);
    *projectiles = ps_count_projectiles(env);
    *drops = ps_count_drops(env);
}
