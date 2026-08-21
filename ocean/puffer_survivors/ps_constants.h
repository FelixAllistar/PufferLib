#pragma once

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define PS_MAX_ENEMIES 256
#define PS_MAX_PROJECTILES 512
#define PS_MAX_DROPS 256
#define PS_MAX_OBSTACLES 32
// Compile-time backing storage; env.area_cap may be lower, never higher.
#define PS_AREA_STORAGE_CAP 96
#define PS_MAX_MOVING_OBSTACLES 8
#define PS_GRID_W 32
#define PS_GRID_H 32
#define PS_GRID_CELLS (PS_GRID_W * PS_GRID_H)

#define PS_SECTORS 8
#define PS_RINGS 3
#define PS_MOVE_ACTION_COUNT 10
#define PS_ACTION_DASH 9
#define PS_PLAYER_FEATURES 28
#define PS_BOSS_FEATURES 7
#define PS_ENEMY_CHANNELS 4
#define PS_DROP_CHANNELS 3
#define PS_OBSTACLE_CHANNELS 2
#define PS_WEAPON_COUNT 7
#define PS_ENEMY_KIND_COUNT 4
#define PS_WAVE_TABLE_COUNT 24
#define PS_WEAPON_FEATURES (PS_WEAPON_COUNT * 4)
#define PS_UPGRADE_SLOTS 3
#define PS_UPGRADE_FEATURES 14
#define PS_OBSTACLE_FEATURES (PS_SECTORS * PS_RINGS * 2)
#define PS_MOVING_OBSTACLE_SLOTS 4
#define PS_MOVING_OBSTACLE_FEATURES 4
#define PS_MOVING_OBSTACLE_OBS_FEATURES (PS_MOVING_OBSTACLE_SLOTS * PS_MOVING_OBSTACLE_FEATURES)
#define PS_OBS_SIZE (PS_PLAYER_FEATURES \
    + PS_BOSS_FEATURES \
    + PS_SECTORS * PS_RINGS * PS_ENEMY_CHANNELS \
    + PS_SECTORS * PS_RINGS * PS_DROP_CHANNELS \
    + PS_OBSTACLE_FEATURES \
    + PS_WEAPON_FEATURES \
    + PS_UPGRADE_SLOTS * PS_UPGRADE_FEATURES \
    + PS_MOVING_OBSTACLE_OBS_FEATURES)

#define PS_SPRITE_PLAYER 0
#define PS_SPRITE_JELLY 1
#define PS_SPRITE_URCHIN 2
#define PS_SPRITE_BUBBLE 3
#define PS_SPRITE_XP 4
#define PS_SPRITE_HEALTH 5
#define PS_SPRITE_CORAL 6
#define PS_SPRITE_EEL 9
#define PS_SPRITE_ORB 10
#define PS_SPRITE_INK 11
#define PS_SPRITE_SONAR 12
#define PS_SPRITE_WHIRL 13
#define PS_SPRITE_ELITE 14
#define PS_SPRITE_BOSS 15

#define PS_ENEMY_KIND_MASK 7
#define PS_ENEMY_ELITE_FLAG 8
#define PS_ENEMY_BOSS_FLAG 16
#define PS_ENEMY_ARI_K_FLAG 32

typedef enum {
    PS_SHAPE_CIRCLE = 0,
    PS_SHAPE_AABB = 1,
} PSShape;

typedef enum {
    PS_MOVING_OBSTACLE_ANCHOR = 0,
    PS_MOVING_OBSTACLE_SUBMARINE = 1,
    PS_MOVING_OBSTACLE_TYPE_COUNT = 2,
} PSMovingObstacleType;

typedef enum {
    PS_WEAPON_BUBBLE = 0,
    PS_WEAPON_WHIRLPOOL = 1,
    PS_WEAPON_ORBIT = 2,
    PS_WEAPON_INK = 3,
    PS_WEAPON_SONAR = 4,
    PS_WEAPON_GLACIER = 5,
    PS_WEAPON_SPIKES = 6,
} PSWeaponId;

typedef enum {
    PS_UPGRADE_BUBBLE = 0,
    PS_UPGRADE_WHIRLPOOL = 1,
    PS_UPGRADE_ORBIT = 2,
    PS_UPGRADE_INK = 3,
    PS_UPGRADE_SONAR = 4,
    PS_UPGRADE_GLACIER = 5,
    PS_UPGRADE_SPIKES = 6,
    PS_UPGRADE_SPEED = 7,
    PS_UPGRADE_MAGNET = 8,
    PS_UPGRADE_HEALTH = 9,
    PS_UPGRADE_MIGHT = 10,
    PS_UPGRADE_COOLDOWN = 11,
    PS_UPGRADE_AREA = 12,
    PS_UPGRADE_PIERCE = 13,
    PS_UPGRADE_COUNT = 14,
} PSUpgradeId;

typedef struct {
    float hp;
    float speed_mult;
    float damage;
} PSEnemyDef;

typedef struct {
    float base_cd;
    float cd_per_level;
    float base_damage;
    float damage_per_level;
} PSWeaponDef;

typedef struct {
    float arena_size;
    int max_steps;
    int wave_length_steps;
    int enemy_cap;
    int projectile_cap;
    int drop_cap;
    int obstacle_count;
    int area_cap;
    // Runtime gameplay geometry. These values come from the [env] INI.
    float player_radius;
    float enemy_radius[PS_ENEMY_KIND_COUNT];
    float elite_radius;
    float boss_radius;
    float ari_k_radius;
    float ari_k_half_width;
    float ari_k_half_height;
    float obstacle_radius_min;
    float obstacle_radius_max;
    float obstacle_spawn_clearance;
    float enemy_spawn_radius;
    float enemy_spawn_padding;
    float drop_spawn_radius;
    float weapon_base_radius[PS_WEAPON_COUNT];
    float weapon_radius_per_level[PS_WEAPON_COUNT];
    float weapon_orbit_distance;
    float weapon_orbit_distance_per_level;
    float obstacle_player_spawn_clearance;
    float obstacle_spawn_min_ratio;
    float obstacle_spawn_max_ratio;
    float obstacle_fallback_min_ratio;
    float obstacle_fallback_max_ratio;
    float obstacle_fallback_angle_step;
    float obstacle_fallback_angle_jitter;
    int obstacle_fallback_spoke_count;
    float obstacle_recycle_spawn_min_ratio;
    float obstacle_recycle_spawn_max_ratio;
    float obstacle_recycle_radius_ratio;
    float enemy_spawn_edge_margin;
    float enemy_spawn_along_ratio;
    float enemy_recycle_radius_ratio;
    float spawn_velocity_bias_probability;
    float special_ring_radius_ratio;
    int special_ring_enemy_count;
    float special_ring_speed_mult;
    int special_ring_wave_count;
    int special_ring_waves[3];
    // Runtime content tables. These are also loaded from the [env] INI.
    float enemy_base_hp[PS_ENEMY_KIND_COUNT];
    float enemy_speed_mult[PS_ENEMY_KIND_COUNT];
    float enemy_base_damage[PS_ENEMY_KIND_COUNT];
    float weapon_base_cooldown[PS_WEAPON_COUNT];
    float weapon_cooldown_per_level[PS_WEAPON_COUNT];
    float weapon_base_damage[PS_WEAPON_COUNT];
    float weapon_damage_per_level[PS_WEAPON_COUNT];
    int wave_minimum[PS_WAVE_TABLE_COUNT];
    int wave_interval[PS_WAVE_TABLE_COUNT];
    int enemy_mix_start_wave;
    int enemy_mix_phase_one_end_wave;
    int enemy_mix_phase_two_end_wave;
    int enemy_mix_phase_one_jelly_pct;
    int enemy_mix_phase_two_urchin_pct;
    int enemy_mix_phase_two_jelly_pct;
    int enemy_mix_late_urchin_pct;
    int enemy_mix_late_eel_pct;
    int enemy_mix_late_jelly_pct;
    float enemy_hp_growth_per_wave;
    float enemy_hp_progress_scale;
    float enemy_speed_growth_per_wave;
    int enemy_speed_growth_wave_cap;
    float elite_hp_multiplier;
    float elite_speed_multiplier;
    float elite_damage_multiplier;
    float boss_hp_base;
    float boss_hp_per_wave;
    float boss_speed_multiplier;
    float boss_damage;
    float ari_k_hp_multiplier;
    float ari_k_speed_multiplier;
    float ari_k_damage;
    float elite_spawn_ramp_per_tick;
    int boss_period_steps;
    int ari_k_start_wave;
    int ari_k_wave_period;
    float kill_score_default;
    float kill_score_elite;
    float kill_score_boss;
    float drop_value_default;
    float drop_value_elite;
    float drop_value_boss;
    float health_drop_elite_bonus;
    float health_drop_boss_bonus;
    float health_drop_missing_hp_bonus;
    float health_drop_offset_x;
    float health_drop_offset_y;
    float enemy_spawn_rate;
    float elite_spawn_rate;
    float player_speed;
    float player_health;
    float enemy_speed;
    float enemy_hp_scale;
    float enemy_damage_scale;
    float spawn_ramp;
    float projectile_speed;
    float projectile_damage;
    float fire_cooldown;
    float pickup_radius;
    float magnet_radius;
    float health_drop_rate;
    float health_heal;
    float reward_xp;
    float reward_kill;
    float reward_damage;
    float reward_survival;
    float reward_hurt;
    float reward_death;
    float reward_success;
    float reward_pickup;
    float reward_levelup;
    float obstacle_penalty;
    float contact_damage;
    int invuln_steps;
    int enemy_obstacle_stride;
    float movement_smoothing;
    float dash_speed;
    int dash_duration;
    float dash_shrink;
    float dash_cooldown;
    float dash_cooldown_per_level;
    float pickup_magnet_speed;
    int weapon_max_level;
    float upgrade_speed_bonus;
    float upgrade_magnet_bonus;
    float upgrade_health_bonus;
    float upgrade_might_bonus;
    float upgrade_cooldown_multiplier;
    float upgrade_area_bonus;
    float xp_threshold_base;
    float xp_threshold_per_level;
    float wave_spawn_reference_rate;
    float wave_spawn_scale_min;
    float wave_spawn_scale_max;
    float wave_progress_spawn_scale;
    int wave_population_cap;
    int wave_tail_minimum_base;
    int wave_tail_minimum_step;
    int wave_tail_interval;
    int wave_min_spawn_interval;
    int progress_normal_wave_count;
    float weapon_min_cooldown;
    float bubble_target_range;
    float bubble_target_area_range;
    float bubble_shot_spread;
    int bubble_projectile_ttl;
    float whirlpool_knockback;
    int whirlpool_ttl;
    int whirlpool_tick_rate;
    float orbit_area_distance_bonus;
    float orbit_knockback;
    float ink_target_range_ratio;
    float ink_pool_spread;
    float ink_pool_angle_jitter;
    int ink_pool_ttl_base;
    int ink_pool_ttl_per_level;
    int ink_pool_tick_rate;
    float ink_trail_min_speed2;
    int ink_trail_max_base;
    int ink_trail_max_per_level;
    int ink_trail_cadence_base;
    int ink_trail_cadence_level_divisor;
    int ink_trail_cadence_min;
    float ink_trail_radius_base;
    float ink_trail_radius_per_level;
    float ink_trail_radius_config_scale;
    float ink_trail_damage_multiplier;
    int ink_trail_ttl_base;
    int ink_trail_ttl_per_level;
    float ink_trail_offset;
    int ink_trail_tick_rate;
    float area_tick_knockback;
    float weapon_active_decay;
    float orbit_phase_speed;
    float orbit_phase_per_level;
    float sonar_knockback;
    int sonar_ttl;
    int sonar_tick_rate;
    int moving_obstacle_cap;
    int moving_obstacle_start_wave;
    int moving_obstacle_spawn_interval;
    int moving_obstacle_ttl;
    float moving_obstacle_spawn_margin;
    float moving_obstacle_speed[PS_MOVING_OBSTACLE_TYPE_COUNT];
    float moving_obstacle_half_width[PS_MOVING_OBSTACLE_TYPE_COUNT];
    float moving_obstacle_half_height[PS_MOVING_OBSTACLE_TYPE_COUNT];
    float moving_obstacle_damage;
    float frost_slow;
    float frost_half_angle;
    float frost_range;
    int frost_slow_ttl;
    float spike_speed;
    float spike_range;
    int free_upgrade;
    int free_upgrade_count;
} PSConfig;

enum {
    PS_OBS_PLAYER_BASE = 0,
    PS_OBS_BOSS_BASE = PS_OBS_PLAYER_BASE + PS_PLAYER_FEATURES,
    PS_OBS_ENEMY_BASE = PS_OBS_BOSS_BASE + PS_BOSS_FEATURES,
    PS_OBS_DROP_BASE = PS_OBS_ENEMY_BASE + PS_SECTORS * PS_RINGS * PS_ENEMY_CHANNELS,
    PS_OBS_OBSTACLE_BASE = PS_OBS_DROP_BASE + PS_SECTORS * PS_RINGS * PS_DROP_CHANNELS,
    PS_OBS_WEAPON_BASE = PS_OBS_OBSTACLE_BASE + PS_OBSTACLE_FEATURES,
    PS_OBS_UPGRADE_BASE = PS_OBS_WEAPON_BASE + PS_WEAPON_FEATURES,
    PS_OBS_MOVING_OBSTACLE_BASE = PS_OBS_UPGRADE_BASE + PS_UPGRADE_SLOTS * PS_UPGRADE_FEATURES,
    PS_OBS_END = PS_OBS_MOVING_OBSTACLE_BASE + PS_MOVING_OBSTACLE_OBS_FEATURES,
};

enum {
    PS_BOSS_PRESENT = 0,
    PS_BOSS_DX = 1,
    PS_BOSS_DY = 2,
    PS_BOSS_PROXIMITY = 3,
    PS_BOSS_HP_FRACTION = 4,
    PS_BOSS_MAX_HP = 5,
    PS_BOSS_COUNT = 6,
};

#if defined(__cplusplus)
#define PS_STATIC_ASSERT static_assert
#else
#define PS_STATIC_ASSERT _Static_assert
#endif
PS_STATIC_ASSERT(PS_OBS_END == PS_OBS_SIZE, "Observation layout does not match PS_OBS_SIZE");
PS_STATIC_ASSERT(PS_OBS_SIZE == 337, "Unexpected Puffer Survivors observation size");
