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
#include "../ps_geometry.h"
#include "../ps_log.h"

#ifndef PS_CUDA_BLOCK_SIZE
#define PS_CUDA_BLOCK_SIZE 256
#endif

// Config and simulator SoA
// -----------------------------------------------------------------------------



struct PSCudaSim {
    int num_envs;
    PSConfig cfg;
    int owns_io;
    void* blob;
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
    int *enemy_next, *enemy_dense, *enemy_dense_pos;

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
};


#include "../ps_sim.h"

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

#define PS_BLOB_ACCOUNT(type, count) do { \
    state_bytes = (state_bytes + sizeof(type) - 1) & ~(sizeof(type) - 1); \
    state_bytes += sizeof(type) * (size_t)(count); \
} while (0)
#define PS_BLOB_FIELD(type, field, count) do { \
    state_offset = (state_offset + sizeof(type) - 1) & ~(sizeof(type) - 1); \
    (sim)->field = (type*)((char*)(sim)->blob + state_offset); \
    state_offset += sizeof(type) * (size_t)(count); \
} while (0)

static inline void ps_cuda_alloc(PSCudaSim* sim, int num_envs, PSConfig cfg) {
    std::memset(sim, 0, sizeof(*sim));
    sim->num_envs = num_envs;
    sim->cfg = cfg;
    sim->owns_io = 1;

    const size_t N = (size_t)num_envs;
    const size_t NE = (size_t)cfg.enemy_cap * N;
    const size_t NP = (size_t)cfg.projectile_cap * N;
    const size_t ND = (size_t)cfg.drop_cap * N;
    const size_t NA = (size_t)PS_MAX_AREAS * N;
    const size_t NO = (size_t)(cfg.obstacle_count > 0 ? cfg.obstacle_count : 1) * N;
    const size_t NMO = (size_t)PS_MAX_MOVING_OBSTACLES * N;
    const size_t NG = (size_t)PS_GRID_CELLS * N;
    const size_t NGT = (size_t)cfg.enemy_cap * N;

    PS_CUDA_CHECK(cudaMalloc((void**)&sim->observations, sizeof(float) * N * PS_OBS_SIZE));
    PS_CUDA_CHECK(cudaMalloc((void**)&sim->actions, sizeof(float) * N * 2));
    PS_CUDA_CHECK(cudaMalloc((void**)&sim->rewards, sizeof(float) * N));
    PS_CUDA_CHECK(cudaMalloc((void**)&sim->terminals, sizeof(float) * N));

    size_t state_bytes = 0;
    PS_BLOB_ACCOUNT(uint32_t, N);                       // rng
    PS_BLOB_ACCOUNT(float, N * 8);                      // px py pvx pvy hp max_hp xp + orbit_phase
    PS_BLOB_ACCOUNT(int, N * 6);                        // player_facing_left level pierce pending queued last_boss
    PS_BLOB_ACCOUNT(float, N * 6);                      // speed damage cooldown projectile_speed magnet area
    PS_BLOB_ACCOUNT(int, N * 9);                        // tick invuln nearest + all pool counts
    PS_BLOB_ACCOUNT(int, N * 5);                        // next_*_slot cursors
    PS_BLOB_ACCOUNT(int, N * PS_UPGRADE_SLOTS);         // offered
    PS_BLOB_ACCOUNT(float, N * PS_WEAPON_COUNT * 2);    // weapon_cd + weapon_active
    PS_BLOB_ACCOUNT(int, N * PS_WEAPON_COUNT);          // weapon_level
    PS_BLOB_ACCOUNT(float, N * 22);                     // episode stats + nearest_enemy_d2
    PS_BLOB_ACCOUNT(int, N * (PS_GRID_CELLS + 2));      // grid_head + grid_touched_count + aabb_count
    PS_BLOB_ACCOUNT(int, NGT * 2);                      // grid_touched + aabb_indices
    PS_BLOB_ACCOUNT(uint8_t, NE * 3);
    PS_BLOB_ACCOUNT(float, NE * 12);
    PS_BLOB_ACCOUNT(int, NE * 3);
    // Projectile pool
    PS_BLOB_ACCOUNT(uint8_t, NP * 2);
    PS_BLOB_ACCOUNT(float, NP * 6);
    PS_BLOB_ACCOUNT(int, NP * 4);
    // Drop pool
    PS_BLOB_ACCOUNT(uint8_t, ND * 2);
    PS_BLOB_ACCOUNT(float, ND * 3);
    PS_BLOB_ACCOUNT(int, ND * 2);
    // Area pool
    PS_BLOB_ACCOUNT(uint8_t, NA * 2);
    PS_BLOB_ACCOUNT(float, NA * 4);
    PS_BLOB_ACCOUNT(int, NA * 5);
    // Obstacles
    PS_BLOB_ACCOUNT(uint8_t, NO);
    PS_BLOB_ACCOUNT(float, NO * 3);
    // Moving obstacles
    PS_BLOB_ACCOUNT(uint8_t, NMO * 3);
    PS_BLOB_ACCOUNT(float, NMO * 7);
    PS_BLOB_ACCOUNT(int, NMO * 3);

    PS_CUDA_CHECK(cudaMalloc(&sim->blob, state_bytes));
    size_t state_offset = 0;
    PS_BLOB_FIELD(uint32_t, rng, N);
    PS_BLOB_FIELD(float, px, N); PS_BLOB_FIELD(float, py, N);
    PS_BLOB_FIELD(float, pvx, N); PS_BLOB_FIELD(float, pvy, N);
    PS_BLOB_FIELD(float, hp, N); PS_BLOB_FIELD(float, max_hp, N);
    PS_BLOB_FIELD(float, xp, N); PS_BLOB_FIELD(float, orbit_phase, N);
    PS_BLOB_FIELD(int, player_facing_left, N); PS_BLOB_FIELD(int, level, N);
    PS_BLOB_FIELD(int, pierce_bonus, N); PS_BLOB_FIELD(int, pending_upgrade, N);
    PS_BLOB_FIELD(int, queued_upgrades, N); PS_BLOB_FIELD(int, last_boss_tick, N);
    PS_BLOB_FIELD(float, speed_bonus, N); PS_BLOB_FIELD(float, damage_bonus, N);
    PS_BLOB_FIELD(float, cooldown_mult, N); PS_BLOB_FIELD(float, projectile_speed_bonus, N);
    PS_BLOB_FIELD(float, magnet_bonus, N); PS_BLOB_FIELD(float, area_bonus, N);
    PS_BLOB_FIELD(int, tick, N); PS_BLOB_FIELD(int, invuln_timer, N);
    PS_BLOB_FIELD(int, nearest_enemy, N);
    PS_BLOB_FIELD(int, enemy_count, N); PS_BLOB_FIELD(int, projectile_count, N);
    PS_BLOB_FIELD(int, drop_count, N); PS_BLOB_FIELD(int, area_count, N);
    PS_BLOB_FIELD(int, moving_obstacle_count, N); PS_BLOB_FIELD(int, active_ink_count, N);
    PS_BLOB_FIELD(int, next_enemy_slot, N); PS_BLOB_FIELD(int, next_projectile_slot, N);
    PS_BLOB_FIELD(int, next_drop_slot, N); PS_BLOB_FIELD(int, next_area_slot, N);
    PS_BLOB_FIELD(int, next_moving_obstacle_slot, N);
    PS_BLOB_FIELD(int, offered, N * PS_UPGRADE_SLOTS);
    PS_BLOB_FIELD(float, weapon_cd, N * PS_WEAPON_COUNT);
    PS_BLOB_FIELD(float, weapon_active, N * PS_WEAPON_COUNT);
    PS_BLOB_FIELD(int, weapon_level, N * PS_WEAPON_COUNT);
    PS_BLOB_FIELD(float, episode_return, N);
    PS_BLOB_FIELD(float, episode_reward_survival, N); PS_BLOB_FIELD(float, episode_reward_damage, N);
    PS_BLOB_FIELD(float, episode_reward_kill, N); PS_BLOB_FIELD(float, episode_reward_hurt, N);
    PS_BLOB_FIELD(float, episode_reward_pickup, N); PS_BLOB_FIELD(float, episode_reward_xp, N);
    PS_BLOB_FIELD(float, episode_reward_levelup, N); PS_BLOB_FIELD(float, episode_reward_obstacle, N);
    PS_BLOB_FIELD(float, episode_reward_terminal, N); PS_BLOB_FIELD(float, episode_score, N);
    PS_BLOB_FIELD(float, episode_kills, N); PS_BLOB_FIELD(float, episode_xp, N);
    PS_BLOB_FIELD(float, episode_damage_dealt, N); PS_BLOB_FIELD(float, episode_damage_taken, N);
    PS_BLOB_FIELD(float, episode_pickups, N); PS_BLOB_FIELD(float, episode_levelups, N);
    PS_BLOB_FIELD(float, episode_obstacle_hits, N); PS_BLOB_FIELD(float, episode_peak_enemies, N);
    PS_BLOB_FIELD(float, episode_peak_projectiles, N); PS_BLOB_FIELD(float, episode_min_hp, N);
    PS_BLOB_FIELD(float, nearest_enemy_d2, N);
    PS_BLOB_FIELD(int, grid_head, NG);
    PS_BLOB_FIELD(int, grid_touched_count, N); PS_BLOB_FIELD(int, aabb_count, N);
    PS_BLOB_FIELD(int, grid_touched, NGT); PS_BLOB_FIELD(int, aabb_indices, NGT);
    PS_BLOB_FIELD(uint8_t, enemy_active, NE); PS_BLOB_FIELD(uint8_t, enemy_type, NE);
    PS_BLOB_FIELD(uint8_t, enemy_shape, NE);
    PS_BLOB_FIELD(float, enemy_x, NE); PS_BLOB_FIELD(float, enemy_y, NE);
    PS_BLOB_FIELD(float, enemy_vx, NE); PS_BLOB_FIELD(float, enemy_vy, NE);
    PS_BLOB_FIELD(float, enemy_hp, NE); PS_BLOB_FIELD(float, enemy_max_hp, NE);
    PS_BLOB_FIELD(float, enemy_radius, NE); PS_BLOB_FIELD(float, enemy_bound_radius, NE);
    PS_BLOB_FIELD(float, enemy_half_width, NE); PS_BLOB_FIELD(float, enemy_half_height, NE);
    PS_BLOB_FIELD(float, enemy_speed, NE); PS_BLOB_FIELD(float, enemy_damage, NE);
    PS_BLOB_FIELD(int, enemy_next, NE); PS_BLOB_FIELD(int, enemy_dense, NE);
    PS_BLOB_FIELD(int, enemy_dense_pos, NE);
    PS_BLOB_FIELD(uint8_t, projectile_active, NP); PS_BLOB_FIELD(uint8_t, projectile_type, NP);
    PS_BLOB_FIELD(float, projectile_x, NP); PS_BLOB_FIELD(float, projectile_y, NP);
    PS_BLOB_FIELD(float, projectile_vx, NP); PS_BLOB_FIELD(float, projectile_vy, NP);
    PS_BLOB_FIELD(float, projectile_damage, NP); PS_BLOB_FIELD(float, projectile_radius, NP);
    PS_BLOB_FIELD(int, projectile_ttl, NP); PS_BLOB_FIELD(int, projectile_pierce, NP);
    PS_BLOB_FIELD(int, projectile_dense, NP); PS_BLOB_FIELD(int, projectile_dense_pos, NP);
    PS_BLOB_FIELD(uint8_t, drop_active, ND); PS_BLOB_FIELD(uint8_t, drop_type, ND);
    PS_BLOB_FIELD(float, drop_x, ND); PS_BLOB_FIELD(float, drop_y, ND);
    PS_BLOB_FIELD(float, drop_value, ND);
    PS_BLOB_FIELD(int, drop_dense, ND); PS_BLOB_FIELD(int, drop_dense_pos, ND);
    PS_BLOB_FIELD(uint8_t, area_active, NA); PS_BLOB_FIELD(uint8_t, area_type, NA);
    PS_BLOB_FIELD(float, area_x, NA); PS_BLOB_FIELD(float, area_y, NA);
    PS_BLOB_FIELD(float, area_radius, NA); PS_BLOB_FIELD(float, area_damage, NA);
    PS_BLOB_FIELD(int, area_ttl, NA); PS_BLOB_FIELD(int, area_tick_rate, NA);
    PS_BLOB_FIELD(int, area_tick_timer, NA);
    PS_BLOB_FIELD(int, area_dense, NA); PS_BLOB_FIELD(int, area_dense_pos, NA);
    PS_BLOB_FIELD(uint8_t, obstacle_type, NO);
    PS_BLOB_FIELD(float, obstacle_x, NO); PS_BLOB_FIELD(float, obstacle_y, NO);
    PS_BLOB_FIELD(float, obstacle_radius, NO);
    PS_BLOB_FIELD(uint8_t, moving_obstacle_active, NMO);
    PS_BLOB_FIELD(uint8_t, moving_obstacle_type, NMO);
    PS_BLOB_FIELD(uint8_t, moving_obstacle_shape, NMO);
    PS_BLOB_FIELD(float, moving_obstacle_x, NMO); PS_BLOB_FIELD(float, moving_obstacle_y, NMO);
    PS_BLOB_FIELD(float, moving_obstacle_vx, NMO); PS_BLOB_FIELD(float, moving_obstacle_vy, NMO);
    PS_BLOB_FIELD(float, moving_obstacle_bound_radius, NMO);
    PS_BLOB_FIELD(float, moving_obstacle_half_width, NMO);
    PS_BLOB_FIELD(float, moving_obstacle_half_height, NMO);
    PS_BLOB_FIELD(int, moving_obstacle_ttl, NMO);
    PS_BLOB_FIELD(int, moving_obstacle_dense, NMO);
    PS_BLOB_FIELD(int, moving_obstacle_dense_pos, NMO);
}

static inline void ps_cuda_free(PSCudaSim* sim) {
    if (sim->owns_io) {
        cudaFree(sim->observations);
        cudaFree(sim->actions);
        cudaFree(sim->rewards);
        cudaFree(sim->terminals);
    }
    if (sim->blob) cudaFree(sim->blob);
    std::memset(sim, 0, sizeof(*sim));
}

// -----------------------------------------------------------------------------
// Kernels and host launchers
// -----------------------------------------------------------------------------

__global__ void ps_reset_all_kernel(PSCudaSim sim, uint32_t seed) {
    int env = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= sim.num_envs) return;
    uint32_t s = seed ? seed : 1u;
    uint32_t x = s ^ (0x9e3779b9u * (uint32_t)(env + 1));
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    sim.rng[env] = x ? x : 1u;
    ps_reset_core(&sim, env, 1);
}

__global__ void ps_step_kernel(PSCudaSim sim) {
    int env = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= sim.num_envs) return;
    ps_step_env(&sim, env);
}

__global__ void ps_step_range_kernel(PSCudaSim sim, int start, int count) {
    int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count) return;
    int env = start + lane;
    if (env >= sim.num_envs) return;
    ps_step_env(&sim, env);
}

static inline void ps_cuda_reset_all(PSCudaSim* sim, uint32_t seed, cudaStream_t stream = 0) {
    int blocks = (sim->num_envs + PS_CUDA_BLOCK_SIZE - 1) / PS_CUDA_BLOCK_SIZE;
    cudaMemsetAsync(sim->observations, 0,
        (size_t)sim->num_envs * PS_OBS_SIZE * sizeof(float), stream);
    ps_reset_all_kernel<<<blocks, PS_CUDA_BLOCK_SIZE, 0, stream>>>(*sim, seed);
    PS_CUDA_CHECK(cudaGetLastError());
}

static inline void ps_cuda_step_range(PSCudaSim* sim, int start, int count, cudaStream_t stream = 0) {
    int blocks = (count + PS_CUDA_BLOCK_SIZE - 1) / PS_CUDA_BLOCK_SIZE;
    cudaMemsetAsync(sim->observations + (size_t)start * PS_OBS_SIZE, 0,
        (size_t)count * PS_OBS_SIZE * sizeof(float), stream);
    ps_step_range_kernel<<<blocks, PS_CUDA_BLOCK_SIZE, 0, stream>>>(*sim, start, count);
    PS_CUDA_CHECK(cudaGetLastError());
}
