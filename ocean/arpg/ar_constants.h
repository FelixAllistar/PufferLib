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

// Actions: movement [9] screen-relative {0 idle, 1 up, 2 down, 3 left,
// 4 right, 5 up-left, 6 up-right, 7 down-left, 8 down-right},
// summon [4] {0 none, 1 wisp, 2 fang, 3 aegis},
// order [4] {0 follow, 1 attack, 2 guard, 3 focus},
// ability [4] {0 none, 1 dash, 2 nova, 3 frost},
// build [3] {0 none, 1 totem, 2 wall}.
#define AR_MOVE_ACTION_COUNT 9
#define AR_SUMMON_ACTION_COUNT 4
#define AR_ORDER_ACTION_COUNT 4
#define AR_ABILITY_ACTION_COUNT 4
#define AR_BUILD_ACTION_COUNT 3

// RTS pools.
#define AR_MAX_SHARDS 24
#define AR_MAX_BUILDINGS 8

// Buildings.
typedef enum {
    AR_BUILD_TOTEM = 0,  // static damage-aura pulse
    AR_BUILD_WALL = 1,   // static blocker enemies chew through
    AR_BUILD_KIND_COUNT = 2,
} ARBuildKind;

// Observation schema (see ar_sim.h: ar_compute_observations).
// Player: pos(2) vel(2) hp(1) summon_cd(1) pets_alive(1) enemies_alive(1)
//         order(1) dash_cd(1) nova_cd(1) frost_cd(1) shards(1) builds(1) = 14.
// Pet slot: active(1) rel_pos(2) hp(1) cd(1) attacking(1) has_target(1)
//         kind(1) = 8. Enemy slot: rel_pos(2) hp(1) kind(1) slowed(1) = 5.
#define AR_PLAYER_FEATURES 14
#define AR_PET_SLOTS AR_MAX_PETS
#define AR_PET_FEATURES 8
#define AR_ENEMY_SLOTS 8
#define AR_ENEMY_FEATURES 5
#define AR_OBS_SIZE (AR_PLAYER_FEATURES \
    + AR_PET_SLOTS * AR_PET_FEATURES \
    + AR_ENEMY_SLOTS * AR_ENEMY_FEATURES)

typedef enum {
    AR_ENEMY_GRUNT = 0,
    AR_ENEMY_BRUTE = 1,
    AR_ENEMY_KIND_COUNT = 2,
} AREnemyKind;

// Pet classes (Pikmin restraint: 3 types, distinct roles).
typedef enum {
    AR_PET_WISP = 0,   // balanced all-rounder
    AR_PET_FANG = 1,   // fast, fragile, high damage
    AR_PET_AEGIS = 2,  // slow, tanky body-blocker
    AR_PET_CLASS_COUNT = 3,
} ARPetClass;

// Squad orders (Halo Wars one-button rule: a single discrete head).
typedef enum {
    AR_ORDER_FOLLOW = 0,  // stick to the player, engage nearby
    AR_ORDER_ATTACK = 1,  // wide aggro + long leash, hunt
    AR_ORDER_GUARD = 2,   // hold position, only strike in reach
    AR_ORDER_FOCUS = 3,   // all pets converge on the player's nearest enemy
    AR_ORDER_COUNT = 4,
} AROrder;

// Avatar abilities (one discrete head, cooldown-gated).
typedef enum {
    AR_ABILITY_NONE = 0,
    AR_ABILITY_DASH = 1,  // blink through enemies, stops at rock
    AR_ABILITY_NOVA = 2,  // radial damage around the player
    AR_ABILITY_FROST = 3, // cone damage + slow in facing/move dir
    AR_ABILITY_COUNT = 4,
} ARAbility;

// Open-world terrain tiles stored in the AR_DUN grid.
typedef enum {
    AR_TILE_ROCK = 0,    // solid mountain / wall
    AR_TILE_GRASS = 1,   // walkable
    AR_TILE_FOREST = 2,  // walkable, 0.85x speed, trees
    AR_TILE_SAND = 3,    // walkable shore
    AR_TILE_SHALLOW = 4, // walkable, 0.6x speed
    AR_TILE_DEEP = 5,    // solid water
} ARTile;

// Procedural terrain grid. Cell size = arena_size / AR_DUN_W world units.
// Cells hold ARTile values (the "dungeon" is now open world).
#define AR_DUN_W 32
#define AR_DUN_H 32
#define AR_DUN_CELLS (AR_DUN_W * AR_DUN_H)

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

    // Pet. Per-class tables indexed by ARPetClass; shared behavior below.
    float pet_radius[AR_PET_CLASS_COUNT];
    float pet_health[AR_PET_CLASS_COUNT];
    float pet_speed[AR_PET_CLASS_COUNT];
    float pet_damage[AR_PET_CLASS_COUNT];
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

    // Avatar abilities.
    float dash_cooldown;
    float dash_distance;     // blink range in world units (through enemies)
    float dash_iframes;      // seconds of invulnerability on dash
    float nova_cooldown;
    float nova_radius;
    float nova_damage;
    float frost_cooldown;
    float frost_range;
    float frost_half_angle;  // radians
    float frost_damage;
    float frost_slow_mult;   // enemy speed multiplier while slowed
    float frost_slow_time;   // seconds of slow

    // RTS economy.
    float start_shards;
    float kill_shards;
    float summon_cost[AR_PET_CLASS_COUNT];
    float build_cost[AR_BUILD_KIND_COUNT];
    float build_hp[AR_BUILD_KIND_COUNT];
    float build_radius[AR_BUILD_KIND_COUNT];
    float totem_radius;
    float totem_damage;
    float totem_period;
    float shard_trickle_period;
    int shard_trickle_cap;

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
AR_STATIC_ASSERT(AR_OBS_SIZE == 86, "Unexpected arpg observation size");
