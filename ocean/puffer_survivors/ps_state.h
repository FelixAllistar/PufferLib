#pragma once

#include "pufferenv.h"
#include "ps_constants.h"
#include "ps_log.h"

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
    float slow[PS_MAX_ENEMIES];
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
    uint8_t active[PS_AREA_STORAGE_CAP];
    uint8_t type[PS_AREA_STORAGE_CAP];
    float x[PS_AREA_STORAGE_CAP];
    float y[PS_AREA_STORAGE_CAP];
    float radius[PS_AREA_STORAGE_CAP];
    float damage[PS_AREA_STORAGE_CAP];
    int ttl[PS_AREA_STORAGE_CAP];
    int tick_rate[PS_AREA_STORAGE_CAP];
    int tick_timer[PS_AREA_STORAGE_CAP];
    int dense[PS_AREA_STORAGE_CAP];
    int dense_pos[PS_AREA_STORAGE_CAP];
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
    float frost_aim;
    float dash_cd;
    int dash_timer;
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
