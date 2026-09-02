// arpg CUDA simulator implementation.
// -----------------------------------------------------------------------------
// This file owns the internal SoA state and kernels used by the native 5c
// puf_envs_* implementation in arpg.cu.
//
// Storage is structure-of-arrays across environments. One CUDA thread owns one
// complete environment step, giving coalesced same-field accesses across a
// warp while preserving straightforward CPU/CUDA logic comparison — the same
// strategy as puffer_survivors. box3d has no device kernels, so movement uses
// the analytic integration/separation in ar_sim.h (see README).
// -----------------------------------------------------------------------------

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../ar_constants.h"
#include "../ar_geometry.h"
#include "../ar_log.h"

#ifndef AR_CUDA_BLOCK_SIZE
#define AR_CUDA_BLOCK_SIZE 256
#endif

// Config and simulator SoA
// -----------------------------------------------------------------------------

struct ARCudaSim {
    int num_envs;
    ARConfig cfg;
    int owns_io;
    void* blob;
    Env* native_envs;

    // External tensors. Default layout:
    //   observations: [num_envs, AR_OBS_SIZE]
    //   actions:      [num_envs, 2]
    //   rewards:      [num_envs]
    //   terminals:    [num_envs]
    float* observations;
    float* actions;
    float* rewards;
    float* terminals;

    // Scalar state, SoA over envs.
    uint32_t* rng;
    float *px, *py, *pvx, *pvy, *hp, *max_hp;
    int *facing_left, *invuln_timer, *tick;
    int *enemy_count, *next_enemy_slot, *pets_alive, *spawn_timer, *nearest_enemy;

    // Episode stats.
    float *episode_return;
    float *episode_reward_survival, *episode_reward_kill, *episode_reward_damage;
    float *episode_reward_hurt, *episode_reward_summon, *episode_reward_terminal;
    float *episode_kills, *episode_summons, *episode_pets_lost;
    float *episode_damage_dealt, *episode_damage_taken;
    float *episode_peak_enemies, *episode_min_hp;

    // Pet pool [AR_MAX_PETS, N]
    uint8_t *pet_active, *pet_attacking;
    float *pet_x, *pet_y, *pet_vx, *pet_vy;
    float *pet_hp, *pet_max_hp, *pet_cd, *pet_age;
    int *pet_invuln, *pet_target;

    // Enemy pool [enemy_cap, N]. enemy_next doubles as the separation grid
    // linked list during ar_gpu_move_authority.
    uint8_t *enemy_active, *enemy_type;
    float *enemy_x, *enemy_y, *enemy_vx, *enemy_vy;
    float *enemy_hp, *enemy_max_hp, *enemy_radius, *enemy_speed, *enemy_damage;
    int *enemy_next, *enemy_dense, *enemy_dense_pos;

    // Obstacles [AR_MAX_OBSTACLES, N]
    uint8_t* obstacle_active;
    float *obstacle_x, *obstacle_y, *obstacle_radius;

    // Separation grid [AR_GRID_CELLS, N]
    int* grid_head;
};

#include "../ar_sim.h"

// -----------------------------------------------------------------------------
// Host utilities
// -----------------------------------------------------------------------------

static inline void ar_cuda_check(cudaError_t err, const char* expr,
        const char* file, int line) {
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA error %s at %s:%d: %s\n", expr, file, line,
            cudaGetErrorString(err));
        std::abort();
    }
}
#define AR_CUDA_CHECK(expr) ar_cuda_check((expr), #expr, __FILE__, __LINE__)

#define AR_BLOB_ACCOUNT(type, count) do { \
    state_bytes = (state_bytes + sizeof(type) - 1) & ~(sizeof(type) - 1); \
    state_bytes += sizeof(type) * (size_t)(count); \
} while (0)
#define AR_BLOB_FIELD(type, field, count) do { \
    state_offset = (state_offset + sizeof(type) - 1) & ~(sizeof(type) - 1); \
    (sim)->field = (type*)((char*)(sim)->blob + state_offset); \
    state_offset += sizeof(type) * (size_t)(count); \
} while (0)

static inline void ar_cuda_alloc(ARCudaSim* sim, int num_envs, ARConfig cfg) {
    std::memset(sim, 0, sizeof(*sim));
    sim->num_envs = num_envs;
    sim->cfg = cfg;
    sim->owns_io = 1;

    const size_t N = (size_t)num_envs;
    const size_t NE = (size_t)cfg.enemy_cap * N;
    const size_t NP = (size_t)AR_MAX_PETS * N;
    const size_t NO = (size_t)AR_MAX_OBSTACLES * N;
    const size_t NG = (size_t)AR_GRID_CELLS * N;

    AR_CUDA_CHECK(cudaMalloc((void**)&sim->observations,
        sizeof(float) * N * AR_OBS_SIZE));
    AR_CUDA_CHECK(cudaMalloc((void**)&sim->actions, sizeof(float) * N * NUM_ATNS));
    AR_CUDA_CHECK(cudaMalloc((void**)&sim->rewards, sizeof(float) * N));
    AR_CUDA_CHECK(cudaMalloc((void**)&sim->terminals, sizeof(float) * N));

    size_t state_bytes = 0;
    AR_BLOB_ACCOUNT(uint32_t, N);            // rng
    AR_BLOB_ACCOUNT(float, N * 6);           // px py pvx pvy hp max_hp
    AR_BLOB_ACCOUNT(int, N * 8);             // facing invuln tick counts cursors
    AR_BLOB_ACCOUNT(float, N * 14);          // episode stats
    // Pet pool
    AR_BLOB_ACCOUNT(uint8_t, NP * 2);
    AR_BLOB_ACCOUNT(float, NP * 8);
    AR_BLOB_ACCOUNT(int, NP * 2);
    // Enemy pool
    AR_BLOB_ACCOUNT(uint8_t, NE * 2);
    AR_BLOB_ACCOUNT(float, NE * 9);
    AR_BLOB_ACCOUNT(int, NE * 3);
    // Obstacles
    AR_BLOB_ACCOUNT(uint8_t, NO);
    AR_BLOB_ACCOUNT(float, NO * 3);
    // Grid
    AR_BLOB_ACCOUNT(int, NG);

    AR_CUDA_CHECK(cudaMalloc(&sim->blob, state_bytes));
    size_t state_offset = 0;
    AR_BLOB_FIELD(uint32_t, rng, N);
    AR_BLOB_FIELD(float, px, N); AR_BLOB_FIELD(float, py, N);
    AR_BLOB_FIELD(float, pvx, N); AR_BLOB_FIELD(float, pvy, N);
    AR_BLOB_FIELD(float, hp, N); AR_BLOB_FIELD(float, max_hp, N);
    AR_BLOB_FIELD(int, facing_left, N); AR_BLOB_FIELD(int, invuln_timer, N);
    AR_BLOB_FIELD(int, tick, N);
    AR_BLOB_FIELD(int, enemy_count, N); AR_BLOB_FIELD(int, next_enemy_slot, N);
    AR_BLOB_FIELD(int, pets_alive, N); AR_BLOB_FIELD(int, spawn_timer, N);
    AR_BLOB_FIELD(int, nearest_enemy, N);
    AR_BLOB_FIELD(float, episode_return, N);
    AR_BLOB_FIELD(float, episode_reward_survival, N);
    AR_BLOB_FIELD(float, episode_reward_kill, N);
    AR_BLOB_FIELD(float, episode_reward_damage, N);
    AR_BLOB_FIELD(float, episode_reward_hurt, N);
    AR_BLOB_FIELD(float, episode_reward_summon, N);
    AR_BLOB_FIELD(float, episode_reward_terminal, N);
    AR_BLOB_FIELD(float, episode_kills, N);
    AR_BLOB_FIELD(float, episode_summons, N);
    AR_BLOB_FIELD(float, episode_pets_lost, N);
    AR_BLOB_FIELD(float, episode_damage_dealt, N);
    AR_BLOB_FIELD(float, episode_damage_taken, N);
    AR_BLOB_FIELD(float, episode_peak_enemies, N);
    AR_BLOB_FIELD(float, episode_min_hp, N);
    AR_BLOB_FIELD(uint8_t, pet_active, NP); AR_BLOB_FIELD(uint8_t, pet_attacking, NP);
    AR_BLOB_FIELD(float, pet_x, NP); AR_BLOB_FIELD(float, pet_y, NP);
    AR_BLOB_FIELD(float, pet_vx, NP); AR_BLOB_FIELD(float, pet_vy, NP);
    AR_BLOB_FIELD(float, pet_hp, NP); AR_BLOB_FIELD(float, pet_max_hp, NP);
    AR_BLOB_FIELD(float, pet_cd, NP); AR_BLOB_FIELD(float, pet_age, NP);
    AR_BLOB_FIELD(int, pet_invuln, NP); AR_BLOB_FIELD(int, pet_target, NP);
    AR_BLOB_FIELD(uint8_t, enemy_active, NE); AR_BLOB_FIELD(uint8_t, enemy_type, NE);
    AR_BLOB_FIELD(float, enemy_x, NE); AR_BLOB_FIELD(float, enemy_y, NE);
    AR_BLOB_FIELD(float, enemy_vx, NE); AR_BLOB_FIELD(float, enemy_vy, NE);
    AR_BLOB_FIELD(float, enemy_hp, NE); AR_BLOB_FIELD(float, enemy_max_hp, NE);
    AR_BLOB_FIELD(float, enemy_radius, NE); AR_BLOB_FIELD(float, enemy_speed, NE);
    AR_BLOB_FIELD(float, enemy_damage, NE);
    AR_BLOB_FIELD(int, enemy_next, NE); AR_BLOB_FIELD(int, enemy_dense, NE);
    AR_BLOB_FIELD(int, enemy_dense_pos, NE);
    AR_BLOB_FIELD(uint8_t, obstacle_active, NO);
    AR_BLOB_FIELD(float, obstacle_x, NO); AR_BLOB_FIELD(float, obstacle_y, NO);
    AR_BLOB_FIELD(float, obstacle_radius, NO);
    AR_BLOB_FIELD(int, grid_head, NG);
}

static inline void ar_cuda_free(ARCudaSim* sim) {
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

__global__ void ar_reset_all_kernel(ARCudaSim sim, uint32_t seed) {
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
    ar_reset_env(&sim, env);
}

__global__ void ar_step_range_kernel(ARCudaSim sim, int start, int count) {
    int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count) return;
    int env = start + lane;
    if (env >= sim.num_envs) return;
    ar_step_env(&sim, env);
}

static inline void ar_cuda_reset_all(ARCudaSim* sim, uint32_t seed,
        cudaStream_t stream = 0) {
    int blocks = (sim->num_envs + AR_CUDA_BLOCK_SIZE - 1) / AR_CUDA_BLOCK_SIZE;
    ar_reset_all_kernel<<<blocks, AR_CUDA_BLOCK_SIZE, 0, stream>>>(*sim, seed);
    AR_CUDA_CHECK(cudaGetLastError());
}

static inline void ar_cuda_step_range(ARCudaSim* sim, int start, int count,
        cudaStream_t stream = 0) {
    int blocks = (count + AR_CUDA_BLOCK_SIZE - 1) / AR_CUDA_BLOCK_SIZE;
    ar_step_range_kernel<<<blocks, AR_CUDA_BLOCK_SIZE, 0, stream>>>(*sim, start,
        count);
    AR_CUDA_CHECK(cudaGetLastError());
}
