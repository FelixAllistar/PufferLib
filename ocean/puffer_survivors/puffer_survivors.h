#pragma once

#include <limits.h>

#include "pufferenv.h"
#include "ps_constants.h"
#include "ps_log.h"
#ifndef PUFFER_GPU_ENV
#include "ps_state.h"
#include "ps_sim.h"
#ifndef PS_HEADLESS_BINDING
#include "ps_render.h"
#endif
#else
// The GPU implementation keeps simulation state in a native SoA allocation.
// This compact per-environment array exists for 5c's generic device log reducer.
struct Env {
    Log log;
};
#endif

#define OBS_SIZE PS_OBS_SIZE
#define NUM_ATNS 2
#define ACT_SIZES {9, 3}

typedef float obs_t;

static inline double ps_kwarg_double(Dict* kwargs, const char* name) {
    DictItem* item = dict_find(kwargs, name);
    if (item == NULL) {
        fprintf(stderr, "missing puffer_survivors config key [%s] %s\n",
            kwargs->name ? kwargs->name : "?", name);
        exit(1);
    }
    if (item->str != NULL) {
        double value = 0.0;
        if (!puf_ini_parse_val(item->str, &value)) {
            fprintf(stderr, "invalid puffer_survivors config value [%s]\n", name);
            exit(1);
        }
        return value;
    }
    // Tests and programmatic callers may populate a Dict with dict_set().
    return item->value;
}

static inline float ps_kwarg(Dict* kwargs, const char* name) {
    return (float)ps_kwarg_double(kwargs, name);
}

static inline int ps_kwarg_int(Dict* kwargs, const char* name) {
    double value = ps_kwarg_double(kwargs, name);
    if (!isfinite(value) || value < (double)INT_MIN || value > (double)INT_MAX
            || floor(value) != value) {
        fprintf(stderr, "config key [%s] must contain an integer\n", name);
        exit(1);
    }
    return (int)value;
}

static inline void ps_kwarg_float_array(Dict* kwargs, const char* name,
        float* dst, int count) {
    DictItem* item = dict_find(kwargs, name);
    if (item == NULL || item->len != count || item->values == NULL) {
        fprintf(stderr, "config key [%s] must contain %d values\n", name, count);
        exit(1);
    }
    for (int i = 0; i < count; i++) dst[i] = (float)item->values[i];
}

static inline void ps_kwarg_int_array(Dict* kwargs, const char* name,
        int* dst, int count) {
    DictItem* item = dict_find(kwargs, name);
    if (item == NULL || item->len != count || item->values == NULL) {
        fprintf(stderr, "config key [%s] must contain %d values\n", name, count);
        exit(1);
    }
    for (int i = 0; i < count; i++) {
        double value = item->values[i];
        if (!isfinite(value) || value < (double)INT_MIN || value > (double)INT_MAX
                || floor(value) != value) {
            fprintf(stderr, "config key [%s] must contain integers\n", name);
            exit(1);
        }
        dst[i] = (int)value;
    }
}

static inline PSConfig ps_config_from_kwargs(Dict* kwargs) {
    PSConfig cfg = {0};
    cfg.arena_size = ps_kwarg(kwargs, "arena_size");
    cfg.max_steps = ps_kwarg_int(kwargs, "max_steps");
    cfg.wave_length_steps = ps_kwarg_int(kwargs, "wave_length_steps");
    cfg.enemy_cap = ps_kwarg_int(kwargs, "enemy_cap");
    cfg.projectile_cap = ps_kwarg_int(kwargs, "projectile_cap");
    cfg.drop_cap = ps_kwarg_int(kwargs, "drop_cap");
    cfg.obstacle_count = ps_kwarg_int(kwargs, "obstacle_count");
    cfg.area_cap = ps_kwarg_int(kwargs, "area_cap");
    cfg.player_radius = ps_kwarg(kwargs, "player_radius");
    cfg.enemy_radius[0] = ps_kwarg(kwargs, "enemy_radius_default");
    cfg.enemy_radius[1] = ps_kwarg(kwargs, "enemy_radius_jelly");
    cfg.enemy_radius[2] = ps_kwarg(kwargs, "enemy_radius_urchin");
    cfg.enemy_radius[3] = ps_kwarg(kwargs, "enemy_radius_eel");
    ps_kwarg_int_array(kwargs, "enemy_shape", cfg.enemy_shape, PS_ENEMY_KIND_COUNT);
    ps_kwarg_float_array(kwargs, "enemy_half_width", cfg.enemy_half_width, PS_ENEMY_KIND_COUNT);
    ps_kwarg_float_array(kwargs, "enemy_half_height", cfg.enemy_half_height, PS_ENEMY_KIND_COUNT);
    cfg.elite_radius_bonus = ps_kwarg(kwargs, "elite_radius_bonus");
    cfg.elite_min_radius = ps_kwarg(kwargs, "elite_min_radius");
    cfg.boss_radius = ps_kwarg(kwargs, "boss_radius");
    cfg.ari_k_radius = ps_kwarg(kwargs, "ari_k_radius");
    cfg.ari_k_shape = ps_kwarg_int(kwargs, "ari_k_shape");
    cfg.ari_k_half_width = ps_kwarg(kwargs, "ari_k_half_width");
    cfg.ari_k_half_height = ps_kwarg(kwargs, "ari_k_half_height");
    cfg.obstacle_radius_min = ps_kwarg(kwargs, "obstacle_radius_min");
    cfg.obstacle_radius_max = ps_kwarg(kwargs, "obstacle_radius_max");
    cfg.obstacle_spawn_clearance = ps_kwarg(kwargs, "obstacle_spawn_clearance");
    cfg.enemy_spawn_radius = ps_kwarg(kwargs, "enemy_spawn_radius");
    cfg.enemy_spawn_padding = ps_kwarg(kwargs, "enemy_spawn_padding");
    cfg.drop_spawn_radius = ps_kwarg(kwargs, "drop_spawn_radius");
    cfg.weapon_base_radius[PS_WEAPON_BUBBLE] = ps_kwarg(kwargs, "weapon_bubble_radius");
    cfg.weapon_base_radius[PS_WEAPON_WHIRLPOOL] = ps_kwarg(kwargs, "weapon_whirlpool_radius");
    cfg.weapon_base_radius[PS_WEAPON_ORBIT] = ps_kwarg(kwargs, "weapon_orbit_radius");
    cfg.weapon_base_radius[PS_WEAPON_INK] = ps_kwarg(kwargs, "weapon_ink_radius");
    cfg.weapon_base_radius[PS_WEAPON_SONAR] = ps_kwarg(kwargs, "weapon_sonar_radius");
    cfg.weapon_radius_per_level[PS_WEAPON_BUBBLE] = ps_kwarg(kwargs, "weapon_bubble_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_WHIRLPOOL] = ps_kwarg(kwargs, "weapon_whirlpool_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_ORBIT] = ps_kwarg(kwargs, "weapon_orbit_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_INK] = ps_kwarg(kwargs, "weapon_ink_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_SONAR] = ps_kwarg(kwargs, "weapon_sonar_radius_per_level");
    cfg.weapon_orbit_distance = ps_kwarg(kwargs, "weapon_orbit_distance");
    cfg.weapon_orbit_distance_per_level = ps_kwarg(kwargs, "weapon_orbit_distance_per_level");
    cfg.obstacle_player_spawn_clearance = ps_kwarg(kwargs, "obstacle_player_spawn_clearance");
    cfg.obstacle_spawn_min_ratio = ps_kwarg(kwargs, "obstacle_spawn_min_ratio");
    cfg.obstacle_spawn_max_ratio = ps_kwarg(kwargs, "obstacle_spawn_max_ratio");
    cfg.obstacle_fallback_min_ratio = ps_kwarg(kwargs, "obstacle_fallback_min_ratio");
    cfg.obstacle_fallback_max_ratio = ps_kwarg(kwargs, "obstacle_fallback_max_ratio");
    cfg.obstacle_fallback_angle_step = ps_kwarg(kwargs, "obstacle_fallback_angle_step");
    cfg.obstacle_fallback_angle_jitter = ps_kwarg(kwargs, "obstacle_fallback_angle_jitter");
    cfg.obstacle_fallback_spoke_count = ps_kwarg_int(kwargs, "obstacle_fallback_spoke_count");
    cfg.obstacle_recycle_spawn_min_ratio = ps_kwarg(kwargs, "obstacle_recycle_spawn_min_ratio");
    cfg.obstacle_recycle_spawn_max_ratio = ps_kwarg(kwargs, "obstacle_recycle_spawn_max_ratio");
    cfg.obstacle_recycle_radius_ratio = ps_kwarg(kwargs, "obstacle_recycle_radius_ratio");
    cfg.enemy_spawn_edge_margin = ps_kwarg(kwargs, "enemy_spawn_edge_margin");
    cfg.enemy_spawn_along_ratio = ps_kwarg(kwargs, "enemy_spawn_along_ratio");
    cfg.enemy_recycle_radius_ratio = ps_kwarg(kwargs, "enemy_recycle_radius_ratio");
    cfg.spawn_velocity_bias_probability = ps_kwarg(kwargs, "spawn_velocity_bias_probability");
    cfg.special_ring_radius_ratio = ps_kwarg(kwargs, "special_ring_radius_ratio");
    cfg.special_ring_enemy_count = ps_kwarg_int(kwargs, "special_ring_enemy_count");
    cfg.special_ring_speed_mult = ps_kwarg(kwargs, "special_ring_speed_mult");
    cfg.special_ring_wave_count = ps_kwarg_int(kwargs, "special_ring_wave_count");
    ps_kwarg_int_array(kwargs, "special_ring_waves", cfg.special_ring_waves, 3);
    ps_kwarg_float_array(kwargs, "enemy_base_hp", cfg.enemy_base_hp, PS_ENEMY_KIND_COUNT);
    ps_kwarg_float_array(kwargs, "enemy_speed_mult", cfg.enemy_speed_mult, PS_ENEMY_KIND_COUNT);
    ps_kwarg_float_array(kwargs, "enemy_base_damage", cfg.enemy_base_damage, PS_ENEMY_KIND_COUNT);
    ps_kwarg_float_array(kwargs, "weapon_base_cooldown", cfg.weapon_base_cooldown, PS_WEAPON_COUNT);
    ps_kwarg_float_array(kwargs, "weapon_cooldown_per_level", cfg.weapon_cooldown_per_level, PS_WEAPON_COUNT);
    ps_kwarg_float_array(kwargs, "weapon_base_damage", cfg.weapon_base_damage, PS_WEAPON_COUNT);
    ps_kwarg_float_array(kwargs, "weapon_damage_per_level", cfg.weapon_damage_per_level, PS_WEAPON_COUNT);
    ps_kwarg_int_array(kwargs, "wave_minimum", cfg.wave_minimum, PS_WAVE_TABLE_COUNT);
    ps_kwarg_int_array(kwargs, "wave_interval", cfg.wave_interval, PS_WAVE_TABLE_COUNT);
    cfg.enemy_mix_start_wave = ps_kwarg_int(kwargs, "enemy_mix_start_wave");
    cfg.enemy_mix_phase_one_end_wave = ps_kwarg_int(kwargs, "enemy_mix_phase_one_end_wave");
    cfg.enemy_mix_phase_two_end_wave = ps_kwarg_int(kwargs, "enemy_mix_phase_two_end_wave");
    cfg.enemy_mix_phase_one_jelly_pct = ps_kwarg_int(kwargs, "enemy_mix_phase_one_jelly_pct");
    cfg.enemy_mix_phase_two_urchin_pct = ps_kwarg_int(kwargs, "enemy_mix_phase_two_urchin_pct");
    cfg.enemy_mix_phase_two_jelly_pct = ps_kwarg_int(kwargs, "enemy_mix_phase_two_jelly_pct");
    cfg.enemy_mix_late_urchin_pct = ps_kwarg_int(kwargs, "enemy_mix_late_urchin_pct");
    cfg.enemy_mix_late_eel_pct = ps_kwarg_int(kwargs, "enemy_mix_late_eel_pct");
    cfg.enemy_mix_late_jelly_pct = ps_kwarg_int(kwargs, "enemy_mix_late_jelly_pct");
    cfg.enemy_hp_growth_per_wave = ps_kwarg(kwargs, "enemy_hp_growth_per_wave");
    cfg.enemy_hp_progress_scale = ps_kwarg(kwargs, "enemy_hp_progress_scale");
    cfg.enemy_speed_growth_per_wave = ps_kwarg(kwargs, "enemy_speed_growth_per_wave");
    cfg.enemy_speed_growth_wave_cap = ps_kwarg_int(kwargs, "enemy_speed_growth_wave_cap");
    cfg.elite_hp_multiplier = ps_kwarg(kwargs, "elite_hp_multiplier");
    cfg.elite_speed_multiplier = ps_kwarg(kwargs, "elite_speed_multiplier");
    cfg.elite_damage_multiplier = ps_kwarg(kwargs, "elite_damage_multiplier");
    cfg.boss_hp_base = ps_kwarg(kwargs, "boss_hp_base");
    cfg.boss_hp_per_wave = ps_kwarg(kwargs, "boss_hp_per_wave");
    cfg.boss_speed_multiplier = ps_kwarg(kwargs, "boss_speed_multiplier");
    cfg.boss_damage = ps_kwarg(kwargs, "boss_damage");
    cfg.ari_k_hp_multiplier = ps_kwarg(kwargs, "ari_k_hp_multiplier");
    cfg.ari_k_speed_multiplier = ps_kwarg(kwargs, "ari_k_speed_multiplier");
    cfg.ari_k_damage = ps_kwarg(kwargs, "ari_k_damage");
    cfg.elite_spawn_ramp_per_tick = ps_kwarg(kwargs, "elite_spawn_ramp_per_tick");
    cfg.boss_period_steps = ps_kwarg_int(kwargs, "boss_period_steps");
    cfg.ari_k_start_wave = ps_kwarg_int(kwargs, "ari_k_start_wave");
    cfg.ari_k_wave_period = ps_kwarg_int(kwargs, "ari_k_wave_period");
    cfg.kill_score_default = ps_kwarg(kwargs, "kill_score_default");
    cfg.kill_score_elite = ps_kwarg(kwargs, "kill_score_elite");
    cfg.kill_score_boss = ps_kwarg(kwargs, "kill_score_boss");
    cfg.drop_value_default = ps_kwarg(kwargs, "drop_value_default");
    cfg.drop_value_elite = ps_kwarg(kwargs, "drop_value_elite");
    cfg.drop_value_boss = ps_kwarg(kwargs, "drop_value_boss");
    cfg.health_drop_elite_bonus = ps_kwarg(kwargs, "health_drop_elite_bonus");
    cfg.health_drop_boss_bonus = ps_kwarg(kwargs, "health_drop_boss_bonus");
    cfg.health_drop_missing_hp_bonus = ps_kwarg(kwargs, "health_drop_missing_hp_bonus");
    cfg.health_drop_offset_x = ps_kwarg(kwargs, "health_drop_offset_x");
    cfg.health_drop_offset_y = ps_kwarg(kwargs, "health_drop_offset_y");
    cfg.enemy_spawn_rate = ps_kwarg(kwargs, "enemy_spawn_rate");
    cfg.elite_spawn_rate = ps_kwarg(kwargs, "elite_spawn_rate");
    cfg.player_speed = ps_kwarg(kwargs, "player_speed");
    cfg.player_health = ps_kwarg(kwargs, "player_health");
    cfg.enemy_speed = ps_kwarg(kwargs, "enemy_speed");
    cfg.enemy_hp_scale = ps_kwarg(kwargs, "enemy_hp_scale");
    cfg.enemy_damage_scale = ps_kwarg(kwargs, "enemy_damage_scale");
    cfg.spawn_ramp = ps_kwarg(kwargs, "spawn_ramp");
    cfg.projectile_speed = ps_kwarg(kwargs, "projectile_speed");
    cfg.projectile_damage = ps_kwarg(kwargs, "projectile_damage");
    cfg.fire_cooldown = ps_kwarg(kwargs, "fire_cooldown");
    cfg.pickup_radius = ps_kwarg(kwargs, "pickup_radius");
    cfg.magnet_radius = ps_kwarg(kwargs, "magnet_radius");
    cfg.health_drop_rate = ps_kwarg(kwargs, "health_drop_rate");
    cfg.health_heal = ps_kwarg(kwargs, "health_heal");
    cfg.reward_xp = ps_kwarg(kwargs, "reward_xp");
    cfg.reward_kill = ps_kwarg(kwargs, "reward_kill");
    cfg.reward_damage = ps_kwarg(kwargs, "reward_damage");
    cfg.reward_survival = ps_kwarg(kwargs, "reward_survival");
    cfg.reward_hurt = ps_kwarg(kwargs, "reward_hurt");
    cfg.reward_death = ps_kwarg(kwargs, "reward_death");
    cfg.reward_success = ps_kwarg(kwargs, "reward_success");
    cfg.reward_pickup = ps_kwarg(kwargs, "reward_pickup");
    cfg.reward_levelup = ps_kwarg(kwargs, "reward_levelup");
    cfg.obstacle_penalty = ps_kwarg(kwargs, "obstacle_penalty");
    cfg.contact_damage = ps_kwarg(kwargs, "contact_damage");
    cfg.invuln_steps = ps_kwarg_int(kwargs, "invuln_steps");
    cfg.enemy_obstacle_stride = ps_kwarg_int(kwargs, "enemy_obstacle_stride");
    cfg.movement_smoothing = ps_kwarg(kwargs, "movement_smoothing");
    cfg.pickup_magnet_speed = ps_kwarg(kwargs, "pickup_magnet_speed");
    cfg.weapon_max_level = ps_kwarg_int(kwargs, "weapon_max_level");
    cfg.upgrade_speed_bonus = ps_kwarg(kwargs, "upgrade_speed_bonus");
    cfg.upgrade_magnet_bonus = ps_kwarg(kwargs, "upgrade_magnet_bonus");
    cfg.upgrade_health_bonus = ps_kwarg(kwargs, "upgrade_health_bonus");
    cfg.upgrade_might_bonus = ps_kwarg(kwargs, "upgrade_might_bonus");
    cfg.upgrade_cooldown_multiplier = ps_kwarg(kwargs, "upgrade_cooldown_multiplier");
    cfg.upgrade_area_bonus = ps_kwarg(kwargs, "upgrade_area_bonus");
    cfg.xp_threshold_base = ps_kwarg(kwargs, "xp_threshold_base");
    cfg.xp_threshold_per_level = ps_kwarg(kwargs, "xp_threshold_per_level");
    cfg.wave_spawn_reference_rate = ps_kwarg(kwargs, "wave_spawn_reference_rate");
    cfg.wave_spawn_scale_min = ps_kwarg(kwargs, "wave_spawn_scale_min");
    cfg.wave_spawn_scale_max = ps_kwarg(kwargs, "wave_spawn_scale_max");
    cfg.wave_progress_spawn_scale = ps_kwarg(kwargs, "wave_progress_spawn_scale");
    cfg.wave_population_cap = ps_kwarg_int(kwargs, "wave_population_cap");
    cfg.wave_tail_minimum_base = ps_kwarg_int(kwargs, "wave_tail_minimum_base");
    cfg.wave_tail_minimum_step = ps_kwarg_int(kwargs, "wave_tail_minimum_step");
    cfg.wave_tail_interval = ps_kwarg_int(kwargs, "wave_tail_interval");
    cfg.wave_min_spawn_interval = ps_kwarg_int(kwargs, "wave_min_spawn_interval");
    cfg.progress_normal_wave_count = ps_kwarg_int(kwargs, "progress_normal_wave_count");
    cfg.weapon_min_cooldown = ps_kwarg(kwargs, "weapon_min_cooldown");
    cfg.bubble_target_range = ps_kwarg(kwargs, "bubble_target_range");
    cfg.bubble_target_area_range = ps_kwarg(kwargs, "bubble_target_area_range");
    cfg.bubble_shot_spread = ps_kwarg(kwargs, "bubble_shot_spread");
    cfg.bubble_projectile_ttl = ps_kwarg_int(kwargs, "bubble_projectile_ttl");
    cfg.whirlpool_knockback = ps_kwarg(kwargs, "whirlpool_knockback");
    cfg.whirlpool_ttl = ps_kwarg_int(kwargs, "whirlpool_ttl");
    cfg.whirlpool_tick_rate = ps_kwarg_int(kwargs, "whirlpool_tick_rate");
    cfg.orbit_area_distance_bonus = ps_kwarg(kwargs, "orbit_area_distance_bonus");
    cfg.orbit_knockback = ps_kwarg(kwargs, "orbit_knockback");
    cfg.ink_target_range_ratio = ps_kwarg(kwargs, "ink_target_range_ratio");
    cfg.ink_pool_spread = ps_kwarg(kwargs, "ink_pool_spread");
    cfg.ink_pool_angle_jitter = ps_kwarg(kwargs, "ink_pool_angle_jitter");
    cfg.ink_pool_ttl_base = ps_kwarg_int(kwargs, "ink_pool_ttl_base");
    cfg.ink_pool_ttl_per_level = ps_kwarg_int(kwargs, "ink_pool_ttl_per_level");
    cfg.ink_pool_tick_rate = ps_kwarg_int(kwargs, "ink_pool_tick_rate");
    cfg.ink_trail_min_speed2 = ps_kwarg(kwargs, "ink_trail_min_speed2");
    cfg.ink_trail_max_base = ps_kwarg_int(kwargs, "ink_trail_max_base");
    cfg.ink_trail_max_per_level = ps_kwarg_int(kwargs, "ink_trail_max_per_level");
    cfg.ink_trail_cadence_base = ps_kwarg_int(kwargs, "ink_trail_cadence_base");
    cfg.ink_trail_cadence_level_divisor = ps_kwarg_int(kwargs, "ink_trail_cadence_level_divisor");
    cfg.ink_trail_cadence_min = ps_kwarg_int(kwargs, "ink_trail_cadence_min");
    cfg.ink_trail_radius_base = ps_kwarg(kwargs, "ink_trail_radius_base");
    cfg.ink_trail_radius_per_level = ps_kwarg(kwargs, "ink_trail_radius_per_level");
    cfg.ink_trail_radius_config_scale = ps_kwarg(kwargs, "ink_trail_radius_config_scale");
    cfg.ink_trail_damage_multiplier = ps_kwarg(kwargs, "ink_trail_damage_multiplier");
    cfg.ink_trail_ttl_base = ps_kwarg_int(kwargs, "ink_trail_ttl_base");
    cfg.ink_trail_ttl_per_level = ps_kwarg_int(kwargs, "ink_trail_ttl_per_level");
    cfg.ink_trail_offset = ps_kwarg(kwargs, "ink_trail_offset");
    cfg.ink_trail_tick_rate = ps_kwarg_int(kwargs, "ink_trail_tick_rate");
    cfg.area_tick_knockback = ps_kwarg(kwargs, "area_tick_knockback");
    cfg.weapon_active_decay = ps_kwarg(kwargs, "weapon_active_decay");
    cfg.orbit_phase_speed = ps_kwarg(kwargs, "orbit_phase_speed");
    cfg.orbit_phase_per_level = ps_kwarg(kwargs, "orbit_phase_per_level");
    cfg.sonar_knockback = ps_kwarg(kwargs, "sonar_knockback");
    cfg.sonar_ttl = ps_kwarg_int(kwargs, "sonar_ttl");
    cfg.sonar_tick_rate = ps_kwarg_int(kwargs, "sonar_tick_rate");
    cfg.moving_obstacle_cap = ps_kwarg_int(kwargs, "moving_obstacle_cap");
    cfg.moving_obstacle_start_wave = ps_kwarg_int(kwargs, "moving_obstacle_start_wave");
    cfg.moving_obstacle_spawn_interval = ps_kwarg_int(kwargs, "moving_obstacle_spawn_interval");
    cfg.moving_obstacle_ttl = ps_kwarg_int(kwargs, "moving_obstacle_ttl");
    cfg.moving_obstacle_spawn_margin = ps_kwarg(kwargs, "moving_obstacle_spawn_margin");
    ps_kwarg_float_array(kwargs, "moving_obstacle_speed", cfg.moving_obstacle_speed, PS_MOVING_OBSTACLE_TYPE_COUNT);
    ps_kwarg_float_array(kwargs, "moving_obstacle_half_width", cfg.moving_obstacle_half_width, PS_MOVING_OBSTACLE_TYPE_COUNT);
    ps_kwarg_float_array(kwargs, "moving_obstacle_half_height", cfg.moving_obstacle_half_height, PS_MOVING_OBSTACLE_TYPE_COUNT);
    cfg.moving_obstacle_damage = ps_kwarg(kwargs, "moving_obstacle_damage");
    cfg.free_upgrade = ps_kwarg_int(kwargs, "free_upgrade");
    cfg.free_upgrade_count = ps_kwarg_int(kwargs, "free_upgrade_count");
    return cfg;
}

#ifndef PUFFER_GPU_ENV
void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;
    env->cfg = ps_config_from_kwargs(kwargs);
    env->show_hitboxes = ps_kwarg_int(kwargs, "show_hitboxes");
}

void c_reset(PufferSurvivors* env) {
    ps_reset_env(env, 0);
}

void c_step(PufferSurvivors* env) {
    ps_step_env(env, 0);
}

void puf_reset(Env* env) {
    c_reset(env);
}

void puf_step(Env* env) {
    c_step(env);
}

void puf_render(Env* env) {
#ifdef PS_HEADLESS_BINDING
    (void)env;
#else
    c_render(env);
#endif
}

void puf_close(Env* env) {
#ifdef PS_HEADLESS_BINDING
    (void)env;
#else
    c_close(env);
#endif
}
#else
void puf_render(Env* env) {
    (void)env;
}
#endif

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "reward_survival", log->reward_survival);
    dict_set(out, "reward_damage", log->reward_damage);
    dict_set(out, "reward_kill", log->reward_kill);
    dict_set(out, "reward_hurt", log->reward_hurt);
    dict_set(out, "reward_pickup", log->reward_pickup);
    dict_set(out, "reward_xp", log->reward_xp);
    dict_set(out, "reward_levelup", log->reward_levelup);
    dict_set(out, "reward_obstacle", log->reward_obstacle);
    dict_set(out, "reward_terminal", log->reward_terminal);
    dict_set(out, "kills", log->kills);
    dict_set(out, "level", log->level);
    dict_set(out, "xp", log->xp);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_taken", log->damage_taken);
    dict_set(out, "pickups", log->pickups);
    dict_set(out, "levelups", log->levelups);
    dict_set(out, "obstacle_hits", log->obstacle_hits);
    dict_set(out, "enemies_alive", log->enemies_alive);
    dict_set(out, "projectiles_alive", log->projectiles_alive);
    dict_set(out, "drops_alive", log->drops_alive);
    dict_set(out, "areas_alive", log->areas_alive);
    dict_set(out, "weapon_levels", log->weapon_levels);
    dict_set(out, "wave", log->wave);
    dict_set(out, "hp", log->hp);
    dict_set(out, "survived", log->survived);
    dict_set(out, "death_0_25", log->death_0_25);
    dict_set(out, "death_25_50", log->death_25_50);
    dict_set(out, "death_50_75", log->death_50_75);
    dict_set(out, "death_75_100", log->death_75_100);
    dict_set(out, "success", log->success);
    dict_set(out, "peak_enemies", log->peak_enemies);
    dict_set(out, "peak_projectiles", log->peak_projectiles);
    dict_set(out, "min_hp", log->min_hp);
}
