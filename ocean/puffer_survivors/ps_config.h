#pragma once

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "ps_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    int enemy_shape[PS_ENEMY_KIND_COUNT];
    float enemy_half_width[PS_ENEMY_KIND_COUNT];
    float enemy_half_height[PS_ENEMY_KIND_COUNT];
    float elite_radius_bonus;
    float elite_min_radius;
    float boss_radius;
    float ari_k_radius;
    int ari_k_shape;
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
    int observation_version;
    int free_upgrade;
    int free_upgrade_count;
} PSConfig;

static inline void ps_config_error(const char* field) {
    fprintf(stderr, "invalid puffer_survivors config value: %s\n", field);
    abort();
}

static inline void ps_config_validate(const PSConfig* cfg) {
#define PS_REQUIRE_FINITE(value, field) do { \
        if (!isfinite((double)(value))) ps_config_error(field); \
    } while (0)
#define PS_REQUIRE_POSITIVE(value, field) do { \
        PS_REQUIRE_FINITE(value, field); \
        if ((value) <= 0.0f) ps_config_error(field); \
    } while (0)
#define PS_REQUIRE_NONNEGATIVE(value, field) do { \
        PS_REQUIRE_FINITE(value, field); \
        if ((value) < 0.0f) ps_config_error(field); \
    } while (0)
#define PS_REQUIRE_INT_RANGE(value, lo, hi, field) do { \
        if ((value) < (lo) || (value) > (hi)) ps_config_error(field); \
    } while (0)
#define PS_REQUIRE_UNIT(value, field) do { \
        PS_REQUIRE_FINITE(value, field); \
        if ((value) < 0.0f || (value) > 1.0f) ps_config_error(field); \
    } while (0)

    PS_REQUIRE_POSITIVE(cfg->arena_size, "arena_size");
    PS_REQUIRE_INT_RANGE(cfg->max_steps, 1, INT_MAX, "max_steps");
    PS_REQUIRE_INT_RANGE(cfg->wave_length_steps, 1, INT_MAX, "wave_length_steps");
    PS_REQUIRE_INT_RANGE(cfg->enemy_cap, 1, PS_MAX_ENEMIES, "enemy_cap");
    PS_REQUIRE_INT_RANGE(cfg->projectile_cap, 1, PS_MAX_PROJECTILES, "projectile_cap");
    PS_REQUIRE_INT_RANGE(cfg->drop_cap, 1, PS_MAX_DROPS, "drop_cap");
    PS_REQUIRE_INT_RANGE(cfg->obstacle_count, 0, PS_MAX_OBSTACLES, "obstacle_count");
    PS_REQUIRE_INT_RANGE(cfg->area_cap, 1, PS_MAX_AREAS, "area_cap");

    PS_REQUIRE_POSITIVE(cfg->player_radius, "player_radius");
    for (int i = 0; i < PS_ENEMY_KIND_COUNT; i++) {
        PS_REQUIRE_POSITIVE(cfg->enemy_radius[i], "enemy_radius");
        PS_REQUIRE_INT_RANGE(cfg->enemy_shape[i], PS_SHAPE_CIRCLE, PS_SHAPE_AABB, "enemy_shape");
        PS_REQUIRE_POSITIVE(cfg->enemy_half_width[i], "enemy_half_width");
        PS_REQUIRE_POSITIVE(cfg->enemy_half_height[i], "enemy_half_height");
        PS_REQUIRE_POSITIVE(cfg->enemy_base_hp[i], "enemy_base_hp");
        PS_REQUIRE_POSITIVE(cfg->enemy_speed_mult[i], "enemy_speed_mult");
        PS_REQUIRE_POSITIVE(cfg->enemy_base_damage[i], "enemy_base_damage");
    }
    PS_REQUIRE_NONNEGATIVE(cfg->elite_radius_bonus, "elite_radius_bonus");
    PS_REQUIRE_POSITIVE(cfg->elite_min_radius, "elite_min_radius");
    PS_REQUIRE_POSITIVE(cfg->boss_radius, "boss_radius");
    PS_REQUIRE_POSITIVE(cfg->ari_k_radius, "ari_k_radius");
    PS_REQUIRE_INT_RANGE(cfg->ari_k_shape, PS_SHAPE_CIRCLE, PS_SHAPE_AABB, "ari_k_shape");
    PS_REQUIRE_POSITIVE(cfg->ari_k_half_width, "ari_k_half_width");
    PS_REQUIRE_POSITIVE(cfg->ari_k_half_height, "ari_k_half_height");
    PS_REQUIRE_POSITIVE(cfg->obstacle_radius_min, "obstacle_radius_min");
    PS_REQUIRE_POSITIVE(cfg->obstacle_radius_max, "obstacle_radius_max");
    if (cfg->obstacle_radius_max < cfg->obstacle_radius_min)
        ps_config_error("obstacle_radius_max");
    PS_REQUIRE_NONNEGATIVE(cfg->obstacle_spawn_clearance, "obstacle_spawn_clearance");
    PS_REQUIRE_POSITIVE(cfg->enemy_spawn_radius, "enemy_spawn_radius");
    PS_REQUIRE_NONNEGATIVE(cfg->enemy_spawn_padding, "enemy_spawn_padding");
    PS_REQUIRE_POSITIVE(cfg->drop_spawn_radius, "drop_spawn_radius");
    PS_REQUIRE_POSITIVE(cfg->weapon_orbit_distance, "weapon_orbit_distance");
    PS_REQUIRE_NONNEGATIVE(cfg->weapon_orbit_distance_per_level, "weapon_orbit_distance_per_level");

    PS_REQUIRE_POSITIVE(cfg->obstacle_spawn_min_ratio, "obstacle_spawn_min_ratio");
    PS_REQUIRE_POSITIVE(cfg->obstacle_spawn_max_ratio, "obstacle_spawn_max_ratio");
    if (cfg->obstacle_spawn_max_ratio < cfg->obstacle_spawn_min_ratio)
        ps_config_error("obstacle_spawn_max_ratio");
    PS_REQUIRE_POSITIVE(cfg->obstacle_fallback_min_ratio, "obstacle_fallback_min_ratio");
    PS_REQUIRE_POSITIVE(cfg->obstacle_fallback_max_ratio, "obstacle_fallback_max_ratio");
    if (cfg->obstacle_fallback_max_ratio < cfg->obstacle_fallback_min_ratio)
        ps_config_error("obstacle_fallback_max_ratio");
    PS_REQUIRE_POSITIVE(cfg->obstacle_fallback_angle_step, "obstacle_fallback_angle_step");
    PS_REQUIRE_NONNEGATIVE(cfg->obstacle_fallback_angle_jitter, "obstacle_fallback_angle_jitter");
    PS_REQUIRE_INT_RANGE(cfg->obstacle_fallback_spoke_count, 2, INT_MAX, "obstacle_fallback_spoke_count");
    PS_REQUIRE_POSITIVE(cfg->obstacle_recycle_spawn_min_ratio, "obstacle_recycle_spawn_min_ratio");
    PS_REQUIRE_POSITIVE(cfg->obstacle_recycle_spawn_max_ratio, "obstacle_recycle_spawn_max_ratio");
    if (cfg->obstacle_recycle_spawn_max_ratio < cfg->obstacle_recycle_spawn_min_ratio)
        ps_config_error("obstacle_recycle_spawn_max_ratio");
    PS_REQUIRE_POSITIVE(cfg->obstacle_recycle_radius_ratio, "obstacle_recycle_radius_ratio");
    PS_REQUIRE_NONNEGATIVE(cfg->enemy_spawn_edge_margin, "enemy_spawn_edge_margin");
    PS_REQUIRE_UNIT(cfg->enemy_spawn_along_ratio, "enemy_spawn_along_ratio");
    PS_REQUIRE_POSITIVE(cfg->enemy_recycle_radius_ratio, "enemy_recycle_radius_ratio");
    PS_REQUIRE_UNIT(cfg->spawn_velocity_bias_probability, "spawn_velocity_bias_probability");
    PS_REQUIRE_POSITIVE(cfg->special_ring_radius_ratio, "special_ring_radius_ratio");
    PS_REQUIRE_INT_RANGE(cfg->special_ring_enemy_count, 1, PS_MAX_ENEMIES, "special_ring_enemy_count");
    PS_REQUIRE_POSITIVE(cfg->special_ring_speed_mult, "special_ring_speed_mult");
    PS_REQUIRE_INT_RANGE(cfg->special_ring_wave_count, 0, 3, "special_ring_wave_count");
    for (int i = 0; i < 3; i++) {
        if (cfg->special_ring_waves[i] < 0) ps_config_error("special_ring_waves");
    }

    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        PS_REQUIRE_POSITIVE(cfg->weapon_base_radius[i], "weapon_radius");
        PS_REQUIRE_NONNEGATIVE(cfg->weapon_radius_per_level[i], "weapon_radius_per_level");
        PS_REQUIRE_POSITIVE(cfg->weapon_base_cooldown[i], "weapon_base_cooldown");
        PS_REQUIRE_FINITE(cfg->weapon_cooldown_per_level[i], "weapon_cooldown_per_level");
        PS_REQUIRE_POSITIVE(cfg->weapon_base_damage[i], "weapon_base_damage");
        PS_REQUIRE_NONNEGATIVE(cfg->weapon_damage_per_level[i], "weapon_damage_per_level");
    }
    for (int i = 0; i < PS_WAVE_TABLE_COUNT; i++) {
        if (cfg->wave_minimum[i] <= 0) ps_config_error("wave_minimum");
        if (cfg->wave_interval[i] <= 0) ps_config_error("wave_interval");
    }

    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_start_wave, 0, INT_MAX, "enemy_mix_start_wave");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_phase_one_end_wave, 0, INT_MAX, "enemy_mix_phase_one_end_wave");
    if (cfg->enemy_mix_phase_one_end_wave <= cfg->enemy_mix_start_wave)
        ps_config_error("enemy_mix_phase_one_end_wave");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_phase_two_end_wave, 0, INT_MAX, "enemy_mix_phase_two_end_wave");
    if (cfg->enemy_mix_phase_two_end_wave <= cfg->enemy_mix_phase_one_end_wave)
        ps_config_error("enemy_mix_phase_two_end_wave");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_phase_one_jelly_pct, 0, 100, "enemy_mix_phase_one_jelly_pct");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_phase_two_urchin_pct, 0, 100, "enemy_mix_phase_two_urchin_pct");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_phase_two_jelly_pct, cfg->enemy_mix_phase_two_urchin_pct, 100, "enemy_mix_phase_two_jelly_pct");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_late_urchin_pct, 0, 100, "enemy_mix_late_urchin_pct");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_late_eel_pct, cfg->enemy_mix_late_urchin_pct, 100, "enemy_mix_late_eel_pct");
    PS_REQUIRE_INT_RANGE(cfg->enemy_mix_late_jelly_pct, cfg->enemy_mix_late_eel_pct, 100, "enemy_mix_late_jelly_pct");
    PS_REQUIRE_POSITIVE(cfg->enemy_hp_growth_per_wave, "enemy_hp_growth_per_wave");
    PS_REQUIRE_NONNEGATIVE(cfg->enemy_hp_progress_scale, "enemy_hp_progress_scale");
    PS_REQUIRE_POSITIVE(cfg->enemy_speed_growth_per_wave, "enemy_speed_growth_per_wave");
    PS_REQUIRE_INT_RANGE(cfg->enemy_speed_growth_wave_cap, 0, INT_MAX, "enemy_speed_growth_wave_cap");
    PS_REQUIRE_POSITIVE(cfg->elite_hp_multiplier, "elite_hp_multiplier");
    PS_REQUIRE_POSITIVE(cfg->elite_speed_multiplier, "elite_speed_multiplier");
    PS_REQUIRE_POSITIVE(cfg->elite_damage_multiplier, "elite_damage_multiplier");
    PS_REQUIRE_POSITIVE(cfg->boss_hp_base, "boss_hp_base");
    PS_REQUIRE_NONNEGATIVE(cfg->boss_hp_per_wave, "boss_hp_per_wave");
    PS_REQUIRE_POSITIVE(cfg->boss_speed_multiplier, "boss_speed_multiplier");
    PS_REQUIRE_POSITIVE(cfg->boss_damage, "boss_damage");
    PS_REQUIRE_POSITIVE(cfg->ari_k_hp_multiplier, "ari_k_hp_multiplier");
    PS_REQUIRE_POSITIVE(cfg->ari_k_speed_multiplier, "ari_k_speed_multiplier");
    PS_REQUIRE_POSITIVE(cfg->ari_k_damage, "ari_k_damage");
    PS_REQUIRE_NONNEGATIVE(cfg->elite_spawn_ramp_per_tick, "elite_spawn_ramp_per_tick");
    PS_REQUIRE_INT_RANGE(cfg->boss_period_steps, 1, INT_MAX, "boss_period_steps");
    PS_REQUIRE_INT_RANGE(cfg->ari_k_start_wave, 0, INT_MAX, "ari_k_start_wave");
    PS_REQUIRE_INT_RANGE(cfg->ari_k_wave_period, 1, INT_MAX, "ari_k_wave_period");
    PS_REQUIRE_FINITE(cfg->kill_score_default, "kill_score_default");
    PS_REQUIRE_FINITE(cfg->kill_score_elite, "kill_score_elite");
    PS_REQUIRE_FINITE(cfg->kill_score_boss, "kill_score_boss");
    PS_REQUIRE_POSITIVE(cfg->drop_value_default, "drop_value_default");
    PS_REQUIRE_POSITIVE(cfg->drop_value_elite, "drop_value_elite");
    PS_REQUIRE_POSITIVE(cfg->drop_value_boss, "drop_value_boss");
    PS_REQUIRE_NONNEGATIVE(cfg->health_drop_elite_bonus, "health_drop_elite_bonus");
    PS_REQUIRE_NONNEGATIVE(cfg->health_drop_boss_bonus, "health_drop_boss_bonus");
    PS_REQUIRE_NONNEGATIVE(cfg->health_drop_missing_hp_bonus, "health_drop_missing_hp_bonus");
    PS_REQUIRE_FINITE(cfg->health_drop_offset_x, "health_drop_offset_x");
    PS_REQUIRE_FINITE(cfg->health_drop_offset_y, "health_drop_offset_y");

    PS_REQUIRE_POSITIVE(cfg->enemy_spawn_rate, "enemy_spawn_rate");
    PS_REQUIRE_NONNEGATIVE(cfg->elite_spawn_rate, "elite_spawn_rate");
    PS_REQUIRE_POSITIVE(cfg->player_speed, "player_speed");
    PS_REQUIRE_POSITIVE(cfg->player_health, "player_health");
    if (cfg->player_health < 1.0f) ps_config_error("player_health");
    PS_REQUIRE_POSITIVE(cfg->enemy_speed, "enemy_speed");
    PS_REQUIRE_POSITIVE(cfg->enemy_hp_scale, "enemy_hp_scale");
    PS_REQUIRE_POSITIVE(cfg->enemy_damage_scale, "enemy_damage_scale");
    PS_REQUIRE_NONNEGATIVE(cfg->spawn_ramp, "spawn_ramp");
    PS_REQUIRE_POSITIVE(cfg->projectile_speed, "projectile_speed");
    PS_REQUIRE_POSITIVE(cfg->projectile_damage, "projectile_damage");
    PS_REQUIRE_POSITIVE(cfg->fire_cooldown, "fire_cooldown");
    PS_REQUIRE_POSITIVE(cfg->pickup_radius, "pickup_radius");
    PS_REQUIRE_POSITIVE(cfg->magnet_radius, "magnet_radius");
    PS_REQUIRE_UNIT(cfg->health_drop_rate, "health_drop_rate");
    PS_REQUIRE_POSITIVE(cfg->health_heal, "health_heal");
    PS_REQUIRE_FINITE(cfg->reward_xp, "reward_xp");
    PS_REQUIRE_FINITE(cfg->reward_kill, "reward_kill");
    PS_REQUIRE_FINITE(cfg->reward_damage, "reward_damage");
    PS_REQUIRE_FINITE(cfg->reward_survival, "reward_survival");
    PS_REQUIRE_FINITE(cfg->reward_hurt, "reward_hurt");
    PS_REQUIRE_FINITE(cfg->reward_death, "reward_death");
    PS_REQUIRE_FINITE(cfg->reward_success, "reward_success");
    PS_REQUIRE_FINITE(cfg->reward_pickup, "reward_pickup");
    PS_REQUIRE_FINITE(cfg->reward_levelup, "reward_levelup");
    PS_REQUIRE_FINITE(cfg->obstacle_penalty, "obstacle_penalty");
    PS_REQUIRE_NONNEGATIVE(cfg->contact_damage, "contact_damage");
    PS_REQUIRE_INT_RANGE(cfg->invuln_steps, 0, INT_MAX, "invuln_steps");
    PS_REQUIRE_INT_RANGE(cfg->enemy_obstacle_stride, 1, INT_MAX, "enemy_obstacle_stride");
    PS_REQUIRE_UNIT(cfg->movement_smoothing, "movement_smoothing");
    PS_REQUIRE_NONNEGATIVE(cfg->pickup_magnet_speed, "pickup_magnet_speed");
    PS_REQUIRE_INT_RANGE(cfg->weapon_max_level, 1, INT_MAX, "weapon_max_level");
    PS_REQUIRE_NONNEGATIVE(cfg->upgrade_speed_bonus, "upgrade_speed_bonus");
    PS_REQUIRE_NONNEGATIVE(cfg->upgrade_magnet_bonus, "upgrade_magnet_bonus");
    PS_REQUIRE_NONNEGATIVE(cfg->upgrade_health_bonus, "upgrade_health_bonus");
    PS_REQUIRE_NONNEGATIVE(cfg->upgrade_might_bonus, "upgrade_might_bonus");
    PS_REQUIRE_POSITIVE(cfg->upgrade_cooldown_multiplier, "upgrade_cooldown_multiplier");
    PS_REQUIRE_NONNEGATIVE(cfg->upgrade_area_bonus, "upgrade_area_bonus");
    PS_REQUIRE_POSITIVE(cfg->xp_threshold_base, "xp_threshold_base");
    PS_REQUIRE_POSITIVE(cfg->xp_threshold_per_level, "xp_threshold_per_level");
    PS_REQUIRE_POSITIVE(cfg->wave_spawn_reference_rate, "wave_spawn_reference_rate");
    PS_REQUIRE_POSITIVE(cfg->wave_spawn_scale_min, "wave_spawn_scale_min");
    PS_REQUIRE_POSITIVE(cfg->wave_spawn_scale_max, "wave_spawn_scale_max");
    if (cfg->wave_spawn_scale_max < cfg->wave_spawn_scale_min)
        ps_config_error("wave_spawn_scale_max");
    PS_REQUIRE_NONNEGATIVE(cfg->wave_progress_spawn_scale, "wave_progress_spawn_scale");
    PS_REQUIRE_INT_RANGE(cfg->wave_population_cap, 1, cfg->enemy_cap, "wave_population_cap");
    PS_REQUIRE_INT_RANGE(cfg->wave_tail_minimum_base, 1, INT_MAX, "wave_tail_minimum_base");
    PS_REQUIRE_INT_RANGE(cfg->wave_tail_minimum_step, 0, INT_MAX, "wave_tail_minimum_step");
    PS_REQUIRE_INT_RANGE(cfg->wave_tail_interval, 1, INT_MAX, "wave_tail_interval");
    PS_REQUIRE_INT_RANGE(cfg->wave_min_spawn_interval, 1, INT_MAX, "wave_min_spawn_interval");
    PS_REQUIRE_INT_RANGE(cfg->progress_normal_wave_count, 1, INT_MAX, "progress_normal_wave_count");
    PS_REQUIRE_POSITIVE(cfg->weapon_min_cooldown, "weapon_min_cooldown");
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        float final_cd = cfg->weapon_base_cooldown[i]
            + cfg->weapon_cooldown_per_level[i] * (float)(cfg->weapon_max_level - 1);
        if (!isfinite((double)final_cd) || final_cd < cfg->weapon_min_cooldown)
            ps_config_error("weapon_cooldown_per_level");
    }
    PS_REQUIRE_POSITIVE(cfg->bubble_target_range, "bubble_target_range");
    PS_REQUIRE_NONNEGATIVE(cfg->bubble_target_area_range, "bubble_target_area_range");
    PS_REQUIRE_NONNEGATIVE(cfg->bubble_shot_spread, "bubble_shot_spread");
    PS_REQUIRE_INT_RANGE(cfg->bubble_projectile_ttl, 1, INT_MAX, "bubble_projectile_ttl");
    PS_REQUIRE_NONNEGATIVE(cfg->whirlpool_knockback, "whirlpool_knockback");
    PS_REQUIRE_INT_RANGE(cfg->whirlpool_ttl, 1, INT_MAX, "whirlpool_ttl");
    PS_REQUIRE_INT_RANGE(cfg->whirlpool_tick_rate, 1, INT_MAX, "whirlpool_tick_rate");
    PS_REQUIRE_NONNEGATIVE(cfg->orbit_area_distance_bonus, "orbit_area_distance_bonus");
    PS_REQUIRE_NONNEGATIVE(cfg->orbit_knockback, "orbit_knockback");
    PS_REQUIRE_POSITIVE(cfg->ink_target_range_ratio, "ink_target_range_ratio");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_pool_spread, "ink_pool_spread");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_pool_angle_jitter, "ink_pool_angle_jitter");
    PS_REQUIRE_INT_RANGE(cfg->ink_pool_ttl_base, 1, INT_MAX, "ink_pool_ttl_base");
    PS_REQUIRE_INT_RANGE(cfg->ink_pool_ttl_per_level, 0, INT_MAX, "ink_pool_ttl_per_level");
    PS_REQUIRE_INT_RANGE(cfg->ink_pool_tick_rate, 1, INT_MAX, "ink_pool_tick_rate");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_trail_min_speed2, "ink_trail_min_speed2");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_max_base, 0, PS_MAX_AREAS, "ink_trail_max_base");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_max_per_level, 0, PS_MAX_AREAS, "ink_trail_max_per_level");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_cadence_base, 1, INT_MAX, "ink_trail_cadence_base");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_cadence_level_divisor, 1, INT_MAX, "ink_trail_cadence_level_divisor");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_cadence_min, 1, INT_MAX, "ink_trail_cadence_min");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_trail_radius_base, "ink_trail_radius_base");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_trail_radius_per_level, "ink_trail_radius_per_level");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_trail_radius_config_scale, "ink_trail_radius_config_scale");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_trail_damage_multiplier, "ink_trail_damage_multiplier");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_ttl_base, 1, INT_MAX, "ink_trail_ttl_base");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_ttl_per_level, 0, INT_MAX, "ink_trail_ttl_per_level");
    PS_REQUIRE_NONNEGATIVE(cfg->ink_trail_offset, "ink_trail_offset");
    PS_REQUIRE_INT_RANGE(cfg->ink_trail_tick_rate, 1, INT_MAX, "ink_trail_tick_rate");
    PS_REQUIRE_NONNEGATIVE(cfg->area_tick_knockback, "area_tick_knockback");
    PS_REQUIRE_UNIT(cfg->weapon_active_decay, "weapon_active_decay");
    PS_REQUIRE_POSITIVE(cfg->orbit_phase_speed, "orbit_phase_speed");
    PS_REQUIRE_NONNEGATIVE(cfg->orbit_phase_per_level, "orbit_phase_per_level");
    PS_REQUIRE_NONNEGATIVE(cfg->sonar_knockback, "sonar_knockback");
    PS_REQUIRE_INT_RANGE(cfg->sonar_ttl, 1, INT_MAX, "sonar_ttl");
    PS_REQUIRE_INT_RANGE(cfg->sonar_tick_rate, 1, INT_MAX, "sonar_tick_rate");
    PS_REQUIRE_INT_RANGE(cfg->moving_obstacle_cap, 0, PS_MAX_MOVING_OBSTACLES, "moving_obstacle_cap");
    PS_REQUIRE_INT_RANGE(cfg->moving_obstacle_start_wave, 0, INT_MAX, "moving_obstacle_start_wave");
    PS_REQUIRE_INT_RANGE(cfg->moving_obstacle_spawn_interval, 1, INT_MAX, "moving_obstacle_spawn_interval");
    PS_REQUIRE_INT_RANGE(cfg->moving_obstacle_ttl, 1, INT_MAX, "moving_obstacle_ttl");
    PS_REQUIRE_NONNEGATIVE(cfg->moving_obstacle_spawn_margin, "moving_obstacle_spawn_margin");
    for (int i = 0; i < PS_MOVING_OBSTACLE_TYPE_COUNT; i++) {
        PS_REQUIRE_POSITIVE(cfg->moving_obstacle_speed[i], "moving_obstacle_speed");
        PS_REQUIRE_POSITIVE(cfg->moving_obstacle_half_width[i], "moving_obstacle_half_width");
        PS_REQUIRE_POSITIVE(cfg->moving_obstacle_half_height[i], "moving_obstacle_half_height");
    }
    PS_REQUIRE_NONNEGATIVE(cfg->moving_obstacle_damage, "moving_obstacle_damage");
    long long max_ink_areas = (long long)cfg->ink_trail_max_base
        + (long long)cfg->weapon_max_level * cfg->ink_trail_max_per_level;
    if (max_ink_areas > cfg->area_cap)
        ps_config_error("ink_trail_max_per_level");
    PS_REQUIRE_INT_RANGE(cfg->observation_version, 6, 10, "observation_version");
    PS_REQUIRE_INT_RANGE(cfg->free_upgrade, -1, PS_UPGRADE_COUNT - 1, "free_upgrade");
    PS_REQUIRE_INT_RANGE(cfg->free_upgrade_count, 0, INT_MAX, "free_upgrade_count");
    if (cfg->free_upgrade < 0 && cfg->free_upgrade_count != 0)
        ps_config_error("free_upgrade_count");

#undef PS_REQUIRE_FINITE
#undef PS_REQUIRE_POSITIVE
#undef PS_REQUIRE_NONNEGATIVE
#undef PS_REQUIRE_INT_RANGE
#undef PS_REQUIRE_UNIT
}

#ifdef __cplusplus
}
#endif
