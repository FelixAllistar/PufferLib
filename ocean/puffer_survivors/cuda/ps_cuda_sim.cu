// Puffer Survivors CUDA simulator implementation.
// -----------------------------------------------------------------------------
// The Puffer-facing adapter lives in ps_cuda_vec.cu. This file owns only the
// simulator state, kernels, allocation, reset/step API, and device log reduction.
// It is compiled as its own CUDA translation unit; no implementation .cu file is
// textually included by another source file.
//
// Storage is structure-of-arrays across environments. One CUDA thread currently
// owns one complete environment step, giving coalesced same-field accesses across
// a warp while preserving straightforward CPU/CUDA logic comparison.
// -----------------------------------------------------------------------------

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../ps_constants.h"
#include "../ps_config.h"
#include "../ps_log.h"
#include "../ps_observation_layout.h"
#include "ps_cuda_sim.cuh"

#ifndef PS_CUDA_BLOCK_SIZE
#define PS_CUDA_BLOCK_SIZE 256
#endif

// Default layout is env-major because most RL tensors are [num_envs, features].
// For better coalesced observation writes from one-thread-per-env, compile with:
//   -DPS_CUDA_OBS_ENV_MAJOR=0 -DPS_CUDA_ACTION_ENV_MAJOR=0
#ifndef PS_CUDA_OBS_ENV_MAJOR
#define PS_CUDA_OBS_ENV_MAJOR 1
#endif
#ifndef PS_CUDA_ACTION_ENV_MAJOR
#define PS_CUDA_ACTION_ENV_MAJOR 1
#endif

#define PS_HD __host__ __device__ __forceinline__
#define PS_D  __device__ __forceinline__
#define PS_EIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_PIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_DIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_AIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_OIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_GIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_WIDX(sim, weapon, env) ((weapon) * (sim).num_envs + (env))
#define PS_UIDX(sim, slot, env) ((slot) * (sim).num_envs + (env))

// -----------------------------------------------------------------------------
// Enums and static content
// -----------------------------------------------------------------------------

typedef struct {
    float hp;
    float speed_mult;
    float radius;
    float damage;
} PSEnemyDef;

typedef struct {
    float base_cd;
    float cd_per_level;
    float base_damage;
    float damage_per_level;
    float base_radius;
    float radius_per_level;
} PSWeaponDef;

__device__ __constant__ PSEnemyDef PS_D_ENEMY_DEFS[4] = {
    {2.0f, 1.00f, 0.42f, 1.0f},
    {1.0f, 1.28f, 0.34f, 1.0f},
    {4.0f, 0.76f, 0.52f, 1.0f},
    {3.0f, 0.92f, 0.46f, 1.0f},
};

__device__ __constant__ PSWeaponDef PS_D_WEAPON_DEFS[PS_WEAPON_COUNT] = {
    {16.0f, -0.8f, 1.15f, 0.22f, 0.30f, 0.015f},
    {24.0f, -1.8f, 0.90f, 0.34f, 2.20f, 0.24f},
    {10.0f, -0.7f, 1.15f, 0.42f, 0.44f, 0.04f},
    {96.0f, -6.0f, 0.95f, 0.34f, 1.35f, 0.18f},
    {135.0f, -7.0f, 1.00f, 0.38f, 4.85f, 0.52f},
};

__device__ __constant__ int PS_D_WAVE_MINS[24] = {
    10, 16, 26, 36, 32, 26, 48, 58, 70, 46, 68, 86,
    96, 108, 80, 104, 126, 94, 116, 138, 154, 168, 180, 192,
};

__device__ __constant__ int PS_D_WAVE_INTERVALS[24] = {
    74, 68, 48, 30, 68, 62, 40, 38, 88, 40, 34, 24,
    24, 34, 18, 16, 15, 54, 34, 28, 16, 14, 13, 12,
};

// -----------------------------------------------------------------------------
// Config and simulator SoA
// -----------------------------------------------------------------------------



struct PSCudaSim {
    int num_envs;
    PSConfig cfg;
    int owns_io;

    // External tensors. Default layout:
    //   observations: [num_envs, PS_OBS_SIZE]
    //   actions:      [num_envs, 2]
    //   rewards:      [num_envs]
    //   terminals:    [num_envs]
    float* observations;
    float* actions;
    float* rewards;
    float* terminals;

    // Scalar state, SoA over envs.
    uint32_t* rng;
    float *px, *py, *pvx, *pvy, *hp, *max_hp, *xp;
    int *player_facing_left;
    float *speed_bonus, *damage_bonus, *cooldown_mult, *projectile_speed_bonus;
    float *magnet_bonus, *area_bonus;
    int *level, *pierce_bonus, *pending_upgrade, *queued_upgrades, *last_boss_tick;
    int *offered;          // [PS_UPGRADE_SLOTS, N]
    float *weapon_cd;      // [PS_WEAPON_COUNT, N]
    float *weapon_active;  // [PS_WEAPON_COUNT, N]
    int *weapon_level;     // [PS_WEAPON_COUNT, N]
    float *orbit_phase;
    int *tick, *invuln_timer;

    // Enemy pool [PS_MAX_ENEMIES, N]
    uint8_t *enemy_active, *enemy_type;
    float *enemy_x, *enemy_y, *enemy_vx, *enemy_vy;
    float *enemy_hp, *enemy_max_hp, *enemy_radius, *enemy_speed, *enemy_damage;
    int *enemy_next;

    // Projectile pool [PS_MAX_PROJECTILES, N]
    uint8_t *projectile_active, *projectile_type;
    float *projectile_x, *projectile_y, *projectile_vx, *projectile_vy;
    float *projectile_damage, *projectile_radius;
    int *projectile_ttl, *projectile_pierce;
    int *projectile_dense, *projectile_dense_pos;

    // Drop pool [PS_MAX_DROPS, N]
    uint8_t *drop_active, *drop_type;
    float *drop_x, *drop_y, *drop_value;
    int *drop_dense, *drop_dense_pos;

    // Area pool [PS_MAX_AREAS, N]
    uint8_t *area_active, *area_type;
    float *area_x, *area_y, *area_radius, *area_damage;
    int *area_ttl, *area_tick_rate, *area_tick_timer;
    int *area_dense, *area_dense_pos;

    // Obstacles [PS_MAX_OBSTACLES, N]
    uint8_t* obstacle_type;
    float *obstacle_x, *obstacle_y, *obstacle_radius;

    // Grid [PS_GRID_CELLS, N]
    int* grid_head;
    int* grid_touched;       // [PS_MAX_ENEMIES, N]
    int* grid_touched_count; // [N]

    // Counts/cursors.
    int* nearest_enemy;
    float* nearest_enemy_d2;
    int *enemy_count, *projectile_count, *drop_count, *area_count;
    int *next_enemy_slot, *next_projectile_slot, *next_drop_slot, *next_area_slot;

    // Episode stats.
    float *episode_return, *episode_score, *episode_kills, *episode_xp;
    float *episode_damage_dealt, *episode_damage_taken, *episode_pickups;
    float *episode_levelups, *episode_obstacle_hits;

    // Accumulated logs over completed episodes, SoA fields.
    float *log_perf, *log_score, *log_episode_return, *log_episode_length;
    float *log_kills, *log_level, *log_xp, *log_damage_dealt, *log_damage_taken;
    float *log_pickups, *log_levelups, *log_obstacle_hits;
    float *log_enemies_alive, *log_projectiles_alive, *log_drops_alive, *log_areas_alive;
    float *log_weapon_levels, *log_wave, *log_hp, *log_survived, *log_n;
    Log* log_reduced;
};

// -----------------------------------------------------------------------------
// Host utilities
// -----------------------------------------------------------------------------

static inline void ps_cuda_check(cudaError_t err, const char* expr, const char* file, int line) {
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA error %s at %s:%d: %s\n", expr, file, line, cudaGetErrorString(err));
        std::abort();
    }
}
#define PS_CUDA_CHECK(expr) ps_cuda_check((expr), #expr, __FILE__, __LINE__)

static inline PSConfig ps_sanitize_config(PSConfig cfg) {
    if (cfg.arena_size <= 0.0f) cfg.arena_size = 48.0f;
    cfg.enemy_cap = cfg.enemy_cap < 1 ? 1 : (cfg.enemy_cap > PS_MAX_ENEMIES ? PS_MAX_ENEMIES : cfg.enemy_cap);
    cfg.projectile_cap = cfg.projectile_cap < 1 ? 1 : (cfg.projectile_cap > PS_MAX_PROJECTILES ? PS_MAX_PROJECTILES : cfg.projectile_cap);
    cfg.drop_cap = cfg.drop_cap < 1 ? 1 : (cfg.drop_cap > PS_MAX_DROPS ? PS_MAX_DROPS : cfg.drop_cap);
    cfg.obstacle_count = cfg.obstacle_count < 0 ? 0 : (cfg.obstacle_count > PS_MAX_OBSTACLES ? PS_MAX_OBSTACLES : cfg.obstacle_count);
    cfg.wave_length_steps = cfg.wave_length_steps < 60 ? 60 : cfg.wave_length_steps;
    cfg.player_health = cfg.player_health < 1.0f ? 1.0f : cfg.player_health;
    cfg.projectile_damage = cfg.projectile_damage <= 0.0f ? 1.0f : cfg.projectile_damage;
    cfg.fire_cooldown = cfg.fire_cooldown <= 0.0f ? 22.0f : cfg.fire_cooldown;
    cfg.health_heal = cfg.health_heal < 1.0f ? 1.0f : cfg.health_heal;
    cfg.invuln_steps = cfg.invuln_steps < 0 ? 0 : cfg.invuln_steps;
    cfg.enemy_obstacle_stride = cfg.enemy_obstacle_stride < 1 ? 1 : cfg.enemy_obstacle_stride;
    return cfg;
}

#define PS_ALLOC_FIELD(sim, field, count) \
    PS_CUDA_CHECK(cudaMalloc((void**)&((sim)->field), sizeof(*((sim)->field)) * (size_t)(count)))
#define PS_ZERO_FIELD(sim, field, count) \
    PS_CUDA_CHECK(cudaMemset((sim)->field, 0, sizeof(*((sim)->field)) * (size_t)(count)))

static inline void ps_cuda_alloc(PSCudaSim* sim, int num_envs, PSConfig cfg) {
    if (num_envs <= 0) {
        std::fprintf(stderr, "num_envs must be positive\n");
        std::abort();
    }
    std::memset(sim, 0, sizeof(*sim));
    sim->num_envs = num_envs;
    sim->cfg = ps_sanitize_config(cfg);
    sim->owns_io = 1;

    const size_t N = (size_t)num_envs;
    const size_t NE = (size_t)sim->cfg.enemy_cap * N;
    const size_t NP = (size_t)sim->cfg.projectile_cap * N;
    const size_t ND = (size_t)sim->cfg.drop_cap * N;
    const size_t NA = (size_t)PS_MAX_AREAS * N;
    const size_t NO = (size_t)(sim->cfg.obstacle_count > 0 ? sim->cfg.obstacle_count : 1) * N;
    const size_t NG = (size_t)PS_GRID_CELLS * N;
    const size_t NGT = (size_t)sim->cfg.enemy_cap * N;

    PS_ALLOC_FIELD(sim, observations, N * PS_OBS_SIZE);
    PS_ALLOC_FIELD(sim, actions, N * 2);
    PS_ALLOC_FIELD(sim, rewards, N);
    PS_ALLOC_FIELD(sim, terminals, N);

    PS_ALLOC_FIELD(sim, rng, N);
    PS_ALLOC_FIELD(sim, px, N); PS_ALLOC_FIELD(sim, py, N); PS_ALLOC_FIELD(sim, pvx, N); PS_ALLOC_FIELD(sim, pvy, N);
    PS_ALLOC_FIELD(sim, hp, N); PS_ALLOC_FIELD(sim, max_hp, N); PS_ALLOC_FIELD(sim, xp, N);
    PS_ALLOC_FIELD(sim, player_facing_left, N);
    PS_ALLOC_FIELD(sim, speed_bonus, N); PS_ALLOC_FIELD(sim, damage_bonus, N); PS_ALLOC_FIELD(sim, cooldown_mult, N);
    PS_ALLOC_FIELD(sim, projectile_speed_bonus, N); PS_ALLOC_FIELD(sim, magnet_bonus, N); PS_ALLOC_FIELD(sim, area_bonus, N);
    PS_ALLOC_FIELD(sim, level, N); PS_ALLOC_FIELD(sim, pierce_bonus, N); PS_ALLOC_FIELD(sim, pending_upgrade, N);
    PS_ALLOC_FIELD(sim, queued_upgrades, N); PS_ALLOC_FIELD(sim, last_boss_tick, N);
    PS_ALLOC_FIELD(sim, offered, N * PS_UPGRADE_SLOTS);
    PS_ALLOC_FIELD(sim, weapon_cd, N * PS_WEAPON_COUNT); PS_ALLOC_FIELD(sim, weapon_active, N * PS_WEAPON_COUNT);
    PS_ALLOC_FIELD(sim, weapon_level, N * PS_WEAPON_COUNT); PS_ALLOC_FIELD(sim, orbit_phase, N);
    PS_ALLOC_FIELD(sim, tick, N); PS_ALLOC_FIELD(sim, invuln_timer, N);

    PS_ALLOC_FIELD(sim, enemy_active, NE); PS_ALLOC_FIELD(sim, enemy_type, NE);
    PS_ALLOC_FIELD(sim, enemy_x, NE); PS_ALLOC_FIELD(sim, enemy_y, NE); PS_ALLOC_FIELD(sim, enemy_vx, NE); PS_ALLOC_FIELD(sim, enemy_vy, NE);
    PS_ALLOC_FIELD(sim, enemy_hp, NE); PS_ALLOC_FIELD(sim, enemy_max_hp, NE); PS_ALLOC_FIELD(sim, enemy_radius, NE);
    PS_ALLOC_FIELD(sim, enemy_speed, NE); PS_ALLOC_FIELD(sim, enemy_damage, NE); PS_ALLOC_FIELD(sim, enemy_next, NE);

    PS_ALLOC_FIELD(sim, projectile_active, NP); PS_ALLOC_FIELD(sim, projectile_type, NP);
    PS_ALLOC_FIELD(sim, projectile_x, NP); PS_ALLOC_FIELD(sim, projectile_y, NP); PS_ALLOC_FIELD(sim, projectile_vx, NP); PS_ALLOC_FIELD(sim, projectile_vy, NP);
    PS_ALLOC_FIELD(sim, projectile_damage, NP); PS_ALLOC_FIELD(sim, projectile_radius, NP);
    PS_ALLOC_FIELD(sim, projectile_ttl, NP); PS_ALLOC_FIELD(sim, projectile_pierce, NP);
    PS_ALLOC_FIELD(sim, projectile_dense, NP); PS_ALLOC_FIELD(sim, projectile_dense_pos, NP);

    PS_ALLOC_FIELD(sim, drop_active, ND); PS_ALLOC_FIELD(sim, drop_type, ND);
    PS_ALLOC_FIELD(sim, drop_x, ND); PS_ALLOC_FIELD(sim, drop_y, ND); PS_ALLOC_FIELD(sim, drop_value, ND);
    PS_ALLOC_FIELD(sim, drop_dense, ND); PS_ALLOC_FIELD(sim, drop_dense_pos, ND);

    PS_ALLOC_FIELD(sim, area_active, NA); PS_ALLOC_FIELD(sim, area_type, NA);
    PS_ALLOC_FIELD(sim, area_x, NA); PS_ALLOC_FIELD(sim, area_y, NA); PS_ALLOC_FIELD(sim, area_radius, NA); PS_ALLOC_FIELD(sim, area_damage, NA);
    PS_ALLOC_FIELD(sim, area_ttl, NA); PS_ALLOC_FIELD(sim, area_tick_rate, NA); PS_ALLOC_FIELD(sim, area_tick_timer, NA);
    PS_ALLOC_FIELD(sim, area_dense, NA); PS_ALLOC_FIELD(sim, area_dense_pos, NA);

    PS_ALLOC_FIELD(sim, obstacle_type, NO); PS_ALLOC_FIELD(sim, obstacle_x, NO); PS_ALLOC_FIELD(sim, obstacle_y, NO); PS_ALLOC_FIELD(sim, obstacle_radius, NO);
    PS_ALLOC_FIELD(sim, grid_head, NG);
    PS_ALLOC_FIELD(sim, grid_touched, NGT); PS_ALLOC_FIELD(sim, grid_touched_count, N);

    PS_ALLOC_FIELD(sim, nearest_enemy, N); PS_ALLOC_FIELD(sim, nearest_enemy_d2, N);
    PS_ALLOC_FIELD(sim, enemy_count, N); PS_ALLOC_FIELD(sim, projectile_count, N); PS_ALLOC_FIELD(sim, drop_count, N); PS_ALLOC_FIELD(sim, area_count, N);
    PS_ALLOC_FIELD(sim, next_enemy_slot, N); PS_ALLOC_FIELD(sim, next_projectile_slot, N); PS_ALLOC_FIELD(sim, next_drop_slot, N); PS_ALLOC_FIELD(sim, next_area_slot, N);

    PS_ALLOC_FIELD(sim, episode_return, N); PS_ALLOC_FIELD(sim, episode_score, N); PS_ALLOC_FIELD(sim, episode_kills, N); PS_ALLOC_FIELD(sim, episode_xp, N);
    PS_ALLOC_FIELD(sim, episode_damage_dealt, N); PS_ALLOC_FIELD(sim, episode_damage_taken, N); PS_ALLOC_FIELD(sim, episode_pickups, N);
    PS_ALLOC_FIELD(sim, episode_levelups, N); PS_ALLOC_FIELD(sim, episode_obstacle_hits, N);

    PS_ALLOC_FIELD(sim, log_perf, N); PS_ALLOC_FIELD(sim, log_score, N); PS_ALLOC_FIELD(sim, log_episode_return, N); PS_ALLOC_FIELD(sim, log_episode_length, N);
    PS_ALLOC_FIELD(sim, log_kills, N); PS_ALLOC_FIELD(sim, log_level, N); PS_ALLOC_FIELD(sim, log_xp, N); PS_ALLOC_FIELD(sim, log_damage_dealt, N);
    PS_ALLOC_FIELD(sim, log_damage_taken, N); PS_ALLOC_FIELD(sim, log_pickups, N); PS_ALLOC_FIELD(sim, log_levelups, N); PS_ALLOC_FIELD(sim, log_obstacle_hits, N);
    PS_ALLOC_FIELD(sim, log_enemies_alive, N); PS_ALLOC_FIELD(sim, log_projectiles_alive, N); PS_ALLOC_FIELD(sim, log_drops_alive, N); PS_ALLOC_FIELD(sim, log_areas_alive, N);
    PS_ALLOC_FIELD(sim, log_weapon_levels, N); PS_ALLOC_FIELD(sim, log_wave, N); PS_ALLOC_FIELD(sim, log_hp, N); PS_ALLOC_FIELD(sim, log_survived, N); PS_ALLOC_FIELD(sim, log_n, N);
    PS_ALLOC_FIELD(sim, log_reduced, 1);

    // Zero everything. Reset kernel will fill live state.
    PS_CUDA_CHECK(cudaMemset(sim->observations, 0, sizeof(float) * N * PS_OBS_SIZE));
    PS_CUDA_CHECK(cudaMemset(sim->actions, 0, sizeof(float) * N * 2));
    PS_CUDA_CHECK(cudaMemset(sim->rewards, 0, sizeof(float) * N));
    PS_CUDA_CHECK(cudaMemset(sim->terminals, 0, sizeof(float) * N));
    PS_CUDA_CHECK(cudaMemset(sim->rng, 0, sizeof(uint32_t) * N));
    PS_CUDA_CHECK(cudaMemset(sim->enemy_active, 0, sizeof(uint8_t) * NE));
    PS_CUDA_CHECK(cudaMemset(sim->projectile_active, 0, sizeof(uint8_t) * NP));
    PS_CUDA_CHECK(cudaMemset(sim->drop_active, 0, sizeof(uint8_t) * ND));
    PS_CUDA_CHECK(cudaMemset(sim->area_active, 0, sizeof(uint8_t) * NA));
    // Explicitly zero the rest pointer-by-pointer for correctness.
    PS_ZERO_FIELD(sim, px, N); PS_ZERO_FIELD(sim, py, N); PS_ZERO_FIELD(sim, pvx, N); PS_ZERO_FIELD(sim, pvy, N);
    PS_ZERO_FIELD(sim, hp, N); PS_ZERO_FIELD(sim, max_hp, N); PS_ZERO_FIELD(sim, xp, N); PS_ZERO_FIELD(sim, player_facing_left, N);
    PS_ZERO_FIELD(sim, speed_bonus, N); PS_ZERO_FIELD(sim, damage_bonus, N); PS_ZERO_FIELD(sim, cooldown_mult, N); PS_ZERO_FIELD(sim, projectile_speed_bonus, N);
    PS_ZERO_FIELD(sim, magnet_bonus, N); PS_ZERO_FIELD(sim, area_bonus, N); PS_ZERO_FIELD(sim, level, N); PS_ZERO_FIELD(sim, pierce_bonus, N);
    PS_ZERO_FIELD(sim, pending_upgrade, N); PS_ZERO_FIELD(sim, queued_upgrades, N); PS_ZERO_FIELD(sim, last_boss_tick, N); PS_ZERO_FIELD(sim, offered, N * PS_UPGRADE_SLOTS);
    PS_ZERO_FIELD(sim, weapon_cd, N * PS_WEAPON_COUNT); PS_ZERO_FIELD(sim, weapon_active, N * PS_WEAPON_COUNT); PS_ZERO_FIELD(sim, weapon_level, N * PS_WEAPON_COUNT);
    PS_ZERO_FIELD(sim, orbit_phase, N); PS_ZERO_FIELD(sim, tick, N); PS_ZERO_FIELD(sim, invuln_timer, N);
    PS_ZERO_FIELD(sim, enemy_type, NE); PS_ZERO_FIELD(sim, enemy_x, NE); PS_ZERO_FIELD(sim, enemy_y, NE); PS_ZERO_FIELD(sim, enemy_vx, NE); PS_ZERO_FIELD(sim, enemy_vy, NE);
    PS_ZERO_FIELD(sim, enemy_hp, NE); PS_ZERO_FIELD(sim, enemy_max_hp, NE); PS_ZERO_FIELD(sim, enemy_radius, NE); PS_ZERO_FIELD(sim, enemy_speed, NE); PS_ZERO_FIELD(sim, enemy_damage, NE); PS_ZERO_FIELD(sim, enemy_next, NE);
    PS_ZERO_FIELD(sim, projectile_type, NP); PS_ZERO_FIELD(sim, projectile_x, NP); PS_ZERO_FIELD(sim, projectile_y, NP); PS_ZERO_FIELD(sim, projectile_vx, NP); PS_ZERO_FIELD(sim, projectile_vy, NP);
    PS_ZERO_FIELD(sim, projectile_damage, NP); PS_ZERO_FIELD(sim, projectile_radius, NP); PS_ZERO_FIELD(sim, projectile_ttl, NP); PS_ZERO_FIELD(sim, projectile_pierce, NP);
    PS_ZERO_FIELD(sim, projectile_dense, NP); PS_ZERO_FIELD(sim, projectile_dense_pos, NP);
    PS_ZERO_FIELD(sim, drop_type, ND); PS_ZERO_FIELD(sim, drop_x, ND); PS_ZERO_FIELD(sim, drop_y, ND); PS_ZERO_FIELD(sim, drop_value, ND);
    PS_ZERO_FIELD(sim, drop_dense, ND); PS_ZERO_FIELD(sim, drop_dense_pos, ND);
    PS_ZERO_FIELD(sim, area_type, NA); PS_ZERO_FIELD(sim, area_x, NA); PS_ZERO_FIELD(sim, area_y, NA); PS_ZERO_FIELD(sim, area_radius, NA); PS_ZERO_FIELD(sim, area_damage, NA);
    PS_ZERO_FIELD(sim, area_ttl, NA); PS_ZERO_FIELD(sim, area_tick_rate, NA); PS_ZERO_FIELD(sim, area_tick_timer, NA);
    PS_ZERO_FIELD(sim, area_dense, NA); PS_ZERO_FIELD(sim, area_dense_pos, NA);
    PS_ZERO_FIELD(sim, obstacle_type, NO); PS_ZERO_FIELD(sim, obstacle_x, NO); PS_ZERO_FIELD(sim, obstacle_y, NO); PS_ZERO_FIELD(sim, obstacle_radius, NO); PS_ZERO_FIELD(sim, grid_head, NG);
    PS_ZERO_FIELD(sim, grid_touched, NGT); PS_ZERO_FIELD(sim, grid_touched_count, N);
    PS_ZERO_FIELD(sim, nearest_enemy, N); PS_ZERO_FIELD(sim, nearest_enemy_d2, N);
    PS_ZERO_FIELD(sim, enemy_count, N); PS_ZERO_FIELD(sim, projectile_count, N); PS_ZERO_FIELD(sim, drop_count, N); PS_ZERO_FIELD(sim, area_count, N);
    PS_ZERO_FIELD(sim, next_enemy_slot, N); PS_ZERO_FIELD(sim, next_projectile_slot, N); PS_ZERO_FIELD(sim, next_drop_slot, N); PS_ZERO_FIELD(sim, next_area_slot, N);
    PS_ZERO_FIELD(sim, episode_return, N); PS_ZERO_FIELD(sim, episode_score, N); PS_ZERO_FIELD(sim, episode_kills, N); PS_ZERO_FIELD(sim, episode_xp, N);
    PS_ZERO_FIELD(sim, episode_damage_dealt, N); PS_ZERO_FIELD(sim, episode_damage_taken, N); PS_ZERO_FIELD(sim, episode_pickups, N); PS_ZERO_FIELD(sim, episode_levelups, N); PS_ZERO_FIELD(sim, episode_obstacle_hits, N);
    PS_ZERO_FIELD(sim, log_perf, N); PS_ZERO_FIELD(sim, log_score, N); PS_ZERO_FIELD(sim, log_episode_return, N); PS_ZERO_FIELD(sim, log_episode_length, N);
    PS_ZERO_FIELD(sim, log_kills, N); PS_ZERO_FIELD(sim, log_level, N); PS_ZERO_FIELD(sim, log_xp, N); PS_ZERO_FIELD(sim, log_damage_dealt, N); PS_ZERO_FIELD(sim, log_damage_taken, N);
    PS_ZERO_FIELD(sim, log_pickups, N); PS_ZERO_FIELD(sim, log_levelups, N); PS_ZERO_FIELD(sim, log_obstacle_hits, N); PS_ZERO_FIELD(sim, log_enemies_alive, N);
    PS_ZERO_FIELD(sim, log_projectiles_alive, N); PS_ZERO_FIELD(sim, log_drops_alive, N); PS_ZERO_FIELD(sim, log_areas_alive, N); PS_ZERO_FIELD(sim, log_weapon_levels, N);
    PS_ZERO_FIELD(sim, log_wave, N); PS_ZERO_FIELD(sim, log_hp, N); PS_ZERO_FIELD(sim, log_survived, N); PS_ZERO_FIELD(sim, log_n, N);
    PS_ZERO_FIELD(sim, log_reduced, 1);
}

static inline void ps_cuda_alloc_with_io(
        PSCudaSim* sim,
        int num_envs,
        PSConfig cfg,
        float* observations,
        float* actions,
        float* rewards,
        float* terminals) {
    ps_cuda_alloc(sim, num_envs, cfg);
    PS_CUDA_CHECK(cudaFree(sim->observations));
    PS_CUDA_CHECK(cudaFree(sim->actions));
    PS_CUDA_CHECK(cudaFree(sim->rewards));
    PS_CUDA_CHECK(cudaFree(sim->terminals));
    sim->observations = observations;
    sim->actions = actions;
    sim->rewards = rewards;
    sim->terminals = terminals;
    sim->owns_io = 0;
    const size_t N = (size_t)num_envs;
    PS_CUDA_CHECK(cudaMemset(sim->observations, 0, sizeof(float) * N * PS_OBS_SIZE));
    PS_CUDA_CHECK(cudaMemset(sim->actions, 0, sizeof(float) * N * 2));
    PS_CUDA_CHECK(cudaMemset(sim->rewards, 0, sizeof(float) * N));
    PS_CUDA_CHECK(cudaMemset(sim->terminals, 0, sizeof(float) * N));
}

#define PS_FREE_FIELD(sim, field) do { if ((sim)->field) { cudaFree((sim)->field); (sim)->field = nullptr; } } while (0)

static inline void ps_cuda_free(PSCudaSim* sim) {
    if (sim->owns_io) {
        PS_FREE_FIELD(sim, observations); PS_FREE_FIELD(sim, actions); PS_FREE_FIELD(sim, rewards); PS_FREE_FIELD(sim, terminals);
    } else {
        sim->observations = nullptr;
        sim->actions = nullptr;
        sim->rewards = nullptr;
        sim->terminals = nullptr;
    }
    PS_FREE_FIELD(sim, rng);
    PS_FREE_FIELD(sim, px); PS_FREE_FIELD(sim, py); PS_FREE_FIELD(sim, pvx); PS_FREE_FIELD(sim, pvy); PS_FREE_FIELD(sim, hp); PS_FREE_FIELD(sim, max_hp); PS_FREE_FIELD(sim, xp);
    PS_FREE_FIELD(sim, player_facing_left); PS_FREE_FIELD(sim, speed_bonus); PS_FREE_FIELD(sim, damage_bonus); PS_FREE_FIELD(sim, cooldown_mult); PS_FREE_FIELD(sim, projectile_speed_bonus);
    PS_FREE_FIELD(sim, magnet_bonus); PS_FREE_FIELD(sim, area_bonus); PS_FREE_FIELD(sim, level); PS_FREE_FIELD(sim, pierce_bonus); PS_FREE_FIELD(sim, pending_upgrade); PS_FREE_FIELD(sim, queued_upgrades); PS_FREE_FIELD(sim, last_boss_tick);
    PS_FREE_FIELD(sim, offered); PS_FREE_FIELD(sim, weapon_cd); PS_FREE_FIELD(sim, weapon_active); PS_FREE_FIELD(sim, weapon_level); PS_FREE_FIELD(sim, orbit_phase); PS_FREE_FIELD(sim, tick); PS_FREE_FIELD(sim, invuln_timer);
    PS_FREE_FIELD(sim, enemy_active); PS_FREE_FIELD(sim, enemy_type); PS_FREE_FIELD(sim, enemy_x); PS_FREE_FIELD(sim, enemy_y); PS_FREE_FIELD(sim, enemy_vx); PS_FREE_FIELD(sim, enemy_vy); PS_FREE_FIELD(sim, enemy_hp); PS_FREE_FIELD(sim, enemy_max_hp); PS_FREE_FIELD(sim, enemy_radius); PS_FREE_FIELD(sim, enemy_speed); PS_FREE_FIELD(sim, enemy_damage); PS_FREE_FIELD(sim, enemy_next);
    PS_FREE_FIELD(sim, projectile_active); PS_FREE_FIELD(sim, projectile_type); PS_FREE_FIELD(sim, projectile_x); PS_FREE_FIELD(sim, projectile_y); PS_FREE_FIELD(sim, projectile_vx); PS_FREE_FIELD(sim, projectile_vy); PS_FREE_FIELD(sim, projectile_damage); PS_FREE_FIELD(sim, projectile_radius); PS_FREE_FIELD(sim, projectile_ttl); PS_FREE_FIELD(sim, projectile_pierce); PS_FREE_FIELD(sim, projectile_dense); PS_FREE_FIELD(sim, projectile_dense_pos);
    PS_FREE_FIELD(sim, drop_active); PS_FREE_FIELD(sim, drop_type); PS_FREE_FIELD(sim, drop_x); PS_FREE_FIELD(sim, drop_y); PS_FREE_FIELD(sim, drop_value); PS_FREE_FIELD(sim, drop_dense); PS_FREE_FIELD(sim, drop_dense_pos);
    PS_FREE_FIELD(sim, area_active); PS_FREE_FIELD(sim, area_type); PS_FREE_FIELD(sim, area_x); PS_FREE_FIELD(sim, area_y); PS_FREE_FIELD(sim, area_radius); PS_FREE_FIELD(sim, area_damage); PS_FREE_FIELD(sim, area_ttl); PS_FREE_FIELD(sim, area_tick_rate); PS_FREE_FIELD(sim, area_tick_timer); PS_FREE_FIELD(sim, area_dense); PS_FREE_FIELD(sim, area_dense_pos);
    PS_FREE_FIELD(sim, obstacle_type); PS_FREE_FIELD(sim, obstacle_x); PS_FREE_FIELD(sim, obstacle_y); PS_FREE_FIELD(sim, obstacle_radius); PS_FREE_FIELD(sim, grid_head); PS_FREE_FIELD(sim, grid_touched); PS_FREE_FIELD(sim, grid_touched_count);
    PS_FREE_FIELD(sim, nearest_enemy); PS_FREE_FIELD(sim, nearest_enemy_d2);
    PS_FREE_FIELD(sim, enemy_count); PS_FREE_FIELD(sim, projectile_count); PS_FREE_FIELD(sim, drop_count); PS_FREE_FIELD(sim, area_count); PS_FREE_FIELD(sim, next_enemy_slot); PS_FREE_FIELD(sim, next_projectile_slot); PS_FREE_FIELD(sim, next_drop_slot); PS_FREE_FIELD(sim, next_area_slot);
    PS_FREE_FIELD(sim, episode_return); PS_FREE_FIELD(sim, episode_score); PS_FREE_FIELD(sim, episode_kills); PS_FREE_FIELD(sim, episode_xp); PS_FREE_FIELD(sim, episode_damage_dealt); PS_FREE_FIELD(sim, episode_damage_taken); PS_FREE_FIELD(sim, episode_pickups); PS_FREE_FIELD(sim, episode_levelups); PS_FREE_FIELD(sim, episode_obstacle_hits);
    PS_FREE_FIELD(sim, log_perf); PS_FREE_FIELD(sim, log_score); PS_FREE_FIELD(sim, log_episode_return); PS_FREE_FIELD(sim, log_episode_length); PS_FREE_FIELD(sim, log_kills); PS_FREE_FIELD(sim, log_level); PS_FREE_FIELD(sim, log_xp); PS_FREE_FIELD(sim, log_damage_dealt); PS_FREE_FIELD(sim, log_damage_taken); PS_FREE_FIELD(sim, log_pickups); PS_FREE_FIELD(sim, log_levelups); PS_FREE_FIELD(sim, log_obstacle_hits); PS_FREE_FIELD(sim, log_enemies_alive); PS_FREE_FIELD(sim, log_projectiles_alive); PS_FREE_FIELD(sim, log_drops_alive); PS_FREE_FIELD(sim, log_areas_alive); PS_FREE_FIELD(sim, log_weapon_levels); PS_FREE_FIELD(sim, log_wave); PS_FREE_FIELD(sim, log_hp); PS_FREE_FIELD(sim, log_survived); PS_FREE_FIELD(sim, log_n); PS_FREE_FIELD(sim, log_reduced);
    std::memset(sim, 0, sizeof(*sim));
}

// -----------------------------------------------------------------------------
// Device helpers
// -----------------------------------------------------------------------------

PS_HD float ps_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

PS_HD float ps_obs_soft_norm(float value, float half_scale) {
    value = fmaxf(value, 0.0f);
    half_scale = fmaxf(half_scale, 0.0001f);
    return value / (value + half_scale);
}

PS_HD float ps_dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

PS_D uint32_t ps_rand_u32(const PSCudaSim& sim, int env) {
    uint32_t x = sim.rng[env] ? sim.rng[env] : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sim.rng[env] = x ? x : 1u;
    return sim.rng[env];
}

PS_D float ps_randf(const PSCudaSim& sim, int env) {
    return (float)(ps_rand_u32(sim, env) & 0x00ffffffu) / 16777216.0f;
}

PS_D float ps_action_get(const PSCudaSim& sim, int env, int slot) {
#if PS_CUDA_ACTION_ENV_MAJOR
    return sim.actions[env * 2 + slot];
#else
    return sim.actions[slot * sim.num_envs + env];
#endif
}

PS_D void ps_action_set(const PSCudaSim& sim, int env, int slot, float value) {
#if PS_CUDA_ACTION_ENV_MAJOR
    sim.actions[env * 2 + slot] = value;
#else
    sim.actions[slot * sim.num_envs + env] = value;
#endif
}

PS_D int ps_obs_index(const PSCudaSim& sim, int env, int feature) {
#if PS_CUDA_OBS_ENV_MAJOR
    return env * PS_OBS_SIZE + feature;
#else
    return feature * sim.num_envs + env;
#endif
}

PS_D float ps_obs_get(const PSCudaSim& sim, int env, int feature) {
    return sim.observations[ps_obs_index(sim, env, feature)];
}

PS_D void ps_obs_set(const PSCudaSim& sim, int env, int feature, float value) {
    sim.observations[ps_obs_index(sim, env, feature)] = value;
}

PS_D void ps_obs_add_min1(const PSCudaSim& sim, int env, int feature, float value) {
    float v = ps_obs_get(sim, env, feature) + value;
    ps_obs_set(sim, env, feature, v < 1.0f ? v : 1.0f);
}

PS_D void ps_obs_max(const PSCudaSim& sim, int env, int feature, float value) {
    float old = ps_obs_get(sim, env, feature);
    ps_obs_set(sim, env, feature, old > value ? old : value);
}

PS_D int ps_cell(const PSCudaSim& sim, int env, float x, float y) {
    float half = 0.5f * sim.cfg.arena_size;
    int gx = (int)((((x - sim.px[env]) + half) / sim.cfg.arena_size) * (float)PS_GRID_W);
    int gy = (int)((((y - sim.py[env]) + half) / sim.cfg.arena_size) * (float)PS_GRID_H);
    gx = gx < 0 ? 0 : (gx >= PS_GRID_W ? PS_GRID_W - 1 : gx);
    gy = gy < 0 ? 0 : (gy >= PS_GRID_H ? PS_GRID_H - 1 : gy);
    return gy * PS_GRID_W + gx;
}

PS_D float ps_xp_threshold(const PSCudaSim& sim, int env) {
    return 6.0f + 4.0f * (float)(sim.level[env] - 1);
}

PS_D int ps_wave_index(const PSCudaSim& sim, int env) {
    int len = sim.cfg.wave_length_steps > 0 ? sim.cfg.wave_length_steps : 600;
    return sim.tick[env] / len;
}

PS_D float ps_episode_progress(const PSCudaSim& sim, int env) {
    float max_steps = fmaxf((float)sim.cfg.max_steps, 1.0f);
    float normal_steps = fmaxf((float)(sim.cfg.wave_length_steps > 0 ? sim.cfg.wave_length_steps : 600) * 7.0f, 1.0f);
    float scale = fminf(max_steps, normal_steps);
    return ps_clampf((float)sim.tick[env] / scale, 0.0f, 1.0f);
}

PS_D float ps_weapon_cooldown_total(const PSCudaSim& sim, int env, int weapon) {
    int level = sim.weapon_level[PS_WIDX(sim, weapon, env)];
    if (level <= 0) return 1.0f;
    PSWeaponDef def = PS_D_WEAPON_DEFS[weapon];
    float cd = def.base_cd + def.cd_per_level * (float)(level - 1);
    cd *= sim.cooldown_mult[env] * sim.cfg.fire_cooldown / fmaxf(PS_D_WEAPON_DEFS[PS_WEAPON_BUBBLE].base_cd, 1.0f);
    return fmaxf(cd, 3.0f);
}

PS_D float ps_weapon_power(const PSCudaSim& sim, int env, int weapon) {
    int level = sim.weapon_level[PS_WIDX(sim, weapon, env)];
    if (level <= 0) return 0.0f;
    float area = 1.0f + sim.area_bonus[env];
    float might = sim.cfg.projectile_damage * (1.0f + sim.damage_bonus[env]);
    return ps_clampf(((float)level / 8.0f) * might * area, 0.0f, 3.0f) / 3.0f;
}

PS_D float ps_weapon_damage(const PSCudaSim& sim, int env, int weapon, int level, int first_level_zero) {
    PSWeaponDef def = PS_D_WEAPON_DEFS[weapon];
    float level_delta = (float)(first_level_zero ? level - 1 : level);
    return (def.base_damage + def.damage_per_level * level_delta) * sim.cfg.projectile_damage * (1.0f + sim.damage_bonus[env]);
}

PS_D int ps_upgrade_available(const PSCudaSim& sim, int env, int type) {
    if (type >= PS_UPGRADE_BUBBLE && type <= PS_UPGRADE_SONAR) {
        return sim.weapon_level[PS_WIDX(sim, type, env)] < 8;
    }
    return type >= 0 && type < PS_UPGRADE_COUNT;
}

PS_D int ps_wave_minimum(const PSCudaSim& sim, int env) {
    int wave = ps_wave_index(sim, env);
    int base = wave < 24 ? PS_D_WAVE_MINS[wave] : 210 + 7 * (wave - 24);
    float spawn_scale = ps_clampf(sim.cfg.enemy_spawn_rate / 0.085f, 0.25f, 3.0f);
    float progress = ps_episode_progress(sim, env);
    int scaled = (int)ceilf((float)base * spawn_scale * (1.0f + 0.12f * sim.cfg.spawn_ramp * progress));
    int cap = sim.cfg.enemy_cap < 240 ? sim.cfg.enemy_cap : 240;
    return scaled > cap ? cap : scaled;
}

PS_D int ps_wave_spawn_interval(const PSCudaSim& sim, int env) {
    int wave = ps_wave_index(sim, env);
    int base = wave < 24 ? PS_D_WAVE_INTERVALS[wave] : 8;
    float spawn_scale = ps_clampf(sim.cfg.enemy_spawn_rate / 0.085f, 0.25f, 3.0f);
    int scaled = (int)ceilf((float)base / spawn_scale);
    return scaled < 5 ? 5 : scaled;
}

PS_D int ps_find_free_slot(uint8_t* active, int cap, int* cursor, int N, int env) {
    if (cap <= 0) return -1;
    int start = *cursor;
    if (start < 0 || start >= cap) start = 0;
    for (int tries = 0; tries < cap; tries++) {
        int i = start + tries;
        if (i >= cap) i -= cap;
        if (!active[i * N + env]) {
            int next = i + 1;
            *cursor = next >= cap ? 0 : next;
            return i;
        }
    }
    *cursor = start;
    return -1;
}

// -----------------------------------------------------------------------------
// Observation encoder
// -----------------------------------------------------------------------------

PS_D int ps_obs_sector_fast8(float dx, float dy) {
    if (dy == 0.0f) return dx >= 0.0f ? 0 : 4;
    if (dx == 0.0f) return dy > 0.0f ? 2 : 6;
    if (dx > 0.0f) {
        if (dy > 0.0f) return dy < dx ? 0 : 1;
        return -dy > dx ? 6 : 7;
    }
    if (dy > 0.0f) return dy > -dx ? 2 : 3;
    return -dy < -dx ? 4 : 5;
}

PS_D int ps_obs_sector(float dx, float dy) {
#if defined(PS_OBS_EXACT_SECTOR) && PS_OBS_EXACT_SECTOR
    float angle = atan2f(dy, dx);
    if (angle < 0.0f) angle += 2.0f * PI;
    int sector = (int)(angle / (2.0f * PI) * PS_SECTORS);
    return sector >= PS_SECTORS ? PS_SECTORS - 1 : sector;
#else
    return ps_obs_sector_fast8(dx, dy);
#endif
}

PS_D int ps_obs_ring_d2(float d2, float observe_radius2) {
    if (d2 < observe_radius2 * 0.0225f) return 0;
    if (d2 < observe_radius2 * 0.1444f) return 1;
    return PS_RINGS - 1;
}

PS_D void ps_compute_observations(const PSCudaSim& sim, int env) {
    for (int i = 0; i < PS_OBS_SIZE; i++) ps_obs_set(sim, env, i, 0.0f);

    int idx = 0;
    int wave_len = sim.cfg.wave_length_steps > 0 ? sim.cfg.wave_length_steps : 600;
    int wave = ps_wave_index(sim, env);
    int boss_period = 900;

    ps_obs_set(sim, env, idx++, ps_clampf(sim.hp[env] / fmaxf(sim.max_hp[env], 1.0f), 0.0f, 1.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.hp[env], 4.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.max_hp[env], 8.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm((float)sim.level[env], 20.0f));
    ps_obs_set(sim, env, idx++, ps_clampf(sim.xp[env] / fmaxf(ps_xp_threshold(sim, env), 1.0f), 0.0f, 1.0f));
    ps_obs_set(sim, env, idx++, (float)(sim.tick[env] % wave_len) / (float)wave_len);
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm((float)(wave + 1), 12.0f));
    ps_obs_set(sim, env, idx++, (float)(sim.tick[env] % boss_period) / (float)boss_period);

    int visible_enemies_idx = idx++;
    int visible_projectiles_idx = idx++;
    int visible_drops_idx = idx++;
    ps_obs_set(sim, env, idx++, 1.0f - ps_clampf(sim.weapon_cd[PS_WIDX(sim, PS_WEAPON_BUBBLE, env)] / ps_weapon_cooldown_total(sim, env, PS_WEAPON_BUBBLE), 0.0f, 1.0f));
    ps_obs_set(sim, env, idx++, sim.pvx[env] / fmaxf(sim.cfg.player_speed * (1.0f + sim.speed_bonus[env]), 0.001f));
    ps_obs_set(sim, env, idx++, sim.pvy[env] / fmaxf(sim.cfg.player_speed * (1.0f + sim.speed_bonus[env]), 0.001f));
    ps_obs_set(sim, env, idx++, sim.pending_upgrade[env] ? 1.0f : 0.0f);
    ps_obs_set(sim, env, idx++, sim.cfg.invuln_steps > 0 ? ps_clampf((float)sim.invuln_timer[env] / (float)sim.cfg.invuln_steps, 0.0f, 1.0f) : 0.0f);
    ps_obs_set(sim, env, idx++, ps_clampf((float)sim.queued_upgrades[env] / 4.0f, 0.0f, 1.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.speed_bonus[env], 1.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.damage_bonus[env], 1.0f));
    ps_obs_set(sim, env, idx++, ps_clampf(1.0f - sim.cooldown_mult[env], 0.0f, 1.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.magnet_bonus[env], 1.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.area_bonus[env], 1.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm((float)sim.pierce_bonus[env], 4.0f));
    int lethal_threat_idx = idx++;
    int nearest_health_proximity_idx = idx++;

    int nearest_xp_dx_idx = idx++;
    int nearest_xp_dy_idx = idx++;
    int nearest_xp_proximity_idx = idx++;
    int visible_xp_value_idx = idx++;
    int visible_xp_can_level_idx = idx++;

    float observe_radius = sim.cfg.arena_size * 0.45f;
    float observe_radius2 = observe_radius * observe_radius;
    float inv_observe_radius = 1.0f / fmaxf(observe_radius, 0.001f);
    float inv_enemy_cap = 1.0f / fmaxf((float)sim.cfg.enemy_cap, 1.0f);
    float inv_projectile_cap = 1.0f / fmaxf((float)sim.cfg.projectile_cap, 1.0f);
    float inv_drop_cap = 1.0f / fmaxf((float)sim.cfg.drop_cap, 1.0f);

    int boss_base = idx;
    idx += PS_BOSS_FEATURES;
    int enemy_base = idx;
    idx += PS_SECTORS * PS_RINGS * PS_ENEMY_CHANNELS;
    int drop_base = idx;
    idx += PS_SECTORS * PS_RINGS * PS_DROP_CHANNELS;
    int obstacle_base = idx;
    idx += PS_SECTORS * PS_RINGS * PS_OBSTACLE_CHANNELS;
    int danger_base = idx;
    idx += PS_SECTORS * PS_DANGER_CHANNELS;
    int area_base = idx;
    idx += PS_SECTORS * PS_AREA_CHANNELS;

    float sector_pressure[PS_SECTORS];
    float sector_front[PS_SECTORS];
    float sector_ttc[PS_SECTORS];
    float sector_obstacle[PS_SECTORS];
    for (int s = 0; s < PS_SECTORS; s++) {
        sector_pressure[s] = 0.0f;
        sector_front[s] = 0.0f;
        sector_ttc[s] = 0.0f;
        sector_obstacle[s] = 0.0f;
    }

    int visible_enemies = 0;
    int visible_projectiles = 0;
    int visible_drops = 0;
    float nearest_xp_dx = 0.0f;
    float nearest_xp_dy = 0.0f;
    float nearest_xp_d2 = 1e30f;
    float visible_xp_value = 0.0f;
    float nearest_health_d2 = 1e30f;
    int nearest_boss = -1;
    int boss_count = 0;
    float nearest_boss_d2 = 1e30f;
    float lethal_threat = 0.0f;

    for (int i = 0; i < sim.cfg.enemy_cap; i++) {
        int e = PS_EIDX(sim, i, env);
        if (!sim.enemy_active[e]) continue;

        float dx = sim.enemy_x[e] - sim.px[env];
        float dy = sim.enemy_y[e] - sim.py[env];
        float d2 = dx * dx + dy * dy;

        if (sim.enemy_type[e] & PS_ENEMY_BOSS_FLAG) {
            boss_count++;
            if (d2 < nearest_boss_d2) {
                nearest_boss_d2 = d2;
                nearest_boss = i;
            }
        }

        if (d2 > observe_radius2) continue;
        float d = sqrtf(d2);
        visible_enemies++;
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = enemy_base + (ring * PS_SECTORS + sector) * PS_ENEMY_CHANNELS;
        float proximity = 1.0f - d * inv_observe_radius;
        float hit_fraction = sim.enemy_damage[e] / fmaxf(sim.hp[env], 0.25f);
        float lethal_risk = ps_clampf(hit_fraction, 0.0f, 1.0f) * (0.35f + 0.65f * proximity);
        lethal_threat = fmaxf(lethal_threat, lethal_risk);
        ps_obs_add_min1(sim, env, o + 0, 0.125f);
        ps_obs_max(sim, env, o + 1, proximity);
        ps_obs_max(sim, env, o + 2, sim.enemy_hp[e] / fmaxf(sim.enemy_max_hp[e], 1.0f));
        ps_obs_max(sim, env, o + 3, sim.enemy_damage[e] / 4.0f);

        float threat = (0.35f + 0.65f * proximity) * ps_clampf(sim.enemy_damage[e] / 4.0f, 0.15f, 1.0f);
        sector_pressure[sector] = fminf(sector_pressure[sector] + 0.12f * threat, 1.0f);
        sector_front[sector] = fmaxf(sector_front[sector], proximity);
        float clearance = fmaxf(d - (sim.enemy_radius[e] + 0.42f), 0.0f);
        float closing = -(sim.enemy_vx[e] * dx + sim.enemy_vy[e] * dy) / fmaxf(d, 0.001f);
        float ttc = clearance / fmaxf(closing, 0.001f);
        sector_ttc[sector] = fmaxf(sector_ttc[sector], 1.0f - ps_clampf(ttc / 180.0f, 0.0f, 1.0f));
    }

    ps_obs_set(sim, env, lethal_threat_idx, ps_clampf(lethal_threat, 0.0f, 1.0f));

    if (nearest_boss >= 0) {
        int e = PS_EIDX(sim, nearest_boss, env);
        float dx = sim.enemy_x[e] - sim.px[env];
        float dy = sim.enemy_y[e] - sim.py[env];
        float d = sqrtf(fmaxf(nearest_boss_d2, 0.0001f));
        float closing = -(sim.enemy_vx[e] * dx + sim.enemy_vy[e] * dy) / d;
        ps_obs_set(sim, env, boss_base + PS_BOSS_PRESENT, 1.0f);
        ps_obs_set(sim, env, boss_base + PS_BOSS_DX, ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f));
        ps_obs_set(sim, env, boss_base + PS_BOSS_DY, ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f));
        ps_obs_set(sim, env, boss_base + PS_BOSS_PROXIMITY, 1.0f - ps_clampf(d * inv_observe_radius, 0.0f, 1.0f));
        ps_obs_set(sim, env, boss_base + PS_BOSS_HP_FRACTION, ps_clampf(sim.enemy_hp[e] / fmaxf(sim.enemy_max_hp[e], 1.0f), 0.0f, 1.0f));
        ps_obs_set(sim, env, boss_base + PS_BOSS_MAX_HP, ps_obs_soft_norm(sim.enemy_max_hp[e], 96.0f));
        ps_obs_set(sim, env, boss_base + PS_BOSS_CLOSING_SPEED, ps_clampf(closing / 0.25f, -1.0f, 1.0f));
        ps_obs_set(sim, env, boss_base + PS_BOSS_COUNT, ps_obs_soft_norm((float)boss_count, 2.0f));
    }

    for (int k = 0; k < sim.drop_count[env]; k++) {
        int i = sim.drop_dense[PS_DIDX(sim, k, env)];
        int d_i = PS_DIDX(sim, i, env);
        float dx = sim.drop_x[d_i] - sim.px[env];
        float dy = sim.drop_y[d_i] - sim.py[env];
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        float d = sqrtf(d2);
        visible_drops++;
        if (sim.drop_type[d_i] == 0) {
            visible_xp_value += sim.drop_value[d_i];
            if (d2 < nearest_xp_d2) {
                nearest_xp_d2 = d2;
                nearest_xp_dx = dx;
                nearest_xp_dy = dy;
            }
        } else if (d2 < nearest_health_d2) {
            nearest_health_d2 = d2;
        }
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = drop_base + (ring * PS_SECTORS + sector) * PS_DROP_CHANNELS;
        ps_obs_add_min1(sim, env, o + 0, sim.drop_value[d_i] * 0.1f);
        ps_obs_max(sim, env, o + 1, 1.0f - d * inv_observe_radius);
        ps_obs_max(sim, env, o + 2, sim.drop_type[d_i] == 1 ? 1.0f : 0.0f);
    }

    if (nearest_xp_d2 < 1e29f) {
        float nearest_xp_dist = sqrtf(nearest_xp_d2);
        ps_obs_set(sim, env, nearest_xp_dx_idx, ps_clampf(nearest_xp_dx * inv_observe_radius, -1.0f, 1.0f));
        ps_obs_set(sim, env, nearest_xp_dy_idx, ps_clampf(nearest_xp_dy * inv_observe_radius, -1.0f, 1.0f));
        ps_obs_set(sim, env, nearest_xp_proximity_idx, 1.0f - ps_clampf(nearest_xp_dist * inv_observe_radius, 0.0f, 1.0f));
    }
    if (nearest_health_d2 < 1e29f) {
        float nearest_health_dist = sqrtf(nearest_health_d2);
        ps_obs_set(sim, env, nearest_health_proximity_idx, 1.0f - ps_clampf(nearest_health_dist * inv_observe_radius, 0.0f, 1.0f));
    }
    ps_obs_set(sim, env, visible_xp_value_idx, ps_clampf(visible_xp_value / fmaxf(ps_xp_threshold(sim, env), 1.0f), 0.0f, 1.0f));
    ps_obs_set(sim, env, visible_xp_can_level_idx, visible_xp_value >= fmaxf(ps_xp_threshold(sim, env) - sim.xp[env], 0.0f) ? 1.0f : 0.0f);

    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        float dx = sim.obstacle_x[oi] - sim.px[env];
        float dy = sim.obstacle_y[oi] - sim.py[env];
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        float d = sqrtf(d2);
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = obstacle_base + (ring * PS_SECTORS + sector) * PS_OBSTACLE_CHANNELS;
        float proximity = 1.0f - d * inv_observe_radius;
        ps_obs_add_min1(sim, env, o + 0, 0.25f);
        ps_obs_max(sim, env, o + 1, proximity);
        sector_obstacle[sector] = fminf(sector_obstacle[sector] + 0.30f * proximity, 1.0f);
    }

    for (int k = 0; k < sim.projectile_count[env]; k++) {
        int i = sim.projectile_dense[PS_PIDX(sim, k, env)];
        int p = PS_PIDX(sim, i, env);
        if (ps_dist2(sim.projectile_x[p], sim.projectile_y[p], sim.px[env], sim.py[env]) <= observe_radius2) visible_projectiles++;
    }

    for (int k = 0; k < sim.area_count[env]; k++) {
        int i = sim.area_dense[PS_AIDX(sim, k, env)];
        int a = PS_AIDX(sim, i, env);
        float dx = sim.area_x[a] - sim.px[env];
        float dy = sim.area_y[a] - sim.py[env];
        float d2 = dx * dx + dy * dy;
        float effective_radius = observe_radius + sim.area_radius[a];
        if (d2 > effective_radius * effective_radius) continue;
        float d = sqrtf(d2);
        int sector = ps_obs_sector(dx, dy);
        int o = area_base + sector * PS_AREA_CHANNELS;
        float coverage = ps_clampf(sim.area_radius[a] * inv_observe_radius + fmaxf(0.0f, 1.0f - d * inv_observe_radius), 0.0f, 1.0f);
        ps_obs_max(sim, env, o + 0, coverage);
        ps_obs_max(sim, env, o + 1, ps_clampf(sim.area_damage[a] / 4.0f, 0.0f, 1.0f));
        ps_obs_max(sim, env, o + 2, ps_clampf((float)sim.area_ttl[a] / 180.0f, 0.0f, 1.0f));
    }

    for (int s = 0; s < PS_SECTORS; s++) {
        int left = (s + PS_SECTORS - 1) % PS_SECTORS;
        int right = (s + 1) % PS_SECTORS;
        float neighbor_pressure = 0.5f * (sector_pressure[left] + sector_pressure[right]);
        float blocked = ps_clampf(sector_pressure[s] + 0.35f * neighbor_pressure + 0.75f * sector_obstacle[s], 0.0f, 1.0f);
        int o = danger_base + s * PS_DANGER_CHANNELS;
        ps_obs_set(sim, env, o + 0, ps_clampf(sector_pressure[s], 0.0f, 1.0f));
        ps_obs_set(sim, env, o + 1, ps_clampf(sector_front[s], 0.0f, 1.0f));
        ps_obs_set(sim, env, o + 2, ps_clampf(sector_ttc[s], 0.0f, 1.0f));
        ps_obs_set(sim, env, o + 3, 1.0f - blocked);
    }

    ps_obs_set(sim, env, visible_enemies_idx, ps_clampf((float)visible_enemies * inv_enemy_cap, 0.0f, 1.0f));
    ps_obs_set(sim, env, visible_projectiles_idx, ps_clampf((float)visible_projectiles * inv_projectile_cap, 0.0f, 1.0f));
    ps_obs_set(sim, env, visible_drops_idx, ps_clampf((float)visible_drops * inv_drop_cap, 0.0f, 1.0f));

    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        int level = sim.weapon_level[PS_WIDX(sim, i, env)];
        float cd_total = ps_weapon_cooldown_total(sim, env, i);
        float ready = level > 0 ? 1.0f - ps_clampf(sim.weapon_cd[PS_WIDX(sim, i, env)] / cd_total, 0.0f, 1.0f) : 0.0f;
        ps_obs_set(sim, env, idx++, (float)level / 8.0f);
        ps_obs_set(sim, env, idx++, ready);
        ps_obs_set(sim, env, idx++, ps_clampf(sim.weapon_active[PS_WIDX(sim, i, env)], 0.0f, 1.0f));
        ps_obs_set(sim, env, idx++, ps_weapon_power(sim, env, i));
    }

    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int type = sim.pending_upgrade[env] ? sim.offered[PS_UIDX(sim, i, env)] : -1;
        ps_obs_set(sim, env, idx++, type >= 0 ? 1.0f : 0.0f);
        ps_obs_set(sim, env, idx++, type >= 0 ? (float)type / (float)(PS_UPGRADE_COUNT - 1) : 0.0f);
        ps_obs_set(sim, env, idx++, type >= PS_UPGRADE_BUBBLE && type <= PS_UPGRADE_SONAR ? 1.0f : 0.0f);
        ps_obs_set(sim, env, idx++, type == PS_UPGRADE_MIGHT || type == PS_UPGRADE_COOLDOWN || type == PS_UPGRADE_AREA ? 1.0f : 0.0f);
        ps_obs_set(sim, env, idx++, type == PS_UPGRADE_SPEED || type == PS_UPGRADE_MAGNET || type == PS_UPGRADE_PIERCE ? 1.0f : 0.0f);
        ps_obs_set(sim, env, idx++, type == PS_UPGRADE_HEALTH ? 1.0f : 0.0f);
    }
}
PS_D int ps_obstacle_position_clear(const PSCudaSim& sim, int env, int count, int skip, float x, float y, float radius) {
    if (ps_dist2(x, y, sim.px[env], sim.py[env]) < 48.0f) return 0;
    for (int i = 0; i < count; i++) {
        if (i == skip) continue;
        int oi = PS_OIDX(sim, i, env);
        float min_dist = radius + sim.obstacle_radius[oi] + 1.15f;
        float dx = x - sim.obstacle_x[oi];
        if (dx >= min_dist || dx <= -min_dist) continue;
        float dy = y - sim.obstacle_y[oi];
        if (dy >= min_dist || dy <= -min_dist) continue;
        if (dx * dx + dy * dy < min_dist * min_dist) return 0;
    }
    return 1;
}

PS_D int ps_overlaps_obstacle(const PSCudaSim& sim, int env, float x, float y, float radius, float padding) {
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        float min_dist = radius + sim.obstacle_radius[oi] + padding;
        float dx = x - sim.obstacle_x[oi];
        if (dx >= min_dist || dx <= -min_dist) continue;
        float dy = y - sim.obstacle_y[oi];
        if (dy >= min_dist || dy <= -min_dist) continue;
        if (dx * dx + dy * dy < min_dist * min_dist) return 1;
    }
    return 0;
}

PS_D void ps_push_out_obstacles(const PSCudaSim& sim, int env, float* x, float* y, float radius, int penalize) {
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        float min_dist = radius + sim.obstacle_radius[oi];
        float dx = *x - sim.obstacle_x[oi];
        if (dx >= min_dist || dx <= -min_dist) continue;
        float dy = *y - sim.obstacle_y[oi];
        if (dy >= min_dist || dy <= -min_dist) continue;
        float d2 = dx * dx + dy * dy;
        float min2 = min_dist * min_dist;
        if (d2 >= min2) continue;
        float d = sqrtf(fmaxf(d2, 0.0001f));
        float push = min_dist - d;
        *x += dx / d * push;
        *y += dy / d * push;
        if (penalize) {
            sim.rewards[env] += sim.cfg.obstacle_penalty;
            sim.episode_obstacle_hits[env] += 1.0f;
        }
    }
}

PS_D void ps_spawn_obstacles(const PSCudaSim& sim, int env) {
    float half = 0.5f * sim.cfg.arena_size;
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        sim.obstacle_radius[oi] = 0.52f + ps_randf(sim, env) * 0.52f;
        sim.obstacle_type[oi] = (uint8_t)(ps_rand_u32(sim, env) % 3u);
        int placed = 0;
        for (int tries = 0; tries < 96; tries++) {
            float angle = ps_randf(sim, env) * 2.0f * PI;
            float dist = half * (0.28f + 0.62f * ps_randf(sim, env));
            float x = sim.px[env] + cosf(angle) * dist;
            float y = sim.py[env] + sinf(angle) * dist;
            if (!ps_obstacle_position_clear(sim, env, i, -1, x, y, sim.obstacle_radius[oi])) continue;
            sim.obstacle_x[oi] = x;
            sim.obstacle_y[oi] = y;
            placed = 1;
            break;
        }
        if (!placed) {
            float a = ((float)i * 2.3999632f) + ps_randf(sim, env) * 0.35f;
            float r = half * (0.34f + 0.48f * ((float)(i % 7) / 6.0f));
            sim.obstacle_x[oi] = sim.px[env] + cosf(a) * r;
            sim.obstacle_y[oi] = sim.py[env] + sinf(a) * r;
        }
    }
}

PS_D void ps_recycle_obstacle(const PSCudaSim& sim, int env, int idx) {
    float half = 0.5f * sim.cfg.arena_size;
    int oi = PS_OIDX(sim, idx, env);
    for (int tries = 0; tries < 64; tries++) {
        float angle = ps_randf(sim, env) * 2.0f * PI;
        float dist = half * (0.62f + 0.34f * ps_randf(sim, env));
        float x = sim.px[env] + cosf(angle) * dist;
        float y = sim.py[env] + sinf(angle) * dist;
        if (!ps_obstacle_position_clear(sim, env, sim.cfg.obstacle_count, idx, x, y, sim.obstacle_radius[oi])) continue;
        sim.obstacle_x[oi] = x;
        sim.obstacle_y[oi] = y;
        sim.obstacle_type[oi] = (uint8_t)(ps_rand_u32(sim, env) % 3u);
        return;
    }
}

PS_D void ps_recycle_far_obstacles(const PSCudaSim& sim, int env) {
    float recycle_radius = sim.cfg.arena_size * 0.62f;
    float recycle2 = recycle_radius * recycle_radius;
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        if (ps_dist2(sim.obstacle_x[oi], sim.obstacle_y[oi], sim.px[env], sim.py[env]) > recycle2) {
            ps_recycle_obstacle(sim, env, i);
        }
    }
}

PS_D void ps_clear_entities(const PSCudaSim& sim, int env) {
    for (int i = 0; i < sim.cfg.enemy_cap; i++) {
        int e = PS_EIDX(sim, i, env);
        sim.enemy_active[e] = 0;
        sim.enemy_next[e] = -1;
    }
    for (int i = 0; i < sim.cfg.projectile_cap; i++) sim.projectile_active[PS_PIDX(sim, i, env)] = 0;
    for (int i = 0; i < sim.cfg.drop_cap; i++) sim.drop_active[PS_DIDX(sim, i, env)] = 0;
    for (int i = 0; i < PS_MAX_AREAS; i++) sim.area_active[PS_AIDX(sim, i, env)] = 0;
    for (int i = 0; i < PS_GRID_CELLS; i++) sim.grid_head[PS_GIDX(sim, i, env)] = -1;
    sim.grid_touched_count[env] = 0;

    sim.enemy_count[env] = 0;
    sim.projectile_count[env] = 0;
    sim.drop_count[env] = 0;
    sim.area_count[env] = 0;
    sim.next_enemy_slot[env] = 0;
    sim.next_projectile_slot[env] = 0;
    sim.next_drop_slot[env] = 0;
    sim.next_area_slot[env] = 0;
}

PS_D void ps_deactivate_enemy(const PSCudaSim& sim, int env, int i) {
    if (i < 0 || i >= sim.cfg.enemy_cap) return;
    int e = PS_EIDX(sim, i, env);
    if (!sim.enemy_active[e]) return;
    sim.enemy_active[e] = 0;
    if (sim.enemy_count[env] > 0) sim.enemy_count[env]--;
}

PS_D void ps_deactivate_projectile(const PSCudaSim& sim, int env, int i) {
    if (i < 0 || i >= sim.cfg.projectile_cap) return;
    int p = PS_PIDX(sim, i, env);
    if (!sim.projectile_active[p]) return;
    int count = sim.projectile_count[env];
    int pos = sim.projectile_dense_pos[p];
    if (count > 0 && pos >= 0 && pos < count) {
        int last = sim.projectile_dense[PS_PIDX(sim, count - 1, env)];
        sim.projectile_dense[PS_PIDX(sim, pos, env)] = last;
        sim.projectile_dense_pos[PS_PIDX(sim, last, env)] = pos;
    }
    sim.projectile_active[p] = 0;
    sim.projectile_count[env] = count > 0 ? count - 1 : 0;
}

PS_D void ps_deactivate_drop(const PSCudaSim& sim, int env, int i) {
    if (i < 0 || i >= sim.cfg.drop_cap) return;
    int d = PS_DIDX(sim, i, env);
    if (!sim.drop_active[d]) return;
    int count = sim.drop_count[env];
    int pos = sim.drop_dense_pos[d];
    if (count > 0 && pos >= 0 && pos < count) {
        int last = sim.drop_dense[PS_DIDX(sim, count - 1, env)];
        sim.drop_dense[PS_DIDX(sim, pos, env)] = last;
        sim.drop_dense_pos[PS_DIDX(sim, last, env)] = pos;
    }
    sim.drop_active[d] = 0;
    sim.drop_count[env] = count > 0 ? count - 1 : 0;
}

PS_D void ps_deactivate_area(const PSCudaSim& sim, int env, int i) {
    if (i < 0 || i >= PS_MAX_AREAS) return;
    int a = PS_AIDX(sim, i, env);
    if (!sim.area_active[a]) return;
    int count = sim.area_count[env];
    int pos = sim.area_dense_pos[a];
    if (count > 0 && pos >= 0 && pos < count) {
        int last = sim.area_dense[PS_AIDX(sim, count - 1, env)];
        sim.area_dense[PS_AIDX(sim, pos, env)] = last;
        sim.area_dense_pos[PS_AIDX(sim, last, env)] = pos;
    }
    sim.area_active[a] = 0;
    sim.area_count[env] = count > 0 ? count - 1 : 0;
}

PS_D void ps_offer_upgrades(const PSCudaSim& sim, int env) {
    if (sim.pending_upgrade[env]) return;
    sim.pending_upgrade[env] = 1;
    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int offer = -1;
        for (int tries = 0; tries < 32; tries++) {
            int candidate = (int)(ps_rand_u32(sim, env) % PS_UPGRADE_COUNT);
            int duplicate = 0;
            for (int j = 0; j < i; j++) duplicate |= sim.offered[PS_UIDX(sim, j, env)] == candidate;
            if (!duplicate && ps_upgrade_available(sim, env, candidate)) {
                offer = candidate;
                break;
            }
        }
        sim.offered[PS_UIDX(sim, i, env)] = offer >= 0 ? offer : PS_UPGRADE_MIGHT;
    }
}

PS_D void ps_apply_upgrade(const PSCudaSim& sim, int env, int choice) {
    if (!sim.pending_upgrade[env] || choice < 0 || choice >= PS_UPGRADE_SLOTS) return;
    int upgrade = sim.offered[PS_UIDX(sim, choice, env)];
    switch (upgrade) {
        case PS_UPGRADE_BUBBLE:
        case PS_UPGRADE_WHIRLPOOL:
        case PS_UPGRADE_ORBIT:
        case PS_UPGRADE_INK:
        case PS_UPGRADE_SONAR:
            if (sim.weapon_level[PS_WIDX(sim, upgrade, env)] < 8) sim.weapon_level[PS_WIDX(sim, upgrade, env)]++;
            break;
        case PS_UPGRADE_SPEED: sim.speed_bonus[env] += 0.10f; break;
        case PS_UPGRADE_MAGNET: sim.magnet_bonus[env] += 0.25f; break;
        case PS_UPGRADE_HEALTH:
            sim.max_hp[env] += 1.0f;
            sim.hp[env] = fminf(sim.max_hp[env], sim.hp[env] + sim.cfg.health_heal);
            break;
        case PS_UPGRADE_MIGHT: sim.damage_bonus[env] += 0.18f; break;
        case PS_UPGRADE_COOLDOWN: sim.cooldown_mult[env] *= 0.91f; break;
        case PS_UPGRADE_AREA: sim.area_bonus[env] += 0.12f; break;
        case PS_UPGRADE_PIERCE: sim.pierce_bonus[env] += 1; break;
    }
    sim.episode_levelups[env] += 1.0f;
    sim.pending_upgrade[env] = 0;
    if (sim.queued_upgrades[env] > 0) sim.queued_upgrades[env]--;
    if (sim.queued_upgrades[env] > 0) ps_offer_upgrades(sim, env);
}

PS_D void ps_add_log(const PSCudaSim& sim, int env, int survived) {
    float perf = (float)survived;    
    sim.log_perf[env] += perf;
    sim.log_score[env] += sim.episode_score[env];
    sim.log_episode_return[env] += sim.episode_return[env];
    sim.log_episode_length[env] += (float)sim.tick[env];
    sim.log_kills[env] += sim.episode_kills[env];
    sim.log_level[env] += (float)sim.level[env];
    sim.log_xp[env] += sim.episode_xp[env];
    sim.log_damage_dealt[env] += sim.episode_damage_dealt[env];
    sim.log_damage_taken[env] += sim.episode_damage_taken[env];
    sim.log_pickups[env] += sim.episode_pickups[env];
    sim.log_levelups[env] += sim.episode_levelups[env];
    sim.log_obstacle_hits[env] += sim.episode_obstacle_hits[env];
    sim.log_enemies_alive[env] += (float)sim.enemy_count[env];
    sim.log_projectiles_alive[env] += (float)sim.projectile_count[env];
    sim.log_drops_alive[env] += (float)sim.drop_count[env];
    sim.log_areas_alive[env] += (float)sim.area_count[env];
    int weapon_levels = 0;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) weapon_levels += sim.weapon_level[PS_WIDX(sim, i, env)];
    sim.log_weapon_levels[env] += (float)weapon_levels;
    sim.log_wave[env] += (float)(ps_wave_index(sim, env) + 1);
    sim.log_hp[env] += fmaxf(sim.hp[env], 0.0f);
    sim.log_survived[env] += (float)survived;
    sim.log_n[env] += 1.0f;
}

PS_D int ps_pick_spawn_side(const PSCudaSim& sim, int env) {
    float m2 = sim.pvx[env] * sim.pvx[env] + sim.pvy[env] * sim.pvy[env];
    if (m2 > 0.0001f && ps_randf(sim, env) < 0.14f) {
        if (fabsf(sim.pvx[env]) > fabsf(sim.pvy[env])) return sim.pvx[env] > 0.0f ? 1 : 0;
        return sim.pvy[env] > 0.0f ? 3 : 2;
    }
    return (int)(ps_rand_u32(sim, env) & 3u);
}

PS_D void ps_pick_spawn_position(const PSCudaSim& sim, int env, float radius, float* x, float* y) {
    float half = 0.5f * sim.cfg.arena_size;
    float edge = half + radius + 1.0f;
    float along = (ps_randf(sim, env) * 2.0f - 1.0f) * half * 0.98f;
    int side = ps_pick_spawn_side(sim, env);
    if (side == 0) {
        *x = sim.px[env] - edge;
        *y = sim.py[env] + along;
    } else if (side == 1) {
        *x = sim.px[env] + edge;
        *y = sim.py[env] + along;
    } else if (side == 2) {
        *x = sim.px[env] + along;
        *y = sim.py[env] - edge;
    } else {
        *x = sim.px[env] + along;
        *y = sim.py[env] + edge;
    }
}

PS_D PSEnemyDef ps_enemy_stats(const PSCudaSim& sim, int env, int* kind_out, int elite, int boss) {
    int wave = ps_wave_index(sim, env);
    float progress = ps_episode_progress(sim, env);
    int kind = 0;
    if (wave >= 2) {
        uint32_t roll = ps_rand_u32(sim, env) % 100u;
        if (wave < 4) kind = roll < 35u ? 1 : 0;
        else if (wave < 7) kind = roll < 25u ? 2 : (roll < 55u ? 1 : 0);
        else kind = roll < 20u ? 2 : (roll < 55u ? 3 : (roll < 80u ? 1 : 0));
    }

    PSEnemyDef stats = PS_D_ENEMY_DEFS[kind];
    float hp_growth = 1.0f + 0.065f * (float)wave + 0.42f * progress * sim.cfg.spawn_ramp;
    float speed_growth = 1.0f + 0.012f * (float)(wave < 18 ? wave : 18);
    stats.hp *= hp_growth * sim.cfg.enemy_hp_scale;
    stats.speed_mult *= sim.cfg.enemy_speed * speed_growth;
    stats.damage *= sim.cfg.enemy_damage_scale;

    if (elite) {
        stats.hp *= 4.0f;
        stats.speed_mult *= 0.88f;
        stats.radius = fmaxf(stats.radius + 0.18f, 0.62f);
        stats.damage *= 2.0f;
    }
    if (boss) {
        stats.hp = (32.0f + 8.0f * (float)wave) * sim.cfg.enemy_hp_scale;
        stats.speed_mult = sim.cfg.enemy_speed * 0.62f;
        stats.radius = 1.20f;
        stats.damage = 4.0f * sim.cfg.enemy_damage_scale;
    }

    stats.hp = fmaxf(1.0f, ceilf(stats.hp));
    stats.damage = fmaxf(1.0f, ceilf(stats.damage));
    *kind_out = kind;
    return stats;
}

PS_D int ps_spawn_enemy(const PSCudaSim& sim, int env) {
    if (sim.enemy_count[env] >= sim.cfg.enemy_cap) return 0;
    int slot = ps_find_free_slot(sim.enemy_active, sim.cfg.enemy_cap, &sim.next_enemy_slot[env], sim.num_envs, env);
    if (slot < 0) return 0;

    float x = 0.0f, y = 0.0f;
    ps_pick_spawn_position(sim, env, 0.45f, &x, &y);
    for (int tries = 0; tries < 16 && ps_overlaps_obstacle(sim, env, x, y, 0.45f, 0.25f); tries++) {
        ps_pick_spawn_position(sim, env, 0.45f, &x, &y);
    }

    int elite = ps_randf(sim, env) < sim.cfg.elite_spawn_rate + 0.0000015f * (float)sim.tick[env];
    int boss = sim.tick[env] > 0 && sim.tick[env] % 900 == 0 && sim.last_boss_tick[env] != sim.tick[env];
    if (boss) sim.last_boss_tick[env] = sim.tick[env];
    int kind = 0;
    PSEnemyDef stats = ps_enemy_stats(sim, env, &kind, elite, boss);
    uint8_t visual_type = (uint8_t)(kind & PS_ENEMY_KIND_MASK);
    if (elite) visual_type |= PS_ENEMY_ELITE_FLAG;
    if (boss) visual_type = PS_ENEMY_BOSS_FLAG;

    int e = PS_EIDX(sim, slot, env);
    sim.enemy_type[e] = visual_type;
    sim.enemy_x[e] = x;
    sim.enemy_y[e] = y;
    sim.enemy_vx[e] = 0.0f;
    sim.enemy_vy[e] = 0.0f;
    sim.enemy_max_hp[e] = stats.hp;
    sim.enemy_hp[e] = stats.hp;
    sim.enemy_radius[e] = stats.radius;
    sim.enemy_speed[e] = stats.speed_mult;
    sim.enemy_damage[e] = stats.damage;
    sim.enemy_active[e] = 1;
    sim.enemy_count[env]++;
    return slot + 1;
}

PS_D void ps_spawn_drop(const PSCudaSim& sim, int env, float x, float y, float value, int type) {
    if (sim.drop_count[env] >= sim.cfg.drop_cap) return;
    int i = ps_find_free_slot(sim.drop_active, sim.cfg.drop_cap, &sim.next_drop_slot[env], sim.num_envs, env);
    if (i < 0) return;
    ps_push_out_obstacles(sim, env, &x, &y, 0.25f, 0);
    int d = PS_DIDX(sim, i, env);
    sim.drop_x[d] = x;
    sim.drop_y[d] = y;
    sim.drop_value[d] = value;
    sim.drop_type[d] = (uint8_t)type;
    sim.drop_active[d] = 1;
    sim.drop_dense_pos[d] = sim.drop_count[env];
    sim.drop_dense[PS_DIDX(sim, sim.drop_count[env], env)] = i;
    sim.drop_count[env]++;
}

PS_D void ps_spawn_projectile(const PSCudaSim& sim, int env, int type, float sx, float sy, float tx, float ty, float damage, float radius, float speed, int pierce, int ttl) {
    if (sim.projectile_count[env] >= sim.cfg.projectile_cap) return;
    int i = ps_find_free_slot(sim.projectile_active, sim.cfg.projectile_cap, &sim.next_projectile_slot[env], sim.num_envs, env);
    if (i < 0) return;
    float dx = tx - sx;
    float dy = ty - sy;
    float dnorm = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
    int p = PS_PIDX(sim, i, env);
    sim.projectile_type[p] = (uint8_t)type;
    sim.projectile_x[p] = sx;
    sim.projectile_y[p] = sy;
    sim.projectile_vx[p] = dx / dnorm * speed;
    sim.projectile_vy[p] = dy / dnorm * speed;
    sim.projectile_damage[p] = damage;
    sim.projectile_radius[p] = radius;
    sim.projectile_ttl[p] = ttl;
    sim.projectile_pierce[p] = pierce;
    sim.projectile_active[p] = 1;
    sim.projectile_dense_pos[p] = sim.projectile_count[env];
    sim.projectile_dense[PS_PIDX(sim, sim.projectile_count[env], env)] = i;
    sim.projectile_count[env]++;
}

PS_D void ps_spawn_area(const PSCudaSim& sim, int env, int type, float x, float y, float radius, float damage, int ttl, int tick_rate) {
    if (sim.area_count[env] >= PS_MAX_AREAS) return;
    int i = ps_find_free_slot(sim.area_active, PS_MAX_AREAS, &sim.next_area_slot[env], sim.num_envs, env);
    if (i < 0) return;
    ps_push_out_obstacles(sim, env, &x, &y, radius, 0);
    int a = PS_AIDX(sim, i, env);
    sim.area_type[a] = (uint8_t)type;
    sim.area_x[a] = x;
    sim.area_y[a] = y;
    sim.area_radius[a] = radius;
    sim.area_damage[a] = damage;
    sim.area_ttl[a] = ttl;
    sim.area_tick_rate[a] = tick_rate < 1 ? 1 : tick_rate;
    sim.area_tick_timer[a] = 0;
    sim.area_active[a] = 1;
    sim.area_dense_pos[a] = sim.area_count[env];
    sim.area_dense[PS_AIDX(sim, sim.area_count[env], env)] = i;
    sim.area_count[env]++;
}

PS_D void ps_rebuild_grid(const PSCudaSim& sim, int env) {
    for (int i = 0; i < sim.grid_touched_count[env]; i++) {
        int cell = sim.grid_touched[PS_EIDX(sim, i, env)];
        sim.grid_head[PS_GIDX(sim, cell, env)] = -1;
    }
    sim.grid_touched_count[env] = 0;
    for (int i = 0; i < sim.cfg.enemy_cap; i++) {
        int e = PS_EIDX(sim, i, env);
        sim.enemy_next[e] = -1;
        if (!sim.enemy_active[e]) continue;
        int cell = ps_cell(sim, env, sim.enemy_x[e], sim.enemy_y[e]);
        int head_idx = PS_GIDX(sim, cell, env);
        if (sim.grid_head[head_idx] == -1 && sim.grid_touched_count[env] < sim.cfg.enemy_cap) {
            sim.grid_touched[PS_EIDX(sim, sim.grid_touched_count[env], env)] = cell;
            sim.grid_touched_count[env]++;
        }
        sim.enemy_next[e] = sim.grid_head[head_idx];
        sim.grid_head[head_idx] = i;
    }
}

PS_D int ps_damage_enemy(const PSCudaSim& sim, int env, int eidx, float damage) {
    int e = PS_EIDX(sim, eidx, env);
    if (!sim.enemy_active[e] || damage <= 0.0f) return 0;
    sim.enemy_hp[e] -= damage;
    sim.rewards[env] += sim.cfg.reward_damage * damage;
    sim.episode_damage_dealt[env] += damage;
    if (sim.enemy_hp[e] > 0.0f) return 0;

    uint8_t type = sim.enemy_type[e];
    int elite = (type & PS_ENEMY_ELITE_FLAG) != 0;
    int boss = (type & PS_ENEMY_BOSS_FLAG) != 0;
    sim.rewards[env] += sim.cfg.reward_kill;
    sim.episode_kills[env] += 1.0f;
    sim.episode_score[env] += boss ? 24.0f : (elite ? 12.0f : 5.0f);
    ps_spawn_drop(sim, env, sim.enemy_x[e], sim.enemy_y[e], boss ? 8.0f : (elite ? 3.0f : 1.0f), 0);
    float missing_hp = sim.max_hp[env] > 0.0f ? ps_clampf((sim.max_hp[env] - sim.hp[env]) / sim.max_hp[env], 0.0f, 1.0f) : 0.0f;
    float health_chance = sim.cfg.health_drop_rate * (1.0f + 2.0f * (float)elite + 5.0f * (float)boss + 1.5f * missing_hp);
    if (ps_randf(sim, env) < health_chance) {
        ps_spawn_drop(sim, env, sim.enemy_x[e] + 0.2f, sim.enemy_y[e] - 0.2f, sim.cfg.health_heal, 1);
    }
    ps_deactivate_enemy(sim, env, eidx);
    return 1;
}

PS_D void ps_wave_spawns(const PSCudaSim& sim, int env) {
    int enemies = sim.enemy_count[env];
    int target = ps_wave_minimum(sim, env);
    int burst = 0;
    while (enemies < target && burst < 4) {
        if (!ps_spawn_enemy(sim, env)) break;
        enemies++;
        burst++;
    }

    int interval = ps_wave_spawn_interval(sim, env);
    if (interval > 0 && sim.tick[env] % interval == 0 && enemies < sim.cfg.enemy_cap) ps_spawn_enemy(sim, env);

    int len = sim.cfg.wave_length_steps > 0 ? sim.cfg.wave_length_steps : 600;
    int local = sim.tick[env] % len;
    int wave = ps_wave_index(sim, env);
    if (local == 1 && (wave == 5 || wave == 10 || wave == 15)) {
        float half = 0.5f * sim.cfg.arena_size;
        float radius = half * 0.82f;
        for (int i = 0; i < 18; i++) {
            int slot = ps_spawn_enemy(sim, env);
            if (!slot) return;
            int idx = slot - 1;
            int e = PS_EIDX(sim, idx, env);
            float angle = 2.0f * PI * ((float)i / 18.0f);
            sim.enemy_x[e] = sim.px[env] + cosf(angle) * radius;
            sim.enemy_y[e] = sim.py[env] + sinf(angle) * radius;
            sim.enemy_speed[e] *= 0.78f;
        }
    }
}

PS_D void ps_update_enemies(const PSCudaSim& sim, int env) {
    float half = 0.5f * sim.cfg.arena_size;
    float far2 = (half * 1.55f) * (half * 1.55f);
    float player_x = sim.px[env];
    float player_y = sim.py[env];
    float player_radius = 0.42f;
    int obstacle_stride = sim.cfg.enemy_obstacle_stride < 1 ? 1 : sim.cfg.enemy_obstacle_stride;
    sim.nearest_enemy[env] = -1;
    sim.nearest_enemy_d2[env] = 1e30f;
    for (int i = 0; i < sim.cfg.enemy_cap; i++) {
        int e = PS_EIDX(sim, i, env);
        if (!sim.enemy_active[e]) continue;
        float dx = player_x - sim.enemy_x[e];
        float dy = player_y - sim.enemy_y[e];
        float d = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
        sim.enemy_vx[e] = dx / d * sim.enemy_speed[e];
        sim.enemy_vy[e] = dy / d * sim.enemy_speed[e];
        float x = sim.enemy_x[e] + sim.enemy_vx[e];
        float y = sim.enemy_y[e] + sim.enemy_vy[e];
        if (obstacle_stride <= 1 || ((sim.tick[env] + i) % obstacle_stride) == 0) {
            ps_push_out_obstacles(sim, env, &x, &y, sim.enemy_radius[e], 0);
        }
        sim.enemy_x[e] = x;
        sim.enemy_y[e] = y;
        float post_dx = sim.enemy_x[e] - player_x;
        float post_dy = sim.enemy_y[e] - player_y;
        float post_d2 = post_dx * post_dx + post_dy * post_dy;
        if (post_d2 > far2) {
            float nx, ny;
            ps_pick_spawn_position(sim, env, sim.enemy_radius[e], &nx, &ny);
            sim.enemy_x[e] = nx;
            sim.enemy_y[e] = ny;
            // Original code compares exact visual type to 2. Masking would make
            // elite kind-2 behavior more intuitive, but this keeps parity.
            if ((sim.enemy_type[e] & PS_ENEMY_KIND_MASK) != 2) sim.enemy_hp[e] = sim.enemy_max_hp[e];
            continue;
        }
        if (post_d2 < sim.nearest_enemy_d2[env]) {
            sim.nearest_enemy_d2[env] = post_d2;
            sim.nearest_enemy[env] = i;
        }
        float hit = sim.enemy_radius[e] + player_radius;
        if (sim.invuln_timer[env] <= 0 && post_d2 < hit * hit) {
            float dmg = fmaxf(1.0f, ceilf(sim.enemy_damage[e] * sim.cfg.contact_damage));
            sim.hp[env] -= dmg;
            sim.rewards[env] += sim.cfg.reward_hurt * dmg;
            sim.episode_damage_taken[env] += dmg;
            sim.invuln_timer[env] = sim.cfg.invuln_steps;
        }
    }
}

PS_D void ps_update_projectiles(const PSCudaSim& sim, int env) {
    float half = 0.5f * sim.cfg.arena_size;
    for (int k = 0; k < sim.projectile_count[env]; ) {
        int i = sim.projectile_dense[PS_PIDX(sim, k, env)];
        int p = PS_PIDX(sim, i, env);
        sim.projectile_x[p] += sim.projectile_vx[p];
        sim.projectile_y[p] += sim.projectile_vy[p];
        sim.projectile_ttl[p]--;
        if (sim.projectile_ttl[p] <= 0 || fabsf(sim.projectile_x[p] - sim.px[env]) > half || fabsf(sim.projectile_y[p] - sim.py[env]) > half) {
            ps_deactivate_projectile(sim, env, i);
            continue;
        }
        int blocked = 0;
        for (int o = 0; o < sim.cfg.obstacle_count; o++) {
            int oi = PS_OIDX(sim, o, env);
            float r = sim.obstacle_radius[oi] + sim.projectile_radius[p];
            float dx = sim.projectile_x[p] - sim.obstacle_x[oi];
            if (dx >= r || dx <= -r) continue;
            float dy = sim.projectile_y[p] - sim.obstacle_y[oi];
            if (dy >= r || dy <= -r) continue;
            if (dx * dx + dy * dy < r * r) {
                blocked = 1;
                break;
            }
        }
        if (blocked) {
            ps_deactivate_projectile(sim, env, i);
            continue;
        }

        int cell = ps_cell(sim, env, sim.projectile_x[p], sim.projectile_y[p]);
        int cx = cell % PS_GRID_W;
        int cy = cell / PS_GRID_W;
        for (int oy = -1; oy <= 1 && sim.projectile_active[p]; oy++) {
            int gy = cy + oy;
            if (gy < 0 || gy >= PS_GRID_H) continue;
            for (int ox = -1; ox <= 1 && sim.projectile_active[p]; ox++) {
                int gx = cx + ox;
                if (gx < 0 || gx >= PS_GRID_W) continue;
                for (int eidx = sim.grid_head[PS_GIDX(sim, gy * PS_GRID_W + gx, env)]; eidx >= 0; eidx = sim.enemy_next[PS_EIDX(sim, eidx, env)]) {
                    int e = PS_EIDX(sim, eidx, env);
                    if (!sim.enemy_active[e]) continue;
                    float r = sim.enemy_radius[e] + sim.projectile_radius[p];
                    if (ps_dist2(sim.projectile_x[p], sim.projectile_y[p], sim.enemy_x[e], sim.enemy_y[e]) >= r * r) continue;
                    ps_damage_enemy(sim, env, eidx, sim.projectile_damage[p]);
                    if (sim.projectile_pierce[p] <= 0) ps_deactivate_projectile(sim, env, i);
                    else sim.projectile_pierce[p]--;
                    break;
                }
            }
        }
        if (sim.projectile_active[p]) k++;
    }
}

PS_D void ps_update_drops(const PSCudaSim& sim, int env) {
    float magnet = sim.cfg.magnet_radius * (1.0f + sim.magnet_bonus[env]);
    for (int k = 0; k < sim.drop_count[env]; ) {
        int i = sim.drop_dense[PS_DIDX(sim, k, env)];
        int d = PS_DIDX(sim, i, env);
        float dx = sim.px[env] - sim.drop_x[d];
        float dy = sim.py[env] - sim.drop_y[d];
        float dist2 = dx * dx + dy * dy;
        if (dist2 < magnet * magnet) {
            float dist = sqrtf(fmaxf(dist2, 0.0001f));
            sim.drop_x[d] += dx / dist * 0.28f;
            sim.drop_y[d] += dy / dist * 0.28f;
        }
        if (dist2 < sim.cfg.pickup_radius * sim.cfg.pickup_radius) {
            if (sim.drop_type[d] == 1) {
                sim.hp[env] = fminf(sim.max_hp[env], sim.hp[env] + sim.drop_value[d]);
                sim.rewards[env] += 0.03f;
            } else {
                sim.xp[env] += sim.drop_value[d];
                sim.episode_xp[env] += sim.drop_value[d];
                sim.rewards[env] += 0.03f + sim.cfg.reward_xp * sim.drop_value[d];
                sim.episode_score[env] += sim.drop_value[d];
            }
            sim.episode_pickups[env] += 1.0f;
            ps_deactivate_drop(sim, env, i);
            continue;
        }
        k++;
    }
    while (sim.xp[env] >= ps_xp_threshold(sim, env)) {
        sim.xp[env] -= ps_xp_threshold(sim, env);
        sim.level[env]++;
        sim.rewards[env] += 1.0f;
        sim.queued_upgrades[env]++;
        ps_offer_upgrades(sim, env);
    }
}

PS_D int ps_nearest_enemy(const PSCudaSim& sim, int env, float range) {
    float best_d2 = range * range;
    int cached = sim.nearest_enemy[env];
    if (cached >= 0 && cached < sim.cfg.enemy_cap) {
        int e = PS_EIDX(sim, cached, env);
        if (sim.enemy_active[e] && sim.nearest_enemy_d2[env] < best_d2) return cached;
    }
    int best = -1;
    for (int i = 0; i < sim.cfg.enemy_cap; i++) {
        int e = PS_EIDX(sim, i, env);
        if (!sim.enemy_active[e]) continue;
        float d2 = ps_dist2(sim.px[env], sim.py[env], sim.enemy_x[e], sim.enemy_y[e]);
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

PS_D void ps_damage_radius(const PSCudaSim& sim, int env, float x, float y, float radius, float damage, float knockback) {
    if (knockback > 0.0f) {
        sim.nearest_enemy[env] = -1;
        sim.nearest_enemy_d2[env] = 1e30f;
    }

    const float query_pad = 1.50f;
    float half = 0.5f * sim.cfg.arena_size;
    float inv_cell_x = (float)PS_GRID_W / sim.cfg.arena_size;
    float inv_cell_y = (float)PS_GRID_H / sim.cfg.arena_size;
    float qr = radius + query_pad;
    int min_gx = (int)floorf((x - qr - sim.px[env] + half) * inv_cell_x);
    int max_gx = (int)floorf((x + qr - sim.px[env] + half) * inv_cell_x);
    int min_gy = (int)floorf((y - qr - sim.py[env] + half) * inv_cell_y);
    int max_gy = (int)floorf((y + qr - sim.py[env] + half) * inv_cell_y);
    min_gx = min_gx < 0 ? 0 : (min_gx >= PS_GRID_W ? PS_GRID_W - 1 : min_gx);
    max_gx = max_gx < 0 ? 0 : (max_gx >= PS_GRID_W ? PS_GRID_W - 1 : max_gx);
    min_gy = min_gy < 0 ? 0 : (min_gy >= PS_GRID_H ? PS_GRID_H - 1 : min_gy);
    max_gy = max_gy < 0 ? 0 : (max_gy >= PS_GRID_H ? PS_GRID_H - 1 : max_gy);

    for (int gy = min_gy; gy <= max_gy; gy++) {
        for (int gx = min_gx; gx <= max_gx; gx++) {
            int eidx = sim.grid_head[PS_GIDX(sim, gy * PS_GRID_W + gx, env)];
            while (eidx >= 0) {
                int e = PS_EIDX(sim, eidx, env);
                int next = sim.enemy_next[e];
                if (sim.enemy_active[e]) {
                    float dx = sim.enemy_x[e] - x;
                    float dy = sim.enemy_y[e] - y;
                    float hit_r = radius + sim.enemy_radius[e];
                    float d2 = dx * dx + dy * dy;
                    if (d2 <= hit_r * hit_r) {
                        int killed = ps_damage_enemy(sim, env, eidx, damage);
                        if (!killed && knockback > 0.0f && sim.enemy_active[e]) {
                            float d = sqrtf(fmaxf(d2, 0.0001f));
                            sim.enemy_x[e] += dx / d * knockback;
                            sim.enemy_y[e] += dy / d * knockback;
                        }
                    }
                }
                eidx = next;
            }
        }
    }
}
PS_D void ps_update_areas(const PSCudaSim& sim, int env) {
    int active_ink = 0;
    for (int k = 0; k < sim.area_count[env]; ) {
        int i = sim.area_dense[PS_AIDX(sim, k, env)];
        int a = PS_AIDX(sim, i, env);
        if (sim.area_type[a] == PS_WEAPON_INK) active_ink++;
        sim.area_ttl[a]--;
        sim.area_tick_timer[a]--;
        if (sim.area_tick_timer[a] <= 0 && sim.area_damage[a] > 0.0f) {
            ps_damage_radius(sim, env, sim.area_x[a], sim.area_y[a], sim.area_radius[a], sim.area_damage[a], 0.02f);
            sim.area_tick_timer[a] = sim.area_tick_rate[a];
        }
        if (sim.area_ttl[a] <= 0) {
            ps_deactivate_area(sim, env, i);
            continue;
        }
        k++;
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_INK, env)] = ps_clampf((float)active_ink / 8.0f, 0.0f, 1.0f);
}

PS_D void ps_cast_bubble(const PSCudaSim& sim, int env, int level) {
    int target = ps_nearest_enemy(sim, env, 18.0f + 4.0f * sim.area_bonus[env]);
    if (target < 0) return;
    int shots = 1 + level / 3;
    PSWeaponDef def = PS_D_WEAPON_DEFS[PS_WEAPON_BUBBLE];
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_BUBBLE, level, 1);
    float radius = def.base_radius * (1.0f + sim.area_bonus[env]);
    float speed = sim.cfg.projectile_speed * (1.0f + sim.projectile_speed_bonus[env]);
    int pierce = sim.pierce_bonus[env] + level / 4;
    int e = PS_EIDX(sim, target, env);
    for (int i = 0; i < shots; i++) {
        float jitter = ((float)i - 0.5f * (float)(shots - 1)) * 0.35f;
        ps_spawn_projectile(sim, env, PS_WEAPON_BUBBLE, sim.px[env], sim.py[env], sim.enemy_x[e] + jitter, sim.enemy_y[e] - jitter, damage, radius, speed, pierce, 100);
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_BUBBLE, env)] = 1.0f;
}

PS_D void ps_cast_whirlpool(const PSCudaSim& sim, int env, int level) {
    PSWeaponDef def = PS_D_WEAPON_DEFS[PS_WEAPON_WHIRLPOOL];
    float radius = (def.base_radius + def.radius_per_level * (float)(level - 1)) * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_WHIRLPOOL, level, 0);
    ps_damage_radius(sim, env, sim.px[env], sim.py[env], radius, damage, 0.06f);
    ps_spawn_area(sim, env, PS_WEAPON_WHIRLPOOL, sim.px[env], sim.py[env], radius, 0.0f, 18, 99);
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_WHIRLPOOL, env)] = 1.0f;
}

PS_D void ps_cast_orbit(const PSCudaSim& sim, int env, int level) {
    PSWeaponDef def = PS_D_WEAPON_DEFS[PS_WEAPON_ORBIT];
    int count = 1 + level / 2;
    float orbit_r = (2.25f + 0.18f * (float)level) * (1.0f + 0.5f * sim.area_bonus[env]);
    float hit_r = (def.base_radius + def.radius_per_level * (float)level) * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_ORBIT, level, 0);
    for (int i = 0; i < count; i++) {
        float a = sim.orbit_phase[env] + 2.0f * PI * ((float)i / (float)count);
        ps_damage_radius(sim, env, sim.px[env] + cosf(a) * orbit_r, sim.py[env] + sinf(a) * orbit_r, hit_r, damage, 0.04f);
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_ORBIT, env)] = 1.0f;
}

PS_D void ps_cast_ink(const PSCudaSim& sim, int env, int level) {
    int target = ps_nearest_enemy(sim, env, sim.cfg.arena_size * 0.50f);
    if (target < 0) return;
    int pools = 1 + level / 3;
    PSWeaponDef def = PS_D_WEAPON_DEFS[PS_WEAPON_INK];
    float radius = (def.base_radius + def.radius_per_level * (float)level) * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_INK, level, 0);
    int ttl = 140 + 12 * level;
    int e = PS_EIDX(sim, target, env);
    for (int i = 0; i < pools; i++) {
        float angle = 2.0f * PI * ((float)i / (float)pools) + ps_randf(sim, env) * 0.5f;
        float dist = pools > 1 ? 1.4f : 0.0f;
        ps_spawn_area(sim, env, PS_WEAPON_INK, sim.enemy_x[e] + cosf(angle) * dist, sim.enemy_y[e] + sinf(angle) * dist, radius, damage, ttl, 8);
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_INK, env)] = 1.0f;
}

PS_D void ps_update_poison_oil_trail(const PSCudaSim& sim, int env, int level) {
    if (level <= 0) return;
    float speed2 = sim.pvx[env] * sim.pvx[env] + sim.pvy[env] * sim.pvy[env];
    if (speed2 < 0.0004f) return;

    int active_oil = 0;
    for (int k = 0; k < sim.area_count[env]; k++) {
        int i = sim.area_dense[PS_AIDX(sim, k, env)];
        int a = PS_AIDX(sim, i, env);
        active_oil += sim.area_active[a] && sim.area_type[a] == PS_WEAPON_INK;
    }
    int max_oil = 14 + level * 4;
    if (active_oil >= max_oil) return;

    int cadence = 6 - level / 2;
    cadence = cadence < 3 ? 3 : cadence;
    if (sim.tick[env] % cadence != 0) return;

    PSWeaponDef def = PS_D_WEAPON_DEFS[PS_WEAPON_INK];
    float speed = sqrtf(speed2);
    float nx = sim.pvx[env] / speed;
    float ny = sim.pvy[env] / speed;
    float radius = (0.88f + 0.10f * (float)level + 0.10f * def.radius_per_level * (float)level) * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_INK, level, 0) * 0.36f;
    int ttl = 42 + 8 * level;
    ps_spawn_area(sim, env, PS_WEAPON_INK, sim.px[env] - nx * 0.95f, sim.py[env] - ny * 0.95f, radius, damage, ttl, 6);
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_INK, env)] = 1.0f;
}

PS_D void ps_cast_sonar(const PSCudaSim& sim, int env, int level) {
    PSWeaponDef def = PS_D_WEAPON_DEFS[PS_WEAPON_SONAR];
    float radius = (def.base_radius + def.radius_per_level * (float)level) * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_SONAR, level, 0);
    ps_damage_radius(sim, env, sim.px[env], sim.py[env], radius, damage, 0.50f);
    ps_spawn_area(sim, env, PS_WEAPON_SONAR, sim.px[env], sim.py[env], radius, 0.0f, 24, 99);
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_SONAR, env)] = 1.0f;
}

PS_D void ps_update_weapons(const PSCudaSim& sim, int env) {
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        int w = PS_WIDX(sim, i, env);
        sim.weapon_active[w] *= 0.82f;
        if (sim.weapon_cd[w] > 0.0f) sim.weapon_cd[w] -= 1.0f;
    }
    sim.orbit_phase[env] += 0.045f + 0.006f * (float)sim.weapon_level[PS_WIDX(sim, PS_WEAPON_ORBIT, env)];
    ps_update_areas(sim, env);
    ps_update_poison_oil_trail(sim, env, sim.weapon_level[PS_WIDX(sim, PS_WEAPON_INK, env)]);

    for (int weapon = 0; weapon < PS_WEAPON_COUNT; weapon++) {
        int level = sim.weapon_level[PS_WIDX(sim, weapon, env)];
        if (level <= 0 || sim.weapon_cd[PS_WIDX(sim, weapon, env)] > 0.0f) continue;
        switch (weapon) {
            case PS_WEAPON_BUBBLE: ps_cast_bubble(sim, env, level); break;
            case PS_WEAPON_WHIRLPOOL: ps_cast_whirlpool(sim, env, level); break;
            case PS_WEAPON_ORBIT: ps_cast_orbit(sim, env, level); break;
            case PS_WEAPON_INK: ps_cast_ink(sim, env, level); break;
            case PS_WEAPON_SONAR: ps_cast_sonar(sim, env, level); break;
        }
        sim.weapon_cd[PS_WIDX(sim, weapon, env)] = ps_weapon_cooldown_total(sim, env, weapon);
    }
}

PS_D void ps_reset_env(const PSCudaSim& sim, int env, int clear_outputs) {
    if (clear_outputs) {
        sim.rewards[env] = 0.0f;
        sim.terminals[env] = 0.0f;
    }
    sim.px[env] = 0.0f;
    sim.py[env] = 0.0f;
    sim.pvx[env] = 0.0f;
    sim.pvy[env] = 0.0f;
    sim.player_facing_left[env] = 0;
    sim.max_hp[env] = fmaxf(1.0f, floorf(sim.cfg.player_health));
    sim.hp[env] = sim.max_hp[env];
    sim.xp[env] = 0.0f;
    sim.level[env] = 1;
    sim.speed_bonus[env] = 0.0f;
    sim.damage_bonus[env] = 0.0f;
    sim.cooldown_mult[env] = 1.0f;
    sim.projectile_speed_bonus[env] = 0.0f;
    sim.magnet_bonus[env] = 0.0f;
    sim.area_bonus[env] = 0.0f;
    sim.pierce_bonus[env] = 0;
    sim.pending_upgrade[env] = 0;
    sim.queued_upgrades[env] = 0;
    sim.last_boss_tick[env] = -1;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        sim.weapon_cd[PS_WIDX(sim, i, env)] = 0.0f;
        sim.weapon_active[PS_WIDX(sim, i, env)] = 0.0f;
        sim.weapon_level[PS_WIDX(sim, i, env)] = 0;
    }
    sim.weapon_level[PS_WIDX(sim, PS_WEAPON_BUBBLE, env)] = 1;
    sim.orbit_phase[env] = ps_randf(sim, env) * 2.0f * PI;
    sim.tick[env] = 0;
    sim.invuln_timer[env] = 0;
    sim.episode_return[env] = 0.0f;
    sim.episode_score[env] = 0.0f;
    sim.episode_kills[env] = 0.0f;
    sim.episode_xp[env] = 0.0f;
    sim.episode_damage_dealt[env] = 0.0f;
    sim.episode_damage_taken[env] = 0.0f;
    sim.episode_pickups[env] = 0.0f;
    sim.episode_levelups[env] = 0.0f;
    sim.episode_obstacle_hits[env] = 0.0f;
    sim.nearest_enemy[env] = -1;
    sim.nearest_enemy_d2[env] = 1e30f;
    ps_clear_entities(sim, env);
    ps_spawn_obstacles(sim, env);
    ps_compute_observations(sim, env);
}

PS_D void ps_step_env(const PSCudaSim& sim, int env) {
    sim.rewards[env] = sim.cfg.reward_survival;
    sim.terminals[env] = 0.0f;
    sim.tick[env]++;
    if (sim.invuln_timer[env] > 0) sim.invuln_timer[env]--;

    int upgrade_action = (int)ps_action_get(sim, env, 1);
    if (sim.pending_upgrade[env] && upgrade_action < PS_UPGRADE_SLOTS) ps_apply_upgrade(sim, env, upgrade_action);

    int action = (int)ps_action_get(sim, env, 0);
    action = action < 0 ? 0 : (action > 8 ? 8 : action);
    float dx = 0.0f;
    float dy = 0.0f;
    switch (action) {
        case 1: dy = -1.0f; break;
        case 2: dy = 1.0f; break;
        case 3: dx = -1.0f; break;
        case 4: dx = 1.0f; break;
        case 5: dx = -0.70710678f; dy = -0.70710678f; break;
        case 6: dx = 0.70710678f; dy = -0.70710678f; break;
        case 7: dx = -0.70710678f; dy = 0.70710678f; break;
        case 8: dx = 0.70710678f; dy = 0.70710678f; break;
        default: break;
    }
    float speed = sim.cfg.player_speed * (1.0f + sim.speed_bonus[env]);
    float target_vx = dx * speed;
    float target_vy = dy * speed;
    if (target_vx < -0.001f) sim.player_facing_left[env] = 1;
    else if (target_vx > 0.001f) sim.player_facing_left[env] = 0;
    sim.pvx[env] += (target_vx - sim.pvx[env]) * 0.35f;
    sim.pvy[env] += (target_vy - sim.pvy[env]) * 0.35f;
    float v2 = sim.pvx[env] * sim.pvx[env] + sim.pvy[env] * sim.pvy[env];
    if (v2 > speed * speed) {
        float inv = speed / sqrtf(v2);
        sim.pvx[env] *= inv;
        sim.pvy[env] *= inv;
    }
    sim.px[env] += sim.pvx[env];
    sim.py[env] += sim.pvy[env];
    float px = sim.px[env];
    float py = sim.py[env];
    ps_push_out_obstacles(sim, env, &px, &py, 0.42f, 1);
    sim.px[env] = px;
    sim.py[env] = py;
    ps_recycle_far_obstacles(sim, env);

    ps_wave_spawns(sim, env);
    ps_update_enemies(sim, env);
    ps_rebuild_grid(sim, env);
    ps_update_weapons(sim, env);
    if (sim.projectile_count[env] > 0) {
        ps_update_projectiles(sim, env);
    }
    ps_update_drops(sim, env);

    sim.episode_return[env] += sim.rewards[env];
    if (sim.hp[env] <= 0.0f || sim.tick[env] >= sim.cfg.max_steps) {
        int survived = sim.tick[env] >= sim.cfg.max_steps && sim.hp[env] > 0.0f;
        if (!survived) {
            sim.rewards[env] += sim.cfg.reward_death;
            sim.episode_return[env] += sim.cfg.reward_death;
        }
        sim.terminals[env] = 1.0f;
        ps_add_log(sim, env, survived);
        ps_reset_env(sim, env, 0);  // preserve final reward + terminal flag for wrapper/learner
        return;
    }

    ps_compute_observations(sim, env);
}

// -----------------------------------------------------------------------------
// Kernels and host launchers
// -----------------------------------------------------------------------------

__global__ void ps_reset_all_kernel(PSCudaSim sim, uint32_t seed) {
    int env = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= sim.num_envs) return;
    uint32_t s = seed ? seed : 1u;
    // Deterministic, distinct nonzero per-env seed.
    uint32_t x = s ^ (0x9e3779b9u * (uint32_t)(env + 1));
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    sim.rng[env] = x ? x : 1u;
    ps_reset_env(sim, env, 1);
}

__global__ void ps_step_kernel(PSCudaSim sim) {
    int env = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= sim.num_envs) return;
    ps_step_env(sim, env);
}

__global__ void ps_step_range_kernel(PSCudaSim sim, int start, int count) {
    int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count) return;
    int env = start + lane;
    if (env >= sim.num_envs) return;
    ps_step_env(sim, env);
}

__global__ void ps_reduce_logs_kernel(PSCudaSim sim, int clear) {
    int env = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= sim.num_envs) return;

    atomicAdd(&sim.log_reduced->perf, sim.log_perf[env]);
    atomicAdd(&sim.log_reduced->score, sim.log_score[env]);
    atomicAdd(&sim.log_reduced->episode_return, sim.log_episode_return[env]);
    atomicAdd(&sim.log_reduced->episode_length, sim.log_episode_length[env]);
    atomicAdd(&sim.log_reduced->kills, sim.log_kills[env]);
    atomicAdd(&sim.log_reduced->level, sim.log_level[env]);
    atomicAdd(&sim.log_reduced->xp, sim.log_xp[env]);
    atomicAdd(&sim.log_reduced->damage_dealt, sim.log_damage_dealt[env]);
    atomicAdd(&sim.log_reduced->damage_taken, sim.log_damage_taken[env]);
    atomicAdd(&sim.log_reduced->pickups, sim.log_pickups[env]);
    atomicAdd(&sim.log_reduced->levelups, sim.log_levelups[env]);
    atomicAdd(&sim.log_reduced->obstacle_hits, sim.log_obstacle_hits[env]);
    atomicAdd(&sim.log_reduced->enemies_alive, sim.log_enemies_alive[env]);
    atomicAdd(&sim.log_reduced->projectiles_alive, sim.log_projectiles_alive[env]);
    atomicAdd(&sim.log_reduced->drops_alive, sim.log_drops_alive[env]);
    atomicAdd(&sim.log_reduced->areas_alive, sim.log_areas_alive[env]);
    atomicAdd(&sim.log_reduced->weapon_levels, sim.log_weapon_levels[env]);
    atomicAdd(&sim.log_reduced->wave, sim.log_wave[env]);
    atomicAdd(&sim.log_reduced->hp, sim.log_hp[env]);
    atomicAdd(&sim.log_reduced->survived, sim.log_survived[env]);
    atomicAdd(&sim.log_reduced->n, sim.log_n[env]);

    if (clear) {
        sim.log_perf[env] = 0.0f;
        sim.log_score[env] = 0.0f;
        sim.log_episode_return[env] = 0.0f;
        sim.log_episode_length[env] = 0.0f;
        sim.log_kills[env] = 0.0f;
        sim.log_level[env] = 0.0f;
        sim.log_xp[env] = 0.0f;
        sim.log_damage_dealt[env] = 0.0f;
        sim.log_damage_taken[env] = 0.0f;
        sim.log_pickups[env] = 0.0f;
        sim.log_levelups[env] = 0.0f;
        sim.log_obstacle_hits[env] = 0.0f;
        sim.log_enemies_alive[env] = 0.0f;
        sim.log_projectiles_alive[env] = 0.0f;
        sim.log_drops_alive[env] = 0.0f;
        sim.log_areas_alive[env] = 0.0f;
        sim.log_weapon_levels[env] = 0.0f;
        sim.log_wave[env] = 0.0f;
        sim.log_hp[env] = 0.0f;
        sim.log_survived[env] = 0.0f;
        sim.log_n[env] = 0.0f;
    }
}

static inline void ps_cuda_reset_all(PSCudaSim* sim, uint32_t seed, cudaStream_t stream = 0) {
    int blocks = (sim->num_envs + PS_CUDA_BLOCK_SIZE - 1) / PS_CUDA_BLOCK_SIZE;
    ps_reset_all_kernel<<<blocks, PS_CUDA_BLOCK_SIZE, 0, stream>>>(*sim, seed);
    PS_CUDA_CHECK(cudaGetLastError());
}

static inline void ps_cuda_step_range(PSCudaSim* sim, int start, int count, cudaStream_t stream = 0) {
    if (count <= 0) return;
    if (start < 0) {
        count += start;
        start = 0;
    }
    if (start >= sim->num_envs || count <= 0) return;
    if (start + count > sim->num_envs) count = sim->num_envs - start;
    int blocks = (count + PS_CUDA_BLOCK_SIZE - 1) / PS_CUDA_BLOCK_SIZE;
    ps_step_range_kernel<<<blocks, PS_CUDA_BLOCK_SIZE, 0, stream>>>(*sim, start, count);
    PS_CUDA_CHECK(cudaGetLastError());
}

PSCudaSim* ps_cuda_sim_create(
        int num_envs,
        PSConfig cfg,
        float* observations,
        float* actions,
        float* rewards,
        float* terminals) {
    PSCudaSim* sim = (PSCudaSim*)std::calloc(1, sizeof(PSCudaSim));
    if (sim == nullptr) return nullptr;
    ps_cuda_alloc_with_io(sim, num_envs, cfg, observations, actions, rewards, terminals);
    return sim;
}

void ps_cuda_sim_reset(PSCudaSim* sim, unsigned int seed, void* stream) {
    if (sim == nullptr) return;
    ps_cuda_reset_all(sim, seed, (cudaStream_t)stream);
}

void ps_cuda_sim_step_range(PSCudaSim* sim, int start, int count, void* stream) {
    if (sim == nullptr) return;
    ps_cuda_step_range(sim, start, count, (cudaStream_t)stream);
}

float ps_cuda_sim_log(PSCudaSim* sim, Log* out, int clear, void* stream) {
    (void)stream;
    if (sim == nullptr || out == nullptr) return 0.0f;
    std::memset(out, 0, sizeof(*out));

    // Logging is called from the Python wrapper without a stream argument.
    // Synchronize once, reduce all fields on-device, and copy one Log struct.
    PS_CUDA_CHECK(cudaDeviceSynchronize());
    PS_CUDA_CHECK(cudaMemset(sim->log_reduced, 0, sizeof(Log)));
    int blocks = (sim->num_envs + PS_CUDA_BLOCK_SIZE - 1) / PS_CUDA_BLOCK_SIZE;
    ps_reduce_logs_kernel<<<blocks, PS_CUDA_BLOCK_SIZE>>>(*sim, clear);
    PS_CUDA_CHECK(cudaGetLastError());

    Log sums;
    PS_CUDA_CHECK(cudaMemcpy(&sums, sim->log_reduced, sizeof(Log), cudaMemcpyDeviceToHost));
    float n = sums.n;
    if (n <= 0.0f) return 0.0f;

    out->perf = sums.perf / n;
    out->score = sums.score / n;
    out->episode_return = sums.episode_return / n;
    out->episode_length = sums.episode_length / n;
    out->kills = sums.kills / n;
    out->level = sums.level / n;
    out->xp = sums.xp / n;
    out->damage_dealt = sums.damage_dealt / n;
    out->damage_taken = sums.damage_taken / n;
    out->pickups = sums.pickups / n;
    out->levelups = sums.levelups / n;
    out->obstacle_hits = sums.obstacle_hits / n;
    out->enemies_alive = sums.enemies_alive / n;
    out->projectiles_alive = sums.projectiles_alive / n;
    out->drops_alive = sums.drops_alive / n;
    out->areas_alive = sums.areas_alive / n;
    out->weapon_levels = sums.weapon_levels / n;
    out->wave = sums.wave / n;
    out->hp = sums.hp / n;
    out->survived = sums.survived / n;
    out->n = 1.0f;
    return n;
}

void ps_cuda_sim_destroy(PSCudaSim* sim) {
    if (sim == nullptr) return;
    ps_cuda_free(sim);
    std::free(sim);
}
