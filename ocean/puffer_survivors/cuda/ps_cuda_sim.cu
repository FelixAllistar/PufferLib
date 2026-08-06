// Puffer Survivors CUDA simulator implementation.
// -----------------------------------------------------------------------------
// This file owns the internal SoA state and kernels used by the native 5c
// puf_envs_* implementation in puffer_survivors.cu.
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
#include "../ps_geometry.h"
#include "../ps_log.h"
#include "../ps_observation_layout.h"

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
#define PS_MOIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_OIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_GIDX(sim, i, env) ((i) * (sim).num_envs + (env))
#define PS_WIDX(sim, weapon, env) ((weapon) * (sim).num_envs + (env))
#define PS_UIDX(sim, slot, env) ((slot) * (sim).num_envs + (env))

// -----------------------------------------------------------------------------
// Config and simulator SoA
// -----------------------------------------------------------------------------



struct PSCudaSim {
    int num_envs;
    PSConfig cfg;
    int owns_io;
    Env* native_envs;

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
    uint8_t *enemy_active, *enemy_type, *enemy_shape;
    float *enemy_x, *enemy_y, *enemy_vx, *enemy_vy;
    float *enemy_hp, *enemy_max_hp, *enemy_radius, *enemy_bound_radius;
    float *enemy_half_width, *enemy_half_height, *enemy_speed, *enemy_damage;
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

    // Moving obstacle pool [PS_MAX_MOVING_OBSTACLES, N].
    uint8_t *moving_obstacle_active, *moving_obstacle_type, *moving_obstacle_shape;
    float *moving_obstacle_x, *moving_obstacle_y;
    float *moving_obstacle_vx, *moving_obstacle_vy;
    float *moving_obstacle_bound_radius;
    float *moving_obstacle_half_width, *moving_obstacle_half_height;
    int *moving_obstacle_ttl, *moving_obstacle_dense, *moving_obstacle_dense_pos;

    // Grid [PS_GRID_CELLS, N]
    int* grid_head;
    int* grid_touched;       // [PS_MAX_ENEMIES, N]
    int* grid_touched_count; // [N]
    int* aabb_indices;       // [PS_MAX_ENEMIES, N]
    int* aabb_count;         // [N]

    // Counts/cursors.
    int* nearest_enemy;
    float* nearest_enemy_d2;
    int *enemy_count, *projectile_count, *drop_count, *area_count, *moving_obstacle_count, *active_ink_count;
    int *next_enemy_slot, *next_projectile_slot, *next_drop_slot, *next_area_slot, *next_moving_obstacle_slot;

    // Episode stats.
    float *episode_return;
    float *episode_reward_survival, *episode_reward_damage, *episode_reward_kill;
    float *episode_reward_hurt, *episode_reward_pickup, *episode_reward_xp;
    float *episode_reward_levelup, *episode_reward_obstacle, *episode_reward_terminal;
    float *episode_score, *episode_kills, *episode_xp;
    float *episode_damage_dealt, *episode_damage_taken, *episode_pickups;
    float *episode_levelups, *episode_obstacle_hits;
    float *episode_peak_enemies, *episode_peak_projectiles, *episode_min_hp;

    // Accumulated logs over completed episodes, SoA fields.
    float *log_perf, *log_score, *log_episode_return;
    float *log_reward_survival, *log_reward_damage, *log_reward_kill;
    float *log_reward_hurt, *log_reward_pickup, *log_reward_xp;
    float *log_reward_levelup, *log_reward_obstacle, *log_reward_terminal;
    float *log_episode_length;
    float *log_kills, *log_level, *log_xp, *log_damage_dealt, *log_damage_taken;
    float *log_pickups, *log_levelups, *log_obstacle_hits;
    float *log_enemies_alive, *log_projectiles_alive, *log_drops_alive, *log_areas_alive;
    float *log_weapon_levels, *log_wave, *log_hp, *log_survived, *log_n;
    float *log_death_0_25, *log_death_25_50, *log_death_50_75, *log_death_75_100;
    float *log_success, *log_peak_enemies, *log_peak_projectiles, *log_min_hp;
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
    ps_config_validate(&cfg);
    sim->cfg = cfg;
    sim->owns_io = 1;

    const size_t N = (size_t)num_envs;
    const size_t NE = (size_t)sim->cfg.enemy_cap * N;
    const size_t NP = (size_t)sim->cfg.projectile_cap * N;
    const size_t ND = (size_t)sim->cfg.drop_cap * N;
    const size_t NA = (size_t)PS_MAX_AREAS * N;
    const size_t NO = (size_t)(sim->cfg.obstacle_count > 0 ? sim->cfg.obstacle_count : 1) * N;
    const size_t NMO = (size_t)PS_MAX_MOVING_OBSTACLES * N;
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

    PS_ALLOC_FIELD(sim, enemy_active, NE); PS_ALLOC_FIELD(sim, enemy_type, NE); PS_ALLOC_FIELD(sim, enemy_shape, NE);
    PS_ALLOC_FIELD(sim, enemy_x, NE); PS_ALLOC_FIELD(sim, enemy_y, NE); PS_ALLOC_FIELD(sim, enemy_vx, NE); PS_ALLOC_FIELD(sim, enemy_vy, NE);
    PS_ALLOC_FIELD(sim, enemy_hp, NE); PS_ALLOC_FIELD(sim, enemy_max_hp, NE); PS_ALLOC_FIELD(sim, enemy_radius, NE); PS_ALLOC_FIELD(sim, enemy_bound_radius, NE);
    PS_ALLOC_FIELD(sim, enemy_half_width, NE); PS_ALLOC_FIELD(sim, enemy_half_height, NE);
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
    PS_ALLOC_FIELD(sim, moving_obstacle_active, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_type, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_shape, NMO);
    PS_ALLOC_FIELD(sim, moving_obstacle_x, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_y, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_vx, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_vy, NMO);
    PS_ALLOC_FIELD(sim, moving_obstacle_bound_radius, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_half_width, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_half_height, NMO);
    PS_ALLOC_FIELD(sim, moving_obstacle_ttl, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_dense, NMO); PS_ALLOC_FIELD(sim, moving_obstacle_dense_pos, NMO);
    PS_ALLOC_FIELD(sim, grid_head, NG);
    PS_ALLOC_FIELD(sim, grid_touched, NGT); PS_ALLOC_FIELD(sim, grid_touched_count, N);
    PS_ALLOC_FIELD(sim, aabb_indices, NGT); PS_ALLOC_FIELD(sim, aabb_count, N);

    PS_ALLOC_FIELD(sim, nearest_enemy, N); PS_ALLOC_FIELD(sim, nearest_enemy_d2, N);
    PS_ALLOC_FIELD(sim, enemy_count, N); PS_ALLOC_FIELD(sim, projectile_count, N); PS_ALLOC_FIELD(sim, drop_count, N); PS_ALLOC_FIELD(sim, area_count, N); PS_ALLOC_FIELD(sim, moving_obstacle_count, N); PS_ALLOC_FIELD(sim, active_ink_count, N);
    PS_ALLOC_FIELD(sim, next_enemy_slot, N); PS_ALLOC_FIELD(sim, next_projectile_slot, N); PS_ALLOC_FIELD(sim, next_drop_slot, N); PS_ALLOC_FIELD(sim, next_area_slot, N); PS_ALLOC_FIELD(sim, next_moving_obstacle_slot, N);

    PS_ALLOC_FIELD(sim, episode_return, N);
    PS_ALLOC_FIELD(sim, episode_reward_survival, N); PS_ALLOC_FIELD(sim, episode_reward_damage, N); PS_ALLOC_FIELD(sim, episode_reward_kill, N);
    PS_ALLOC_FIELD(sim, episode_reward_hurt, N); PS_ALLOC_FIELD(sim, episode_reward_pickup, N); PS_ALLOC_FIELD(sim, episode_reward_xp, N);
    PS_ALLOC_FIELD(sim, episode_reward_levelup, N); PS_ALLOC_FIELD(sim, episode_reward_obstacle, N); PS_ALLOC_FIELD(sim, episode_reward_terminal, N);
    PS_ALLOC_FIELD(sim, episode_score, N); PS_ALLOC_FIELD(sim, episode_kills, N); PS_ALLOC_FIELD(sim, episode_xp, N);
    PS_ALLOC_FIELD(sim, episode_damage_dealt, N); PS_ALLOC_FIELD(sim, episode_damage_taken, N); PS_ALLOC_FIELD(sim, episode_pickups, N);
    PS_ALLOC_FIELD(sim, episode_levelups, N); PS_ALLOC_FIELD(sim, episode_obstacle_hits, N);
    PS_ALLOC_FIELD(sim, episode_peak_enemies, N); PS_ALLOC_FIELD(sim, episode_peak_projectiles, N); PS_ALLOC_FIELD(sim, episode_min_hp, N);

    PS_ALLOC_FIELD(sim, log_perf, N); PS_ALLOC_FIELD(sim, log_score, N); PS_ALLOC_FIELD(sim, log_episode_return, N);
    PS_ALLOC_FIELD(sim, log_reward_survival, N); PS_ALLOC_FIELD(sim, log_reward_damage, N); PS_ALLOC_FIELD(sim, log_reward_kill, N);
    PS_ALLOC_FIELD(sim, log_reward_hurt, N); PS_ALLOC_FIELD(sim, log_reward_pickup, N); PS_ALLOC_FIELD(sim, log_reward_xp, N);
    PS_ALLOC_FIELD(sim, log_reward_levelup, N); PS_ALLOC_FIELD(sim, log_reward_obstacle, N); PS_ALLOC_FIELD(sim, log_reward_terminal, N);
    PS_ALLOC_FIELD(sim, log_episode_length, N);
    PS_ALLOC_FIELD(sim, log_kills, N); PS_ALLOC_FIELD(sim, log_level, N); PS_ALLOC_FIELD(sim, log_xp, N); PS_ALLOC_FIELD(sim, log_damage_dealt, N);
    PS_ALLOC_FIELD(sim, log_damage_taken, N); PS_ALLOC_FIELD(sim, log_pickups, N); PS_ALLOC_FIELD(sim, log_levelups, N); PS_ALLOC_FIELD(sim, log_obstacle_hits, N);
    PS_ALLOC_FIELD(sim, log_enemies_alive, N); PS_ALLOC_FIELD(sim, log_projectiles_alive, N); PS_ALLOC_FIELD(sim, log_drops_alive, N); PS_ALLOC_FIELD(sim, log_areas_alive, N);
    PS_ALLOC_FIELD(sim, log_weapon_levels, N); PS_ALLOC_FIELD(sim, log_wave, N); PS_ALLOC_FIELD(sim, log_hp, N); PS_ALLOC_FIELD(sim, log_survived, N); PS_ALLOC_FIELD(sim, log_n, N);
    PS_ALLOC_FIELD(sim, log_death_0_25, N); PS_ALLOC_FIELD(sim, log_death_25_50, N); PS_ALLOC_FIELD(sim, log_death_50_75, N); PS_ALLOC_FIELD(sim, log_death_75_100, N);
    PS_ALLOC_FIELD(sim, log_success, N); PS_ALLOC_FIELD(sim, log_peak_enemies, N); PS_ALLOC_FIELD(sim, log_peak_projectiles, N); PS_ALLOC_FIELD(sim, log_min_hp, N);

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
    PS_CUDA_CHECK(cudaMemset(sim->moving_obstacle_active, 0, sizeof(uint8_t) * NMO));
    // Explicitly zero the rest pointer-by-pointer for correctness.
    PS_ZERO_FIELD(sim, px, N); PS_ZERO_FIELD(sim, py, N); PS_ZERO_FIELD(sim, pvx, N); PS_ZERO_FIELD(sim, pvy, N);
    PS_ZERO_FIELD(sim, hp, N); PS_ZERO_FIELD(sim, max_hp, N); PS_ZERO_FIELD(sim, xp, N); PS_ZERO_FIELD(sim, player_facing_left, N);
    PS_ZERO_FIELD(sim, speed_bonus, N); PS_ZERO_FIELD(sim, damage_bonus, N); PS_ZERO_FIELD(sim, cooldown_mult, N); PS_ZERO_FIELD(sim, projectile_speed_bonus, N);
    PS_ZERO_FIELD(sim, magnet_bonus, N); PS_ZERO_FIELD(sim, area_bonus, N); PS_ZERO_FIELD(sim, level, N); PS_ZERO_FIELD(sim, pierce_bonus, N);
    PS_ZERO_FIELD(sim, pending_upgrade, N); PS_ZERO_FIELD(sim, queued_upgrades, N); PS_ZERO_FIELD(sim, last_boss_tick, N); PS_ZERO_FIELD(sim, offered, N * PS_UPGRADE_SLOTS);
    PS_ZERO_FIELD(sim, weapon_cd, N * PS_WEAPON_COUNT); PS_ZERO_FIELD(sim, weapon_active, N * PS_WEAPON_COUNT); PS_ZERO_FIELD(sim, weapon_level, N * PS_WEAPON_COUNT);
    PS_ZERO_FIELD(sim, orbit_phase, N); PS_ZERO_FIELD(sim, tick, N); PS_ZERO_FIELD(sim, invuln_timer, N);
    PS_ZERO_FIELD(sim, enemy_type, NE); PS_ZERO_FIELD(sim, enemy_shape, NE); PS_ZERO_FIELD(sim, enemy_x, NE); PS_ZERO_FIELD(sim, enemy_y, NE); PS_ZERO_FIELD(sim, enemy_vx, NE); PS_ZERO_FIELD(sim, enemy_vy, NE);
    PS_ZERO_FIELD(sim, enemy_hp, NE); PS_ZERO_FIELD(sim, enemy_max_hp, NE); PS_ZERO_FIELD(sim, enemy_radius, NE); PS_ZERO_FIELD(sim, enemy_bound_radius, NE); PS_ZERO_FIELD(sim, enemy_half_width, NE); PS_ZERO_FIELD(sim, enemy_half_height, NE); PS_ZERO_FIELD(sim, enemy_speed, NE); PS_ZERO_FIELD(sim, enemy_damage, NE); PS_ZERO_FIELD(sim, enemy_next, NE);
    PS_ZERO_FIELD(sim, projectile_type, NP); PS_ZERO_FIELD(sim, projectile_x, NP); PS_ZERO_FIELD(sim, projectile_y, NP); PS_ZERO_FIELD(sim, projectile_vx, NP); PS_ZERO_FIELD(sim, projectile_vy, NP);
    PS_ZERO_FIELD(sim, projectile_damage, NP); PS_ZERO_FIELD(sim, projectile_radius, NP); PS_ZERO_FIELD(sim, projectile_ttl, NP); PS_ZERO_FIELD(sim, projectile_pierce, NP);
    PS_ZERO_FIELD(sim, projectile_dense, NP); PS_ZERO_FIELD(sim, projectile_dense_pos, NP);
    PS_ZERO_FIELD(sim, drop_type, ND); PS_ZERO_FIELD(sim, drop_x, ND); PS_ZERO_FIELD(sim, drop_y, ND); PS_ZERO_FIELD(sim, drop_value, ND);
    PS_ZERO_FIELD(sim, drop_dense, ND); PS_ZERO_FIELD(sim, drop_dense_pos, ND);
    PS_ZERO_FIELD(sim, area_type, NA); PS_ZERO_FIELD(sim, area_x, NA); PS_ZERO_FIELD(sim, area_y, NA); PS_ZERO_FIELD(sim, area_radius, NA); PS_ZERO_FIELD(sim, area_damage, NA);
    PS_ZERO_FIELD(sim, area_ttl, NA); PS_ZERO_FIELD(sim, area_tick_rate, NA); PS_ZERO_FIELD(sim, area_tick_timer, NA);
    PS_ZERO_FIELD(sim, area_dense, NA); PS_ZERO_FIELD(sim, area_dense_pos, NA);
    PS_ZERO_FIELD(sim, obstacle_type, NO); PS_ZERO_FIELD(sim, obstacle_x, NO); PS_ZERO_FIELD(sim, obstacle_y, NO); PS_ZERO_FIELD(sim, obstacle_radius, NO);
    PS_ZERO_FIELD(sim, moving_obstacle_type, NMO); PS_ZERO_FIELD(sim, moving_obstacle_shape, NMO); PS_ZERO_FIELD(sim, moving_obstacle_x, NMO); PS_ZERO_FIELD(sim, moving_obstacle_y, NMO); PS_ZERO_FIELD(sim, moving_obstacle_vx, NMO); PS_ZERO_FIELD(sim, moving_obstacle_vy, NMO); PS_ZERO_FIELD(sim, moving_obstacle_bound_radius, NMO); PS_ZERO_FIELD(sim, moving_obstacle_half_width, NMO); PS_ZERO_FIELD(sim, moving_obstacle_half_height, NMO); PS_ZERO_FIELD(sim, moving_obstacle_ttl, NMO); PS_ZERO_FIELD(sim, moving_obstacle_dense, NMO); PS_ZERO_FIELD(sim, moving_obstacle_dense_pos, NMO); PS_ZERO_FIELD(sim, grid_head, NG);
    PS_ZERO_FIELD(sim, grid_touched, NGT); PS_ZERO_FIELD(sim, grid_touched_count, N); PS_ZERO_FIELD(sim, aabb_indices, NGT); PS_ZERO_FIELD(sim, aabb_count, N);
    PS_ZERO_FIELD(sim, nearest_enemy, N); PS_ZERO_FIELD(sim, nearest_enemy_d2, N);
    PS_ZERO_FIELD(sim, enemy_count, N); PS_ZERO_FIELD(sim, projectile_count, N); PS_ZERO_FIELD(sim, drop_count, N); PS_ZERO_FIELD(sim, area_count, N); PS_ZERO_FIELD(sim, moving_obstacle_count, N); PS_ZERO_FIELD(sim, active_ink_count, N);
    PS_ZERO_FIELD(sim, next_enemy_slot, N); PS_ZERO_FIELD(sim, next_projectile_slot, N); PS_ZERO_FIELD(sim, next_drop_slot, N); PS_ZERO_FIELD(sim, next_area_slot, N); PS_ZERO_FIELD(sim, next_moving_obstacle_slot, N);
    PS_ZERO_FIELD(sim, episode_return, N);
    PS_ZERO_FIELD(sim, episode_reward_survival, N); PS_ZERO_FIELD(sim, episode_reward_damage, N); PS_ZERO_FIELD(sim, episode_reward_kill, N);
    PS_ZERO_FIELD(sim, episode_reward_hurt, N); PS_ZERO_FIELD(sim, episode_reward_pickup, N); PS_ZERO_FIELD(sim, episode_reward_xp, N);
    PS_ZERO_FIELD(sim, episode_reward_levelup, N); PS_ZERO_FIELD(sim, episode_reward_obstacle, N); PS_ZERO_FIELD(sim, episode_reward_terminal, N);
    PS_ZERO_FIELD(sim, episode_score, N); PS_ZERO_FIELD(sim, episode_kills, N); PS_ZERO_FIELD(sim, episode_xp, N);
    PS_ZERO_FIELD(sim, episode_damage_dealt, N); PS_ZERO_FIELD(sim, episode_damage_taken, N); PS_ZERO_FIELD(sim, episode_pickups, N); PS_ZERO_FIELD(sim, episode_levelups, N); PS_ZERO_FIELD(sim, episode_obstacle_hits, N);
    PS_ZERO_FIELD(sim, episode_peak_enemies, N); PS_ZERO_FIELD(sim, episode_peak_projectiles, N); PS_ZERO_FIELD(sim, episode_min_hp, N);
    PS_ZERO_FIELD(sim, log_perf, N); PS_ZERO_FIELD(sim, log_score, N); PS_ZERO_FIELD(sim, log_episode_return, N);
    PS_ZERO_FIELD(sim, log_reward_survival, N); PS_ZERO_FIELD(sim, log_reward_damage, N); PS_ZERO_FIELD(sim, log_reward_kill, N);
    PS_ZERO_FIELD(sim, log_reward_hurt, N); PS_ZERO_FIELD(sim, log_reward_pickup, N); PS_ZERO_FIELD(sim, log_reward_xp, N);
    PS_ZERO_FIELD(sim, log_reward_levelup, N); PS_ZERO_FIELD(sim, log_reward_obstacle, N); PS_ZERO_FIELD(sim, log_reward_terminal, N);
    PS_ZERO_FIELD(sim, log_episode_length, N);
    PS_ZERO_FIELD(sim, log_kills, N); PS_ZERO_FIELD(sim, log_level, N); PS_ZERO_FIELD(sim, log_xp, N); PS_ZERO_FIELD(sim, log_damage_dealt, N); PS_ZERO_FIELD(sim, log_damage_taken, N);
    PS_ZERO_FIELD(sim, log_pickups, N); PS_ZERO_FIELD(sim, log_levelups, N); PS_ZERO_FIELD(sim, log_obstacle_hits, N); PS_ZERO_FIELD(sim, log_enemies_alive, N);
    PS_ZERO_FIELD(sim, log_projectiles_alive, N); PS_ZERO_FIELD(sim, log_drops_alive, N); PS_ZERO_FIELD(sim, log_areas_alive, N); PS_ZERO_FIELD(sim, log_weapon_levels, N);
    PS_ZERO_FIELD(sim, log_wave, N); PS_ZERO_FIELD(sim, log_hp, N); PS_ZERO_FIELD(sim, log_survived, N); PS_ZERO_FIELD(sim, log_n, N);
    PS_ZERO_FIELD(sim, log_death_0_25, N); PS_ZERO_FIELD(sim, log_death_25_50, N); PS_ZERO_FIELD(sim, log_death_50_75, N); PS_ZERO_FIELD(sim, log_death_75_100, N);
    PS_ZERO_FIELD(sim, log_success, N); PS_ZERO_FIELD(sim, log_peak_enemies, N); PS_ZERO_FIELD(sim, log_peak_projectiles, N); PS_ZERO_FIELD(sim, log_min_hp, N);
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
    PS_FREE_FIELD(sim, enemy_active); PS_FREE_FIELD(sim, enemy_type); PS_FREE_FIELD(sim, enemy_shape); PS_FREE_FIELD(sim, enemy_x); PS_FREE_FIELD(sim, enemy_y); PS_FREE_FIELD(sim, enemy_vx); PS_FREE_FIELD(sim, enemy_vy); PS_FREE_FIELD(sim, enemy_hp); PS_FREE_FIELD(sim, enemy_max_hp); PS_FREE_FIELD(sim, enemy_radius); PS_FREE_FIELD(sim, enemy_bound_radius); PS_FREE_FIELD(sim, enemy_half_width); PS_FREE_FIELD(sim, enemy_half_height); PS_FREE_FIELD(sim, enemy_speed); PS_FREE_FIELD(sim, enemy_damage); PS_FREE_FIELD(sim, enemy_next);
    PS_FREE_FIELD(sim, projectile_active); PS_FREE_FIELD(sim, projectile_type); PS_FREE_FIELD(sim, projectile_x); PS_FREE_FIELD(sim, projectile_y); PS_FREE_FIELD(sim, projectile_vx); PS_FREE_FIELD(sim, projectile_vy); PS_FREE_FIELD(sim, projectile_damage); PS_FREE_FIELD(sim, projectile_radius); PS_FREE_FIELD(sim, projectile_ttl); PS_FREE_FIELD(sim, projectile_pierce); PS_FREE_FIELD(sim, projectile_dense); PS_FREE_FIELD(sim, projectile_dense_pos);
    PS_FREE_FIELD(sim, drop_active); PS_FREE_FIELD(sim, drop_type); PS_FREE_FIELD(sim, drop_x); PS_FREE_FIELD(sim, drop_y); PS_FREE_FIELD(sim, drop_value); PS_FREE_FIELD(sim, drop_dense); PS_FREE_FIELD(sim, drop_dense_pos);
    PS_FREE_FIELD(sim, area_active); PS_FREE_FIELD(sim, area_type); PS_FREE_FIELD(sim, area_x); PS_FREE_FIELD(sim, area_y); PS_FREE_FIELD(sim, area_radius); PS_FREE_FIELD(sim, area_damage); PS_FREE_FIELD(sim, area_ttl); PS_FREE_FIELD(sim, area_tick_rate); PS_FREE_FIELD(sim, area_tick_timer); PS_FREE_FIELD(sim, area_dense); PS_FREE_FIELD(sim, area_dense_pos);
    PS_FREE_FIELD(sim, obstacle_type); PS_FREE_FIELD(sim, obstacle_x); PS_FREE_FIELD(sim, obstacle_y); PS_FREE_FIELD(sim, obstacle_radius);
    PS_FREE_FIELD(sim, moving_obstacle_active); PS_FREE_FIELD(sim, moving_obstacle_type); PS_FREE_FIELD(sim, moving_obstacle_shape); PS_FREE_FIELD(sim, moving_obstacle_x); PS_FREE_FIELD(sim, moving_obstacle_y); PS_FREE_FIELD(sim, moving_obstacle_vx); PS_FREE_FIELD(sim, moving_obstacle_vy); PS_FREE_FIELD(sim, moving_obstacle_bound_radius); PS_FREE_FIELD(sim, moving_obstacle_half_width); PS_FREE_FIELD(sim, moving_obstacle_half_height); PS_FREE_FIELD(sim, moving_obstacle_ttl); PS_FREE_FIELD(sim, moving_obstacle_dense); PS_FREE_FIELD(sim, moving_obstacle_dense_pos);
    PS_FREE_FIELD(sim, grid_head); PS_FREE_FIELD(sim, grid_touched); PS_FREE_FIELD(sim, grid_touched_count); PS_FREE_FIELD(sim, aabb_indices); PS_FREE_FIELD(sim, aabb_count);
    PS_FREE_FIELD(sim, nearest_enemy); PS_FREE_FIELD(sim, nearest_enemy_d2);
    PS_FREE_FIELD(sim, enemy_count); PS_FREE_FIELD(sim, projectile_count); PS_FREE_FIELD(sim, drop_count); PS_FREE_FIELD(sim, area_count); PS_FREE_FIELD(sim, moving_obstacle_count); PS_FREE_FIELD(sim, active_ink_count); PS_FREE_FIELD(sim, next_enemy_slot); PS_FREE_FIELD(sim, next_projectile_slot); PS_FREE_FIELD(sim, next_drop_slot); PS_FREE_FIELD(sim, next_area_slot); PS_FREE_FIELD(sim, next_moving_obstacle_slot);
    PS_FREE_FIELD(sim, episode_return); PS_FREE_FIELD(sim, episode_reward_survival); PS_FREE_FIELD(sim, episode_reward_damage); PS_FREE_FIELD(sim, episode_reward_kill); PS_FREE_FIELD(sim, episode_reward_hurt); PS_FREE_FIELD(sim, episode_reward_pickup); PS_FREE_FIELD(sim, episode_reward_xp); PS_FREE_FIELD(sim, episode_reward_levelup); PS_FREE_FIELD(sim, episode_reward_obstacle); PS_FREE_FIELD(sim, episode_reward_terminal); PS_FREE_FIELD(sim, episode_score); PS_FREE_FIELD(sim, episode_kills); PS_FREE_FIELD(sim, episode_xp); PS_FREE_FIELD(sim, episode_damage_dealt); PS_FREE_FIELD(sim, episode_damage_taken); PS_FREE_FIELD(sim, episode_pickups); PS_FREE_FIELD(sim, episode_levelups); PS_FREE_FIELD(sim, episode_obstacle_hits); PS_FREE_FIELD(sim, episode_peak_enemies); PS_FREE_FIELD(sim, episode_peak_projectiles); PS_FREE_FIELD(sim, episode_min_hp);
    PS_FREE_FIELD(sim, log_perf); PS_FREE_FIELD(sim, log_score); PS_FREE_FIELD(sim, log_episode_return); PS_FREE_FIELD(sim, log_reward_survival); PS_FREE_FIELD(sim, log_reward_damage); PS_FREE_FIELD(sim, log_reward_kill); PS_FREE_FIELD(sim, log_reward_hurt); PS_FREE_FIELD(sim, log_reward_pickup); PS_FREE_FIELD(sim, log_reward_xp); PS_FREE_FIELD(sim, log_reward_levelup); PS_FREE_FIELD(sim, log_reward_obstacle); PS_FREE_FIELD(sim, log_reward_terminal); PS_FREE_FIELD(sim, log_episode_length); PS_FREE_FIELD(sim, log_kills); PS_FREE_FIELD(sim, log_level); PS_FREE_FIELD(sim, log_xp); PS_FREE_FIELD(sim, log_damage_dealt); PS_FREE_FIELD(sim, log_damage_taken); PS_FREE_FIELD(sim, log_pickups); PS_FREE_FIELD(sim, log_levelups); PS_FREE_FIELD(sim, log_obstacle_hits); PS_FREE_FIELD(sim, log_enemies_alive); PS_FREE_FIELD(sim, log_projectiles_alive); PS_FREE_FIELD(sim, log_drops_alive); PS_FREE_FIELD(sim, log_areas_alive); PS_FREE_FIELD(sim, log_weapon_levels); PS_FREE_FIELD(sim, log_wave); PS_FREE_FIELD(sim, log_hp); PS_FREE_FIELD(sim, log_survived); PS_FREE_FIELD(sim, log_n); PS_FREE_FIELD(sim, log_death_0_25); PS_FREE_FIELD(sim, log_death_25_50); PS_FREE_FIELD(sim, log_death_50_75); PS_FREE_FIELD(sim, log_death_75_100); PS_FREE_FIELD(sim, log_success); PS_FREE_FIELD(sim, log_peak_enemies); PS_FREE_FIELD(sim, log_peak_projectiles); PS_FREE_FIELD(sim, log_min_hp);
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
    return sim.cfg.xp_threshold_base
        + sim.cfg.xp_threshold_per_level * (float)(sim.level[env] - 1);
}

PS_D int ps_wave_index(const PSCudaSim& sim, int env) {
    return sim.tick[env] / sim.cfg.wave_length_steps;
}

PS_D float ps_episode_progress(const PSCudaSim& sim, int env) {
    float max_steps = (float)sim.cfg.max_steps;
    float normal_steps = (float)sim.cfg.wave_length_steps
        * (float)sim.cfg.progress_normal_wave_count;
    float scale = fminf(max_steps, normal_steps);
    return ps_clampf((float)sim.tick[env] / scale, 0.0f, 1.0f);
}

PS_D float ps_weapon_cooldown_total(const PSCudaSim& sim, int env, int weapon) {
    int level = sim.weapon_level[PS_WIDX(sim, weapon, env)];
    if (level <= 0) return 1.0f;
    float cd = sim.cfg.weapon_base_cooldown[weapon]
        + sim.cfg.weapon_cooldown_per_level[weapon] * (float)(level - 1);
    cd *= sim.cooldown_mult[env] * sim.cfg.fire_cooldown
        / fmaxf(sim.cfg.weapon_base_cooldown[PS_WEAPON_BUBBLE], 1.0f);
    return cd;
}

PS_D float ps_weapon_power(const PSCudaSim& sim, int env, int weapon) {
    int level = sim.weapon_level[PS_WIDX(sim, weapon, env)];
    if (level <= 0) return 0.0f;
    float area = 1.0f + sim.area_bonus[env];
    float might = sim.cfg.projectile_damage * (1.0f + sim.damage_bonus[env]);
    return ps_clampf(((float)level / (float)sim.cfg.weapon_max_level) * might * area,
        0.0f, 3.0f) / 3.0f;
}

PS_D float ps_weapon_damage(const PSCudaSim& sim, int env, int weapon, int level, int first_level_zero) {
    float level_delta = (float)(first_level_zero ? level - 1 : level);
    return (sim.cfg.weapon_base_damage[weapon]
        + sim.cfg.weapon_damage_per_level[weapon] * level_delta)
        * sim.cfg.projectile_damage * (1.0f + sim.damage_bonus[env]);
}

PS_D int ps_upgrade_available(const PSCudaSim& sim, int env, int type) {
    if (type >= PS_UPGRADE_BUBBLE && type <= PS_UPGRADE_SONAR) {
        return sim.weapon_level[PS_WIDX(sim, type, env)] < sim.cfg.weapon_max_level;
    }
    return type >= 0 && type < PS_UPGRADE_COUNT;
}

PS_D int ps_wave_minimum(const PSCudaSim& sim, int env) {
    int wave = ps_wave_index(sim, env);
    int base = wave < PS_WAVE_TABLE_COUNT
        ? sim.cfg.wave_minimum[wave]
        : sim.cfg.wave_tail_minimum_base
            + sim.cfg.wave_tail_minimum_step * (wave - PS_WAVE_TABLE_COUNT);
    float spawn_scale = ps_clampf(sim.cfg.enemy_spawn_rate
        / sim.cfg.wave_spawn_reference_rate,
        sim.cfg.wave_spawn_scale_min, sim.cfg.wave_spawn_scale_max);
    float progress = ps_episode_progress(sim, env);
    int scaled = (int)ceilf((float)base * spawn_scale
        * (1.0f + sim.cfg.wave_progress_spawn_scale * sim.cfg.spawn_ramp * progress));
    int cap = sim.cfg.wave_population_cap;
    return scaled > cap ? cap : scaled;
}

PS_D int ps_wave_spawn_interval(const PSCudaSim& sim, int env) {
    int wave = ps_wave_index(sim, env);
    int base = wave < PS_WAVE_TABLE_COUNT
        ? sim.cfg.wave_interval[wave]
        : sim.cfg.wave_tail_interval;
    float spawn_scale = ps_clampf(sim.cfg.enemy_spawn_rate
        / sim.cfg.wave_spawn_reference_rate,
        sim.cfg.wave_spawn_scale_min, sim.cfg.wave_spawn_scale_max);
    int scaled = (int)ceilf((float)base / spawn_scale);
    return scaled < sim.cfg.wave_min_spawn_interval
        ? sim.cfg.wave_min_spawn_interval : scaled;
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
    int wave_len = sim.cfg.wave_length_steps;
    int wave = ps_wave_index(sim, env);
    int boss_period = sim.cfg.boss_period_steps;

    ps_obs_set(sim, env, idx++, ps_clampf(sim.hp[env] / fmaxf(sim.max_hp[env], 1.0f), 0.0f, 1.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.hp[env], 4.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm(sim.max_hp[env], 8.0f));
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm((float)sim.level[env], 20.0f));
    ps_obs_set(sim, env, idx++, ps_clampf(sim.xp[env] / fmaxf(ps_xp_threshold(sim, env), 1.0f), 0.0f, 1.0f));
    ps_obs_set(sim, env, idx++, (float)(sim.tick[env] % wave_len) / (float)wave_len);
    ps_obs_set(sim, env, idx++, ps_obs_soft_norm((float)(wave + 1), 12.0f));
    ps_obs_set(sim, env, idx++, (float)(sim.tick[env] % boss_period) / (float)boss_period);

    int visible_enemies_idx = idx++;
    int alt_a_idx = idx++;
    int visible_drops_idx = idx++;
    int alt_b_idx = idx++;
    if (sim.cfg.observation_version == 6 || sim.cfg.observation_version >= 9) {
        int bubble = PS_WIDX(sim, PS_WEAPON_BUBBLE, env);
        ps_obs_set(sim, env, alt_b_idx, 1.0f - ps_clampf(sim.weapon_cd[bubble]
            / ps_weapon_cooldown_total(sim, env, PS_WEAPON_BUBBLE), 0.0f, 1.0f));
    }
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
    int alt_c_idx = idx++;

    int nearest_xp_dx_idx = idx++;
    int nearest_xp_dy_idx = idx++;
    int alt_d_idx = idx++;
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
    float obstacle_bin_nearest_d2[PS_SECTORS * PS_RINGS];
    for (int s = 0; s < PS_SECTORS; s++) {
        sector_pressure[s] = 0.0f;
        sector_front[s] = 0.0f;
        sector_ttc[s] = 0.0f;
        sector_obstacle[s] = 0.0f;
    }
    for (int i = 0; i < PS_SECTORS * PS_RINGS; i++)
        obstacle_bin_nearest_d2[i] = 1e30f;

    int visible_enemies = 0;
    int visible_projectiles = 0;
    int visible_drops = 0;
    float nearest_xp_dx = 0.0f;
    float nearest_xp_dy = 0.0f;
    float nearest_xp_d2 = 1e30f;
    float visible_xp_value = 0.0f;
    float nearest_health_d2 = 1e30f;
    float nearest_health_dx = 0.0f;
    float nearest_health_dy = 0.0f;
    float nearest_obstacle_d2 = 1e30f;
    float nearest_obstacle_radius = 0.0f;
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
        float clearance = fmaxf(d - (sim.enemy_bound_radius[e] + sim.cfg.player_radius), 0.0f);
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
            nearest_health_dx = dx;
            nearest_health_dy = dy;
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
        if (sim.cfg.observation_version <= 7 || sim.cfg.observation_version >= 9)
            ps_obs_set(sim, env, alt_d_idx, 1.0f - ps_clampf(nearest_xp_dist * inv_observe_radius, 0.0f, 1.0f));
    }
    if (nearest_health_d2 < 1e29f) {
        float nearest_health_dist = sqrtf(nearest_health_d2);
        if (sim.cfg.observation_version >= 7 && sim.cfg.observation_version <= 8) {
            ps_obs_set(sim, env, alt_a_idx, ps_clampf(nearest_health_dx * inv_observe_radius, -1.0f, 1.0f));
            ps_obs_set(sim, env, alt_b_idx, ps_clampf(nearest_health_dy * inv_observe_radius, -1.0f, 1.0f));
        }
        if (sim.cfg.observation_version <= 7 || sim.cfg.observation_version >= 9)
            ps_obs_set(sim, env, alt_c_idx, 1.0f - ps_clampf(nearest_health_dist * inv_observe_radius, 0.0f, 1.0f));
    }
    ps_obs_set(sim, env, visible_xp_value_idx, ps_clampf(visible_xp_value / fmaxf(ps_xp_threshold(sim, env), 1.0f), 0.0f, 1.0f));
    ps_obs_set(sim, env, visible_xp_can_level_idx, visible_xp_value >= fmaxf(ps_xp_threshold(sim, env) - sim.xp[env], 0.0f) ? 1.0f : 0.0f);

    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        float dx = sim.obstacle_x[oi] - sim.px[env];
        float dy = sim.obstacle_y[oi] - sim.py[env];
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        if (d2 < nearest_obstacle_d2) {
            nearest_obstacle_d2 = d2;
            nearest_obstacle_radius = sim.obstacle_radius[oi];
        }
        float d = sqrtf(d2);
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = obstacle_base + (ring * PS_SECTORS + sector) * PS_OBSTACLE_CHANNELS;
        float proximity = 1.0f - d * inv_observe_radius;
        int bin = ring * PS_SECTORS + sector;
        if (sim.cfg.observation_version == 8) {
            if (d2 < obstacle_bin_nearest_d2[bin]) {
                obstacle_bin_nearest_d2[bin] = d2;
                ps_obs_set(sim, env, o + 0, ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f));
                ps_obs_set(sim, env, o + 1, ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f));
            }
        } else {
            ps_obs_add_min1(sim, env, o + 0, 0.25f);
            ps_obs_max(sim, env, o + 1, proximity);
        }
        if (sim.cfg.observation_version >= 9
                && d2 < obstacle_bin_nearest_d2[bin]) {
            obstacle_bin_nearest_d2[bin] = d2;
            int exact = PS_OBS_EXACT_OBSTACLE_BASE + 2 * bin;
            ps_obs_set(sim, env, exact + 0,
                ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f));
            ps_obs_set(sim, env, exact + 1,
                ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f));
        }
        sector_obstacle[sector] = fminf(sector_obstacle[sector] + 0.30f * proximity, 1.0f);
    }

    if (sim.cfg.observation_version == 8 && nearest_obstacle_d2 < 1e29f) {
        float center_dist = sqrtf(nearest_obstacle_d2);
        float clearance = fmaxf(center_dist - nearest_obstacle_radius - sim.cfg.player_radius, 0.0f);
        ps_obs_set(sim, env, alt_c_idx, ps_clampf(clearance * inv_observe_radius, 0.0f, 1.0f));
        float obstacle_radius_norm = fmaxf(sim.cfg.obstacle_radius_max, 1.1f);
        ps_obs_set(sim, env, alt_d_idx, ps_clampf(nearest_obstacle_radius / obstacle_radius_norm, 0.0f, 1.0f));
    }

    if (sim.cfg.observation_version == 6 || sim.cfg.observation_version >= 9) {
        for (int k = 0; k < sim.projectile_count[env]; k++) {
            int i = sim.projectile_dense[PS_PIDX(sim, k, env)];
            int p = PS_PIDX(sim, i, env);
            float dx = sim.projectile_x[p] - sim.px[env];
            float dy = sim.projectile_y[p] - sim.py[env];
            if (dx * dx + dy * dy <= observe_radius2) visible_projectiles++;
        }
        ps_obs_set(sim, env, alt_a_idx,
            ps_clampf((float)visible_projectiles * inv_projectile_cap, 0.0f, 1.0f));
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
    ps_obs_set(sim, env, visible_drops_idx, ps_clampf((float)visible_drops * inv_drop_cap, 0.0f, 1.0f));

    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        int level = sim.weapon_level[PS_WIDX(sim, i, env)];
        float cd_total = ps_weapon_cooldown_total(sim, env, i);
        float ready = level > 0 ? 1.0f - ps_clampf(sim.weapon_cd[PS_WIDX(sim, i, env)] / cd_total, 0.0f, 1.0f) : 0.0f;
        ps_obs_set(sim, env, idx++, (float)level / (float)sim.cfg.weapon_max_level);
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

    idx += PS_EXACT_OBSTACLE_FEATURES;

    int moving_idx[PS_MOVING_OBSTACLE_SLOTS];
    float moving_score[PS_MOVING_OBSTACLE_SLOTS];
    for (int s = 0; s < PS_MOVING_OBSTACLE_SLOTS; s++) {
        moving_idx[s] = -1;
        moving_score[s] = 1e30f;
    }
    for (int k = 0; k < sim.moving_obstacle_count[env]; k++) {
        int i = sim.moving_obstacle_dense[PS_MOIDX(sim, k, env)];
        int m = PS_MOIDX(sim, i, env);
        float dx = sim.moving_obstacle_x[m] - sim.px[env];
        float dy = sim.moving_obstacle_y[m] - sim.py[env];
        float score = dx * dx + dy * dy;
        for (int s = 0; s < PS_MOVING_OBSTACLE_SLOTS; s++) {
            if (score >= moving_score[s]) continue;
            for (int j = PS_MOVING_OBSTACLE_SLOTS - 1; j > s; j--) {
                moving_score[j] = moving_score[j - 1];
                moving_idx[j] = moving_idx[j - 1];
            }
            moving_score[s] = score;
            moving_idx[s] = i;
            break;
        }
    }
    for (int s = 0; s < PS_MOVING_OBSTACLE_SLOTS; s++) {
        int o = PS_OBS_MOVING_OBSTACLE_BASE + s * PS_MOVING_OBSTACLE_FEATURES;
        int i = moving_idx[s];
        if (i < 0) continue;
        int m = PS_MOIDX(sim, i, env);
        float dx = sim.moving_obstacle_x[m] - sim.px[env];
        float dy = sim.moving_obstacle_y[m] - sim.py[env];
        ps_obs_set(sim, env, o + 0, 1.0f);
        ps_obs_set(sim, env, o + 1,
            sim.moving_obstacle_type[m] == PS_MOVING_OBSTACLE_SUBMARINE ? 1.0f : 0.0f);
        ps_obs_set(sim, env, o + 2, ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f));
        ps_obs_set(sim, env, o + 3, ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f));
    }
    idx += PS_MOVING_OBSTACLE_OBS_FEATURES;
}
PS_D int ps_obstacle_position_clear(const PSCudaSim& sim, int env, int count, int skip, float x, float y, float radius) {
    if (ps_dist2(x, y, sim.px[env], sim.py[env])
            < sim.cfg.obstacle_player_spawn_clearance
            * sim.cfg.obstacle_player_spawn_clearance) return 0;
    for (int i = 0; i < count; i++) {
        if (i == skip) continue;
        int oi = PS_OIDX(sim, i, env);
        float min_dist = radius + sim.obstacle_radius[oi] + sim.cfg.obstacle_spawn_clearance;
        float dx = x - sim.obstacle_x[oi];
        float dy = y - sim.obstacle_y[oi];
        if (ps_geometry_circle_overlaps(dx, dy, min_dist)) return 0;
    }
    return 1;
}

PS_D int ps_overlaps_obstacle(const PSCudaSim& sim, int env, float x, float y, float radius, float padding) {
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        float min_dist = radius + sim.obstacle_radius[oi] + padding;
        float dx = x - sim.obstacle_x[oi];
        float dy = y - sim.obstacle_y[oi];
        if (ps_geometry_circle_overlaps(dx, dy, min_dist)) return 1;
    }
    return 0;
}

PS_D void ps_push_out_obstacles(const PSCudaSim& sim, int env, float* x, float* y, float radius, int penalize) {
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        int pushed = ps_geometry_push_out_shape_circle(x, y, PS_SHAPE_CIRCLE,
            radius, radius, radius, sim.obstacle_x[oi], sim.obstacle_y[oi],
            sim.obstacle_radius[oi]);
        if (pushed && penalize) {
            sim.rewards[env] += sim.cfg.obstacle_penalty;
            sim.episode_reward_obstacle[env] += sim.cfg.obstacle_penalty;
            sim.episode_obstacle_hits[env] += 1.0f;
        }
    }
}

PS_D void ps_push_out_obstacles_shape(const PSCudaSim& sim, int env,
        float* x, float* y, int shape, float radius, float half_width,
        float half_height, int penalize) {
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        int pushed = ps_geometry_push_out_shape_circle(x, y, shape, radius,
            half_width, half_height, sim.obstacle_x[oi], sim.obstacle_y[oi],
            sim.obstacle_radius[oi]);
        if (pushed && penalize) {
            sim.rewards[env] += sim.cfg.obstacle_penalty;
            sim.episode_reward_obstacle[env] += sim.cfg.obstacle_penalty;
            sim.episode_obstacle_hits[env] += 1.0f;
        }
    }
}

PS_D void ps_spawn_obstacles(const PSCudaSim& sim, int env) {
    float half = 0.5f * sim.cfg.arena_size;
    for (int i = 0; i < sim.cfg.obstacle_count; i++) {
        int oi = PS_OIDX(sim, i, env);
        sim.obstacle_radius[oi] = sim.cfg.obstacle_radius_min
            + ps_randf(sim, env) * (sim.cfg.obstacle_radius_max - sim.cfg.obstacle_radius_min);
        sim.obstacle_type[oi] = (uint8_t)(ps_rand_u32(sim, env) % 3u);
        int placed = 0;
        for (int tries = 0; tries < 96; tries++) {
            float angle = ps_randf(sim, env) * 2.0f * PI;
            float dist = half * (sim.cfg.obstacle_spawn_min_ratio
                + (sim.cfg.obstacle_spawn_max_ratio
                - sim.cfg.obstacle_spawn_min_ratio) * ps_randf(sim, env));
            float x = sim.px[env] + cosf(angle) * dist;
            float y = sim.py[env] + sinf(angle) * dist;
            if (!ps_obstacle_position_clear(sim, env, i, -1, x, y, sim.obstacle_radius[oi])) continue;
            sim.obstacle_x[oi] = x;
            sim.obstacle_y[oi] = y;
            placed = 1;
            break;
        }
        if (!placed) {
            float a = (float)i * sim.cfg.obstacle_fallback_angle_step
                + ps_randf(sim, env) * sim.cfg.obstacle_fallback_angle_jitter;
            float r = half * (sim.cfg.obstacle_fallback_min_ratio
                + (sim.cfg.obstacle_fallback_max_ratio
                - sim.cfg.obstacle_fallback_min_ratio)
                * ((float)(i % sim.cfg.obstacle_fallback_spoke_count)
                / (float)(sim.cfg.obstacle_fallback_spoke_count - 1)));
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
        float dist = half * (sim.cfg.obstacle_recycle_spawn_min_ratio
            + (sim.cfg.obstacle_recycle_spawn_max_ratio
            - sim.cfg.obstacle_recycle_spawn_min_ratio) * ps_randf(sim, env));
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
    float recycle_radius = sim.cfg.arena_size * sim.cfg.obstacle_recycle_radius_ratio;
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
    for (int i = 0; i < PS_MAX_MOVING_OBSTACLES; i++) sim.moving_obstacle_active[PS_MOIDX(sim, i, env)] = 0;
    for (int i = 0; i < PS_GRID_CELLS; i++) sim.grid_head[PS_GIDX(sim, i, env)] = -1;
    sim.grid_touched_count[env] = 0;
    sim.aabb_count[env] = 0;

    sim.enemy_count[env] = 0;
    sim.projectile_count[env] = 0;
    sim.drop_count[env] = 0;
    sim.area_count[env] = 0;
    sim.moving_obstacle_count[env] = 0;
    sim.active_ink_count[env] = 0;
    sim.next_enemy_slot[env] = 0;
    sim.next_projectile_slot[env] = 0;
    sim.next_drop_slot[env] = 0;
    sim.next_area_slot[env] = 0;
    sim.next_moving_obstacle_slot[env] = 0;
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
    if (sim.area_type[a] == PS_WEAPON_INK && sim.active_ink_count[env] > 0)
        sim.active_ink_count[env]--;
    sim.area_count[env] = count > 0 ? count - 1 : 0;
}

PS_D void ps_deactivate_moving_obstacle(const PSCudaSim& sim, int env, int i) {
    if (i < 0 || i >= PS_MAX_MOVING_OBSTACLES) return;
    int m = PS_MOIDX(sim, i, env);
    if (!sim.moving_obstacle_active[m]) return;
    int count = sim.moving_obstacle_count[env];
    int pos = sim.moving_obstacle_dense_pos[m];
    if (count > 0 && pos >= 0 && pos < count) {
        int last = sim.moving_obstacle_dense[PS_MOIDX(sim, count - 1, env)];
        sim.moving_obstacle_dense[PS_MOIDX(sim, pos, env)] = last;
        sim.moving_obstacle_dense_pos[PS_MOIDX(sim, last, env)] = pos;
    }
    sim.moving_obstacle_active[m] = 0;
    sim.moving_obstacle_count[env] = count > 0 ? count - 1 : 0;
}

PS_D int ps_spawn_moving_obstacle(const PSCudaSim& sim, int env) {
    if (sim.cfg.moving_obstacle_cap <= 0
            || sim.moving_obstacle_count[env] >= sim.cfg.moving_obstacle_cap) return 0;
    int i = ps_find_free_slot(sim.moving_obstacle_active,
        PS_MAX_MOVING_OBSTACLES, &sim.next_moving_obstacle_slot[env],
        sim.num_envs, env);
    if (i < 0) return 0;

    int type = (int)(ps_rand_u32(sim, env) % PS_MOVING_OBSTACLE_TYPE_COUNT);
    float half = 0.5f * sim.cfg.arena_size;
    float margin = sim.cfg.moving_obstacle_spawn_margin;
    float hw = sim.cfg.moving_obstacle_half_width[type];
    float hh = sim.cfg.moving_obstacle_half_height[type];
    float speed = sim.cfg.moving_obstacle_speed[type];
    int m = PS_MOIDX(sim, i, env);
    sim.moving_obstacle_type[m] = (uint8_t)type;
    sim.moving_obstacle_shape[m] = PS_SHAPE_AABB;
    sim.moving_obstacle_half_width[m] = hw;
    sim.moving_obstacle_half_height[m] = hh;
    sim.moving_obstacle_bound_radius[m] = ps_geometry_shape_bound_radius(
        PS_SHAPE_AABB, 0.0f, hw, hh);
    sim.moving_obstacle_ttl[m] = sim.cfg.moving_obstacle_ttl;
    if (type == PS_MOVING_OBSTACLE_ANCHOR) {
        sim.moving_obstacle_x[m] = sim.px[env]
            + (ps_randf(sim, env) * 2.0f - 1.0f) * half * 0.72f;
        sim.moving_obstacle_y[m] = sim.py[env] - half - margin - hh;
        sim.moving_obstacle_vx[m] = 0.0f;
        sim.moving_obstacle_vy[m] = speed;
    } else {
        int from_left = (int)(ps_rand_u32(sim, env) & 1u) == 0;
        sim.moving_obstacle_x[m] = sim.px[env]
            + (from_left ? -1.0f : 1.0f) * (half + margin + hw);
        sim.moving_obstacle_y[m] = sim.py[env]
            + (ps_randf(sim, env) * 2.0f - 1.0f) * half * 0.72f;
        sim.moving_obstacle_vx[m] = (from_left ? 1.0f : -1.0f) * speed;
        sim.moving_obstacle_vy[m] = 0.0f;
    }
    sim.moving_obstacle_active[m] = 1;
    sim.moving_obstacle_dense_pos[m] = sim.moving_obstacle_count[env];
    sim.moving_obstacle_dense[PS_MOIDX(sim,
        sim.moving_obstacle_count[env], env)] = i;
    sim.moving_obstacle_count[env]++;
    return 1;
}

PS_D void ps_update_moving_obstacles(const PSCudaSim& sim, int env) {
    int wave = ps_wave_index(sim, env);
    if (wave >= sim.cfg.moving_obstacle_start_wave
            && sim.tick[env] % sim.cfg.moving_obstacle_spawn_interval == 0) {
        ps_spawn_moving_obstacle(sim, env);
    }

    float half = 0.5f * sim.cfg.arena_size;
    float cleanup = half + sim.cfg.moving_obstacle_spawn_margin + half;
    float cleanup2 = cleanup * cleanup;
    for (int k = 0; k < sim.moving_obstacle_count[env]; ) {
        int i = sim.moving_obstacle_dense[PS_MOIDX(sim, k, env)];
        int m = PS_MOIDX(sim, i, env);
        sim.moving_obstacle_x[m] += sim.moving_obstacle_vx[m];
        sim.moving_obstacle_y[m] += sim.moving_obstacle_vy[m];
        sim.moving_obstacle_ttl[m]--;
        float dx = sim.px[env] - sim.moving_obstacle_x[m];
        float dy = sim.py[env] - sim.moving_obstacle_y[m];
        if (sim.moving_obstacle_ttl[m] <= 0
                || dx * dx + dy * dy > cleanup2) {
            ps_deactivate_moving_obstacle(sim, env, i);
            continue;
        }
        if (sim.invuln_timer[env] <= 0
                && sim.cfg.moving_obstacle_damage > 0.0f
                && ps_geometry_shape_overlaps_circle(
                    sim.moving_obstacle_shape[m], dx, dy,
                    sim.moving_obstacle_bound_radius[m],
                    sim.moving_obstacle_half_width[m],
                    sim.moving_obstacle_half_height[m], sim.cfg.player_radius)) {
            float damage = fmaxf(1.0f, ceilf(sim.cfg.moving_obstacle_damage));
            sim.hp[env] -= damage;
            sim.rewards[env] += sim.cfg.reward_hurt * damage;
            sim.episode_reward_hurt[env] += sim.cfg.reward_hurt * damage;
            sim.episode_damage_taken[env] += damage;
            sim.invuln_timer[env] = sim.cfg.invuln_steps;
        }
        k++;
    }
}

PS_D void ps_offer_upgrades(const PSCudaSim& sim, int env) {
    if (sim.pending_upgrade[env]) return;
    sim.pending_upgrade[env] = 1;
    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int offer = -1;
        for (;;) {
            int candidate = (int)(ps_rand_u32(sim, env) % PS_UPGRADE_COUNT);
            int duplicate = 0;
            for (int j = 0; j < i; j++) duplicate |= sim.offered[PS_UIDX(sim, j, env)] == candidate;
            if (!duplicate && ps_upgrade_available(sim, env, candidate)) {
                offer = candidate;
                break;
            }
        }
        sim.offered[PS_UIDX(sim, i, env)] = offer;
    }
}

PS_D void ps_apply_upgrade_effect(const PSCudaSim& sim, int env, int upgrade) {
    switch (upgrade) {
        case PS_UPGRADE_BUBBLE:
        case PS_UPGRADE_WHIRLPOOL:
        case PS_UPGRADE_ORBIT:
        case PS_UPGRADE_INK:
        case PS_UPGRADE_SONAR:
            if (sim.weapon_level[PS_WIDX(sim, upgrade, env)] < sim.cfg.weapon_max_level)
                sim.weapon_level[PS_WIDX(sim, upgrade, env)]++;
            break;
        case PS_UPGRADE_SPEED: sim.speed_bonus[env] += sim.cfg.upgrade_speed_bonus; break;
        case PS_UPGRADE_MAGNET: sim.magnet_bonus[env] += sim.cfg.upgrade_magnet_bonus; break;
        case PS_UPGRADE_HEALTH:
            sim.max_hp[env] += sim.cfg.upgrade_health_bonus;
            sim.hp[env] = fminf(sim.max_hp[env], sim.hp[env] + sim.cfg.health_heal);
            break;
        case PS_UPGRADE_MIGHT: sim.damage_bonus[env] += sim.cfg.upgrade_might_bonus; break;
        case PS_UPGRADE_COOLDOWN: sim.cooldown_mult[env] *= sim.cfg.upgrade_cooldown_multiplier; break;
        case PS_UPGRADE_AREA: sim.area_bonus[env] += sim.cfg.upgrade_area_bonus; break;
        case PS_UPGRADE_PIERCE: sim.pierce_bonus[env] += 1; break;
    }
}

PS_D void ps_apply_upgrade(const PSCudaSim& sim, int env, int choice) {
    if (!sim.pending_upgrade[env] || choice < 0 || choice >= PS_UPGRADE_SLOTS) return;
    int upgrade = sim.offered[PS_UIDX(sim, choice, env)];
    ps_apply_upgrade_effect(sim, env, upgrade);
    sim.episode_levelups[env] += 1.0f;
    sim.pending_upgrade[env] = 0;
    if (sim.queued_upgrades[env] > 0) sim.queued_upgrades[env]--;
    if (sim.queued_upgrades[env] > 0) ps_offer_upgrades(sim, env);
}

PS_D void ps_add_log(const PSCudaSim& sim, int env, int survived) {
    float perf = sim.cfg.max_steps > 0
        ? ps_clampf((float)sim.tick[env] / (float)sim.cfg.max_steps, 0.0f, 1.0f)
        : 0.0f;
    sim.log_perf[env] += perf;
    sim.log_score[env] += sim.episode_score[env];
    sim.log_episode_return[env] += sim.episode_return[env];
    sim.log_reward_survival[env] += sim.episode_reward_survival[env];
    sim.log_reward_damage[env] += sim.episode_reward_damage[env];
    sim.log_reward_kill[env] += sim.episode_reward_kill[env];
    sim.log_reward_hurt[env] += sim.episode_reward_hurt[env];
    sim.log_reward_pickup[env] += sim.episode_reward_pickup[env];
    sim.log_reward_xp[env] += sim.episode_reward_xp[env];
    sim.log_reward_levelup[env] += sim.episode_reward_levelup[env];
    sim.log_reward_obstacle[env] += sim.episode_reward_obstacle[env];
    sim.log_reward_terminal[env] += sim.episode_reward_terminal[env];
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
    sim.log_peak_enemies[env] += sim.episode_peak_enemies[env];
    sim.log_peak_projectiles[env] += sim.episode_peak_projectiles[env];
    sim.log_min_hp[env] += sim.episode_min_hp[env];
    if (survived) {
        sim.log_success[env] += 1.0f;
    } else {
        float progress = sim.cfg.max_steps > 0
            ? (float)sim.tick[env] / (float)sim.cfg.max_steps : 0.0f;
        if (progress < 0.25f) sim.log_death_0_25[env] += 1.0f;
        else if (progress < 0.50f) sim.log_death_25_50[env] += 1.0f;
        else if (progress < 0.75f) sim.log_death_50_75[env] += 1.0f;
        else sim.log_death_75_100[env] += 1.0f;
    }
}

PS_D int ps_pick_spawn_side(const PSCudaSim& sim, int env) {
    float m2 = sim.pvx[env] * sim.pvx[env] + sim.pvy[env] * sim.pvy[env];
    if (m2 > 0.0001f && ps_randf(sim, env) < sim.cfg.spawn_velocity_bias_probability) {
        if (fabsf(sim.pvx[env]) > fabsf(sim.pvy[env])) return sim.pvx[env] > 0.0f ? 1 : 0;
        return sim.pvy[env] > 0.0f ? 3 : 2;
    }
    return (int)(ps_rand_u32(sim, env) & 3u);
}

PS_D void ps_pick_spawn_position(const PSCudaSim& sim, int env, float radius, float* x, float* y) {
    float half = 0.5f * sim.cfg.arena_size;
    float edge = half + radius + sim.cfg.enemy_spawn_edge_margin;
    float along = (ps_randf(sim, env) * 2.0f - 1.0f) * half * sim.cfg.enemy_spawn_along_ratio;
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

PS_D PSEnemyDef ps_enemy_stats(const PSCudaSim& sim, int env, int* kind_out,
        float* radius_out, int elite, int boss, int ari_k) {
    int wave = ps_wave_index(sim, env);
    float progress = ps_episode_progress(sim, env);
    int kind = 0;
    if (wave >= sim.cfg.enemy_mix_start_wave) {
        uint32_t roll = ps_rand_u32(sim, env) % 100u;
        if (wave < sim.cfg.enemy_mix_phase_one_end_wave) {
            kind = roll < (uint32_t)sim.cfg.enemy_mix_phase_one_jelly_pct ? 1 : 0;
        } else if (wave < sim.cfg.enemy_mix_phase_two_end_wave) {
            kind = roll < (uint32_t)sim.cfg.enemy_mix_phase_two_urchin_pct
                ? 2
                : (roll < (uint32_t)sim.cfg.enemy_mix_phase_two_jelly_pct ? 1 : 0);
        } else {
            kind = roll < (uint32_t)sim.cfg.enemy_mix_late_urchin_pct
                ? 2
                : (roll < (uint32_t)sim.cfg.enemy_mix_late_eel_pct
                    ? 3
                    : (roll < (uint32_t)sim.cfg.enemy_mix_late_jelly_pct ? 1 : 0));
        }
    }

    PSEnemyDef stats = {
        sim.cfg.enemy_base_hp[kind],
        sim.cfg.enemy_speed_mult[kind],
        sim.cfg.enemy_base_damage[kind],
    };
    float radius = sim.cfg.enemy_radius[kind];
    float hp_growth = 1.0f + sim.cfg.enemy_hp_growth_per_wave * (float)wave
        + sim.cfg.enemy_hp_progress_scale * progress * sim.cfg.spawn_ramp;
    float speed_growth_wave = (float)(wave < sim.cfg.enemy_speed_growth_wave_cap
        ? wave : sim.cfg.enemy_speed_growth_wave_cap);
    float speed_growth = 1.0f + sim.cfg.enemy_speed_growth_per_wave * speed_growth_wave;
    stats.hp *= hp_growth * sim.cfg.enemy_hp_scale;
    stats.speed_mult *= sim.cfg.enemy_speed * speed_growth;
    stats.damage *= sim.cfg.enemy_damage_scale;

    if (elite) {
        stats.hp *= sim.cfg.elite_hp_multiplier;
        stats.speed_mult *= sim.cfg.elite_speed_multiplier;
        radius = fmaxf(radius + sim.cfg.elite_radius_bonus, sim.cfg.elite_min_radius);
        stats.damage *= sim.cfg.elite_damage_multiplier;
    }
    if (boss) {
        stats.hp = (sim.cfg.boss_hp_base + sim.cfg.boss_hp_per_wave * (float)wave)
            * sim.cfg.enemy_hp_scale;
        stats.speed_mult = sim.cfg.enemy_speed * sim.cfg.boss_speed_multiplier;
        radius = sim.cfg.boss_radius;
        stats.damage = sim.cfg.boss_damage * sim.cfg.enemy_damage_scale;
    }
    if (ari_k) {
        stats.hp *= sim.cfg.ari_k_hp_multiplier;
        stats.speed_mult = sim.cfg.enemy_speed * sim.cfg.ari_k_speed_multiplier;
        radius = sim.cfg.ari_k_radius;
        stats.damage = sim.cfg.ari_k_damage * sim.cfg.enemy_damage_scale;
    }

    stats.hp = fmaxf(1.0f, ceilf(stats.hp));
    stats.damage = fmaxf(1.0f, ceilf(stats.damage));
    *kind_out = kind;
    *radius_out = radius;
    return stats;
}

PS_D void ps_enemy_geometry(const PSCudaSim& sim, int kind, int ari_k,
        float radius, int* shape, float* half_width, float* half_height,
        float* bound_radius) {
    *shape = ari_k ? sim.cfg.ari_k_shape : sim.cfg.enemy_shape[kind];
    *half_width = ari_k ? sim.cfg.ari_k_half_width : sim.cfg.enemy_half_width[kind];
    *half_height = ari_k ? sim.cfg.ari_k_half_height : sim.cfg.enemy_half_height[kind];
    *bound_radius = ps_geometry_shape_bound_radius(*shape, radius,
        *half_width, *half_height);
}

PS_D int ps_spawn_enemy(const PSCudaSim& sim, int env) {
    if (sim.enemy_count[env] >= sim.cfg.enemy_cap) return 0;
    int slot = ps_find_free_slot(sim.enemy_active, sim.cfg.enemy_cap, &sim.next_enemy_slot[env], sim.num_envs, env);
    if (slot < 0) return 0;

    float x = 0.0f, y = 0.0f;
    ps_pick_spawn_position(sim, env, sim.cfg.enemy_spawn_radius, &x, &y);
    for (int tries = 0; tries < 16 && ps_overlaps_obstacle(sim, env, x, y,
            sim.cfg.enemy_spawn_radius, sim.cfg.enemy_spawn_padding); tries++) {
        ps_pick_spawn_position(sim, env, sim.cfg.enemy_spawn_radius, &x, &y);
    }

    int elite = ps_randf(sim, env) < sim.cfg.elite_spawn_rate
        + sim.cfg.elite_spawn_ramp_per_tick * (float)sim.tick[env];
    int wave_len = sim.cfg.wave_length_steps;
    int wave = ps_wave_index(sim, env);
    int ari_k_wave = wave >= sim.cfg.ari_k_start_wave
        && (wave - sim.cfg.ari_k_start_wave) % sim.cfg.ari_k_wave_period == 0;
    int ari_k = ari_k_wave && sim.tick[env] % wave_len == 1
        && sim.last_boss_tick[env] != sim.tick[env];
    int boss = ari_k || (sim.tick[env] > 0 && sim.tick[env] % sim.cfg.boss_period_steps == 0
        && sim.last_boss_tick[env] != sim.tick[env]);
    if (boss) sim.last_boss_tick[env] = sim.tick[env];
    int kind = 0;
    float radius = 0.0f;
    PSEnemyDef stats = ps_enemy_stats(sim, env, &kind, &radius, elite, boss, ari_k);
    uint8_t visual_type = (uint8_t)(kind & PS_ENEMY_KIND_MASK);
    if (elite) visual_type |= PS_ENEMY_ELITE_FLAG;
    if (boss) visual_type = PS_ENEMY_BOSS_FLAG;
    if (ari_k) visual_type |= PS_ENEMY_ARI_K_FLAG;

    int e = PS_EIDX(sim, slot, env);
    sim.enemy_type[e] = visual_type;
    sim.enemy_x[e] = x;
    sim.enemy_y[e] = y;
    sim.enemy_vx[e] = 0.0f;
    sim.enemy_vy[e] = 0.0f;
    sim.enemy_max_hp[e] = stats.hp;
    sim.enemy_hp[e] = stats.hp;
    sim.enemy_radius[e] = radius;
    int shape = PS_SHAPE_CIRCLE;
    float half_width = radius;
    float half_height = radius;
    float bound_radius = radius;
    ps_enemy_geometry(sim, kind, ari_k, radius, &shape, &half_width,
        &half_height, &bound_radius);
    sim.enemy_shape[e] = (uint8_t)shape;
    sim.enemy_half_width[e] = half_width;
    sim.enemy_half_height[e] = half_height;
    sim.enemy_bound_radius[e] = bound_radius;
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
    ps_push_out_obstacles(sim, env, &x, &y, sim.cfg.drop_spawn_radius, 0);
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
    if (sim.area_count[env] >= sim.cfg.area_cap) return;
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
    sim.area_tick_rate[a] = tick_rate;
    sim.area_tick_timer[a] = 0;
    sim.area_active[a] = 1;
    if (type == PS_WEAPON_INK) sim.active_ink_count[env]++;
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
    sim.aabb_count[env] = 0;
    for (int i = 0; i < sim.cfg.enemy_cap; i++) {
        int e = PS_EIDX(sim, i, env);
        sim.enemy_next[e] = -1;
        if (!sim.enemy_active[e]) continue;
        if (sim.enemy_shape[e] == PS_SHAPE_AABB) {
            sim.aabb_indices[PS_EIDX(sim, sim.aabb_count[env], env)] = i;
            sim.aabb_count[env]++;
        }
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
    sim.episode_reward_damage[env] += sim.cfg.reward_damage * damage;
    sim.episode_damage_dealt[env] += damage;
    if (sim.enemy_hp[e] > 0.0f) return 0;

    uint8_t type = sim.enemy_type[e];
    int elite = (type & PS_ENEMY_ELITE_FLAG) != 0;
    int boss = (type & PS_ENEMY_BOSS_FLAG) != 0;
    sim.rewards[env] += sim.cfg.reward_kill;
    sim.episode_reward_kill[env] += sim.cfg.reward_kill;
    sim.episode_kills[env] += 1.0f;
    sim.episode_score[env] += boss ? sim.cfg.kill_score_boss
        : (elite ? sim.cfg.kill_score_elite : sim.cfg.kill_score_default);
    ps_spawn_drop(sim, env, sim.enemy_x[e], sim.enemy_y[e],
        boss ? sim.cfg.drop_value_boss
        : (elite ? sim.cfg.drop_value_elite : sim.cfg.drop_value_default), 0);
    float missing_hp = ps_clampf((sim.max_hp[env] - sim.hp[env]) / sim.max_hp[env], 0.0f, 1.0f);
    float health_chance = sim.cfg.health_drop_rate
        * (1.0f + sim.cfg.health_drop_elite_bonus * (float)elite
        + sim.cfg.health_drop_boss_bonus * (float)boss
        + sim.cfg.health_drop_missing_hp_bonus * missing_hp);
    if (ps_randf(sim, env) < health_chance) {
        ps_spawn_drop(sim, env, sim.enemy_x[e] + sim.cfg.health_drop_offset_x,
            sim.enemy_y[e] + sim.cfg.health_drop_offset_y, sim.cfg.health_heal, 1);
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

    int len = sim.cfg.wave_length_steps;
    int local = sim.tick[env] % len;
    int wave = ps_wave_index(sim, env);
    if (local == 1 && wave >= sim.cfg.ari_k_start_wave
            && (wave - sim.cfg.ari_k_start_wave) % sim.cfg.ari_k_wave_period == 0)
        ps_spawn_enemy(sim, env);
    int special_ring = 0;
    for (int i = 0; i < sim.cfg.special_ring_wave_count; i++) {
        special_ring |= wave == sim.cfg.special_ring_waves[i];
    }
    if (local == 1 && special_ring) {
        float half = 0.5f * sim.cfg.arena_size;
        float radius = half * sim.cfg.special_ring_radius_ratio;
        for (int i = 0; i < sim.cfg.special_ring_enemy_count; i++) {
            int slot = ps_spawn_enemy(sim, env);
            if (!slot) return;
            int idx = slot - 1;
            int e = PS_EIDX(sim, idx, env);
            float angle = 2.0f * PI * ((float)i
                / (float)sim.cfg.special_ring_enemy_count);
            sim.enemy_x[e] = sim.px[env] + cosf(angle) * radius;
            sim.enemy_y[e] = sim.py[env] + sinf(angle) * radius;
            sim.enemy_speed[e] *= sim.cfg.special_ring_speed_mult;
        }
    }
}

PS_D void ps_update_enemies(const PSCudaSim& sim, int env) {
    float half = 0.5f * sim.cfg.arena_size;
    float far2 = (half * sim.cfg.enemy_recycle_radius_ratio)
        * (half * sim.cfg.enemy_recycle_radius_ratio);
    float player_x = sim.px[env];
    float player_y = sim.py[env];
    float player_radius = sim.cfg.player_radius;
    int obstacle_stride = sim.cfg.enemy_obstacle_stride;
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
            ps_push_out_obstacles_shape(sim, env, &x, &y, sim.enemy_shape[e],
                sim.enemy_radius[e], sim.enemy_half_width[e],
                sim.enemy_half_height[e], 0);
        }
        sim.enemy_x[e] = x;
        sim.enemy_y[e] = y;
        float post_dx = sim.enemy_x[e] - player_x;
        float post_dy = sim.enemy_y[e] - player_y;
        float post_d2 = post_dx * post_dx + post_dy * post_dy;
        if (post_d2 > far2) {
            float nx, ny;
            ps_pick_spawn_position(sim, env, sim.enemy_bound_radius[e], &nx, &ny);
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
        int hit = ps_geometry_shape_overlaps_circle(sim.enemy_shape[e],
            player_x - sim.enemy_x[e], player_y - sim.enemy_y[e],
            sim.enemy_radius[e], sim.enemy_half_width[e],
            sim.enemy_half_height[e], player_radius);
        if (sim.invuln_timer[env] <= 0 && hit) {
            float dmg = fmaxf(1.0f, ceilf(sim.enemy_damage[e] * sim.cfg.contact_damage));
            sim.hp[env] -= dmg;
            sim.rewards[env] += sim.cfg.reward_hurt * dmg;
            sim.episode_reward_hurt[env] += sim.cfg.reward_hurt * dmg;
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
            float dy = sim.projectile_y[p] - sim.obstacle_y[oi];
            if (ps_geometry_circle_overlaps(dx, dy, r)) {
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
                    if (sim.enemy_shape[e] != PS_SHAPE_CIRCLE) continue;
                    if (!ps_geometry_shape_overlaps_circle(sim.enemy_shape[e],
                            sim.projectile_x[p] - sim.enemy_x[e],
                            sim.projectile_y[p] - sim.enemy_y[e],
                            sim.enemy_radius[e], sim.enemy_half_width[e],
                            sim.enemy_half_height[e], sim.projectile_radius[p])) continue;
                    ps_damage_enemy(sim, env, eidx, sim.projectile_damage[p]);
                    if (sim.projectile_pierce[p] <= 0) ps_deactivate_projectile(sim, env, i);
                    else sim.projectile_pierce[p]--;
                    break;
                }
            }
        }
        for (int a = 0; a < sim.aabb_count[env] && sim.projectile_active[p]; a++) {
            int eidx = sim.aabb_indices[PS_EIDX(sim, a, env)];
            int e = PS_EIDX(sim, eidx, env);
            if (!sim.enemy_active[e]) continue;
            if (!ps_geometry_shape_overlaps_circle(sim.enemy_shape[e],
                    sim.projectile_x[p] - sim.enemy_x[e],
                    sim.projectile_y[p] - sim.enemy_y[e],
                    sim.enemy_radius[e], sim.enemy_half_width[e],
                    sim.enemy_half_height[e], sim.projectile_radius[p])) continue;
            ps_damage_enemy(sim, env, eidx, sim.projectile_damage[p]);
            if (sim.projectile_pierce[p] <= 0) ps_deactivate_projectile(sim, env, i);
            else sim.projectile_pierce[p]--;
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
            sim.drop_x[d] += dx / dist * sim.cfg.pickup_magnet_speed;
            sim.drop_y[d] += dy / dist * sim.cfg.pickup_magnet_speed;
        }
        if (dist2 < sim.cfg.pickup_radius * sim.cfg.pickup_radius) {
            sim.rewards[env] += sim.cfg.reward_pickup;
            sim.episode_reward_pickup[env] += sim.cfg.reward_pickup;
            if (sim.drop_type[d] == 1) {
                sim.hp[env] = fminf(sim.max_hp[env], sim.hp[env] + sim.drop_value[d]);
            } else {
                sim.xp[env] += sim.drop_value[d];
                sim.episode_xp[env] += sim.drop_value[d];
                sim.rewards[env] += sim.cfg.reward_xp * sim.drop_value[d];
                sim.episode_reward_xp[env] += sim.cfg.reward_xp * sim.drop_value[d];
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
        sim.rewards[env] += sim.cfg.reward_levelup;
        sim.episode_reward_levelup[env] += sim.cfg.reward_levelup;
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

PS_D void ps_damage_radius_with_query_pad(const PSCudaSim& sim, int env,
        float x, float y, float radius, float damage, float knockback,
        float query_pad) {
    if (knockback > 0.0f) {
        sim.nearest_enemy[env] = -1;
        sim.nearest_enemy_d2[env] = 1e30f;
    }

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
                    if (sim.enemy_shape[e] != PS_SHAPE_CIRCLE) {
                        eidx = next;
                        continue;
                    }
                    float dx = sim.enemy_x[e] - x;
                    float dy = sim.enemy_y[e] - y;
                    float d2 = dx * dx + dy * dy;
                    if (ps_geometry_shape_overlaps_circle(sim.enemy_shape[e],
                            dx, dy, sim.enemy_radius[e],
                            sim.enemy_half_width[e], sim.enemy_half_height[e], radius)) {
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
    for (int a = 0; a < sim.aabb_count[env]; a++) {
        int eidx = sim.aabb_indices[PS_EIDX(sim, a, env)];
        int e = PS_EIDX(sim, eidx, env);
        if (!sim.enemy_active[e]) continue;
        float dx = sim.enemy_x[e] - x;
        float dy = sim.enemy_y[e] - y;
        float d2 = dx * dx + dy * dy;
        if (!ps_geometry_shape_overlaps_circle(sim.enemy_shape[e], dx, dy,
                sim.enemy_radius[e], sim.enemy_half_width[e],
                sim.enemy_half_height[e], radius)) continue;
        int killed = ps_damage_enemy(sim, env, eidx, damage);
        if (!killed && knockback > 0.0f && sim.enemy_active[e]) {
            float d = sqrtf(fmaxf(d2, 0.0001f));
            sim.enemy_x[e] += dx / d * knockback;
            sim.enemy_y[e] += dy / d * knockback;
        }
    }
}

PS_D void ps_damage_radius(const PSCudaSim& sim, int env, float x, float y,
        float radius, float damage, float knockback) {
    ps_damage_radius_with_query_pad(sim, env, x, y, radius, damage, knockback, 1.50f);
}
PS_D void ps_update_areas(const PSCudaSim& sim, int env) {
    for (int k = 0; k < sim.area_count[env]; ) {
        int i = sim.area_dense[PS_AIDX(sim, k, env)];
        int a = PS_AIDX(sim, i, env);
        sim.area_ttl[a]--;
        sim.area_tick_timer[a]--;
        if (sim.area_tick_timer[a] <= 0 && sim.area_damage[a] > 0.0f) {
            ps_damage_radius_with_query_pad(sim, env, sim.area_x[a], sim.area_y[a],
                sim.area_radius[a], sim.area_damage[a],
                sim.cfg.area_tick_knockback, 0.0f);
            sim.area_tick_timer[a] = sim.area_tick_rate[a];
        }
        if (sim.area_ttl[a] <= 0) {
            ps_deactivate_area(sim, env, i);
            continue;
        }
        k++;
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_INK, env)] = ps_clampf((float)sim.active_ink_count[env] / 8.0f, 0.0f, 1.0f);
}

PS_D void ps_cast_bubble(const PSCudaSim& sim, int env, int level) {
    int target = ps_nearest_enemy(sim, env,
        sim.cfg.bubble_target_range + sim.cfg.bubble_target_area_range * sim.area_bonus[env]);
    if (target < 0) return;
    int shots = 1 + level / 3;
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_BUBBLE, level, 1);
    float radius = ps_geometry_weapon_radius(&sim.cfg, PS_WEAPON_BUBBLE, 0)
        * (1.0f + sim.area_bonus[env]);
    float speed = sim.cfg.projectile_speed * (1.0f + sim.projectile_speed_bonus[env]);
    int pierce = sim.pierce_bonus[env] + level / 4;
    int e = PS_EIDX(sim, target, env);
    for (int i = 0; i < shots; i++) {
        float jitter = ((float)i - 0.5f * (float)(shots - 1)) * sim.cfg.bubble_shot_spread;
        ps_spawn_projectile(sim, env, PS_WEAPON_BUBBLE, sim.px[env], sim.py[env],
            sim.enemy_x[e] + jitter, sim.enemy_y[e] - jitter, damage, radius,
            speed, pierce, sim.cfg.bubble_projectile_ttl);
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_BUBBLE, env)] = 1.0f;
}

PS_D void ps_cast_whirlpool(const PSCudaSim& sim, int env, int level) {
    float radius = ps_geometry_weapon_radius(&sim.cfg, PS_WEAPON_WHIRLPOOL, level - 1)
        * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_WHIRLPOOL, level, 0);
    ps_damage_radius(sim, env, sim.px[env], sim.py[env], radius, damage,
        sim.cfg.whirlpool_knockback);
    ps_spawn_area(sim, env, PS_WEAPON_WHIRLPOOL, sim.px[env], sim.py[env],
        radius, 0.0f, sim.cfg.whirlpool_ttl, sim.cfg.whirlpool_tick_rate);
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_WHIRLPOOL, env)] = 1.0f;
}

PS_D void ps_cast_orbit(const PSCudaSim& sim, int env, int level) {
    int count = 1 + level / 2;
    float orbit_r = (sim.cfg.weapon_orbit_distance
        + sim.cfg.weapon_orbit_distance_per_level * (float)level)
        * (1.0f + sim.cfg.orbit_area_distance_bonus * sim.area_bonus[env]);
    float hit_r = ps_geometry_weapon_radius(&sim.cfg, PS_WEAPON_ORBIT, level)
        * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_ORBIT, level, 0);
    for (int i = 0; i < count; i++) {
        float a = sim.orbit_phase[env] + 2.0f * PI * ((float)i / (float)count);
        ps_damage_radius(sim, env, sim.px[env] + cosf(a) * orbit_r,
            sim.py[env] + sinf(a) * orbit_r, hit_r, damage, sim.cfg.orbit_knockback);
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_ORBIT, env)] = 1.0f;
}

PS_D void ps_cast_ink(const PSCudaSim& sim, int env, int level) {
    int target = ps_nearest_enemy(sim, env,
        sim.cfg.arena_size * sim.cfg.ink_target_range_ratio);
    if (target < 0) return;
    int pools = 1 + level / 3;
    float radius = ps_geometry_weapon_radius(&sim.cfg, PS_WEAPON_INK, level)
        * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_INK, level, 0);
    int ttl = sim.cfg.ink_pool_ttl_base + sim.cfg.ink_pool_ttl_per_level * level;
    int e = PS_EIDX(sim, target, env);
    for (int i = 0; i < pools; i++) {
        float angle = 2.0f * PI * ((float)i / (float)pools)
            + ps_randf(sim, env) * sim.cfg.ink_pool_angle_jitter;
        float dist = pools > 1 ? sim.cfg.ink_pool_spread : 0.0f;
        ps_spawn_area(sim, env, PS_WEAPON_INK,
            sim.enemy_x[e] + cosf(angle) * dist,
            sim.enemy_y[e] + sinf(angle) * dist, radius, damage, ttl,
            sim.cfg.ink_pool_tick_rate);
    }
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_INK, env)] = 1.0f;
}

PS_D void ps_update_poison_oil_trail(const PSCudaSim& sim, int env, int level) {
    if (level <= 0) return;
    float speed2 = sim.pvx[env] * sim.pvx[env] + sim.pvy[env] * sim.pvy[env];
    if (speed2 < sim.cfg.ink_trail_min_speed2) return;

    int active_oil = sim.active_ink_count[env];
    int max_oil = sim.cfg.ink_trail_max_base
        + level * sim.cfg.ink_trail_max_per_level;
    if (active_oil >= max_oil) return;

    int cadence = sim.cfg.ink_trail_cadence_base
        - level / sim.cfg.ink_trail_cadence_level_divisor;
    if (cadence < sim.cfg.ink_trail_cadence_min)
        cadence = sim.cfg.ink_trail_cadence_min;
    if (sim.tick[env] % cadence != 0) return;

    float speed = sqrtf(speed2);
    float nx = sim.pvx[env] / speed;
    float ny = sim.pvy[env] / speed;
    float radius = (sim.cfg.ink_trail_radius_base
        + sim.cfg.ink_trail_radius_per_level * (float)level
        + sim.cfg.ink_trail_radius_config_scale
            * sim.cfg.weapon_radius_per_level[PS_WEAPON_INK] * (float)level)
        * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_INK, level, 0)
        * sim.cfg.ink_trail_damage_multiplier;
    int ttl = sim.cfg.ink_trail_ttl_base + sim.cfg.ink_trail_ttl_per_level * level;
    ps_spawn_area(sim, env, PS_WEAPON_INK,
        sim.px[env] - nx * sim.cfg.ink_trail_offset,
        sim.py[env] - ny * sim.cfg.ink_trail_offset, radius, damage, ttl,
        sim.cfg.ink_trail_tick_rate);
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_INK, env)] = 1.0f;
}

PS_D void ps_cast_sonar(const PSCudaSim& sim, int env, int level) {
    float radius = ps_geometry_weapon_radius(&sim.cfg, PS_WEAPON_SONAR, level)
        * (1.0f + sim.area_bonus[env]);
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_SONAR, level, 0);
    ps_damage_radius(sim, env, sim.px[env], sim.py[env], radius, damage,
        sim.cfg.sonar_knockback);
    ps_spawn_area(sim, env, PS_WEAPON_SONAR, sim.px[env], sim.py[env], radius,
        0.0f, sim.cfg.sonar_ttl, sim.cfg.sonar_tick_rate);
    sim.weapon_active[PS_WIDX(sim, PS_WEAPON_SONAR, env)] = 1.0f;
}

PS_D void ps_update_weapons(const PSCudaSim& sim, int env) {
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        int w = PS_WIDX(sim, i, env);
        sim.weapon_active[w] *= sim.cfg.weapon_active_decay;
        if (sim.weapon_cd[w] > 0.0f) sim.weapon_cd[w] -= 1.0f;
    }
    sim.orbit_phase[env] += sim.cfg.orbit_phase_speed
        + sim.cfg.orbit_phase_per_level
            * (float)sim.weapon_level[PS_WIDX(sim, PS_WEAPON_ORBIT, env)];
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
    sim.max_hp[env] = floorf(sim.cfg.player_health);
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
    sim.episode_reward_survival[env] = 0.0f;
    sim.episode_reward_damage[env] = 0.0f;
    sim.episode_reward_kill[env] = 0.0f;
    sim.episode_reward_hurt[env] = 0.0f;
    sim.episode_reward_pickup[env] = 0.0f;
    sim.episode_reward_xp[env] = 0.0f;
    sim.episode_reward_levelup[env] = 0.0f;
    sim.episode_reward_obstacle[env] = 0.0f;
    sim.episode_reward_terminal[env] = 0.0f;
    sim.episode_score[env] = 0.0f;
    sim.episode_kills[env] = 0.0f;
    sim.episode_xp[env] = 0.0f;
    sim.episode_damage_dealt[env] = 0.0f;
    sim.episode_damage_taken[env] = 0.0f;
    sim.episode_pickups[env] = 0.0f;
    sim.episode_levelups[env] = 0.0f;
    sim.episode_obstacle_hits[env] = 0.0f;
    sim.episode_peak_enemies[env] = 0.0f;
    sim.episode_peak_projectiles[env] = 0.0f;
    sim.episode_min_hp[env] = sim.hp[env];
    if (sim.cfg.free_upgrade >= 0) {
        for (int i = 0; i < sim.cfg.free_upgrade_count; i++)
            ps_apply_upgrade_effect(sim, env, sim.cfg.free_upgrade);
    }
    sim.nearest_enemy[env] = -1;
    sim.nearest_enemy_d2[env] = 1e30f;
    ps_clear_entities(sim, env);
    ps_spawn_obstacles(sim, env);
    ps_compute_observations(sim, env);
}

PS_D void ps_step_env(const PSCudaSim& sim, int env) {
    sim.rewards[env] = sim.cfg.reward_survival;
    sim.episode_reward_survival[env] += sim.cfg.reward_survival;
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
    sim.pvx[env] += (target_vx - sim.pvx[env]) * sim.cfg.movement_smoothing;
    sim.pvy[env] += (target_vy - sim.pvy[env]) * sim.cfg.movement_smoothing;
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
    ps_push_out_obstacles(sim, env, &px, &py, sim.cfg.player_radius, 1);
    sim.px[env] = px;
    sim.py[env] = py;
    ps_recycle_far_obstacles(sim, env);
    ps_update_moving_obstacles(sim, env);

    ps_wave_spawns(sim, env);
    ps_update_enemies(sim, env);
    ps_rebuild_grid(sim, env);
    ps_update_weapons(sim, env);
    if (sim.projectile_count[env] > 0) {
        ps_update_projectiles(sim, env);
    }
    ps_update_drops(sim, env);

    if ((float)sim.enemy_count[env] > sim.episode_peak_enemies[env])
        sim.episode_peak_enemies[env] = (float)sim.enemy_count[env];
    if ((float)sim.projectile_count[env] > sim.episode_peak_projectiles[env])
        sim.episode_peak_projectiles[env] = (float)sim.projectile_count[env];
    if (sim.hp[env] < sim.episode_min_hp[env]) sim.episode_min_hp[env] = sim.hp[env];

    sim.episode_return[env] += sim.rewards[env];
    if (sim.hp[env] <= 0.0f || sim.tick[env] >= sim.cfg.max_steps) {
        int survived = sim.tick[env] >= sim.cfg.max_steps && sim.hp[env] > 0.0f;
        float terminal_reward = survived
            ? sim.cfg.reward_success
            : sim.cfg.reward_death;
        sim.rewards[env] += terminal_reward;
        sim.episode_reward_terminal[env] += terminal_reward;
        sim.episode_return[env] += terminal_reward;
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

__global__ void ps_pack_episode_logs_kernel(PSCudaSim sim) {
    int env = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= sim.num_envs) return;
    Log* record = &sim.native_envs[env].log;
    if (sim.log_n[env] <= 0.0f) return;

#define PS_PACK_LOG(field) \
    record->field += sim.log_##field[env]; \
    sim.log_##field[env] = 0.0f
    PS_PACK_LOG(perf); PS_PACK_LOG(score); PS_PACK_LOG(episode_return);
    PS_PACK_LOG(reward_survival); PS_PACK_LOG(reward_damage); PS_PACK_LOG(reward_kill);
    PS_PACK_LOG(reward_hurt); PS_PACK_LOG(reward_pickup); PS_PACK_LOG(reward_xp);
    PS_PACK_LOG(reward_levelup); PS_PACK_LOG(reward_obstacle); PS_PACK_LOG(reward_terminal);
    PS_PACK_LOG(episode_length); PS_PACK_LOG(kills); PS_PACK_LOG(level); PS_PACK_LOG(xp);
    PS_PACK_LOG(damage_dealt); PS_PACK_LOG(damage_taken); PS_PACK_LOG(pickups);
    PS_PACK_LOG(levelups); PS_PACK_LOG(obstacle_hits); PS_PACK_LOG(enemies_alive);
    PS_PACK_LOG(projectiles_alive); PS_PACK_LOG(drops_alive); PS_PACK_LOG(areas_alive);
    PS_PACK_LOG(weapon_levels); PS_PACK_LOG(wave); PS_PACK_LOG(hp);
    PS_PACK_LOG(survived); PS_PACK_LOG(n); PS_PACK_LOG(death_0_25);
    PS_PACK_LOG(death_25_50); PS_PACK_LOG(death_50_75); PS_PACK_LOG(death_75_100);
    PS_PACK_LOG(success); PS_PACK_LOG(peak_enemies); PS_PACK_LOG(peak_projectiles);
    PS_PACK_LOG(min_hp);
#undef PS_PACK_LOG
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
