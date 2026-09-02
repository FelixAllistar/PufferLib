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
    int next[AR_MAX_ENEMIES];
    int dense[AR_MAX_ENEMIES];
    int dense_pos[AR_MAX_ENEMIES];
} AREnemyPool;

typedef struct {
    uint8_t active[AR_MAX_PETS];
    float x[AR_MAX_PETS];
    float y[AR_MAX_PETS];
    float vx[AR_MAX_PETS];
    float vy[AR_MAX_PETS];
    float hp[AR_MAX_PETS];
    float max_hp[AR_MAX_PETS];
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
    int invuln_timer;
    int tick;

    ARPetPool pets;
    AREnemyPool enemies;
    int enemy_count;
    int next_enemy_slot;
    int pets_alive;
    int spawn_timer;
    int nearest_enemy;

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
