#pragma once

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Fixed sim tick. box3d worlds step with this dt on the CPU path and the CUDA
// path integrates the same dt analytically (see README, "Physics backends").
#define AR_DT (1.0f / 60.0f)

// Compile-time backing storage. cfg caps may be lower, never higher.
#define AR_MAX_ENEMIES 128
#define AR_MAX_PETS 4
#define AR_MAX_OBSTACLES 16

// Whole-arena uniform grid for the analytic (GPU) separation and pet targeting.
// Cell size is arena_size / AR_GRID_W, so it adapts to config.
#define AR_GRID_W 16
#define AR_GRID_H 16
#define AR_GRID_CELLS (AR_GRID_W * AR_GRID_H)

// Actions: movement [9] {0 idle, 1 N, 2 S, 3 W, 4 E, 5 NW, 6 NE, 7 SW, 8 SE}
// and summon [2] {0 none, 1 summon pet}.
#define AR_MOVE_ACTION_COUNT 9
#define AR_SUMMON_ACTION_COUNT 2

// Observation schema (see ar_sim.h: ar_compute_observations).
#define AR_PLAYER_FEATURES 8
#define AR_PET_SLOTS AR_MAX_PETS
#define AR_PET_FEATURES 7
#define AR_ENEMY_SLOTS 8
#define AR_ENEMY_FEATURES 4
#define AR_OBS_SIZE (AR_PLAYER_FEATURES \
    + AR_PET_SLOTS * AR_PET_FEATURES \
    + AR_ENEMY_SLOTS * AR_ENEMY_FEATURES)

typedef enum {
    AR_ENEMY_GRUNT = 0,
    AR_ENEMY_BRUTE = 1,
    AR_ENEMY_KIND_COUNT = 2,
} AREnemyKind;

// The sim selects the CUDA SoA branch only when nvcc compiles the native GPU
// env (PUFFER_GPU_ENV comes from build.sh --gpu). Plain nvcc host/device
// compilations of the CPU env (native train binary, no --gpu) take the CPU
// branch with box3d, unlike puffer_survivors whose CPU branch only builds
// under a C host compiler.
#if defined(__CUDACC__) && defined(PUFFER_GPU_ENV)
#define AR_GPU_SIM 1
#endif

typedef struct ARConfig {
    float arena_size;
    int max_steps;
    int wave_length_steps;

    int enemy_cap;
    int pet_cap;
    int obstacle_count;

    // Player (summoner).
    float player_radius;
    float player_speed;
    float player_health;
    int invuln_steps;
    float summon_cooldown;

    // Pet. One pet def; more classes/pets come later.
    float pet_radius;
    float pet_speed;
    float pet_health;
    float pet_damage;
    float pet_attack_range;
    float pet_attack_cooldown;
    float pet_aggro_range;
    float pet_leash_range;
    float pet_follow_distance;
    int pet_invuln_steps;

    // Enemies. Per-kind tables indexed by AREnemyKind.
    float enemy_radius[AR_ENEMY_KIND_COUNT];
    float enemy_base_hp[AR_ENEMY_KIND_COUNT];
    float enemy_base_speed[AR_ENEMY_KIND_COUNT];
    float enemy_base_damage[AR_ENEMY_KIND_COUNT];
    float enemy_hp_growth_per_wave;
    float enemy_speed_growth_per_wave;
    int enemy_growth_wave_cap;
    int enemy_kind_switch_wave;

    // Spawning.
    float enemy_spawn_radius;
    int spawn_interval;
    int spawn_batch;
    int spawn_min_interval;
    int spawn_interval_per_wave;

    // Static pillar obstacles.
    float obstacle_radius_min;
    float obstacle_radius_max;
    float obstacle_center_clearance;

    // Rewards.
    float reward_survival;
    float reward_kill;
    float reward_damage;
    float damage_reward_scale;
    float reward_hurt;
    float reward_summon;
    float reward_pet_lose;
    float reward_death;
    float reward_success;
} ARConfig;

enum {
    AR_OBS_PLAYER_BASE = 0,
    AR_OBS_PET_BASE = AR_OBS_PLAYER_BASE + AR_PLAYER_FEATURES,
    AR_OBS_ENEMY_BASE = AR_OBS_PET_BASE + AR_PET_SLOTS * AR_PET_FEATURES,
    AR_OBS_END = AR_OBS_ENEMY_BASE + AR_ENEMY_SLOTS * AR_ENEMY_FEATURES,
};

#if defined(__cplusplus)
#define AR_STATIC_ASSERT static_assert
#else
#define AR_STATIC_ASSERT _Static_assert
#endif
AR_STATIC_ASSERT(AR_OBS_END == AR_OBS_SIZE, "Observation layout does not match AR_OBS_SIZE");
AR_STATIC_ASSERT(AR_OBS_SIZE == 68, "Unexpected arpg observation size");
