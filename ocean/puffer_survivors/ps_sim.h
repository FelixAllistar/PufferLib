#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps_constants.h"
#include "ps_log.h"
#include "ps_geometry.h"

#ifdef __CUDACC__
#define PS_SIM_FN static __host__ __device__ __forceinline__
#define PSSim PSCudaSim
#define PS_IDX(sim, i, env) ((i) * (sim)->num_envs + (env))
#define PS_AT(sim, env, ptr, i) ((ptr)[PS_IDX(sim, i, env)])
#define PS_P(sim, env, field) ((sim)->field[env])
#define PS_LOG(sim, env) ((sim)->native_envs[env].log)
#define PS_OBS(sim, env) ((sim)->observations + (size_t)(env) * PS_OBS_SIZE)
#define PS_ACTIONS(sim, env) ((sim)->actions + (size_t)(env) * NUM_ATNS)
#define PS_REWARD(sim, env) ((sim)->rewards[env])
#define PS_TERMINAL(sim, env) ((sim)->terminals[env])
#define PS_ENEMY(sim, env, i, f) ((sim)->enemy_##f[PS_IDX(sim, i, env)])
#define PS_PROJECTILE(sim, env, i, f) ((sim)->projectile_##f[PS_IDX(sim, i, env)])
#define PS_DROP(sim, env, i, f) ((sim)->drop_##f[PS_IDX(sim, i, env)])
#define PS_AREA(sim, env, i, f) ((sim)->area_##f[PS_IDX(sim, i, env)])
#define PS_OBSTACLE(sim, env, i, f) ((sim)->obstacle_##f[PS_IDX(sim, i, env)])
#define PS_MOVING(sim, env, i, f) ((sim)->moving_obstacle_##f[PS_IDX(sim, i, env)])
#define PS_ENEMY_ACTIVE(sim, env) ((sim)->enemy_active)
#define PS_ENEMY_DENSE(sim, env) ((sim)->enemy_dense)
#define PS_ENEMY_DENSE_POS(sim, env) ((sim)->enemy_dense_pos)
#define PS_PROJECTILE_ACTIVE(sim, env) ((sim)->projectile_active)
#define PS_PROJECTILE_DENSE(sim, env) ((sim)->projectile_dense)
#define PS_PROJECTILE_DENSE_POS(sim, env) ((sim)->projectile_dense_pos)
#define PS_DROP_ACTIVE(sim, env) ((sim)->drop_active)
#define PS_DROP_DENSE(sim, env) ((sim)->drop_dense)
#define PS_DROP_DENSE_POS(sim, env) ((sim)->drop_dense_pos)
#define PS_AREA_ACTIVE(sim, env) ((sim)->area_active)
#define PS_AREA_DENSE(sim, env) ((sim)->area_dense)
#define PS_AREA_DENSE_POS(sim, env) ((sim)->area_dense_pos)
#define PS_MOVING_ACTIVE(sim, env) ((sim)->moving_obstacle_active)
#define PS_MOVING_DENSE(sim, env) ((sim)->moving_obstacle_dense)
#define PS_MOVING_DENSE_POS(sim, env) ((sim)->moving_obstacle_dense_pos)
#define PS_GRID(sim, env, cell) ((sim)->grid_head[PS_IDX(sim, cell, env)])
#define PS_GRID_TOUCHED(sim, env, i) ((sim)->grid_touched[PS_IDX(sim, i, env)])
#define PS_AABB(sim, env, i) ((sim)->aabb_indices[PS_IDX(sim, i, env)])
#else
#define PS_SIM_FN static inline
#define PSSim PufferSurvivors
#define PS_IDX(sim, i, env) (i)
#define PS_AT(sim, env, ptr, i) ((ptr)[i])
#define PS_P(sim, env, field) ((sim)->field)
#define PS_LOG(sim, env) ((sim)->log)
#define PS_OBS(sim, env) ((sim)->agents[0].observations)
#define PS_ACTIONS(sim, env) ((sim)->agents[0].actions)
#define PS_REWARD(sim, env) ((sim)->agents[0].rewards[0])
#define PS_TERMINAL(sim, env) ((sim)->agents[0].terminals[0])
#define PS_ENEMY(sim, env, i, f) ((sim)->enemies.f[i])
#define PS_PROJECTILE(sim, env, i, f) ((sim)->projectiles.f[i])
#define PS_DROP(sim, env, i, f) ((sim)->drops.f[i])
#define PS_AREA(sim, env, i, f) ((sim)->areas.f[i])
#define PS_OBSTACLE(sim, env, i, f) ((sim)->obstacles.f[i])
#define PS_MOVING(sim, env, i, f) ((sim)->moving_obstacles.f[i])
#define PS_ENEMY_ACTIVE(sim, env) ((sim)->enemies.active)
#define PS_ENEMY_DENSE(sim, env) ((sim)->enemies.dense)
#define PS_ENEMY_DENSE_POS(sim, env) ((sim)->enemies.dense_pos)
#define PS_PROJECTILE_ACTIVE(sim, env) ((sim)->projectiles.active)
#define PS_PROJECTILE_DENSE(sim, env) ((sim)->projectiles.dense)
#define PS_PROJECTILE_DENSE_POS(sim, env) ((sim)->projectiles.dense_pos)
#define PS_DROP_ACTIVE(sim, env) ((sim)->drops.active)
#define PS_DROP_DENSE(sim, env) ((sim)->drops.dense)
#define PS_DROP_DENSE_POS(sim, env) ((sim)->drops.dense_pos)
#define PS_AREA_ACTIVE(sim, env) ((sim)->areas.active)
#define PS_AREA_DENSE(sim, env) ((sim)->areas.dense)
#define PS_AREA_DENSE_POS(sim, env) ((sim)->areas.dense_pos)
#define PS_MOVING_ACTIVE(sim, env) ((sim)->moving_obstacles.active)
#define PS_MOVING_DENSE(sim, env) ((sim)->moving_obstacles.dense)
#define PS_MOVING_DENSE_POS(sim, env) ((sim)->moving_obstacles.dense_pos)
#define PS_GRID(sim, env, cell) ((sim)->grid_head[cell])
#define PS_GRID_TOUCHED(sim, env, i) ((sim)->grid_touched[i])
#define PS_AABB(sim, env, i) ((sim)->aabb_indices[i])
#endif

#define PS_W(sim, env, i, field) PS_AT(sim, env, (sim)->field, i)

#define PS_DENSE(sim, env, ptr, i) PS_AT(sim, env, ptr, i)
#define PS_DENSE_POS(sim, env, ptr, i) PS_AT(sim, env, ptr, i)

PS_SIM_FN void ps_clear_entities(PSSim* sim, int env);

PS_SIM_FN float ps_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

PS_SIM_FN float ps_dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

PS_SIM_FN uint32_t ps_rand_u32(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    uint32_t x = PS_P(sim, env, rng) ? PS_P(sim, env, rng) : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    PS_P(sim, env, rng) = x ? x : 1u;
    return PS_P(sim, env, rng);
}

PS_SIM_FN float ps_randf(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    return (float)(ps_rand_u32(sim, env) & 0x00ffffffu) / 16777216.0f;
}

PS_SIM_FN int ps_cell(PSSim* sim, int env, float x, float y) {
    (void)sim;
    (void)env;
    float half = 0.5f * sim->cfg.arena_size;
    int gx = (int)((((x - PS_P(sim, env, px)) + half) / sim->cfg.arena_size) * (float)PS_GRID_W);
    int gy = (int)((((y - PS_P(sim, env, py)) + half) / sim->cfg.arena_size) * (float)PS_GRID_H);
    gx = gx < 0 ? 0 : (gx >= PS_GRID_W ? PS_GRID_W - 1 : gx);
    gy = gy < 0 ? 0 : (gy >= PS_GRID_H ? PS_GRID_H - 1 : gy);
    return gy * PS_GRID_W + gx;
}

#ifndef __CUDACC__
PS_SIM_FN void ps_clear_entities(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    memset(&sim->enemies, 0, sizeof(sim->enemies));
    memset(&sim->projectiles, 0, sizeof(sim->projectiles));
    memset(&sim->drops, 0, sizeof(sim->drops));
    memset(&sim->areas, 0, sizeof(sim->areas));
    memset(&sim->moving_obstacles, 0, sizeof(sim->moving_obstacles));
    memset(sim->enemies.dense_pos, 0xff, sizeof(sim->enemies.dense_pos));
    memset(sim->projectiles.dense_pos, 0xff, sizeof(sim->projectiles.dense_pos));
    memset(sim->drops.dense_pos, 0xff, sizeof(sim->drops.dense_pos));
    memset(sim->areas.dense_pos, 0xff, sizeof(sim->areas.dense_pos));
    memset(sim->moving_obstacles.dense_pos, 0xff,
        sizeof(sim->moving_obstacles.dense_pos));
    PS_P(sim, env, enemy_count) = 0;
    PS_P(sim, env, projectile_count) = 0;
    PS_P(sim, env, drop_count) = 0;
    PS_P(sim, env, area_count) = 0;
    PS_P(sim, env, moving_obstacle_count) = 0;
    PS_P(sim, env, active_ink_count) = 0;
    PS_P(sim, env, next_enemy_slot) = 0;
    PS_P(sim, env, next_projectile_slot) = 0;
    PS_P(sim, env, next_drop_slot) = 0;
    PS_P(sim, env, next_area_slot) = 0;
    PS_P(sim, env, next_moving_obstacle_slot) = 0;
    PS_P(sim, env, nearest_enemy) = -1;
    PS_P(sim, env, nearest_enemy_d2) = 1e30f;
    for (int i = 0; i < PS_GRID_CELLS; i++) PS_GRID(sim, env, i) = -1;
    PS_P(sim, env, grid_touched_count) = 0;
    PS_P(sim, env, aabb_count) = 0;
}
#endif
PS_SIM_FN float ps_xp_threshold(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    return sim->cfg.xp_threshold_base
        + sim->cfg.xp_threshold_per_level * (float)(PS_P(sim, env, level) - 1);
}

PS_SIM_FN int ps_wave_index(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    return PS_P(sim, env, tick) / sim->cfg.wave_length_steps;
}

PS_SIM_FN float ps_episode_progress(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float max_steps = (float)sim->cfg.max_steps;
    float normal_steps = (float)sim->cfg.wave_length_steps
        * (float)sim->cfg.progress_normal_wave_count;
    float scale = fminf(max_steps, normal_steps);
    return ps_clampf((float)PS_P(sim, env, tick) / scale, 0.0f, 1.0f);
}

PS_SIM_FN float ps_weapon_cooldown_total(PSSim* sim, int env, int weapon) {
    (void)sim;
    (void)env;
    int level = PS_W(sim, env, weapon, weapon_level);
    if (level <= 0) return 1.0f;
    float cd = sim->cfg.weapon_base_cooldown[weapon]
        + sim->cfg.weapon_cooldown_per_level[weapon] * (float)(level - 1);
    cd *= PS_P(sim, env, cooldown_mult) * sim->cfg.fire_cooldown
        / fmaxf(sim->cfg.weapon_base_cooldown[PS_WEAPON_BUBBLE], 1.0f);
    return cd;
}

PS_SIM_FN float ps_weapon_power(PSSim* sim, int env, int weapon) {
    (void)sim;
    (void)env;
    int level = PS_W(sim, env, weapon, weapon_level);
    if (level <= 0) return 0.0f;
    float area = 1.0f + PS_P(sim, env, area_bonus);
    float might = sim->cfg.projectile_damage * (1.0f + PS_P(sim, env, damage_bonus));
    return ps_clampf(((float)level / (float)sim->cfg.weapon_max_level) * might * area,
        0.0f, 3.0f) / 3.0f;
}

PS_SIM_FN float ps_weapon_damage(PSSim* sim, int env, int weapon, int level, int first_level_zero) {
    (void)sim;
    (void)env;
    float level_delta = (float)(first_level_zero ? level - 1 : level);
    return (sim->cfg.weapon_base_damage[weapon]
        + sim->cfg.weapon_damage_per_level[weapon] * level_delta)
        * sim->cfg.projectile_damage * (1.0f + PS_P(sim, env, damage_bonus));
}

PS_SIM_FN int ps_upgrade_available(PSSim* sim, int env, int type) {
    (void)sim;
    (void)env;
    if (type >= PS_UPGRADE_BUBBLE && type <= PS_UPGRADE_SONAR) {
        return PS_W(sim, env, type, weapon_level) < sim->cfg.weapon_max_level;
    }
    return type >= 0 && type < PS_UPGRADE_COUNT;
}

PS_SIM_FN int ps_wave_minimum(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    int wave = ps_wave_index(sim, env);
    int base = wave < PS_WAVE_TABLE_COUNT
        ? sim->cfg.wave_minimum[wave]
        : sim->cfg.wave_tail_minimum_base
            + sim->cfg.wave_tail_minimum_step * (wave - PS_WAVE_TABLE_COUNT);
    float spawn_scale = ps_clampf(sim->cfg.enemy_spawn_rate
        / sim->cfg.wave_spawn_reference_rate,
        sim->cfg.wave_spawn_scale_min, sim->cfg.wave_spawn_scale_max);
    float progress = ps_episode_progress(sim, env);
    int scaled = (int)ceilf((float)base * spawn_scale
        * (1.0f + sim->cfg.wave_progress_spawn_scale * sim->cfg.spawn_ramp * progress));
    int cap = sim->cfg.wave_population_cap;
    return scaled > cap ? cap : scaled;
}

PS_SIM_FN int ps_wave_spawn_interval(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    int wave = ps_wave_index(sim, env);
    int base = wave < PS_WAVE_TABLE_COUNT
        ? sim->cfg.wave_interval[wave]
        : sim->cfg.wave_tail_interval;
    float spawn_scale = ps_clampf(sim->cfg.enemy_spawn_rate
        / sim->cfg.wave_spawn_reference_rate,
        sim->cfg.wave_spawn_scale_min, sim->cfg.wave_spawn_scale_max);
    int scaled = (int)ceilf((float)base / spawn_scale);
    return scaled < sim->cfg.wave_min_spawn_interval
        ? sim->cfg.wave_min_spawn_interval : scaled;
}
PS_SIM_FN int ps_obs_sector(float dx, float dy) {
    if (dy == 0.0f) return dx >= 0.0f ? 0 : 4;
    if (dx == 0.0f) return dy > 0.0f ? 2 : 6;

    if (dx > 0.0f) {
        if (dy > 0.0f) return dy < dx ? 0 : 1;
        return -dy > dx ? 6 : 7;
    }

    if (dy > 0.0f) return dy > -dx ? 2 : 3;
    return -dy < -dx ? 4 : 5;
}

PS_SIM_FN int ps_obs_ring_d2(float d2, float observe_radius2) {
    // Bias resolution toward the immediate dodge space. With the default
    // radius these boundaries are approximately 3.2 and 8.2 world units.
    if (d2 < observe_radius2 * 0.0225f) return 0;
    if (d2 < observe_radius2 * 0.1444f) return 1;
    return PS_RINGS - 1;
}

// Soft normalization for nonnegative values without a hard maximum.
// value == half_scale maps to 0.5 and larger values approach 1.0 smoothly.
PS_SIM_FN float ps_obs_soft_norm(float value, float half_scale) {
    return value / (value + half_scale);
}

PS_SIM_FN void ps_compute_observations(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float* obs = (float*)PS_OBS(sim, env);
    memset(obs, 0, PS_OBS_SIZE * sizeof(float));

    int idx = 0;
    int wave_len = sim->cfg.wave_length_steps;
    int wave = ps_wave_index(sim, env);
    int boss_period = sim->cfg.boss_period_steps;

    // Player/global features. Unbounded counters use soft normalization rather
    // than clipping, so wave 30 remains distinguishable from wave 100.
    obs[idx++] = ps_clampf(PS_P(sim, env, hp) / PS_P(sim, env, max_hp), 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(PS_P(sim, env, hp), 4.0f);
    obs[idx++] = ps_obs_soft_norm(PS_P(sim, env, max_hp), 8.0f);
    obs[idx++] = ps_obs_soft_norm((float)PS_P(sim, env, level), 20.0f);
    obs[idx++] = ps_clampf(PS_P(sim, env, xp) / ps_xp_threshold(sim, env), 0.0f, 1.0f);
    obs[idx++] = (float)(PS_P(sim, env, tick) % wave_len) / (float)wave_len;
    obs[idx++] = ps_obs_soft_norm((float)(wave + 1), 12.0f);
    obs[idx++] = (float)(PS_P(sim, env, tick) % boss_period) / (float)boss_period;

    int visible_enemies_idx = idx++;
    int visible_drops_idx = idx++;
    int bubble_ready_idx = idx++;
    obs[bubble_ready_idx] = 1.0f - ps_clampf(PS_W(sim, env, PS_WEAPON_BUBBLE, weapon_cd)
        / ps_weapon_cooldown_total(sim, env, PS_WEAPON_BUBBLE), 0.0f, 1.0f);
    obs[idx++] = PS_P(sim, env, pending_upgrade) ? 1.0f : 0.0f;
    obs[idx++] = sim->cfg.invuln_steps > 0 ? ps_clampf((float)PS_P(sim, env, invuln_timer) / (float)sim->cfg.invuln_steps, 0.0f, 1.0f) : 0.0f;
    obs[idx++] = ps_clampf((float)PS_P(sim, env, queued_upgrades) / 4.0f, 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(PS_P(sim, env, speed_bonus), 1.0f);
    obs[idx++] = ps_obs_soft_norm(PS_P(sim, env, damage_bonus), 1.0f);
    obs[idx++] = ps_clampf(1.0f - PS_P(sim, env, cooldown_mult), 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(PS_P(sim, env, magnet_bonus), 1.0f);
    obs[idx++] = ps_obs_soft_norm(PS_P(sim, env, area_bonus), 1.0f);
    obs[idx++] = ps_obs_soft_norm((float)PS_P(sim, env, pierce_bonus), 4.0f);
    int nearest_health_distance_idx = idx++;

    // XP routing hints. These make the policy's pickup objective explicit
    // instead of forcing it to infer nearest-orb direction from coarse bins.
    int nearest_xp_dx_idx = idx++;
    int nearest_xp_dy_idx = idx++;
    int nearest_xp_distance_idx = idx++;
    int visible_xp_value_idx = idx++;
    int visible_xp_can_level_idx = idx++;

    float observe_radius = sim->cfg.arena_size * 0.45f;
    float observe_radius2 = observe_radius * observe_radius;
    float inv_observe_radius = 1.0f / observe_radius;
    float inv_enemy_cap = 1.0f / (float)sim->cfg.enemy_cap;
    float inv_drop_cap = 1.0f / (float)sim->cfg.drop_cap;
    int boss_base = idx;
    idx += PS_BOSS_FEATURES;
    int enemy_base = idx;
    idx += PS_SECTORS * PS_RINGS * PS_ENEMY_CHANNELS;
    int drop_base = idx;
    idx += PS_SECTORS * PS_RINGS * PS_DROP_CHANNELS;
    int obstacle_base = idx;
    idx += PS_SECTORS * PS_RINGS * PS_OBSTACLE_CHANNELS;
    float obstacle_bin_nearest_d2[PS_SECTORS * PS_RINGS];
    for (int i = 0; i < PS_SECTORS * PS_RINGS; i++)
        obstacle_bin_nearest_d2[i] = 1e30f;
    int visible_enemies = 0;
    int visible_drops = 0;
    float nearest_xp_dx = 0.0f;
    float nearest_xp_dy = 0.0f;
    float nearest_xp_d2 = 1e30f;
    float visible_xp_value = 0.0f;
    float nearest_health_d2 = 1e30f;
    int nearest_boss = -1;
    int boss_count = 0;
    float nearest_boss_d2 = 1e30f;

    int scan_capacity = PS_P(sim, env, enemy_count) * 2 >= sim->cfg.enemy_cap;
    int scan_count = scan_capacity ? sim->cfg.enemy_cap : PS_P(sim, env, enemy_count);
    for (int k = 0; k < scan_count; k++) {
        int i = scan_capacity ? k : PS_ENEMY(sim, env, k, dense);

        if (PS_ENEMY(sim, env, i, type) & PS_ENEMY_BOSS_FLAG) {
            boss_count++;
            float boss_dx = PS_ENEMY(sim, env, i, x) - PS_P(sim, env, px);
            float boss_dy = PS_ENEMY(sim, env, i, y) - PS_P(sim, env, py);
            float boss_d2 = boss_dx * boss_dx + boss_dy * boss_dy;
            if (boss_d2 < nearest_boss_d2) {
                nearest_boss_d2 = boss_d2;
                nearest_boss = i;
            }
        }
        float dx = PS_ENEMY(sim, env, i, x) - PS_P(sim, env, px);
        float dy = PS_ENEMY(sim, env, i, y) - PS_P(sim, env, py);
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        float d = sqrtf(d2);
        visible_enemies++;
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = enemy_base + (ring * PS_SECTORS + sector) * PS_ENEMY_CHANNELS;
        float proximity = 1.0f - d * inv_observe_radius;
        obs[o + 0] = fminf(obs[o + 0] + 0.125f, 1.0f);
        obs[o + 1] = fmaxf(obs[o + 1], proximity);
        obs[o + 2] = fmaxf(obs[o + 2], PS_ENEMY(sim, env, i, hp) / PS_ENEMY(sim, env, i, max_hp));
        obs[o + 3] = fmaxf(obs[o + 3], PS_ENEMY(sim, env, i, damage) / 4.0f);
    }

    if (nearest_boss >= 0) {
        float dx = PS_ENEMY(sim, env, nearest_boss, x) - PS_P(sim, env, px);
        float dy = PS_ENEMY(sim, env, nearest_boss, y) - PS_P(sim, env, py);
        float d = sqrtf(fmaxf(nearest_boss_d2, 0.0001f));
        obs[boss_base + PS_BOSS_PRESENT] = 1.0f;
        obs[boss_base + PS_BOSS_DX] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
        obs[boss_base + PS_BOSS_DY] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
        obs[boss_base + PS_BOSS_PROXIMITY] = 1.0f - ps_clampf(d * inv_observe_radius, 0.0f, 1.0f);
        obs[boss_base + PS_BOSS_HP_FRACTION] = ps_clampf(PS_ENEMY(sim, env, nearest_boss, hp) / PS_ENEMY(sim, env, nearest_boss, max_hp), 0.0f, 1.0f);
        obs[boss_base + PS_BOSS_MAX_HP] = ps_obs_soft_norm(PS_ENEMY(sim, env, nearest_boss, max_hp), 96.0f);
        obs[boss_base + PS_BOSS_COUNT] = ps_obs_soft_norm((float)boss_count, 2.0f);
    }

    for (int k = 0; k < PS_P(sim, env, drop_count); k++) {
        int i = PS_DROP(sim, env, k, dense);
        float dx = PS_DROP(sim, env, i, x) - PS_P(sim, env, px);
        float dy = PS_DROP(sim, env, i, y) - PS_P(sim, env, py);
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        float d = sqrtf(d2);
        visible_drops++;
        if (PS_DROP(sim, env, i, type) == 0) {
            visible_xp_value += PS_DROP(sim, env, i, value);
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
        obs[o + 0] = fminf(obs[o + 0] + PS_DROP(sim, env, i, value) * 0.1f, 1.0f);
        obs[o + 1] = fmaxf(obs[o + 1], 1.0f - d * inv_observe_radius);
        obs[o + 2] = fmaxf(obs[o + 2], PS_DROP(sim, env, i, type) == 1 ? 1.0f : 0.0f);
    }

    if (nearest_xp_d2 < 1e29f) {
        float nearest_xp_dist = sqrtf(nearest_xp_d2);
        obs[nearest_xp_dx_idx] = ps_clampf(nearest_xp_dx * inv_observe_radius, -1.0f, 1.0f);
        obs[nearest_xp_dy_idx] = ps_clampf(nearest_xp_dy * inv_observe_radius, -1.0f, 1.0f);
        obs[nearest_xp_distance_idx] = 1.0f
            - ps_clampf(nearest_xp_dist * inv_observe_radius, 0.0f, 1.0f);
    }
    if (nearest_health_d2 < 1e29f) {
        float nearest_health_dist = sqrtf(nearest_health_d2);
        obs[nearest_health_distance_idx] = 1.0f
            - ps_clampf(nearest_health_dist * inv_observe_radius, 0.0f, 1.0f);
    }
    obs[visible_xp_value_idx] = ps_clampf(visible_xp_value / ps_xp_threshold(sim, env), 0.0f, 1.0f);
    obs[visible_xp_can_level_idx] = visible_xp_value >= fmaxf(ps_xp_threshold(sim, env) - PS_P(sim, env, xp), 0.0f) ? 1.0f : 0.0f;

    for (int i = 0; i < sim->cfg.obstacle_count; i++) {
        float dx = PS_OBSTACLE(sim, env, i, x) - PS_P(sim, env, px);
        float dy = PS_OBSTACLE(sim, env, i, y) - PS_P(sim, env, py);
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = obstacle_base + (ring * PS_SECTORS + sector) * PS_OBSTACLE_CHANNELS;
        int bin = ring * PS_SECTORS + sector;
        if (d2 < obstacle_bin_nearest_d2[bin]) {
            obstacle_bin_nearest_d2[bin] = d2;
            obs[o + 0] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
            obs[o + 1] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
        }
    }

    obs[visible_enemies_idx] = ps_clampf((float)visible_enemies * inv_enemy_cap, 0.0f, 1.0f);
    obs[visible_drops_idx] = ps_clampf((float)visible_drops * inv_drop_cap, 0.0f, 1.0f);

    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        int level = PS_W(sim, env, i, weapon_level);
        float cd_total = ps_weapon_cooldown_total(sim, env, i);
        float ready = level > 0 ? 1.0f - ps_clampf(PS_W(sim, env, i, weapon_cd) / cd_total, 0.0f, 1.0f) : 0.0f;
        obs[idx++] = (float)level / (float)sim->cfg.weapon_max_level;
        obs[idx++] = ready;
        obs[idx++] = ps_clampf(PS_W(sim, env, i, weapon_active), 0.0f, 1.0f);
        obs[idx++] = ps_weapon_power(sim, env, i);
    }

    if (PS_P(sim, env, pending_upgrade)) {
        for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
            obs[idx + PS_W(sim, env, i, offered)] = 1.0f;
            idx += PS_UPGRADE_FEATURES;
        }
    } else {
        idx += PS_UPGRADE_SLOTS * PS_UPGRADE_FEATURES;
    }

    // Moving hazards are presented as a stable set of nearest-distance slots,
    // rather than raw pool indices. Each slot is:
    // active, type (0 anchor/1 submarine), player-relative dx, player-relative dy.
    // The coordinates are translated into the player's frame and clipped to
    // the observation radius. Do not bake velocity, proximity, or collision
    // predictions into this: the policy gets the raw geometry and can learn
    // the consequences from the transition stream.
    int moving_idx[PS_MOVING_OBSTACLE_SLOTS];
    float moving_score[PS_MOVING_OBSTACLE_SLOTS];
    for (int s = 0; s < PS_MOVING_OBSTACLE_SLOTS; s++) {
        moving_idx[s] = -1;
        moving_score[s] = 1e30f;
    }
    for (int k = 0; k < PS_P(sim, env, moving_obstacle_count); k++) {
        int i = PS_MOVING(sim, env, k, dense);
        float dx = PS_MOVING(sim, env, i, x) - PS_P(sim, env, px);
        float dy = PS_MOVING(sim, env, i, y) - PS_P(sim, env, py);
        float d2 = dx * dx + dy * dy;
        for (int s = 0; s < PS_MOVING_OBSTACLE_SLOTS; s++) {
            if (d2 >= moving_score[s]) continue;
            for (int j = PS_MOVING_OBSTACLE_SLOTS - 1; j > s; j--) {
                moving_score[j] = moving_score[j - 1];
                moving_idx[j] = moving_idx[j - 1];
            }
            moving_score[s] = d2;
            moving_idx[s] = i;
            break;
        }
    }
    for (int s = 0; s < PS_MOVING_OBSTACLE_SLOTS; s++) {
        int o = PS_OBS_MOVING_OBSTACLE_BASE + s * PS_MOVING_OBSTACLE_FEATURES;
        int i = moving_idx[s];
        if (i < 0) continue;
        float dx = PS_MOVING(sim, env, i, x) - PS_P(sim, env, px);
        float dy = PS_MOVING(sim, env, i, y) - PS_P(sim, env, py);
        obs[o + 0] = 1.0f;
        obs[o + 1] = PS_MOVING(sim, env, i, type) == PS_MOVING_OBSTACLE_SUBMARINE ? 1.0f : 0.0f;
        obs[o + 2] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
        obs[o + 3] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
    }
}
PS_SIM_FN int ps_obstacle_position_clear(PSSim* sim, int env, int count, int skip, float x, float y, float radius) {
    (void)sim;
    (void)env;
    if (ps_dist2(x, y, PS_P(sim, env, px), PS_P(sim, env, py))
            < sim->cfg.obstacle_player_spawn_clearance
            * sim->cfg.obstacle_player_spawn_clearance) return 0;
    for (int i = 0; i < count; i++) {
        if (i == skip) continue;
        float min_dist = radius + PS_OBSTACLE(sim, env, i, radius) + sim->cfg.obstacle_spawn_clearance;
        float dx = x - PS_OBSTACLE(sim, env, i, x);
        float dy = y - PS_OBSTACLE(sim, env, i, y);
        if (ps_geometry_circle_overlaps(dx, dy, min_dist)) return 0;
    }
    return 1;
}

PS_SIM_FN int ps_overlaps_obstacle(PSSim* sim, int env, float x, float y, float radius, float padding) {
    (void)sim;
    (void)env;
    for (int i = 0; i < sim->cfg.obstacle_count; i++) {
        float min_dist = radius + PS_OBSTACLE(sim, env, i, radius) + padding;
        float dx = x - PS_OBSTACLE(sim, env, i, x);
        float dy = y - PS_OBSTACLE(sim, env, i, y);
        if (ps_geometry_circle_overlaps(dx, dy, min_dist)) return 1;
    }
    return 0;
}

PS_SIM_FN void ps_push_out_obstacles(PSSim* sim, int env, float* x, float* y, float radius, int penalize) {
    (void)sim;
    (void)env;
    for (int i = 0; i < sim->cfg.obstacle_count; i++) {
        int pushed = ps_geometry_push_out_shape_circle(x, y, PS_SHAPE_CIRCLE,
            radius, radius, radius, PS_OBSTACLE(sim, env, i, x), PS_OBSTACLE(sim, env, i, y),
            PS_OBSTACLE(sim, env, i, radius));
        if (pushed && penalize) {
            PS_REWARD(sim, env) += sim->cfg.obstacle_penalty;
            PS_P(sim, env, episode_reward_obstacle) += sim->cfg.obstacle_penalty;
            PS_P(sim, env, episode_obstacle_hits) += 1.0f;
        }
    }
}

PS_SIM_FN void ps_push_out_obstacles_shape(PSSim* sim, int env,
        float* x, float* y, int shape, float radius, float half_width,
        float half_height, int penalize) {
    (void)sim;
    (void)env;
    for (int i = 0; i < sim->cfg.obstacle_count; i++) {
        int pushed = ps_geometry_push_out_shape_circle(x, y, shape, radius,
            half_width, half_height, PS_OBSTACLE(sim, env, i, x), PS_OBSTACLE(sim, env, i, y),
            PS_OBSTACLE(sim, env, i, radius));
        if (pushed && penalize) {
            PS_REWARD(sim, env) += sim->cfg.obstacle_penalty;
            PS_P(sim, env, episode_reward_obstacle) += sim->cfg.obstacle_penalty;
            PS_P(sim, env, episode_obstacle_hits) += 1.0f;
        }
    }
}

PS_SIM_FN void ps_spawn_obstacles(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float half = 0.5f * sim->cfg.arena_size;
    for (int i = 0; i < sim->cfg.obstacle_count; i++) {
        PS_OBSTACLE(sim, env, i, radius) = sim->cfg.obstacle_radius_min
            + ps_randf(sim, env) * (sim->cfg.obstacle_radius_max - sim->cfg.obstacle_radius_min);
        PS_OBSTACLE(sim, env, i, type) = (uint8_t)(ps_rand_u32(sim, env) % 3u);
        int placed = 0;
        for (int tries = 0; tries < 96; tries++) {
            float angle = ps_randf(sim, env) * 2.0f * PI;
            float dist = half * (sim->cfg.obstacle_spawn_min_ratio
                + (sim->cfg.obstacle_spawn_max_ratio - sim->cfg.obstacle_spawn_min_ratio)
                * ps_randf(sim, env));
            float x = PS_P(sim, env, px) + cosf(angle) * dist;
            float y = PS_P(sim, env, py) + sinf(angle) * dist;
            if (!ps_obstacle_position_clear(sim, env, i, -1, x, y, PS_OBSTACLE(sim, env, i, radius))) continue;
            PS_OBSTACLE(sim, env, i, x) = x;
            PS_OBSTACLE(sim, env, i, y) = y;
            placed = 1;
            break;
        }
        if (!placed) {
            float a = (float)i * sim->cfg.obstacle_fallback_angle_step
                + ps_randf(sim, env) * sim->cfg.obstacle_fallback_angle_jitter;
            float r = half * (sim->cfg.obstacle_fallback_min_ratio
                + (sim->cfg.obstacle_fallback_max_ratio - sim->cfg.obstacle_fallback_min_ratio)
                * ((float)(i % sim->cfg.obstacle_fallback_spoke_count)
                / (float)(sim->cfg.obstacle_fallback_spoke_count - 1)));
            PS_OBSTACLE(sim, env, i, x) = PS_P(sim, env, px) + cosf(a) * r;
            PS_OBSTACLE(sim, env, i, y) = PS_P(sim, env, py) + sinf(a) * r;
        }
    }
}

PS_SIM_FN void ps_recycle_obstacle(PSSim* sim, int env, int idx) {
    (void)sim;
    (void)env;
    float half = 0.5f * sim->cfg.arena_size;
    for (int tries = 0; tries < 64; tries++) {
        float angle = ps_randf(sim, env) * 2.0f * PI;
        float dist = half * (sim->cfg.obstacle_recycle_spawn_min_ratio
            + (sim->cfg.obstacle_recycle_spawn_max_ratio
            - sim->cfg.obstacle_recycle_spawn_min_ratio) * ps_randf(sim, env));
        float x = PS_P(sim, env, px) + cosf(angle) * dist;
        float y = PS_P(sim, env, py) + sinf(angle) * dist;
        if (!ps_obstacle_position_clear(sim, env, sim->cfg.obstacle_count, idx, x, y, PS_OBSTACLE(sim, env, idx, radius))) continue;
        PS_OBSTACLE(sim, env, idx, x) = x;
        PS_OBSTACLE(sim, env, idx, y) = y;
        PS_OBSTACLE(sim, env, idx, type) = (uint8_t)(ps_rand_u32(sim, env) % 3u);
        return;
    }
}

PS_SIM_FN void ps_recycle_far_obstacles(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float recycle_radius = sim->cfg.arena_size * sim->cfg.obstacle_recycle_radius_ratio;
    float recycle2 = recycle_radius * recycle_radius;
    for (int i = 0; i < sim->cfg.obstacle_count; i++) {
        if (ps_dist2(PS_OBSTACLE(sim, env, i, x), PS_OBSTACLE(sim, env, i, y), PS_P(sim, env, px), PS_P(sim, env, py)) > recycle2) {
            ps_recycle_obstacle(sim, env, i);
        }
    }
}

PS_SIM_FN int ps_find_free_slot(PSSim* sim, int env, uint8_t* active, int cap, int* cursor);
PS_SIM_FN void ps_dense_add(PSSim* sim, int env, int* dense, int* dense_pos, int* count, int slot);
PS_SIM_FN void ps_deactivate_moving_obstacle(PSSim* sim, int env, int i);

PS_SIM_FN int ps_spawn_moving_obstacle(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    if (sim->cfg.moving_obstacle_cap <= 0
            || PS_P(sim, env, moving_obstacle_count) >= sim->cfg.moving_obstacle_cap) return 0;
    int i = ps_find_free_slot(sim, env, PS_MOVING_ACTIVE(sim, env),
        PS_MAX_MOVING_OBSTACLES, &PS_P(sim, env, next_moving_obstacle_slot));

    int type = (int)(ps_rand_u32(sim, env) % PS_MOVING_OBSTACLE_TYPE_COUNT);
    float half = 0.5f * sim->cfg.arena_size;
    float margin = sim->cfg.moving_obstacle_spawn_margin;
    float hw = sim->cfg.moving_obstacle_half_width[type];
    float hh = sim->cfg.moving_obstacle_half_height[type];
    float speed = sim->cfg.moving_obstacle_speed[type];
    PS_MOVING(sim, env, i, type) = (uint8_t)type;
    PS_MOVING(sim, env, i, shape) = PS_SHAPE_AABB;
    PS_MOVING(sim, env, i, half_width) = hw;
    PS_MOVING(sim, env, i, half_height) = hh;
    PS_MOVING(sim, env, i, bound_radius) = ps_geometry_shape_bound_radius(
        PS_SHAPE_AABB, 0.0f, hw, hh);
    PS_MOVING(sim, env, i, ttl) = sim->cfg.moving_obstacle_ttl;
    if (type == PS_MOVING_OBSTACLE_ANCHOR) {
        PS_MOVING(sim, env, i, x) = PS_P(sim, env, px)
            + (ps_randf(sim, env) * 2.0f - 1.0f) * half * 0.72f;
        PS_MOVING(sim, env, i, y) = PS_P(sim, env, py) - half - margin - hh;
        PS_MOVING(sim, env, i, vx) = 0.0f;
        PS_MOVING(sim, env, i, vy) = speed;
    } else {
        int from_left = (int)(ps_rand_u32(sim, env) & 1u) == 0;
        PS_MOVING(sim, env, i, x) = PS_P(sim, env, px)
            + (from_left ? -1.0f : 1.0f) * (half + margin + hw);
        PS_MOVING(sim, env, i, y) = PS_P(sim, env, py)
            + (ps_randf(sim, env) * 2.0f - 1.0f) * half * 0.72f;
        PS_MOVING(sim, env, i, vx) = (from_left ? 1.0f : -1.0f) * speed;
        PS_MOVING(sim, env, i, vy) = 0.0f;
    }
    PS_MOVING(sim, env, i, active) = 1;
    ps_dense_add(sim, env, PS_MOVING_DENSE(sim, env),
        PS_MOVING_DENSE_POS(sim, env), &PS_P(sim, env, moving_obstacle_count), i);
    return 1;
}

PS_SIM_FN void ps_update_moving_obstacles(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    int wave = ps_wave_index(sim, env);
    if (wave >= sim->cfg.moving_obstacle_start_wave
            && PS_P(sim, env, tick) % sim->cfg.moving_obstacle_spawn_interval == 0) {
        ps_spawn_moving_obstacle(sim, env);
    }

    float half = 0.5f * sim->cfg.arena_size;
    float cleanup = half + sim->cfg.moving_obstacle_spawn_margin + half;
    float cleanup2 = cleanup * cleanup;
    int k = 0;
    while (k < PS_P(sim, env, moving_obstacle_count)) {
        int i = PS_MOVING(sim, env, k, dense);
        PS_MOVING(sim, env, i, x) += PS_MOVING(sim, env, i, vx);
        PS_MOVING(sim, env, i, y) += PS_MOVING(sim, env, i, vy);
        PS_MOVING(sim, env, i, ttl)--;
        float dx = PS_P(sim, env, px) - PS_MOVING(sim, env, i, x);
        float dy = PS_P(sim, env, py) - PS_MOVING(sim, env, i, y);
        int outside = dx * dx + dy * dy > cleanup2;
        if (PS_MOVING(sim, env, i, ttl) <= 0 || outside) {
            ps_deactivate_moving_obstacle(sim, env, i);
            continue;
        }
        if (PS_P(sim, env, invuln_timer) <= 0 && sim->cfg.moving_obstacle_damage > 0.0f
                && ps_geometry_shape_overlaps_circle(
                    PS_MOVING(sim, env, i, shape),
                    dx, dy, PS_MOVING(sim, env, i, bound_radius),
                    PS_MOVING(sim, env, i, half_width),
                    PS_MOVING(sim, env, i, half_height),
                    sim->cfg.player_radius)) {
            float damage = fmaxf(1.0f, ceilf(sim->cfg.moving_obstacle_damage));
            PS_P(sim, env, hp) -= damage;
            PS_REWARD(sim, env) += sim->cfg.reward_hurt * damage;
            PS_P(sim, env, episode_reward_hurt) += sim->cfg.reward_hurt * damage;
            PS_P(sim, env, episode_damage_taken) += damage;
            PS_P(sim, env, invuln_timer) = sim->cfg.invuln_steps;
        }
        k++;
    }
}

PS_SIM_FN int ps_find_free_slot(PSSim* sim, int env, uint8_t* active, int cap, int* cursor) {
    (void)sim;
    (void)env;
    int start = *cursor;
    for (int tries = 0; tries < cap; tries++) {
        int i = start + tries;
        if (i >= cap) i -= cap;

        if (!PS_AT(sim, env, active, i)) {
            int next = i + 1;
            *cursor = next >= cap ? 0 : next;
            return i;
        }
    }

    return -1;
}

PS_SIM_FN void ps_dense_add(PSSim* sim, int env, int* dense, int* dense_pos, int* count, int slot) {
    (void)sim;
    (void)env;
    int pos = *count;
    PS_DENSE(sim, env, dense, pos) = slot;
    PS_DENSE_POS(sim, env, dense_pos, slot) = pos;
    *count = pos + 1;
}

PS_SIM_FN void ps_dense_remove(PSSim* sim, int env, int* dense, int* dense_pos, int* count, int slot) {
    (void)sim;
    (void)env;
    int pos = PS_DENSE_POS(sim, env, dense_pos, slot);
    int last_pos = *count - 1;
    int moved = PS_DENSE(sim, env, dense, last_pos);
    PS_DENSE(sim, env, dense, pos) = moved;
    PS_DENSE_POS(sim, env, dense_pos, moved) = pos;
    PS_DENSE_POS(sim, env, dense_pos, slot) = -1;
    *count = last_pos;
}

PS_SIM_FN void ps_deactivate_enemy(PSSim* sim, int env, int i) {
    (void)sim;
    (void)env;
    PS_ENEMY(sim, env, i, active) = 0;
    ps_dense_remove(sim, env, PS_ENEMY_DENSE(sim, env), PS_ENEMY_DENSE_POS(sim, env), &PS_P(sim, env, enemy_count), i);
}

PS_SIM_FN void ps_deactivate_projectile(PSSim* sim, int env, int i) {
    (void)sim;
    (void)env;
    PS_PROJECTILE(sim, env, i, active) = 0;
    ps_dense_remove(sim, env, PS_PROJECTILE_DENSE(sim, env), PS_PROJECTILE_DENSE_POS(sim, env), &PS_P(sim, env, projectile_count), i);
}

PS_SIM_FN void ps_deactivate_drop(PSSim* sim, int env, int i) {
    (void)sim;
    (void)env;
    PS_DROP(sim, env, i, active) = 0;
    ps_dense_remove(sim, env, PS_DROP_DENSE(sim, env), PS_DROP_DENSE_POS(sim, env), &PS_P(sim, env, drop_count), i);
}

PS_SIM_FN void ps_deactivate_area(PSSim* sim, int env, int i) {
    (void)sim;
    (void)env;
    PS_AREA(sim, env, i, active) = 0;
    if (PS_AREA(sim, env, i, type) == PS_WEAPON_INK) PS_P(sim, env, active_ink_count)--;
    ps_dense_remove(sim, env, PS_AREA_DENSE(sim, env), PS_AREA_DENSE_POS(sim, env), &PS_P(sim, env, area_count), i);
}

PS_SIM_FN void ps_deactivate_moving_obstacle(PSSim* sim, int env, int i) {
    (void)sim;
    (void)env;
    PS_MOVING(sim, env, i, active) = 0;
    ps_dense_remove(sim, env, PS_MOVING_DENSE(sim, env),
        PS_MOVING_DENSE_POS(sim, env), &PS_P(sim, env, moving_obstacle_count), i);
}

#ifdef PS_DEBUG_COUNTS
PS_SIM_FN void ps_verify_counts(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    int enemies = 0;
    int projectiles = 0;
    int drops = 0;
    int areas = 0;

    for (int i = 0; i < sim->cfg.enemy_cap; i++) {
        enemies += PS_ENEMY(sim, env, i, active) ? 1 : 0;
    }

    for (int i = 0; i < sim->cfg.projectile_cap; i++) {
        projectiles += PS_PROJECTILE(sim, env, i, active) ? 1 : 0;
    }

    for (int i = 0; i < sim->cfg.drop_cap; i++) {
        drops += PS_DROP(sim, env, i, active) ? 1 : 0;
    }

    for (int i = 0; i < PS_MAX_AREAS; i++) {
        areas += PS_AREA(sim, env, i, active) ? 1 : 0;
    }

    if (
        enemies != PS_P(sim, env, enemy_count) ||
        projectiles != PS_P(sim, env, projectile_count) ||
        drops != PS_P(sim, env, drop_count) ||
        areas != PS_P(sim, env, area_count)
    ) {
        fprintf(stderr,
            "count mismatch: enemies %d/%d projectiles %d/%d drops %d/%d areas %d/%d\n",
            enemies, PS_P(sim, env, enemy_count),
            projectiles, PS_P(sim, env, projectile_count),
            drops, PS_P(sim, env, drop_count),
            areas, PS_P(sim, env, area_count)
        );
        abort();
    }
}
#endif

PS_SIM_FN void ps_offer_upgrades(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    if (PS_P(sim, env, pending_upgrade)) return;
    PS_P(sim, env, pending_upgrade) = 1;
    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int offer = -1;
        for (;;) {
            int candidate = (int)(ps_rand_u32(sim, env) % PS_UPGRADE_COUNT);
            int duplicate = 0;
            for (int j = 0; j < i; j++) duplicate |= PS_W(sim, env, j, offered) == candidate;
            if (!duplicate && ps_upgrade_available(sim, env, candidate)) {
                offer = candidate;
                break;
            }
        }
        PS_W(sim, env, i, offered) = offer;
    }
}

PS_SIM_FN void ps_apply_upgrade_effect(PSSim* sim, int env, int upgrade) {
    switch (upgrade) {
    (void)sim;
    (void)env;
        case PS_UPGRADE_BUBBLE:
        case PS_UPGRADE_WHIRLPOOL:
        case PS_UPGRADE_ORBIT:
        case PS_UPGRADE_INK:
        case PS_UPGRADE_SONAR:
            if (PS_W(sim, env, upgrade, weapon_level) < sim->cfg.weapon_max_level)
                PS_W(sim, env, upgrade, weapon_level)++;
            break;
        case PS_UPGRADE_SPEED: PS_P(sim, env, speed_bonus) += sim->cfg.upgrade_speed_bonus; break;
        case PS_UPGRADE_MAGNET: PS_P(sim, env, magnet_bonus) += sim->cfg.upgrade_magnet_bonus; break;
        case PS_UPGRADE_HEALTH:
            PS_P(sim, env, max_hp) += sim->cfg.upgrade_health_bonus;
            PS_P(sim, env, hp) = fminf(PS_P(sim, env, max_hp), PS_P(sim, env, hp) + sim->cfg.health_heal);
            break;
        case PS_UPGRADE_MIGHT: PS_P(sim, env, damage_bonus) += sim->cfg.upgrade_might_bonus; break;
        case PS_UPGRADE_COOLDOWN: PS_P(sim, env, cooldown_mult) *= sim->cfg.upgrade_cooldown_multiplier; break;
        case PS_UPGRADE_AREA: PS_P(sim, env, area_bonus) += sim->cfg.upgrade_area_bonus; break;
        case PS_UPGRADE_PIERCE: PS_P(sim, env, pierce_bonus) += 1; break;
    }
}

PS_SIM_FN void ps_apply_upgrade(PSSim* sim, int env, int choice) {
    (void)sim;
    (void)env;
    int upgrade = PS_W(sim, env, choice, offered);
    ps_apply_upgrade_effect(sim, env, upgrade);
    PS_P(sim, env, episode_levelups) += 1.0f;
    PS_P(sim, env, pending_upgrade) = 0;
    if (PS_P(sim, env, queued_upgrades) > 0) PS_P(sim, env, queued_upgrades)--;
    if (PS_P(sim, env, queued_upgrades) > 0) ps_offer_upgrades(sim, env);
}

PS_SIM_FN void ps_add_log(PSSim* sim, int env, int survived) {
    (void)sim;
    (void)env;
    float perf = ps_clampf((float)PS_P(sim, env, tick) / (float)sim->cfg.max_steps, 0.0f, 1.0f);
    PS_LOG(sim, env).perf += perf;
    PS_LOG(sim, env).score += PS_P(sim, env, episode_score);
    PS_LOG(sim, env).episode_return += PS_P(sim, env, episode_return);
    PS_LOG(sim, env).reward_survival += PS_P(sim, env, episode_reward_survival);
    PS_LOG(sim, env).reward_damage += PS_P(sim, env, episode_reward_damage);
    PS_LOG(sim, env).reward_kill += PS_P(sim, env, episode_reward_kill);
    PS_LOG(sim, env).reward_hurt += PS_P(sim, env, episode_reward_hurt);
    PS_LOG(sim, env).reward_pickup += PS_P(sim, env, episode_reward_pickup);
    PS_LOG(sim, env).reward_xp += PS_P(sim, env, episode_reward_xp);
    PS_LOG(sim, env).reward_levelup += PS_P(sim, env, episode_reward_levelup);
    PS_LOG(sim, env).reward_obstacle += PS_P(sim, env, episode_reward_obstacle);
    PS_LOG(sim, env).reward_terminal += PS_P(sim, env, episode_reward_terminal);
    PS_LOG(sim, env).episode_length += (float)PS_P(sim, env, tick);
    PS_LOG(sim, env).kills += PS_P(sim, env, episode_kills);
    PS_LOG(sim, env).level += (float)PS_P(sim, env, level);
    PS_LOG(sim, env).xp += PS_P(sim, env, episode_xp);
    PS_LOG(sim, env).damage_dealt += PS_P(sim, env, episode_damage_dealt);
    PS_LOG(sim, env).damage_taken += PS_P(sim, env, episode_damage_taken);
    PS_LOG(sim, env).pickups += PS_P(sim, env, episode_pickups);
    PS_LOG(sim, env).levelups += PS_P(sim, env, episode_levelups);
    PS_LOG(sim, env).obstacle_hits += PS_P(sim, env, episode_obstacle_hits);
    PS_LOG(sim, env).enemies_alive += (float)PS_P(sim, env, enemy_count);
    PS_LOG(sim, env).projectiles_alive += (float)PS_P(sim, env, projectile_count);
    PS_LOG(sim, env).drops_alive += (float)PS_P(sim, env, drop_count);
    PS_LOG(sim, env).areas_alive += (float)PS_P(sim, env, area_count);
    int weapon_levels = 0;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) weapon_levels += PS_W(sim, env, i, weapon_level);
    PS_LOG(sim, env).weapon_levels += (float)weapon_levels;
    PS_LOG(sim, env).wave += (float)(ps_wave_index(sim, env) + 1);
    PS_LOG(sim, env).hp += PS_P(sim, env, hp);
    PS_LOG(sim, env).survived += (float)survived;
    PS_LOG(sim, env).n += 1.0f;
    PS_LOG(sim, env).peak_enemies += PS_P(sim, env, episode_peak_enemies);
    PS_LOG(sim, env).peak_projectiles += PS_P(sim, env, episode_peak_projectiles);
    PS_LOG(sim, env).min_hp += PS_P(sim, env, episode_min_hp);
    if (survived) {
        PS_LOG(sim, env).success += 1.0f;
    } else {
        float progress = (float)PS_P(sim, env, tick) / (float)sim->cfg.max_steps;
        if (progress < 0.25f) PS_LOG(sim, env).death_0_25 += 1.0f;
        else if (progress < 0.50f) PS_LOG(sim, env).death_25_50 += 1.0f;
        else if (progress < 0.75f) PS_LOG(sim, env).death_50_75 += 1.0f;
        else PS_LOG(sim, env).death_75_100 += 1.0f;
    }
}

PS_SIM_FN int ps_pick_spawn_side(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float m2 = PS_P(sim, env, pvx) * PS_P(sim, env, pvx) + PS_P(sim, env, pvy) * PS_P(sim, env, pvy);
    if (m2 > 0.0001f && ps_randf(sim, env) < sim->cfg.spawn_velocity_bias_probability) {
        if (fabsf(PS_P(sim, env, pvx)) > fabsf(PS_P(sim, env, pvy))) return PS_P(sim, env, pvx) > 0.0f ? 1 : 0;
        return PS_P(sim, env, pvy) > 0.0f ? 3 : 2;
    }
    return (int)(ps_rand_u32(sim, env) & 3u);
}

PS_SIM_FN void ps_pick_spawn_position(PSSim* sim, int env, float radius, float* x, float* y) {
    (void)sim;
    (void)env;
    float half = 0.5f * sim->cfg.arena_size;
    float edge = half + radius + sim->cfg.enemy_spawn_edge_margin;
    float along = (ps_randf(sim, env) * 2.0f - 1.0f) * half * sim->cfg.enemy_spawn_along_ratio;
    int side = ps_pick_spawn_side(sim, env);
    if (side == 0) {
        *x = PS_P(sim, env, px) - edge;
        *y = PS_P(sim, env, py) + along;
    } else if (side == 1) {
        *x = PS_P(sim, env, px) + edge;
        *y = PS_P(sim, env, py) + along;
    } else if (side == 2) {
        *x = PS_P(sim, env, px) + along;
        *y = PS_P(sim, env, py) - edge;
    } else {
        *x = PS_P(sim, env, px) + along;
        *y = PS_P(sim, env, py) + edge;
    }
}

PS_SIM_FN PSEnemyDef ps_enemy_stats(PSSim* sim, int env, int* kind_out,
        float* radius_out, int elite, int boss, int ari_k) {
    (void)sim;
    (void)env;
    int wave = ps_wave_index(sim, env);
    float progress = ps_episode_progress(sim, env);
    int kind = 0;
    if (wave >= sim->cfg.enemy_mix_start_wave) {
        uint32_t roll = ps_rand_u32(sim, env) % 100u;
        if (wave < sim->cfg.enemy_mix_phase_one_end_wave) {
            kind = roll < (uint32_t)sim->cfg.enemy_mix_phase_one_jelly_pct ? 1 : 0;
        } else if (wave < sim->cfg.enemy_mix_phase_two_end_wave) {
            kind = roll < (uint32_t)sim->cfg.enemy_mix_phase_two_urchin_pct
                ? 2
                : (roll < (uint32_t)sim->cfg.enemy_mix_phase_two_jelly_pct ? 1 : 0);
        } else {
            kind = roll < (uint32_t)sim->cfg.enemy_mix_late_urchin_pct
                ? 2
                : (roll < (uint32_t)sim->cfg.enemy_mix_late_eel_pct
                    ? 3
                    : (roll < (uint32_t)sim->cfg.enemy_mix_late_jelly_pct ? 1 : 0));
        }
    }

    PSEnemyDef stats = {
        sim->cfg.enemy_base_hp[kind],
        sim->cfg.enemy_speed_mult[kind],
        sim->cfg.enemy_base_damage[kind],
    };
    float radius = sim->cfg.enemy_radius[kind];
    float hp_growth = 1.0f + sim->cfg.enemy_hp_growth_per_wave * (float)wave
        + sim->cfg.enemy_hp_progress_scale * progress * sim->cfg.spawn_ramp;
    float speed_growth_wave = (float)(wave < sim->cfg.enemy_speed_growth_wave_cap
        ? wave : sim->cfg.enemy_speed_growth_wave_cap);
    float speed_growth = 1.0f + sim->cfg.enemy_speed_growth_per_wave * speed_growth_wave;
    stats.hp *= hp_growth * sim->cfg.enemy_hp_scale;
    stats.speed_mult *= sim->cfg.enemy_speed * speed_growth;
    stats.damage *= sim->cfg.enemy_damage_scale;

    if (elite) {
        stats.hp *= sim->cfg.elite_hp_multiplier;
        stats.speed_mult *= sim->cfg.elite_speed_multiplier;
        radius = fmaxf(radius + sim->cfg.elite_radius_bonus, sim->cfg.elite_min_radius);
        stats.damage *= sim->cfg.elite_damage_multiplier;
    }
    if (boss) {
        stats.hp = (sim->cfg.boss_hp_base + sim->cfg.boss_hp_per_wave * (float)wave)
            * sim->cfg.enemy_hp_scale;
        stats.speed_mult = sim->cfg.enemy_speed * sim->cfg.boss_speed_multiplier;
        radius = sim->cfg.boss_radius;
        stats.damage = sim->cfg.boss_damage * sim->cfg.enemy_damage_scale;
    }
    if (ari_k) {
        stats.hp *= sim->cfg.ari_k_hp_multiplier;
        stats.speed_mult = sim->cfg.enemy_speed * sim->cfg.ari_k_speed_multiplier;
        radius = sim->cfg.ari_k_radius;
        stats.damage = sim->cfg.ari_k_damage * sim->cfg.enemy_damage_scale;
    }

    stats.hp = fmaxf(1.0f, ceilf(stats.hp));
    stats.damage = fmaxf(1.0f, ceilf(stats.damage));
    *kind_out = kind;
    *radius_out = radius;
    return stats;
}

PS_SIM_FN void ps_enemy_geometry(PSSim* sim, int env, int kind, int ari_k,
        float radius, int* shape, float* half_width, float* half_height,
        float* bound_radius) {
    (void)sim;
    (void)env;
    *shape = ari_k ? sim->cfg.ari_k_shape : sim->cfg.enemy_shape[kind];
    *half_width = ari_k ? sim->cfg.ari_k_half_width : sim->cfg.enemy_half_width[kind];
    *half_height = ari_k ? sim->cfg.ari_k_half_height : sim->cfg.enemy_half_height[kind];
    *bound_radius = ps_geometry_shape_bound_radius(*shape, radius,
        *half_width, *half_height);
}

PS_SIM_FN int ps_spawn_enemy(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    if (PS_P(sim, env, enemy_count) >= sim->cfg.enemy_cap) return 0;

    int slot = ps_find_free_slot(sim, env, PS_ENEMY_ACTIVE(sim, env), sim->cfg.enemy_cap, &PS_P(sim, env, next_enemy_slot));

    float x = 0.0f, y = 0.0f;
    ps_pick_spawn_position(sim, env, sim->cfg.enemy_spawn_radius, &x, &y);
    for (int tries = 0; tries < 16 && ps_overlaps_obstacle(sim, env, x, y,
            sim->cfg.enemy_spawn_radius, sim->cfg.enemy_spawn_padding); tries++) {
        ps_pick_spawn_position(sim, env, sim->cfg.enemy_spawn_radius, &x, &y);
    }

    int elite = ps_randf(sim, env) < sim->cfg.elite_spawn_rate
        + sim->cfg.elite_spawn_ramp_per_tick * (float)PS_P(sim, env, tick);
    int wave_len = sim->cfg.wave_length_steps;
    int wave = ps_wave_index(sim, env);
    int ari_k_wave = wave >= sim->cfg.ari_k_start_wave
        && (wave - sim->cfg.ari_k_start_wave) % sim->cfg.ari_k_wave_period == 0;
    int ari_k = ari_k_wave && PS_P(sim, env, tick) % wave_len == 1
        && PS_P(sim, env, last_boss_tick) != PS_P(sim, env, tick);
    int boss = ari_k || (PS_P(sim, env, tick) > 0 && PS_P(sim, env, tick) % sim->cfg.boss_period_steps == 0
        && PS_P(sim, env, last_boss_tick) != PS_P(sim, env, tick));
    if (boss) PS_P(sim, env, last_boss_tick) = PS_P(sim, env, tick);
    int kind = 0;
    float radius = 0.0f;
    PSEnemyDef stats = ps_enemy_stats(sim, env, &kind, &radius, elite, boss, ari_k);
    uint8_t visual_type = (uint8_t)(kind & PS_ENEMY_KIND_MASK);
    if (elite) visual_type |= PS_ENEMY_ELITE_FLAG;
    if (boss) visual_type = PS_ENEMY_BOSS_FLAG;
    if (ari_k) visual_type |= PS_ENEMY_ARI_K_FLAG;
    PS_ENEMY(sim, env, slot, type) = visual_type;
    PS_ENEMY(sim, env, slot, x) = x;
    PS_ENEMY(sim, env, slot, y) = y;
    PS_ENEMY(sim, env, slot, vx) = 0.0f;
    PS_ENEMY(sim, env, slot, vy) = 0.0f;
    PS_ENEMY(sim, env, slot, max_hp) = stats.hp;
    PS_ENEMY(sim, env, slot, hp) = stats.hp;
    PS_ENEMY(sim, env, slot, radius) = radius;
    int shape = PS_SHAPE_CIRCLE;
    float half_width = radius;
    float half_height = radius;
    float bound_radius = radius;
    ps_enemy_geometry(sim, env, kind, ari_k, radius, &shape, &half_width,
        &half_height, &bound_radius);
    PS_ENEMY(sim, env, slot, shape) = (uint8_t)shape;
    PS_ENEMY(sim, env, slot, half_width) = half_width;
    PS_ENEMY(sim, env, slot, half_height) = half_height;
    PS_ENEMY(sim, env, slot, bound_radius) = bound_radius;
    PS_ENEMY(sim, env, slot, speed) = stats.speed_mult;
    PS_ENEMY(sim, env, slot, damage) = stats.damage;
    PS_ENEMY(sim, env, slot, active) = 1;
    ps_dense_add(sim, env, PS_ENEMY_DENSE(sim, env), PS_ENEMY_DENSE_POS(sim, env), &PS_P(sim, env, enemy_count), slot);
    return slot + 1;
}

PS_SIM_FN void ps_spawn_drop(PSSim* sim, int env, float x, float y, float value, int type) {
    (void)sim;
    (void)env;
    if (PS_P(sim, env, drop_count) >= sim->cfg.drop_cap) return;

    int i = ps_find_free_slot(sim, env, PS_DROP_ACTIVE(sim, env), sim->cfg.drop_cap, &PS_P(sim, env, next_drop_slot));

    ps_push_out_obstacles(sim, env, &x, &y, sim->cfg.drop_spawn_radius, 0);
    PS_DROP(sim, env, i, x) = x;
    PS_DROP(sim, env, i, y) = y;
    PS_DROP(sim, env, i, value) = value;
    PS_DROP(sim, env, i, type) = (uint8_t)type;
    PS_DROP(sim, env, i, active) = 1;
    ps_dense_add(sim, env, PS_DROP_DENSE(sim, env), PS_DROP_DENSE_POS(sim, env), &PS_P(sim, env, drop_count), i);
}

PS_SIM_FN void ps_spawn_projectile(PSSim* sim, int env, int type, float sx, float sy, float tx, float ty, float damage, float radius, float speed, int pierce, int ttl) {
    (void)sim;
    (void)env;
    if (PS_P(sim, env, projectile_count) >= sim->cfg.projectile_cap) return;

    int i = ps_find_free_slot(sim, env, PS_PROJECTILE_ACTIVE(sim, env), sim->cfg.projectile_cap, &PS_P(sim, env, next_projectile_slot));

    float dx = tx - sx;
    float dy = ty - sy;
    float d = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
    PS_PROJECTILE(sim, env, i, type) = (uint8_t)type;
    PS_PROJECTILE(sim, env, i, x) = sx;
    PS_PROJECTILE(sim, env, i, y) = sy;
    PS_PROJECTILE(sim, env, i, vx) = dx / d * speed;
    PS_PROJECTILE(sim, env, i, vy) = dy / d * speed;
    PS_PROJECTILE(sim, env, i, damage) = damage;
    PS_PROJECTILE(sim, env, i, radius) = radius;
    PS_PROJECTILE(sim, env, i, ttl) = ttl;
    PS_PROJECTILE(sim, env, i, pierce) = pierce;
    PS_PROJECTILE(sim, env, i, active) = 1;
    ps_dense_add(sim, env, PS_PROJECTILE_DENSE(sim, env), PS_PROJECTILE_DENSE_POS(sim, env), &PS_P(sim, env, projectile_count), i);
}

PS_SIM_FN void ps_spawn_area(PSSim* sim, int env, int type, float x, float y, float radius, float damage, int ttl, int tick_rate) {
    (void)sim;
    (void)env;
    if (PS_P(sim, env, area_count) >= sim->cfg.area_cap) return;

    int i = ps_find_free_slot(sim, env, PS_AREA_ACTIVE(sim, env), PS_MAX_AREAS, &PS_P(sim, env, next_area_slot));

    ps_push_out_obstacles(sim, env, &x, &y, radius, 0);
    PS_AREA(sim, env, i, type) = (uint8_t)type;
    PS_AREA(sim, env, i, x) = x;
    PS_AREA(sim, env, i, y) = y;
    PS_AREA(sim, env, i, radius) = radius;
    PS_AREA(sim, env, i, damage) = damage;
    PS_AREA(sim, env, i, ttl) = ttl;
    PS_AREA(sim, env, i, tick_rate) = tick_rate;
    PS_AREA(sim, env, i, tick_timer) = 0;
    PS_AREA(sim, env, i, active) = 1;
    if (type == PS_WEAPON_INK) PS_P(sim, env, active_ink_count)++;
    ps_dense_add(sim, env, PS_AREA_DENSE(sim, env), PS_AREA_DENSE_POS(sim, env), &PS_P(sim, env, area_count), i);
}

PS_SIM_FN void ps_rebuild_grid(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    for (int i = 0; i < PS_P(sim, env, grid_touched_count); i++) {
        PS_GRID(sim, env, PS_GRID_TOUCHED(sim, env, i)) = -1;
    }
    PS_P(sim, env, grid_touched_count) = 0;
    PS_P(sim, env, aabb_count) = 0;
    int scan_capacity = PS_P(sim, env, enemy_count) * 2 >= sim->cfg.enemy_cap;
    int scan_count = scan_capacity ? sim->cfg.enemy_cap : PS_P(sim, env, enemy_count);
    for (int k = 0; k < scan_count; k++) {
        int i = scan_capacity ? k : PS_ENEMY(sim, env, k, dense);

        PS_ENEMY(sim, env, i, next) = -1;
        if (PS_ENEMY(sim, env, i, shape) == PS_SHAPE_AABB) {
            PS_AABB(sim, env, PS_P(sim, env, aabb_count)++) = i;
        }
        int cell = ps_cell(sim, env, PS_ENEMY(sim, env, i, x), PS_ENEMY(sim, env, i, y));
        if (PS_GRID(sim, env, cell) == -1 && PS_P(sim, env, grid_touched_count) < PS_MAX_ENEMIES) {
            PS_GRID_TOUCHED(sim, env, PS_P(sim, env, grid_touched_count)++) = cell;
        }
        PS_ENEMY(sim, env, i, next) = PS_GRID(sim, env, cell);
        PS_GRID(sim, env, cell) = i;
    }
}

PS_SIM_FN int ps_grid_needed(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    if (PS_P(sim, env, enemy_count) <= 0) return 0;
    if (PS_P(sim, env, projectile_count) > 0 || PS_P(sim, env, active_ink_count) > 0) return 1;
    for (int weapon = 0; weapon < PS_WEAPON_COUNT; weapon++) {
        if (PS_W(sim, env, weapon, weapon_level) > 0 && PS_W(sim, env, weapon, weapon_cd) <= 1.0f)
            return 1;
    }
    return 0;
}

PS_SIM_FN int ps_damage_enemy(PSSim* sim, int env, int eidx, float damage) {
    (void)sim;
    (void)env;
    if (!PS_ENEMY(sim, env, eidx, active) || damage <= 0.0f) return 0;
    PS_ENEMY(sim, env, eidx, hp) -= damage;
    PS_REWARD(sim, env) += sim->cfg.reward_damage * damage;
    PS_P(sim, env, episode_reward_damage) += sim->cfg.reward_damage * damage;
    PS_P(sim, env, episode_damage_dealt) += damage;
    if (PS_ENEMY(sim, env, eidx, hp) > 0.0f) return 0;

    uint8_t type = PS_ENEMY(sim, env, eidx, type);
    int elite = (type & PS_ENEMY_ELITE_FLAG) != 0;
    int boss = (type & PS_ENEMY_BOSS_FLAG) != 0;
    PS_REWARD(sim, env) += sim->cfg.reward_kill;
    PS_P(sim, env, episode_reward_kill) += sim->cfg.reward_kill;
    PS_P(sim, env, episode_kills) += 1.0f;
    PS_P(sim, env, episode_score) += boss ? sim->cfg.kill_score_boss
        : (elite ? sim->cfg.kill_score_elite : sim->cfg.kill_score_default);
    ps_spawn_drop(sim, env, PS_ENEMY(sim, env, eidx, x), PS_ENEMY(sim, env, eidx, y),
        boss ? sim->cfg.drop_value_boss
        : (elite ? sim->cfg.drop_value_elite : sim->cfg.drop_value_default), 0);
    float missing_hp = ps_clampf((PS_P(sim, env, max_hp) - PS_P(sim, env, hp)) / PS_P(sim, env, max_hp), 0.0f, 1.0f);
    float health_chance = sim->cfg.health_drop_rate
        * (1.0f + sim->cfg.health_drop_elite_bonus * (float)elite
        + sim->cfg.health_drop_boss_bonus * (float)boss
        + sim->cfg.health_drop_missing_hp_bonus * missing_hp);
    if (ps_randf(sim, env) < health_chance) {
        ps_spawn_drop(sim, env, PS_ENEMY(sim, env, eidx, x) + sim->cfg.health_drop_offset_x,
            PS_ENEMY(sim, env, eidx, y) + sim->cfg.health_drop_offset_y,
            sim->cfg.health_heal, 1);
    }
    ps_deactivate_enemy(sim, env, eidx);
    return 1;
}

PS_SIM_FN void ps_wave_spawns(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    int enemies = PS_P(sim, env, enemy_count);
    int target = ps_wave_minimum(sim, env);
    int burst = 0;
    while (enemies < target && burst < 4) {
        if (!ps_spawn_enemy(sim, env)) break;
        enemies++;
        burst++;
    }

    int interval = ps_wave_spawn_interval(sim, env);
    if (PS_P(sim, env, tick) % interval == 0) ps_spawn_enemy(sim, env);

    int len = sim->cfg.wave_length_steps;
    int local = PS_P(sim, env, tick) % len;
    int wave = ps_wave_index(sim, env);
    if (local == 1 && wave >= sim->cfg.ari_k_start_wave
            && (wave - sim->cfg.ari_k_start_wave) % sim->cfg.ari_k_wave_period == 0)
        ps_spawn_enemy(sim, env);
    int special_ring = 0;
    for (int i = 0; i < sim->cfg.special_ring_wave_count; i++) {
        special_ring |= wave == sim->cfg.special_ring_waves[i];
    }
    if (local == 1 && special_ring) {
        float half = 0.5f * sim->cfg.arena_size;
        float radius = half * sim->cfg.special_ring_radius_ratio;
        for (int i = 0; i < sim->cfg.special_ring_enemy_count; i++) {
            int slot = ps_spawn_enemy(sim, env);
            if (!slot) return;
            int idx = slot - 1;
            float angle = 2.0f * PI * ((float)i
                / (float)sim->cfg.special_ring_enemy_count);
            PS_ENEMY(sim, env, idx, x) = PS_P(sim, env, px) + cosf(angle) * radius;
            PS_ENEMY(sim, env, idx, y) = PS_P(sim, env, py) + sinf(angle) * radius;
            PS_ENEMY(sim, env, idx, speed) *= sim->cfg.special_ring_speed_mult;
        }
    }
}

PS_SIM_FN void ps_update_enemies(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float half = 0.5f * sim->cfg.arena_size;
    float far2 = (half * sim->cfg.enemy_recycle_radius_ratio)
        * (half * sim->cfg.enemy_recycle_radius_ratio);
    float player_x = PS_P(sim, env, px);
    float player_y = PS_P(sim, env, py);
    float player_radius = sim->cfg.player_radius;
    PS_P(sim, env, nearest_enemy) = -1;
    PS_P(sim, env, nearest_enemy_d2) = 1e30f;
    int scan_capacity = PS_P(sim, env, enemy_count) * 2 >= sim->cfg.enemy_cap;
    int scan_count = scan_capacity ? sim->cfg.enemy_cap : PS_P(sim, env, enemy_count);
    for (int k = 0; k < scan_count; k++) {
        int i = scan_capacity ? k : PS_ENEMY(sim, env, k, dense);

        float dx = player_x - PS_ENEMY(sim, env, i, x);
        float dy = player_y - PS_ENEMY(sim, env, i, y);
        float d = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
        PS_ENEMY(sim, env, i, vx) = dx / d * PS_ENEMY(sim, env, i, speed);
        PS_ENEMY(sim, env, i, vy) = dy / d * PS_ENEMY(sim, env, i, speed);
        PS_ENEMY(sim, env, i, x) += PS_ENEMY(sim, env, i, vx);
        PS_ENEMY(sim, env, i, y) += PS_ENEMY(sim, env, i, vy);
        if ((PS_P(sim, env, tick) + i) % sim->cfg.enemy_obstacle_stride == 0)
            ps_push_out_obstacles_shape(sim, env, &PS_ENEMY(sim, env, i, x), &PS_ENEMY(sim, env, i, y),
                PS_ENEMY(sim, env, i, shape), PS_ENEMY(sim, env, i, radius),
                PS_ENEMY(sim, env, i, half_width), PS_ENEMY(sim, env, i, half_height), 0);
        float post_dx = PS_ENEMY(sim, env, i, x) - player_x;
        float post_dy = PS_ENEMY(sim, env, i, y) - player_y;
        float post_d2 = post_dx * post_dx + post_dy * post_dy;
        if (post_d2 > far2) {
            ps_pick_spawn_position(sim, env, PS_ENEMY(sim, env, i, bound_radius), &PS_ENEMY(sim, env, i, x), &PS_ENEMY(sim, env, i, y));
            if ((PS_ENEMY(sim, env, i, type) & PS_ENEMY_KIND_MASK) != 2) PS_ENEMY(sim, env, i, hp) = PS_ENEMY(sim, env, i, max_hp);
            continue;
        }
        if (post_d2 < PS_P(sim, env, nearest_enemy_d2)) {
            PS_P(sim, env, nearest_enemy_d2) = post_d2;
            PS_P(sim, env, nearest_enemy) = i;
        }
        int hit = ps_geometry_shape_overlaps_circle(PS_ENEMY(sim, env, i, shape),
            player_x - PS_ENEMY(sim, env, i, x), player_y - PS_ENEMY(sim, env, i, y),
            PS_ENEMY(sim, env, i, radius), PS_ENEMY(sim, env, i, half_width),
            PS_ENEMY(sim, env, i, half_height), player_radius);
        if (PS_P(sim, env, invuln_timer) <= 0 && hit) {
            float dmg = fmaxf(1.0f, ceilf(PS_ENEMY(sim, env, i, damage) * sim->cfg.contact_damage));
            PS_P(sim, env, hp) -= dmg;
            PS_REWARD(sim, env) += sim->cfg.reward_hurt * dmg;
            PS_P(sim, env, episode_reward_hurt) += sim->cfg.reward_hurt * dmg;
            PS_P(sim, env, episode_damage_taken) += dmg;
            PS_P(sim, env, invuln_timer) = sim->cfg.invuln_steps;
        }
    }
}

PS_SIM_FN void ps_update_projectiles(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float half = 0.5f * sim->cfg.arena_size;
    int k = 0;
    while (k < PS_P(sim, env, projectile_count)) {
        int i = PS_PROJECTILE(sim, env, k, dense);
        PS_PROJECTILE(sim, env, i, x) += PS_PROJECTILE(sim, env, i, vx);
        PS_PROJECTILE(sim, env, i, y) += PS_PROJECTILE(sim, env, i, vy);
        PS_PROJECTILE(sim, env, i, ttl)--;
        if (PS_PROJECTILE(sim, env, i, ttl) <= 0 || fabsf(PS_PROJECTILE(sim, env, i, x) - PS_P(sim, env, px)) > half || fabsf(PS_PROJECTILE(sim, env, i, y) - PS_P(sim, env, py)) > half) {
            ps_deactivate_projectile(sim, env, i);
            continue;
        }
        int blocked = 0;
        for (int o = 0; o < sim->cfg.obstacle_count; o++) {
            float r = PS_OBSTACLE(sim, env, o, radius) + PS_PROJECTILE(sim, env, i, radius);
            float dx = PS_PROJECTILE(sim, env, i, x) - PS_OBSTACLE(sim, env, o, x);
            float dy = PS_PROJECTILE(sim, env, i, y) - PS_OBSTACLE(sim, env, o, y);
            if (ps_geometry_circle_overlaps(dx, dy, r)) {
                blocked = 1;
                break;
            }
        }
        if (blocked) {
            ps_deactivate_projectile(sim, env, i);
            continue;
        }

        int cell = ps_cell(sim, env, PS_PROJECTILE(sim, env, i, x), PS_PROJECTILE(sim, env, i, y));
        int cx = cell % PS_GRID_W;
        int cy = cell / PS_GRID_W;
        for (int oy = -1; oy <= 1 && PS_PROJECTILE(sim, env, i, active); oy++) {
            int gy = cy + oy;
            if (gy < 0 || gy >= PS_GRID_H) continue;
            for (int ox = -1; ox <= 1 && PS_PROJECTILE(sim, env, i, active); ox++) {
                int gx = cx + ox;
                if (gx < 0 || gx >= PS_GRID_W) continue;
                for (int eidx = PS_GRID(sim, env, gy * PS_GRID_W + gx); eidx >= 0; eidx = PS_ENEMY(sim, env, eidx, next)) {
                    if (!PS_ENEMY(sim, env, eidx, active)) continue;
                    if (PS_ENEMY(sim, env, eidx, shape) != PS_SHAPE_CIRCLE) continue;
                    if (!ps_geometry_shape_overlaps_circle(PS_ENEMY(sim, env, eidx, shape),
                            PS_PROJECTILE(sim, env, i, x) - PS_ENEMY(sim, env, eidx, x),
                            PS_PROJECTILE(sim, env, i, y) - PS_ENEMY(sim, env, eidx, y),
                            PS_ENEMY(sim, env, eidx, radius),
                            PS_ENEMY(sim, env, eidx, half_width),
                            PS_ENEMY(sim, env, eidx, half_height),
                            PS_PROJECTILE(sim, env, i, radius))) continue;
                    ps_damage_enemy(sim, env, eidx, PS_PROJECTILE(sim, env, i, damage));
                    if (PS_PROJECTILE(sim, env, i, pierce) <= 0) ps_deactivate_projectile(sim, env, i);
                    else PS_PROJECTILE(sim, env, i, pierce)--;
                    break;
                }
            }
        }
        for (int a = 0; a < PS_P(sim, env, aabb_count) && PS_PROJECTILE(sim, env, i, active); a++) {
            int eidx = PS_AABB(sim, env, a);
            if (!PS_ENEMY(sim, env, eidx, active)) continue;
            if (!ps_geometry_shape_overlaps_circle(PS_ENEMY(sim, env, eidx, shape),
                    PS_PROJECTILE(sim, env, i, x) - PS_ENEMY(sim, env, eidx, x),
                    PS_PROJECTILE(sim, env, i, y) - PS_ENEMY(sim, env, eidx, y),
                    PS_ENEMY(sim, env, eidx, radius), PS_ENEMY(sim, env, eidx, half_width),
                    PS_ENEMY(sim, env, eidx, half_height), PS_PROJECTILE(sim, env, i, radius))) continue;
            ps_damage_enemy(sim, env, eidx, PS_PROJECTILE(sim, env, i, damage));
            if (PS_PROJECTILE(sim, env, i, pierce) <= 0) ps_deactivate_projectile(sim, env, i);
            else PS_PROJECTILE(sim, env, i, pierce)--;
        }
        if (PS_PROJECTILE(sim, env, i, active)) k++;
    }
}

PS_SIM_FN void ps_update_drops(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    float magnet = sim->cfg.magnet_radius * (1.0f + PS_P(sim, env, magnet_bonus));
    float magnet2 = magnet * magnet;
    float pickup2 = sim->cfg.pickup_radius * sim->cfg.pickup_radius;
    int k = 0;
    while (k < PS_P(sim, env, drop_count)) {
        int i = PS_DROP(sim, env, k, dense);
        float dx = PS_P(sim, env, px) - PS_DROP(sim, env, i, x);
        float dy = PS_P(sim, env, py) - PS_DROP(sim, env, i, y);
        float dist2 = dx * dx + dy * dy;

        if (dist2 < pickup2) {
            PS_REWARD(sim, env) += sim->cfg.reward_pickup;
            PS_P(sim, env, episode_reward_pickup) += sim->cfg.reward_pickup;
            if (PS_DROP(sim, env, i, type) == 1) {
                PS_P(sim, env, hp) = fminf(PS_P(sim, env, max_hp), PS_P(sim, env, hp) + PS_DROP(sim, env, i, value));
            } else {
                PS_P(sim, env, xp) += PS_DROP(sim, env, i, value);
                PS_P(sim, env, episode_xp) += PS_DROP(sim, env, i, value);
                PS_REWARD(sim, env) += sim->cfg.reward_xp * PS_DROP(sim, env, i, value);
                PS_P(sim, env, episode_reward_xp) += sim->cfg.reward_xp * PS_DROP(sim, env, i, value);
                PS_P(sim, env, episode_score) += PS_DROP(sim, env, i, value);
            }
            PS_P(sim, env, episode_pickups) += 1.0f;
            ps_deactivate_drop(sim, env, i);
            continue;
        }

        if (dist2 < magnet2) {
            float dist = sqrtf(fmaxf(dist2, 0.0001f));
            PS_DROP(sim, env, i, x) += dx / dist * sim->cfg.pickup_magnet_speed;
            PS_DROP(sim, env, i, y) += dy / dist * sim->cfg.pickup_magnet_speed;
        }
        k++;
    }
    while (PS_P(sim, env, xp) >= ps_xp_threshold(sim, env)) {
        PS_P(sim, env, xp) -= ps_xp_threshold(sim, env);
        PS_P(sim, env, level)++;
        PS_REWARD(sim, env) += sim->cfg.reward_levelup;
        PS_P(sim, env, episode_reward_levelup) += sim->cfg.reward_levelup;
        PS_P(sim, env, queued_upgrades)++;
        ps_offer_upgrades(sim, env);
    }
}

PS_SIM_FN int ps_nearest_enemy(PSSim* sim, int env, float range) {
    (void)sim;
    (void)env;
    float best_d2 = range * range;
    int cached = PS_P(sim, env, nearest_enemy);
    if (cached >= 0 && cached < sim->cfg.enemy_cap && PS_ENEMY(sim, env, cached, active) && PS_P(sim, env, nearest_enemy_d2) < best_d2) {
        return cached;
    }
    int best = -1;
    int scan_capacity = PS_P(sim, env, enemy_count) * 2 >= sim->cfg.enemy_cap;
    int scan_count = scan_capacity ? sim->cfg.enemy_cap : PS_P(sim, env, enemy_count);
    for (int k = 0; k < scan_count; k++) {
        int i = scan_capacity ? k : PS_ENEMY(sim, env, k, dense);

        float d2 = ps_dist2(PS_P(sim, env, px), PS_P(sim, env, py), PS_ENEMY(sim, env, i, x), PS_ENEMY(sim, env, i, y));
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

PS_SIM_FN void ps_damage_radius_with_query_pad(PSSim* sim, int env,
        float x, float y, float radius, float damage, float knockback,
        float query_pad) {
    (void)sim;
    (void)env;
    if (knockback > 0.0f) {
        PS_P(sim, env, nearest_enemy) = -1;
        PS_P(sim, env, nearest_enemy_d2) = 1e30f;
    }

    // The grid was built before weapon updates. Expand the cell query enough to
    // cover the largest enemy and small same-step knockback without rebuilding.
    float half = 0.5f * sim->cfg.arena_size;
    float inv_cell_x = (float)PS_GRID_W / sim->cfg.arena_size;
    float inv_cell_y = (float)PS_GRID_H / sim->cfg.arena_size;
    float qr = radius + query_pad;
    int min_gx = (int)floorf(((x - qr - PS_P(sim, env, px) + half) * inv_cell_x));
    int max_gx = (int)floorf(((x + qr - PS_P(sim, env, px) + half) * inv_cell_x));
    int min_gy = (int)floorf(((y - qr - PS_P(sim, env, py) + half) * inv_cell_y));
    int max_gy = (int)floorf(((y + qr - PS_P(sim, env, py) + half) * inv_cell_y));
    min_gx = min_gx < 0 ? 0 : (min_gx >= PS_GRID_W ? PS_GRID_W - 1 : min_gx);
    max_gx = max_gx < 0 ? 0 : (max_gx >= PS_GRID_W ? PS_GRID_W - 1 : max_gx);
    min_gy = min_gy < 0 ? 0 : (min_gy >= PS_GRID_H ? PS_GRID_H - 1 : min_gy);
    max_gy = max_gy < 0 ? 0 : (max_gy >= PS_GRID_H ? PS_GRID_H - 1 : max_gy);

    for (int gy = min_gy; gy <= max_gy; gy++) {
        for (int gx = min_gx; gx <= max_gx; gx++) {
            int eidx = PS_GRID(sim, env, gy * PS_GRID_W + gx);
            while (eidx >= 0) {
                int next = PS_ENEMY(sim, env, eidx, next);
                if (PS_ENEMY(sim, env, eidx, active)) {
                    if (PS_ENEMY(sim, env, eidx, shape) != PS_SHAPE_CIRCLE) {
                        eidx = next;
                        continue;
                    }
                    float dx = PS_ENEMY(sim, env, eidx, x) - x;
                    float dy = PS_ENEMY(sim, env, eidx, y) - y;
                    float d2 = dx * dx + dy * dy;
                    if (ps_geometry_shape_overlaps_circle(PS_ENEMY(sim, env, eidx, shape),
                            dx, dy, PS_ENEMY(sim, env, eidx, radius),
                            PS_ENEMY(sim, env, eidx, half_width),
                            PS_ENEMY(sim, env, eidx, half_height), radius)) {
                        int killed = ps_damage_enemy(sim, env, eidx, damage);
                        if (!killed && knockback > 0.0f) {
                            float d = sqrtf(fmaxf(d2, 0.0001f));
                            PS_ENEMY(sim, env, eidx, x) += dx / d * knockback;
                            PS_ENEMY(sim, env, eidx, y) += dy / d * knockback;
                        }
                    }
                }
                eidx = next;
            }
        }
    }
    for (int a = 0; a < PS_P(sim, env, aabb_count); a++) {
        int eidx = PS_AABB(sim, env, a);
        if (!PS_ENEMY(sim, env, eidx, active)) continue;
        float dx = PS_ENEMY(sim, env, eidx, x) - x;
        float dy = PS_ENEMY(sim, env, eidx, y) - y;
        float d2 = dx * dx + dy * dy;
        if (!ps_geometry_shape_overlaps_circle(PS_ENEMY(sim, env, eidx, shape),
                dx, dy, PS_ENEMY(sim, env, eidx, radius),
                PS_ENEMY(sim, env, eidx, half_width), PS_ENEMY(sim, env, eidx, half_height), radius)) continue;
        int killed = ps_damage_enemy(sim, env, eidx, damage);
        if (!killed && knockback > 0.0f) {
            float d = sqrtf(fmaxf(d2, 0.0001f));
            PS_ENEMY(sim, env, eidx, x) += dx / d * knockback;
            PS_ENEMY(sim, env, eidx, y) += dy / d * knockback;
        }
    }
}

PS_SIM_FN void ps_damage_radius(PSSim* sim, int env, float x, float y,
        float radius, float damage, float knockback) {
    (void)sim;
    (void)env;
    ps_damage_radius_with_query_pad(sim, env, x, y, radius, damage, knockback, 1.50f);
}

PS_SIM_FN void ps_update_areas(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    int k = 0;
    while (k < PS_P(sim, env, area_count)) {
        int i = PS_AREA(sim, env, k, dense);
        PS_AREA(sim, env, i, ttl)--;
        PS_AREA(sim, env, i, tick_timer)--;
        if (PS_AREA(sim, env, i, tick_timer) <= 0 && PS_AREA(sim, env, i, damage) > 0.0f) {
            // The grid query is exact for persistent area ticks. The old
            // knockback safety pad made every oil pool inspect many extra
            // cells while not changing which enemies actually took damage.
            ps_damage_radius_with_query_pad(sim, env, PS_AREA(sim, env, i, x), PS_AREA(sim, env, i, y),
                PS_AREA(sim, env, i, radius), PS_AREA(sim, env, i, damage),
                sim->cfg.area_tick_knockback, 0.0f);
            PS_AREA(sim, env, i, tick_timer) = PS_AREA(sim, env, i, tick_rate);
        }
        if (PS_AREA(sim, env, i, ttl) <= 0) {
            ps_deactivate_area(sim, env, i);
            continue;
        }
        k++;
    }
    PS_W(sim, env, PS_WEAPON_INK, weapon_active) = ps_clampf((float)PS_P(sim, env, active_ink_count) / 8.0f, 0.0f, 1.0f);
}

PS_SIM_FN void ps_cast_bubble(PSSim* sim, int env, int level) {
    (void)sim;
    (void)env;
    int target = ps_nearest_enemy(sim, env,
        sim->cfg.bubble_target_range + sim->cfg.bubble_target_area_range * PS_P(sim, env, area_bonus));
    if (target < 0) return;
    int shots = 1 + level / 3;
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_BUBBLE, level, 1);
    float radius = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_BUBBLE, 0)
        * (1.0f + PS_P(sim, env, area_bonus));
    float speed = sim->cfg.projectile_speed * (1.0f + PS_P(sim, env, projectile_speed_bonus));
    int pierce = PS_P(sim, env, pierce_bonus) + level / 4;
    for (int i = 0; i < shots; i++) {
        float jitter = ((float)i - 0.5f * (float)(shots - 1)) * sim->cfg.bubble_shot_spread;
        ps_spawn_projectile(sim, env, PS_WEAPON_BUBBLE, PS_P(sim, env, px), PS_P(sim, env, py),
            PS_ENEMY(sim, env, target, x) + jitter, PS_ENEMY(sim, env, target, y) - jitter,
            damage, radius, speed, pierce, sim->cfg.bubble_projectile_ttl);
    }
    PS_W(sim, env, PS_WEAPON_BUBBLE, weapon_active) = 1.0f;
}

PS_SIM_FN void ps_cast_whirlpool(PSSim* sim, int env, int level) {
    (void)sim;
    (void)env;
    float radius = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_WHIRLPOOL, level - 1)
        * (1.0f + PS_P(sim, env, area_bonus));
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_WHIRLPOOL, level, 0);
    ps_damage_radius(sim, env, PS_P(sim, env, px), PS_P(sim, env, py), radius, damage, sim->cfg.whirlpool_knockback);
    ps_spawn_area(sim, env, PS_WEAPON_WHIRLPOOL, PS_P(sim, env, px), PS_P(sim, env, py), radius, 0.0f,
        sim->cfg.whirlpool_ttl, sim->cfg.whirlpool_tick_rate);
    PS_W(sim, env, PS_WEAPON_WHIRLPOOL, weapon_active) = 1.0f;
}

PS_SIM_FN void ps_cast_orbit(PSSim* sim, int env, int level) {
    (void)sim;
    (void)env;
    int count = 1 + level / 2;
    float orbit_r = (sim->cfg.weapon_orbit_distance
        + sim->cfg.weapon_orbit_distance_per_level * (float)level)
        * (1.0f + sim->cfg.orbit_area_distance_bonus * PS_P(sim, env, area_bonus));
    float hit_r = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_ORBIT, level)
        * (1.0f + PS_P(sim, env, area_bonus));
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_ORBIT, level, 0);
    for (int i = 0; i < count; i++) {
        float a = PS_P(sim, env, orbit_phase) + 2.0f * PI * ((float)i / (float)count);
        ps_damage_radius(sim, env, PS_P(sim, env, px) + cosf(a) * orbit_r,
            PS_P(sim, env, py) + sinf(a) * orbit_r, hit_r, damage, sim->cfg.orbit_knockback);
    }
    PS_W(sim, env, PS_WEAPON_ORBIT, weapon_active) = 1.0f;
}

PS_SIM_FN void ps_cast_ink(PSSim* sim, int env, int level) {
    (void)sim;
    (void)env;
    int target = ps_nearest_enemy(sim, env, sim->cfg.arena_size * sim->cfg.ink_target_range_ratio);
    if (target < 0) return;
    int pools = 1 + level / 3;
    float radius = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_INK, level)
        * (1.0f + PS_P(sim, env, area_bonus));
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_INK, level, 0);
    int ttl = sim->cfg.ink_pool_ttl_base + sim->cfg.ink_pool_ttl_per_level * level;
    for (int i = 0; i < pools; i++) {
        float angle = 2.0f * PI * ((float)i / (float)pools)
            + ps_randf(sim, env) * sim->cfg.ink_pool_angle_jitter;
        float dist = pools > 1 ? sim->cfg.ink_pool_spread : 0.0f;
        ps_spawn_area(sim, env, PS_WEAPON_INK,
            PS_ENEMY(sim, env, target, x) + cosf(angle) * dist,
            PS_ENEMY(sim, env, target, y) + sinf(angle) * dist,
            radius, damage, ttl, sim->cfg.ink_pool_tick_rate);
    }
    PS_W(sim, env, PS_WEAPON_INK, weapon_active) = 1.0f;
}

PS_SIM_FN void ps_update_poison_oil_trail(PSSim* sim, int env, int level) {
    (void)sim;
    (void)env;
    if (level <= 0) return;
    float speed2 = PS_P(sim, env, pvx) * PS_P(sim, env, pvx) + PS_P(sim, env, pvy) * PS_P(sim, env, pvy);
    if (speed2 < sim->cfg.ink_trail_min_speed2) return;

    int active_oil = PS_P(sim, env, active_ink_count);
    int max_oil = sim->cfg.ink_trail_max_base
        + level * sim->cfg.ink_trail_max_per_level;
    if (active_oil >= max_oil) return;

    int cadence = sim->cfg.ink_trail_cadence_base
        - level / sim->cfg.ink_trail_cadence_level_divisor;
    if (cadence < sim->cfg.ink_trail_cadence_min)
        cadence = sim->cfg.ink_trail_cadence_min;
    if (PS_P(sim, env, tick) % cadence != 0) return;

    float speed = sqrtf(speed2);
    float nx = PS_P(sim, env, pvx) / speed;
    float ny = PS_P(sim, env, pvy) / speed;
    float radius = (sim->cfg.ink_trail_radius_base
        + sim->cfg.ink_trail_radius_per_level * (float)level
        + sim->cfg.ink_trail_radius_config_scale
            * sim->cfg.weapon_radius_per_level[PS_WEAPON_INK] * (float)level)
        * (1.0f + PS_P(sim, env, area_bonus));
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_INK, level, 0)
        * sim->cfg.ink_trail_damage_multiplier;
    int ttl = sim->cfg.ink_trail_ttl_base + sim->cfg.ink_trail_ttl_per_level * level;
    ps_spawn_area(sim, env, PS_WEAPON_INK, PS_P(sim, env, px) - nx * sim->cfg.ink_trail_offset,
        PS_P(sim, env, py) - ny * sim->cfg.ink_trail_offset, radius, damage, ttl,
        sim->cfg.ink_trail_tick_rate);
    PS_W(sim, env, PS_WEAPON_INK, weapon_active) = 1.0f;
}

PS_SIM_FN void ps_cast_sonar(PSSim* sim, int env, int level) {
    (void)sim;
    (void)env;
    float radius = ps_geometry_weapon_radius(&sim->cfg, PS_WEAPON_SONAR, level)
        * (1.0f + PS_P(sim, env, area_bonus));
    float damage = ps_weapon_damage(sim, env, PS_WEAPON_SONAR, level, 0);
    ps_damage_radius(sim, env, PS_P(sim, env, px), PS_P(sim, env, py), radius, damage, sim->cfg.sonar_knockback);
    ps_spawn_area(sim, env, PS_WEAPON_SONAR, PS_P(sim, env, px), PS_P(sim, env, py), radius, 0.0f,
        sim->cfg.sonar_ttl, sim->cfg.sonar_tick_rate);
    PS_W(sim, env, PS_WEAPON_SONAR, weapon_active) = 1.0f;
}

PS_SIM_FN void ps_update_weapons(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        PS_W(sim, env, i, weapon_active) *= sim->cfg.weapon_active_decay;
        if (PS_W(sim, env, i, weapon_cd) > 0.0f) PS_W(sim, env, i, weapon_cd) -= 1.0f;
    }
    PS_P(sim, env, orbit_phase) += sim->cfg.orbit_phase_speed
        + sim->cfg.orbit_phase_per_level
            * (float)PS_W(sim, env, PS_WEAPON_ORBIT, weapon_level);
    ps_update_areas(sim, env);
    ps_update_poison_oil_trail(sim, env, PS_W(sim, env, PS_WEAPON_INK, weapon_level));

    for (int weapon = 0; weapon < PS_WEAPON_COUNT; weapon++) {
        int level = PS_W(sim, env, weapon, weapon_level);
        if (level <= 0 || PS_W(sim, env, weapon, weapon_cd) > 0.0f) continue;
        switch (weapon) {
            case PS_WEAPON_BUBBLE: ps_cast_bubble(sim, env, level); break;
            case PS_WEAPON_WHIRLPOOL: ps_cast_whirlpool(sim, env, level); break;
            case PS_WEAPON_ORBIT: ps_cast_orbit(sim, env, level); break;
            case PS_WEAPON_INK: ps_cast_ink(sim, env, level); break;
            case PS_WEAPON_SONAR: ps_cast_sonar(sim, env, level); break;
        }
        PS_W(sim, env, weapon, weapon_cd) = ps_weapon_cooldown_total(sim, env, weapon);
    }
}

PS_SIM_FN void ps_reset_core(PSSim* sim, int env, int clear_outputs) {
    (void)sim;
    (void)env;
    if (clear_outputs) {
        PS_REWARD(sim, env) = 0.0f;
        PS_TERMINAL(sim, env) = 0.0f;
    }
    PS_P(sim, env, px) = 0.0f;
    PS_P(sim, env, py) = 0.0f;
    PS_P(sim, env, pvx) = 0.0f;
    PS_P(sim, env, pvy) = 0.0f;
    PS_P(sim, env, player_facing_left) = 0;
    PS_P(sim, env, max_hp) = floorf(sim->cfg.player_health);
    PS_P(sim, env, hp) = PS_P(sim, env, max_hp);
    PS_P(sim, env, xp) = 0.0f;
    PS_P(sim, env, level) = 1;
    PS_P(sim, env, speed_bonus) = 0.0f;
    PS_P(sim, env, damage_bonus) = 0.0f;
    PS_P(sim, env, cooldown_mult) = 1.0f;
    PS_P(sim, env, projectile_speed_bonus) = 0.0f;
    PS_P(sim, env, magnet_bonus) = 0.0f;
    PS_P(sim, env, area_bonus) = 0.0f;
    PS_P(sim, env, pierce_bonus) = 0;
    PS_P(sim, env, pending_upgrade) = 0;
    PS_P(sim, env, queued_upgrades) = 0;
    PS_P(sim, env, last_boss_tick) = -1;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) PS_W(sim, env, i, weapon_cd) = 0.0f;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) PS_W(sim, env, i, weapon_active) = 0.0f;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) PS_W(sim, env, i, weapon_level) = 0;
    PS_W(sim, env, PS_WEAPON_BUBBLE, weapon_level) = 1;
    if (sim->cfg.free_upgrade >= 0) {
        int count = sim->cfg.free_upgrade_count;
        for (int i = 0; i < count; i++) {
            ps_apply_upgrade_effect(sim, env, sim->cfg.free_upgrade);
        }
    }
    PS_P(sim, env, orbit_phase) = ps_randf(sim, env) * 2.0f * PI;
    PS_P(sim, env, tick) = 0;
    PS_P(sim, env, invuln_timer) = 0;
    PS_P(sim, env, episode_return) = 0.0f;
    PS_P(sim, env, episode_reward_survival) = 0.0f;
    PS_P(sim, env, episode_reward_damage) = 0.0f;
    PS_P(sim, env, episode_reward_kill) = 0.0f;
    PS_P(sim, env, episode_reward_hurt) = 0.0f;
    PS_P(sim, env, episode_reward_pickup) = 0.0f;
    PS_P(sim, env, episode_reward_xp) = 0.0f;
    PS_P(sim, env, episode_reward_levelup) = 0.0f;
    PS_P(sim, env, episode_reward_obstacle) = 0.0f;
    PS_P(sim, env, episode_reward_terminal) = 0.0f;
    PS_P(sim, env, episode_score) = 0.0f;
    PS_P(sim, env, episode_kills) = 0.0f;
    PS_P(sim, env, episode_xp) = 0.0f;
    PS_P(sim, env, episode_damage_dealt) = 0.0f;
    PS_P(sim, env, episode_damage_taken) = 0.0f;
    PS_P(sim, env, episode_pickups) = 0.0f;
    PS_P(sim, env, episode_levelups) = 0.0f;
    PS_P(sim, env, episode_obstacle_hits) = 0.0f;
    PS_P(sim, env, episode_peak_enemies) = 0.0f;
    PS_P(sim, env, episode_peak_projectiles) = 0.0f;
    PS_P(sim, env, episode_min_hp) = PS_P(sim, env, hp);
    ps_clear_entities(sim, env);
    ps_spawn_obstacles(sim, env);
    ps_compute_observations(sim, env);
}

PS_SIM_FN void ps_reset_env(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    ps_reset_core(sim, env, 1);
}

PS_SIM_FN void ps_step_env(PSSim* sim, int env) {
    (void)sim;
    (void)env;
    PS_REWARD(sim, env) = sim->cfg.reward_survival;
    PS_P(sim, env, episode_reward_survival) += sim->cfg.reward_survival;
    PS_TERMINAL(sim, env) = 0.0f;
    PS_P(sim, env, tick)++;
    if (PS_P(sim, env, invuln_timer) > 0) PS_P(sim, env, invuln_timer)--;

    int upgrade_action = (int)PS_ACTIONS(sim, env)[1];
    if (PS_P(sim, env, pending_upgrade))
        ps_apply_upgrade(sim, env, (int)((unsigned)upgrade_action % PS_UPGRADE_SLOTS));

    static const float dirs[9][2] = {
        {0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0},
        {-0.70710678f, -0.70710678f}, {0.70710678f, -0.70710678f},
        {-0.70710678f, 0.70710678f}, {0.70710678f, 0.70710678f},
    };
    int action = (int)PS_ACTIONS(sim, env)[0];
    action = (int)((unsigned)action % 9u);
    float speed = sim->cfg.player_speed * (1.0f + PS_P(sim, env, speed_bonus));
    float target_vx = dirs[action][0] * speed;
    float target_vy = dirs[action][1] * speed;
    if (target_vx < -0.001f) PS_P(sim, env, player_facing_left) = 1;
    else if (target_vx > 0.001f) PS_P(sim, env, player_facing_left) = 0;
    PS_P(sim, env, pvx) += (target_vx - PS_P(sim, env, pvx)) * sim->cfg.movement_smoothing;
    PS_P(sim, env, pvy) += (target_vy - PS_P(sim, env, pvy)) * sim->cfg.movement_smoothing;
    float v2 = PS_P(sim, env, pvx) * PS_P(sim, env, pvx) + PS_P(sim, env, pvy) * PS_P(sim, env, pvy);
    if (v2 > speed * speed) {
        float inv = speed / sqrtf(v2);
        PS_P(sim, env, pvx) *= inv;
        PS_P(sim, env, pvy) *= inv;
    }
    PS_P(sim, env, px) += PS_P(sim, env, pvx);
    PS_P(sim, env, py) += PS_P(sim, env, pvy);
    ps_push_out_obstacles(sim, env, &PS_P(sim, env, px), &PS_P(sim, env, py), sim->cfg.player_radius, 1);
    ps_recycle_far_obstacles(sim, env);
    ps_update_moving_obstacles(sim, env);

    ps_wave_spawns(sim, env);
    ps_update_enemies(sim, env);
    if (ps_grid_needed(sim, env)) ps_rebuild_grid(sim, env);
    ps_update_weapons(sim, env);
    ps_update_projectiles(sim, env);
    ps_update_drops(sim, env);

    if ((float)PS_P(sim, env, enemy_count) > PS_P(sim, env, episode_peak_enemies))
        PS_P(sim, env, episode_peak_enemies) = (float)PS_P(sim, env, enemy_count);
    if ((float)PS_P(sim, env, projectile_count) > PS_P(sim, env, episode_peak_projectiles))
        PS_P(sim, env, episode_peak_projectiles) = (float)PS_P(sim, env, projectile_count);
    if (PS_P(sim, env, hp) < PS_P(sim, env, episode_min_hp)) PS_P(sim, env, episode_min_hp) = PS_P(sim, env, hp);

    PS_P(sim, env, episode_return) += PS_REWARD(sim, env);
    if (PS_P(sim, env, hp) <= 0.0f || PS_P(sim, env, tick) >= sim->cfg.max_steps) {
        // Success means reaching max_steps while still alive. If HP reaches
        // zero on the final step, the episode is a failure.
        int survived = PS_P(sim, env, tick) >= sim->cfg.max_steps && PS_P(sim, env, hp) > 0.0f;
        float terminal_reward = survived
            ? sim->cfg.reward_success
            : sim->cfg.reward_death;

        // The ordinary reward for this step was already added to
        // episode_return above. Add only the terminal reward here.
        PS_REWARD(sim, env) += terminal_reward;
        PS_P(sim, env, episode_reward_terminal) += terminal_reward;
        PS_P(sim, env, episode_return) += terminal_reward;
        PS_TERMINAL(sim, env) = 1.0f;

        // Log before reset because reset clears episode statistics.
        ps_add_log(sim, env, survived);

        // Preserve this final reward and terminal flag for the learner.
        ps_reset_core(sim, env, 0);
        return;
    }

#ifdef PS_DEBUG_COUNTS
    ps_verify_counts(sim, env);
#endif
    ps_compute_observations(sim, env);
}
