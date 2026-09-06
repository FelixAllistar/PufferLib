#pragma once

// CPU-only environment state. Struct-of-arrays inside one Env so ar_sim.h can
// share gameplay code with the CUDA backend via the AR_ accessor macros.
#ifndef AR_GPU_SIM

#include "pufferenv.h"
#include "ar_constants.h"
#include "ar_log.h"
#include "ar_physics.h"

typedef struct {
    uint8_t active[AR_MAX_ENEMIES];
    uint8_t type[AR_MAX_ENEMIES];
    float x[AR_MAX_ENEMIES];
    float y[AR_MAX_ENEMIES];
    float vx[AR_MAX_ENEMIES];
    float vy[AR_MAX_ENEMIES];
    float hp[AR_MAX_ENEMIES];
    float max_hp[AR_MAX_ENEMIES];
    float radius[AR_MAX_ENEMIES];
    float speed[AR_MAX_ENEMIES];
    float damage[AR_MAX_ENEMIES];
    int invuln[AR_MAX_ENEMIES];
    int slow_timer[AR_MAX_ENEMIES];  // frost slow ticks remaining
    int next[AR_MAX_ENEMIES];
    int dense[AR_MAX_ENEMIES];
    int dense_pos[AR_MAX_ENEMIES];
} AREnemyPool;

typedef struct {
    uint8_t active[AR_MAX_PETS];
    uint8_t kind[AR_MAX_PETS];
    float x[AR_MAX_PETS];
    float y[AR_MAX_PETS];
    float vx[AR_MAX_PETS];
    float vy[AR_MAX_PETS];
    float hp[AR_MAX_PETS];
    float max_hp[AR_MAX_PETS];
    float spd[AR_MAX_PETS];
    float dmg[AR_MAX_PETS];
    float rad[AR_MAX_PETS];
    float cd[AR_MAX_PETS];
    float age[AR_MAX_PETS];
    int invuln[AR_MAX_PETS];
    int target[AR_MAX_PETS];
    uint8_t attacking[AR_MAX_PETS];
} ARPetPool;

struct Env {
    Log log;
    Agent agents[1];
    int tag, boundary_reached;
    void* client;
    int num_agents;
    uint32_t rng;

    ARConfig cfg;
    int show_hitboxes;

    // Player.
    float px, py, pvx, pvy, hp, max_hp;
    int facing_left;
    float summon_cd;
    float dash_cd, nova_cd, frost_cd;
    float fx_nova, fx_frost, fx_dash;  // visual-only event timers (seconds)
    int order;                          // AROrder for the squad
    int pick_class;                     // viewer-selected summon class (human only)
    int invuln_timer;
    int tick;

    // RTS economy: spendable shards, harvest nodes, player buildings.
    float shards;
    uint8_t shard_active[AR_MAX_SHARDS];
    float shard_x[AR_MAX_SHARDS];
    float shard_y[AR_MAX_SHARDS];
    float shard_value[AR_MAX_SHARDS];
    int shard_count;
    uint8_t build_active[AR_MAX_BUILDINGS];
    uint8_t build_kind[AR_MAX_BUILDINGS];
    float build_x[AR_MAX_BUILDINGS];
    float build_y[AR_MAX_BUILDINGS];
    float build_hp[AR_MAX_BUILDINGS];
    float build_max_hp[AR_MAX_BUILDINGS];
    float build_rad[AR_MAX_BUILDINGS];
    float build_flash[AR_MAX_BUILDINGS];
    float build_cd[AR_MAX_BUILDINGS];
    float build_hurtcd[AR_MAX_BUILDINGS];
    int builds_alive;

    ARPetPool pets;
    AREnemyPool enemies;
    int enemy_count;
    int next_enemy_slot;
    int pets_alive;
    int spawn_timer;
    int nearest_enemy;

    // Procedural dungeon: 1 = floor, 0 = solid. Cell size is
    // arena_size / AR_DUN_W world units, centered on the origin.
    uint8_t dungeon[AR_DUN_CELLS];
    uint32_t dungeon_seed;

    // Static pillar obstacles.
    uint8_t obstacle_active[AR_MAX_OBSTACLES];
    float obstacle_x[AR_MAX_OBSTACLES];
    float obstacle_y[AR_MAX_OBSTACLES];
    float obstacle_radius[AR_MAX_OBSTACLES];

    // Whole-arena uniform grid for the analytic separation path (CUDA). The
    // CPU path has box3d do the separation instead. Singly linked per enemy.
    int grid_head[AR_GRID_CELLS];

    float episode_return;
    float episode_reward_survival;
    float episode_reward_kill;
    float episode_reward_damage;
    float episode_reward_hurt;
    float episode_reward_summon;
    float episode_reward_terminal;
    float episode_kills;
    float episode_summons;
    float episode_pets_lost;
    float episode_damage_dealt;
    float episode_damage_taken;
    float episode_peak_enemies;
    float episode_min_hp;

    // box3d handles. One world per env, created on first reset and reused;
    // enemies and pets own bodies for their lifetime.
    b3WorldId world;
    b3BodyId player_body;
    b3BodyId pet_body[AR_MAX_PETS];
    b3BodyId enemy_body[AR_MAX_ENEMIES];
    b3BodyId obstacle_body[AR_MAX_OBSTACLES];
};

typedef Env ARPG;

#endif  // !AR_GPU_SIM
