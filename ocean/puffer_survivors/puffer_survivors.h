#pragma once

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
#define ACT_SIZES {PS_MOVE_ACTION_COUNT, 3}

typedef float obs_t;

static inline void ps_kwarg_float_array(Dict* kwargs, const char* name,
        float* dst, int count) {
    DictItem* item = dict_find(kwargs, name);
    for (int i = 0; i < count; i++) dst[i] = (float)item->values[i];
}

static inline void ps_kwarg_int_array(Dict* kwargs, const char* name,
        int* dst, int count) {
    DictItem* item = dict_find(kwargs, name);
    for (int i = 0; i < count; i++) dst[i] = (int)item->values[i];
}

static inline int ps_config_cap(int value, int max) {
    return value < 0 ? 0 : (value > max ? max : value);
}

static inline PSConfig ps_config_from_kwargs(Dict* kwargs) {
    PSConfig cfg = {0};
    cfg.arena_size = (float)dict_get(kwargs, "arena_size");
    cfg.max_steps = (int)dict_get(kwargs, "max_steps");
    cfg.wave_length_steps = (int)dict_get(kwargs, "wave_length_steps");
    cfg.enemy_cap = ps_config_cap((int)dict_get(kwargs, "enemy_cap"), PS_MAX_ENEMIES);
    cfg.projectile_cap = ps_config_cap((int)dict_get(kwargs, "projectile_cap"), PS_MAX_PROJECTILES);
    cfg.drop_cap = ps_config_cap((int)dict_get(kwargs, "drop_cap"), PS_MAX_DROPS);
    cfg.obstacle_count = ps_config_cap((int)dict_get(kwargs, "obstacle_count"), PS_MAX_OBSTACLES);
    cfg.area_cap = ps_config_cap((int)dict_get(kwargs, "area_cap"), PS_AREA_STORAGE_CAP);
    cfg.player_radius = (float)dict_get(kwargs, "player_radius");
    cfg.enemy_radius[0] = (float)dict_get(kwargs, "enemy_radius_default");
    cfg.enemy_radius[1] = (float)dict_get(kwargs, "enemy_radius_jelly");
    cfg.enemy_radius[2] = (float)dict_get(kwargs, "enemy_radius_urchin");
    cfg.enemy_radius[3] = (float)dict_get(kwargs, "enemy_radius_eel");
    cfg.elite_radius = (float)dict_get(kwargs, "elite_radius");
    cfg.boss_radius = (float)dict_get(kwargs, "boss_radius");
    cfg.ari_k_radius = (float)dict_get(kwargs, "ari_k_radius");
    cfg.ari_k_half_width = (float)dict_get(kwargs, "ari_k_half_width");
    cfg.ari_k_half_height = (float)dict_get(kwargs, "ari_k_half_height");
    cfg.obstacle_radius_min = (float)dict_get(kwargs, "obstacle_radius_min");
    cfg.obstacle_radius_max = (float)dict_get(kwargs, "obstacle_radius_max");
    cfg.obstacle_spawn_clearance = (float)dict_get(kwargs, "obstacle_spawn_clearance");
    cfg.enemy_spawn_radius = (float)dict_get(kwargs, "enemy_spawn_radius");
    cfg.enemy_spawn_padding = (float)dict_get(kwargs, "enemy_spawn_padding");
    cfg.drop_spawn_radius = (float)dict_get(kwargs, "drop_spawn_radius");
    cfg.weapon_base_radius[PS_WEAPON_BUBBLE] = (float)dict_get(kwargs, "weapon_bubble_radius");
    cfg.weapon_base_radius[PS_WEAPON_WHIRLPOOL] = (float)dict_get(kwargs, "weapon_whirlpool_radius");
    cfg.weapon_base_radius[PS_WEAPON_ORBIT] = (float)dict_get(kwargs, "weapon_orbit_radius");
    cfg.weapon_base_radius[PS_WEAPON_INK] = (float)dict_get(kwargs, "weapon_ink_radius");
    cfg.weapon_base_radius[PS_WEAPON_SONAR] = (float)dict_get(kwargs, "weapon_sonar_radius");
    cfg.weapon_base_radius[PS_WEAPON_SPIKES] = (float)dict_get(kwargs, "weapon_spikes_radius");
    cfg.weapon_radius_per_level[PS_WEAPON_BUBBLE] = (float)dict_get(kwargs, "weapon_bubble_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_WHIRLPOOL] = (float)dict_get(kwargs, "weapon_whirlpool_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_ORBIT] = (float)dict_get(kwargs, "weapon_orbit_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_INK] = (float)dict_get(kwargs, "weapon_ink_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_SONAR] = (float)dict_get(kwargs, "weapon_sonar_radius_per_level");
    cfg.weapon_radius_per_level[PS_WEAPON_SPIKES] = (float)dict_get(kwargs, "weapon_spikes_radius_per_level");
    cfg.weapon_orbit_distance = (float)dict_get(kwargs, "weapon_orbit_distance");
    cfg.weapon_orbit_distance_per_level = (float)dict_get(kwargs, "weapon_orbit_distance_per_level");
    cfg.obstacle_player_spawn_clearance = (float)dict_get(kwargs, "obstacle_player_spawn_clearance");
    cfg.obstacle_spawn_min_ratio = (float)dict_get(kwargs, "obstacle_spawn_min_ratio");
    cfg.obstacle_spawn_max_ratio = (float)dict_get(kwargs, "obstacle_spawn_max_ratio");
    cfg.obstacle_fallback_min_ratio = (float)dict_get(kwargs, "obstacle_fallback_min_ratio");
    cfg.obstacle_fallback_max_ratio = (float)dict_get(kwargs, "obstacle_fallback_max_ratio");
    cfg.obstacle_fallback_angle_step = (float)dict_get(kwargs, "obstacle_fallback_angle_step");
    cfg.obstacle_fallback_angle_jitter = (float)dict_get(kwargs, "obstacle_fallback_angle_jitter");
    cfg.obstacle_fallback_spoke_count = (int)dict_get(kwargs, "obstacle_fallback_spoke_count");
    cfg.obstacle_recycle_spawn_min_ratio = (float)dict_get(kwargs, "obstacle_recycle_spawn_min_ratio");
    cfg.obstacle_recycle_spawn_max_ratio = (float)dict_get(kwargs, "obstacle_recycle_spawn_max_ratio");
    cfg.obstacle_recycle_radius_ratio = (float)dict_get(kwargs, "obstacle_recycle_radius_ratio");
    cfg.enemy_spawn_edge_margin = (float)dict_get(kwargs, "enemy_spawn_edge_margin");
    cfg.enemy_spawn_along_ratio = (float)dict_get(kwargs, "enemy_spawn_along_ratio");
    cfg.enemy_recycle_radius_ratio = (float)dict_get(kwargs, "enemy_recycle_radius_ratio");
    cfg.spawn_velocity_bias_probability = (float)dict_get(kwargs, "spawn_velocity_bias_probability");
    cfg.special_ring_radius_ratio = (float)dict_get(kwargs, "special_ring_radius_ratio");
    cfg.special_ring_enemy_count = (int)dict_get(kwargs, "special_ring_enemy_count");
    cfg.special_ring_speed_mult = (float)dict_get(kwargs, "special_ring_speed_mult");
    cfg.special_ring_wave_count = (int)dict_get(kwargs, "special_ring_wave_count");
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
    cfg.enemy_mix_start_wave = (int)dict_get(kwargs, "enemy_mix_start_wave");
    cfg.enemy_mix_phase_one_end_wave = (int)dict_get(kwargs, "enemy_mix_phase_one_end_wave");
    cfg.enemy_mix_phase_two_end_wave = (int)dict_get(kwargs, "enemy_mix_phase_two_end_wave");
    cfg.enemy_mix_phase_one_jelly_pct = (int)dict_get(kwargs, "enemy_mix_phase_one_jelly_pct");
    cfg.enemy_mix_phase_two_urchin_pct = (int)dict_get(kwargs, "enemy_mix_phase_two_urchin_pct");
    cfg.enemy_mix_phase_two_jelly_pct = (int)dict_get(kwargs, "enemy_mix_phase_two_jelly_pct");
    cfg.enemy_mix_late_urchin_pct = (int)dict_get(kwargs, "enemy_mix_late_urchin_pct");
    cfg.enemy_mix_late_eel_pct = (int)dict_get(kwargs, "enemy_mix_late_eel_pct");
    cfg.enemy_mix_late_jelly_pct = (int)dict_get(kwargs, "enemy_mix_late_jelly_pct");
    cfg.enemy_hp_growth_per_wave = (float)dict_get(kwargs, "enemy_hp_growth_per_wave");
    cfg.enemy_hp_progress_scale = (float)dict_get(kwargs, "enemy_hp_progress_scale");
    cfg.enemy_speed_growth_per_wave = (float)dict_get(kwargs, "enemy_speed_growth_per_wave");
    cfg.enemy_speed_growth_wave_cap = (int)dict_get(kwargs, "enemy_speed_growth_wave_cap");
    cfg.elite_hp_multiplier = (float)dict_get(kwargs, "elite_hp_multiplier");
    cfg.elite_speed_multiplier = (float)dict_get(kwargs, "elite_speed_multiplier");
    cfg.elite_damage_multiplier = (float)dict_get(kwargs, "elite_damage_multiplier");
    cfg.boss_hp_base = (float)dict_get(kwargs, "boss_hp_base");
    cfg.boss_hp_per_wave = (float)dict_get(kwargs, "boss_hp_per_wave");
    cfg.boss_speed_multiplier = (float)dict_get(kwargs, "boss_speed_multiplier");
    cfg.boss_damage = (float)dict_get(kwargs, "boss_damage");
    cfg.ari_k_hp_multiplier = (float)dict_get(kwargs, "ari_k_hp_multiplier");
    cfg.ari_k_speed_multiplier = (float)dict_get(kwargs, "ari_k_speed_multiplier");
    cfg.ari_k_damage = (float)dict_get(kwargs, "ari_k_damage");
    cfg.elite_spawn_ramp_per_tick = (float)dict_get(kwargs, "elite_spawn_ramp_per_tick");
    cfg.boss_period_steps = (int)dict_get(kwargs, "boss_period_steps");
    cfg.ari_k_start_wave = (int)dict_get(kwargs, "ari_k_start_wave");
    cfg.ari_k_wave_period = (int)dict_get(kwargs, "ari_k_wave_period");
    cfg.kill_score_default = (float)dict_get(kwargs, "kill_score_default");
    cfg.kill_score_elite = (float)dict_get(kwargs, "kill_score_elite");
    cfg.kill_score_boss = (float)dict_get(kwargs, "kill_score_boss");
    cfg.drop_value_default = (float)dict_get(kwargs, "drop_value_default");
    cfg.drop_value_elite = (float)dict_get(kwargs, "drop_value_elite");
    cfg.drop_value_boss = (float)dict_get(kwargs, "drop_value_boss");
    cfg.health_drop_elite_bonus = (float)dict_get(kwargs, "health_drop_elite_bonus");
    cfg.health_drop_boss_bonus = (float)dict_get(kwargs, "health_drop_boss_bonus");
    cfg.health_drop_missing_hp_bonus = (float)dict_get(kwargs, "health_drop_missing_hp_bonus");
    cfg.health_drop_offset_x = (float)dict_get(kwargs, "health_drop_offset_x");
    cfg.health_drop_offset_y = (float)dict_get(kwargs, "health_drop_offset_y");
    cfg.enemy_spawn_rate = (float)dict_get(kwargs, "enemy_spawn_rate");
    cfg.elite_spawn_rate = (float)dict_get(kwargs, "elite_spawn_rate");
    cfg.player_speed = (float)dict_get(kwargs, "player_speed");
    cfg.player_health = (float)dict_get(kwargs, "player_health");
    cfg.enemy_speed = (float)dict_get(kwargs, "enemy_speed");
    cfg.enemy_hp_scale = (float)dict_get(kwargs, "enemy_hp_scale");
    cfg.enemy_damage_scale = (float)dict_get(kwargs, "enemy_damage_scale");
    cfg.spawn_ramp = (float)dict_get(kwargs, "spawn_ramp");
    cfg.projectile_speed = (float)dict_get(kwargs, "projectile_speed");
    cfg.projectile_damage = (float)dict_get(kwargs, "projectile_damage");
    cfg.fire_cooldown = (float)dict_get(kwargs, "fire_cooldown");
    cfg.pickup_radius = (float)dict_get(kwargs, "pickup_radius");
    cfg.magnet_radius = (float)dict_get(kwargs, "magnet_radius");
    cfg.health_drop_rate = (float)dict_get(kwargs, "health_drop_rate");
    cfg.health_heal = (float)dict_get(kwargs, "health_heal");
    cfg.reward_xp = (float)dict_get(kwargs, "reward_xp");
    cfg.reward_kill = (float)dict_get(kwargs, "reward_kill");
    cfg.reward_damage = (float)dict_get(kwargs, "reward_damage");
    cfg.reward_survival = (float)dict_get(kwargs, "reward_survival");
    cfg.reward_hurt = (float)dict_get(kwargs, "reward_hurt");
    cfg.reward_death = (float)dict_get(kwargs, "reward_death");
    cfg.reward_success = (float)dict_get(kwargs, "reward_success");
    cfg.reward_pickup = (float)dict_get(kwargs, "reward_pickup");
    cfg.reward_levelup = (float)dict_get(kwargs, "reward_levelup");
    cfg.obstacle_penalty = (float)dict_get(kwargs, "obstacle_penalty");
    cfg.contact_damage = (float)dict_get(kwargs, "contact_damage");
    cfg.invuln_steps = (int)dict_get(kwargs, "invuln_steps");
    cfg.enemy_obstacle_stride = (int)dict_get(kwargs, "enemy_obstacle_stride");
    cfg.movement_smoothing = (float)dict_get(kwargs, "movement_smoothing");
    cfg.dash_speed = (float)dict_get(kwargs, "dash_speed");
    cfg.dash_duration = (int)dict_get(kwargs, "dash_duration");
    cfg.dash_shrink = (float)dict_get(kwargs, "dash_shrink");
    cfg.dash_cooldown = (float)dict_get(kwargs, "dash_cooldown");
    cfg.dash_cooldown_per_level = (float)dict_get(kwargs, "dash_cooldown_per_level");
    cfg.pickup_magnet_speed = (float)dict_get(kwargs, "pickup_magnet_speed");
    cfg.weapon_max_level = (int)dict_get(kwargs, "weapon_max_level");
    cfg.upgrade_speed_bonus = (float)dict_get(kwargs, "upgrade_speed_bonus");
    cfg.upgrade_magnet_bonus = (float)dict_get(kwargs, "upgrade_magnet_bonus");
    cfg.upgrade_health_bonus = (float)dict_get(kwargs, "upgrade_health_bonus");
    cfg.upgrade_might_bonus = (float)dict_get(kwargs, "upgrade_might_bonus");
    cfg.upgrade_cooldown_multiplier = (float)dict_get(kwargs, "upgrade_cooldown_multiplier");
    cfg.upgrade_area_bonus = (float)dict_get(kwargs, "upgrade_area_bonus");
    cfg.xp_threshold_base = (float)dict_get(kwargs, "xp_threshold_base");
    cfg.xp_threshold_per_level = (float)dict_get(kwargs, "xp_threshold_per_level");
    cfg.wave_spawn_reference_rate = (float)dict_get(kwargs, "wave_spawn_reference_rate");
    cfg.wave_spawn_scale_min = (float)dict_get(kwargs, "wave_spawn_scale_min");
    cfg.wave_spawn_scale_max = (float)dict_get(kwargs, "wave_spawn_scale_max");
    cfg.wave_progress_spawn_scale = (float)dict_get(kwargs, "wave_progress_spawn_scale");
    cfg.wave_population_cap = (int)dict_get(kwargs, "wave_population_cap");
    cfg.wave_tail_minimum_base = (int)dict_get(kwargs, "wave_tail_minimum_base");
    cfg.wave_tail_minimum_step = (int)dict_get(kwargs, "wave_tail_minimum_step");
    cfg.wave_tail_interval = (int)dict_get(kwargs, "wave_tail_interval");
    cfg.wave_min_spawn_interval = (int)dict_get(kwargs, "wave_min_spawn_interval");
    cfg.progress_normal_wave_count = (int)dict_get(kwargs, "progress_normal_wave_count");
    cfg.weapon_min_cooldown = (float)dict_get(kwargs, "weapon_min_cooldown");
    cfg.bubble_target_range = (float)dict_get(kwargs, "bubble_target_range");
    cfg.bubble_target_area_range = (float)dict_get(kwargs, "bubble_target_area_range");
    cfg.bubble_shot_spread = (float)dict_get(kwargs, "bubble_shot_spread");
    cfg.bubble_projectile_ttl = (int)dict_get(kwargs, "bubble_projectile_ttl");
    cfg.whirlpool_knockback = (float)dict_get(kwargs, "whirlpool_knockback");
    cfg.whirlpool_ttl = (int)dict_get(kwargs, "whirlpool_ttl");
    cfg.whirlpool_tick_rate = (int)dict_get(kwargs, "whirlpool_tick_rate");
    cfg.orbit_area_distance_bonus = (float)dict_get(kwargs, "orbit_area_distance_bonus");
    cfg.orbit_knockback = (float)dict_get(kwargs, "orbit_knockback");
    cfg.ink_target_range_ratio = (float)dict_get(kwargs, "ink_target_range_ratio");
    cfg.ink_pool_spread = (float)dict_get(kwargs, "ink_pool_spread");
    cfg.ink_pool_angle_jitter = (float)dict_get(kwargs, "ink_pool_angle_jitter");
    cfg.ink_pool_ttl_base = (int)dict_get(kwargs, "ink_pool_ttl_base");
    cfg.ink_pool_ttl_per_level = (int)dict_get(kwargs, "ink_pool_ttl_per_level");
    cfg.ink_pool_tick_rate = (int)dict_get(kwargs, "ink_pool_tick_rate");
    cfg.ink_trail_min_speed2 = (float)dict_get(kwargs, "ink_trail_min_speed2");
    cfg.ink_trail_max_base = (int)dict_get(kwargs, "ink_trail_max_base");
    cfg.ink_trail_max_per_level = (int)dict_get(kwargs, "ink_trail_max_per_level");
    cfg.ink_trail_cadence_base = (int)dict_get(kwargs, "ink_trail_cadence_base");
    cfg.ink_trail_cadence_level_divisor = (int)dict_get(kwargs, "ink_trail_cadence_level_divisor");
    cfg.ink_trail_cadence_min = (int)dict_get(kwargs, "ink_trail_cadence_min");
    cfg.ink_trail_radius_base = (float)dict_get(kwargs, "ink_trail_radius_base");
    cfg.ink_trail_radius_per_level = (float)dict_get(kwargs, "ink_trail_radius_per_level");
    cfg.ink_trail_radius_config_scale = (float)dict_get(kwargs, "ink_trail_radius_config_scale");
    cfg.ink_trail_damage_multiplier = (float)dict_get(kwargs, "ink_trail_damage_multiplier");
    cfg.ink_trail_ttl_base = (int)dict_get(kwargs, "ink_trail_ttl_base");
    cfg.ink_trail_ttl_per_level = (int)dict_get(kwargs, "ink_trail_ttl_per_level");
    cfg.ink_trail_offset = (float)dict_get(kwargs, "ink_trail_offset");
    cfg.ink_trail_tick_rate = (int)dict_get(kwargs, "ink_trail_tick_rate");
    cfg.area_tick_knockback = (float)dict_get(kwargs, "area_tick_knockback");
    cfg.weapon_active_decay = (float)dict_get(kwargs, "weapon_active_decay");
    cfg.orbit_phase_speed = (float)dict_get(kwargs, "orbit_phase_speed");
    cfg.orbit_phase_per_level = (float)dict_get(kwargs, "orbit_phase_per_level");
    cfg.sonar_knockback = (float)dict_get(kwargs, "sonar_knockback");
    cfg.sonar_ttl = (int)dict_get(kwargs, "sonar_ttl");
    cfg.sonar_tick_rate = (int)dict_get(kwargs, "sonar_tick_rate");
    cfg.frost_slow = (float)dict_get(kwargs, "frost_slow");
    cfg.frost_half_angle = (float)dict_get(kwargs, "frost_half_angle");
    cfg.frost_range = (float)dict_get(kwargs, "frost_range");
    cfg.frost_slow_ttl = (int)dict_get(kwargs, "frost_slow_ttl");
    cfg.spike_speed = (float)dict_get(kwargs, "spike_speed");
    cfg.spike_range = (float)dict_get(kwargs, "spike_range");
    cfg.moving_obstacle_cap = ps_config_cap((int)dict_get(kwargs, "moving_obstacle_cap"), PS_MAX_MOVING_OBSTACLES);
    cfg.moving_obstacle_start_wave = (int)dict_get(kwargs, "moving_obstacle_start_wave");
    cfg.moving_obstacle_spawn_interval = (int)dict_get(kwargs, "moving_obstacle_spawn_interval");
    cfg.moving_obstacle_ttl = (int)dict_get(kwargs, "moving_obstacle_ttl");
    cfg.moving_obstacle_spawn_margin = (float)dict_get(kwargs, "moving_obstacle_spawn_margin");
    ps_kwarg_float_array(kwargs, "moving_obstacle_speed", cfg.moving_obstacle_speed, PS_MOVING_OBSTACLE_TYPE_COUNT);
    ps_kwarg_float_array(kwargs, "moving_obstacle_half_width", cfg.moving_obstacle_half_width, PS_MOVING_OBSTACLE_TYPE_COUNT);
    ps_kwarg_float_array(kwargs, "moving_obstacle_half_height", cfg.moving_obstacle_half_height, PS_MOVING_OBSTACLE_TYPE_COUNT);
    cfg.moving_obstacle_damage = (float)dict_get(kwargs, "moving_obstacle_damage");
    cfg.free_upgrade = (int)dict_get(kwargs, "free_upgrade");
    cfg.free_upgrade_count = (int)dict_get(kwargs, "free_upgrade_count");
    return cfg;
}

#ifndef PUFFER_GPU_ENV
void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;
    env->cfg = ps_config_from_kwargs(kwargs);
    env->show_hitboxes = (int)dict_get(kwargs, "show_hitboxes");
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
