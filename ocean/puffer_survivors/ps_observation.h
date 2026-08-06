#pragma once

#include "ps_content.h"
#include "ps_observation_layout.h"

#ifndef PS_OBS_EXACT_SECTOR
#define PS_OBS_EXACT_SECTOR 0
#endif

static inline int ps_obs_sector_fast8(float dx, float dy) {
    if (dy == 0.0f) return dx >= 0.0f ? 0 : 4;
    if (dx == 0.0f) return dy > 0.0f ? 2 : 6;

    if (dx > 0.0f) {
        if (dy > 0.0f) return dy < dx ? 0 : 1;
        return -dy > dx ? 6 : 7;
    }

    if (dy > 0.0f) return dy > -dx ? 2 : 3;
    return -dy < -dx ? 4 : 5;
}

static inline int ps_obs_sector(float dx, float dy) {
#if PS_OBS_EXACT_SECTOR
    float angle = atan2f(dy, dx);
    if (angle < 0.0f) angle += 2.0f * PI;
    int sector = (int)(angle / (2.0f * PI) * PS_SECTORS);
    return sector >= PS_SECTORS ? PS_SECTORS - 1 : sector;
#else
#if PS_SECTORS == 8
    return ps_obs_sector_fast8(dx, dy);
#else
    float angle = atan2f(dy, dx);
    if (angle < 0.0f) angle += 2.0f * PI;
    int sector = (int)(angle / (2.0f * PI) * PS_SECTORS);
    return sector >= PS_SECTORS ? PS_SECTORS - 1 : sector;
#endif
#endif
}

static inline int ps_obs_ring_d2(float d2, float observe_radius2) {
    // Bias resolution toward the immediate dodge space. With the default
    // radius these boundaries are approximately 3.2 and 8.2 world units.
    if (d2 < observe_radius2 * 0.0225f) return 0;
    if (d2 < observe_radius2 * 0.1444f) return 1;
    return PS_RINGS - 1;
}

// Soft normalization for nonnegative values without a hard maximum.
// value == half_scale maps to 0.5 and larger values approach 1.0 smoothly.
static inline float ps_obs_soft_norm(float value, float half_scale) {
    value = fmaxf(value, 0.0f);
    half_scale = fmaxf(half_scale, 0.0001f);
    return value / (value + half_scale);
}

static inline void ps_compute_observations(PufferSurvivors* env) {
    float* obs = (float*)env->agents[0].observations;
    memset(obs, 0, PS_OBS_SIZE * sizeof(float));

    int idx = 0;
    int wave_len = env->cfg.wave_length_steps;
    int wave = ps_wave_index(env);
    int boss_period = env->cfg.boss_period_steps;

    // Player/global features. Unbounded counters use soft normalization rather
    // than clipping, so wave 30 remains distinguishable from wave 100.
    obs[idx++] = ps_clampf(env->hp / fmaxf(env->max_hp, 1.0f), 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->hp, 4.0f);
    obs[idx++] = ps_obs_soft_norm(env->max_hp, 8.0f);
    obs[idx++] = ps_obs_soft_norm((float)env->level, 20.0f);
    obs[idx++] = ps_clampf(env->xp / fmaxf(ps_xp_threshold(env), 1.0f), 0.0f, 1.0f);
    obs[idx++] = (float)(env->tick % wave_len) / (float)wave_len;
    obs[idx++] = ps_obs_soft_norm((float)(wave + 1), 12.0f);
    obs[idx++] = (float)(env->tick % boss_period) / (float)boss_period;

    int visible_enemies_idx = idx++;
    int alt_a_idx = idx++;
    int visible_drops_idx = idx++;
    int alt_b_idx = idx++;
    if (env->cfg.observation_version == 6 || env->cfg.observation_version >= 9) {
        obs[alt_b_idx] = 1.0f - ps_clampf(env->weapon_cd[PS_WEAPON_BUBBLE]
            / ps_weapon_cooldown_total(env, PS_WEAPON_BUBBLE), 0.0f, 1.0f);
    }
    obs[idx++] = env->pvx / fmaxf(env->cfg.player_speed * (1.0f + env->speed_bonus), 0.001f);
    obs[idx++] = env->pvy / fmaxf(env->cfg.player_speed * (1.0f + env->speed_bonus), 0.001f);
    obs[idx++] = env->pending_upgrade ? 1.0f : 0.0f;
    obs[idx++] = env->cfg.invuln_steps > 0 ? ps_clampf((float)env->invuln_timer / (float)env->cfg.invuln_steps, 0.0f, 1.0f) : 0.0f;
    obs[idx++] = ps_clampf((float)env->queued_upgrades / 4.0f, 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->speed_bonus, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->damage_bonus, 1.0f);
    obs[idx++] = ps_clampf(1.0f - env->cooldown_mult, 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->magnet_bonus, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->area_bonus, 1.0f);
    obs[idx++] = ps_obs_soft_norm((float)env->pierce_bonus, 4.0f);
    int lethal_threat_idx = idx++;
    int alt_c_idx = idx++;

    // XP routing hints. These make the policy's pickup objective explicit
    // instead of forcing it to infer nearest-orb direction from coarse bins.
    int nearest_xp_dx_idx = idx++;
    int nearest_xp_dy_idx = idx++;
    int alt_d_idx = idx++;
    int visible_xp_value_idx = idx++;
    int visible_xp_can_level_idx = idx++;

    float observe_radius = env->cfg.arena_size * 0.45f;
    float observe_radius2 = observe_radius * observe_radius;
    float inv_observe_radius = 1.0f / fmaxf(observe_radius, 0.001f);
    float inv_enemy_cap = 1.0f / fmaxf((float)env->cfg.enemy_cap, 1.0f);
    float inv_projectile_cap = 1.0f / fmaxf((float)env->cfg.projectile_cap, 1.0f);
    float inv_drop_cap = 1.0f / fmaxf((float)env->cfg.drop_cap, 1.0f);
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

    float sector_pressure[PS_SECTORS] = {0};
    float sector_front[PS_SECTORS] = {0};
    float sector_ttc[PS_SECTORS] = {0};
    float sector_obstacle[PS_SECTORS] = {0};
    float obstacle_bin_nearest_d2[PS_SECTORS * PS_RINGS];
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

    for (int i = 0; i < env->cfg.enemy_cap; i++) {
        if (!env->enemies.active[i]) continue;
        if (env->enemies.type[i] & PS_ENEMY_BOSS_FLAG) {
            boss_count++;
            float boss_dx = env->enemies.x[i] - env->px;
            float boss_dy = env->enemies.y[i] - env->py;
            float boss_d2 = boss_dx * boss_dx + boss_dy * boss_dy;
            if (boss_d2 < nearest_boss_d2) {
                nearest_boss_d2 = boss_d2;
                nearest_boss = i;
            }
        }
        float dx = env->enemies.x[i] - env->px;
        float dy = env->enemies.y[i] - env->py;
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        float d = sqrtf(d2);
        visible_enemies++;
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = enemy_base + (ring * PS_SECTORS + sector) * PS_ENEMY_CHANNELS;
        float proximity = 1.0f - d * inv_observe_radius;
        float hit_fraction = env->enemies.damage[i] / fmaxf(env->hp, 0.25f);
        float lethal_risk = ps_clampf(hit_fraction, 0.0f, 1.0f) * (0.35f + 0.65f * proximity);
        lethal_threat = fmaxf(lethal_threat, lethal_risk);
        obs[o + 0] = fminf(obs[o + 0] + 0.125f, 1.0f);
        obs[o + 1] = fmaxf(obs[o + 1], proximity);
        obs[o + 2] = fmaxf(obs[o + 2], env->enemies.hp[i] / fmaxf(env->enemies.max_hp[i], 1.0f));
        obs[o + 3] = fmaxf(obs[o + 3], env->enemies.damage[i] / 4.0f);

        float threat = (0.35f + 0.65f * proximity) * ps_clampf(env->enemies.damage[i] / 4.0f, 0.15f, 1.0f);
        sector_pressure[sector] = fminf(sector_pressure[sector] + 0.12f * threat, 1.0f);
        sector_front[sector] = fmaxf(sector_front[sector], proximity);

        float clearance = fmaxf(d - (env->enemies.bound_radius[i] + env->cfg.player_radius), 0.0f);
        float closing = -(env->enemies.vx[i] * dx + env->enemies.vy[i] * dy) / fmaxf(d, 0.001f);
        float ttc = clearance / fmaxf(closing, 0.001f);
        sector_ttc[sector] = fmaxf(sector_ttc[sector], 1.0f - ps_clampf(ttc / 180.0f, 0.0f, 1.0f));
    }

    obs[lethal_threat_idx] = ps_clampf(lethal_threat, 0.0f, 1.0f);

    if (nearest_boss >= 0) {
        float dx = env->enemies.x[nearest_boss] - env->px;
        float dy = env->enemies.y[nearest_boss] - env->py;
        float d = sqrtf(fmaxf(nearest_boss_d2, 0.0001f));
        float closing = -(env->enemies.vx[nearest_boss] * dx + env->enemies.vy[nearest_boss] * dy) / d;
        obs[boss_base + PS_BOSS_PRESENT] = 1.0f;
        obs[boss_base + PS_BOSS_DX] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
        obs[boss_base + PS_BOSS_DY] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
        obs[boss_base + PS_BOSS_PROXIMITY] = 1.0f - ps_clampf(d * inv_observe_radius, 0.0f, 1.0f);
        obs[boss_base + PS_BOSS_HP_FRACTION] = ps_clampf(env->enemies.hp[nearest_boss] / fmaxf(env->enemies.max_hp[nearest_boss], 1.0f), 0.0f, 1.0f);
        obs[boss_base + PS_BOSS_MAX_HP] = ps_obs_soft_norm(env->enemies.max_hp[nearest_boss], 96.0f);
        obs[boss_base + PS_BOSS_CLOSING_SPEED] = ps_clampf(closing / 0.25f, -1.0f, 1.0f);
        obs[boss_base + PS_BOSS_COUNT] = ps_obs_soft_norm((float)boss_count, 2.0f);
    }

    for (int k = 0; k < env->drop_count; k++) {
        int i = env->drops.dense[k];
        float dx = env->drops.x[i] - env->px;
        float dy = env->drops.y[i] - env->py;
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        float d = sqrtf(d2);
        visible_drops++;
        if (env->drops.type[i] == 0) {
            visible_xp_value += env->drops.value[i];
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
        obs[o + 0] = fminf(obs[o + 0] + env->drops.value[i] * 0.1f, 1.0f);
        obs[o + 1] = fmaxf(obs[o + 1], 1.0f - d * inv_observe_radius);
        obs[o + 2] = fmaxf(obs[o + 2], env->drops.type[i] == 1 ? 1.0f : 0.0f);
    }

    if (nearest_xp_d2 < 1e29f) {
        float nearest_xp_dist = sqrtf(nearest_xp_d2);
        obs[nearest_xp_dx_idx] = ps_clampf(nearest_xp_dx * inv_observe_radius, -1.0f, 1.0f);
        obs[nearest_xp_dy_idx] = ps_clampf(nearest_xp_dy * inv_observe_radius, -1.0f, 1.0f);
        if (env->cfg.observation_version <= 7 || env->cfg.observation_version >= 9)
            obs[alt_d_idx] = 1.0f - ps_clampf(nearest_xp_dist * inv_observe_radius, 0.0f, 1.0f);
    }
    if (nearest_health_d2 < 1e29f) {
        float nearest_health_dist = sqrtf(nearest_health_d2);
        if (env->cfg.observation_version >= 7 && env->cfg.observation_version <= 8) {
            obs[alt_a_idx] = ps_clampf(nearest_health_dx * inv_observe_radius, -1.0f, 1.0f);
            obs[alt_b_idx] = ps_clampf(nearest_health_dy * inv_observe_radius, -1.0f, 1.0f);
        }
        if (env->cfg.observation_version <= 7 || env->cfg.observation_version >= 9)
            obs[alt_c_idx] = 1.0f - ps_clampf(nearest_health_dist * inv_observe_radius, 0.0f, 1.0f);
    }
    obs[visible_xp_value_idx] = ps_clampf(visible_xp_value / fmaxf(ps_xp_threshold(env), 1.0f), 0.0f, 1.0f);
    obs[visible_xp_can_level_idx] = visible_xp_value >= fmaxf(ps_xp_threshold(env) - env->xp, 0.0f) ? 1.0f : 0.0f;

    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        float dx = env->obstacles.x[i] - env->px;
        float dy = env->obstacles.y[i] - env->py;
        float d2 = dx * dx + dy * dy;
        if (d2 > observe_radius2) continue;
        if (d2 < nearest_obstacle_d2) {
            nearest_obstacle_d2 = d2;
            nearest_obstacle_radius = env->obstacles.radius[i];
        }
        float d = sqrtf(d2);
        int sector = ps_obs_sector(dx, dy);
        int ring = ps_obs_ring_d2(d2, observe_radius2);
        int o = obstacle_base + (ring * PS_SECTORS + sector) * PS_OBSTACLE_CHANNELS;
        float proximity = 1.0f - d * inv_observe_radius;
        int bin = ring * PS_SECTORS + sector;
        if (env->cfg.observation_version == 8) {
            if (d2 < obstacle_bin_nearest_d2[bin]) {
                obstacle_bin_nearest_d2[bin] = d2;
                obs[o + 0] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
                obs[o + 1] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
            }
        } else {
            obs[o + 0] = fminf(obs[o + 0] + 0.25f, 1.0f);
            obs[o + 1] = fmaxf(obs[o + 1], proximity);
        }
        if (env->cfg.observation_version >= 9
                && d2 < obstacle_bin_nearest_d2[bin]) {
            obstacle_bin_nearest_d2[bin] = d2;
            int exact = PS_OBS_EXACT_OBSTACLE_BASE + 2 * bin;
            obs[exact + 0] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
            obs[exact + 1] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
        }
        sector_obstacle[sector] = fminf(sector_obstacle[sector] + 0.30f * proximity, 1.0f);
    }

    if (env->cfg.observation_version == 8 && nearest_obstacle_d2 < 1e29f) {
        float center_dist = sqrtf(nearest_obstacle_d2);
        float clearance = fmaxf(center_dist - nearest_obstacle_radius - env->cfg.player_radius, 0.0f);
        obs[alt_c_idx] = ps_clampf(clearance * inv_observe_radius, 0.0f, 1.0f);
        float obstacle_radius_norm = fmaxf(env->cfg.obstacle_radius_max, 1.1f);
        obs[alt_d_idx] = ps_clampf(nearest_obstacle_radius / obstacle_radius_norm, 0.0f, 1.0f);
    }

    if (env->cfg.observation_version == 6 || env->cfg.observation_version >= 9) {
        for (int k = 0; k < env->projectile_count; k++) {
            int i = env->projectiles.dense[k];
            if (ps_dist2(env->projectiles.x[i], env->projectiles.y[i], env->px, env->py)
                    <= observe_radius2)
                visible_projectiles++;
        }
        obs[alt_a_idx] = ps_clampf((float)visible_projectiles * inv_projectile_cap, 0.0f, 1.0f);
    }

    for (int k = 0; k < env->area_count; k++) {
        int i = env->areas.dense[k];
        float dx = env->areas.x[i] - env->px;
        float dy = env->areas.y[i] - env->py;
        float d2 = dx * dx + dy * dy;
        float effective_radius = observe_radius + env->areas.radius[i];
        if (d2 > effective_radius * effective_radius) continue;
        float d = sqrtf(d2);
        int sector = ps_obs_sector(dx, dy);
        int o = area_base + sector * PS_AREA_CHANNELS;
        float coverage = ps_clampf(env->areas.radius[i] * inv_observe_radius + fmaxf(0.0f, 1.0f - d * inv_observe_radius), 0.0f, 1.0f);
        obs[o + 0] = fmaxf(obs[o + 0], coverage);
        obs[o + 1] = fmaxf(obs[o + 1], ps_clampf(env->areas.damage[i] / 4.0f, 0.0f, 1.0f));
        obs[o + 2] = fmaxf(obs[o + 2], ps_clampf((float)env->areas.ttl[i] / 180.0f, 0.0f, 1.0f));
    }

    for (int s = 0; s < PS_SECTORS; s++) {
        int left = (s + PS_SECTORS - 1) % PS_SECTORS;
        int right = (s + 1) % PS_SECTORS;
        float neighbor_pressure = 0.5f * (sector_pressure[left] + sector_pressure[right]);
        float blocked = ps_clampf(sector_pressure[s] + 0.35f * neighbor_pressure + 0.75f * sector_obstacle[s], 0.0f, 1.0f);
        int o = danger_base + s * PS_DANGER_CHANNELS;
        obs[o + 0] = ps_clampf(sector_pressure[s], 0.0f, 1.0f);
        obs[o + 1] = ps_clampf(sector_front[s], 0.0f, 1.0f);
        obs[o + 2] = ps_clampf(sector_ttc[s], 0.0f, 1.0f);
        obs[o + 3] = 1.0f - blocked;
    }

    obs[visible_enemies_idx] = ps_clampf((float)visible_enemies * inv_enemy_cap, 0.0f, 1.0f);
    obs[visible_drops_idx] = ps_clampf((float)visible_drops * inv_drop_cap, 0.0f, 1.0f);

    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        int level = env->weapon_level[i];
        float cd_total = ps_weapon_cooldown_total(env, i);
        float ready = level > 0 ? 1.0f - ps_clampf(env->weapon_cd[i] / cd_total, 0.0f, 1.0f) : 0.0f;
        obs[idx++] = (float)level / (float)env->cfg.weapon_max_level;
        obs[idx++] = ready;
        obs[idx++] = ps_clampf(env->weapon_active[i], 0.0f, 1.0f);
        obs[idx++] = ps_weapon_power(env, i);
    }

    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int type = env->pending_upgrade ? env->offered[i] : -1;
        obs[idx++] = type >= 0 ? 1.0f : 0.0f;
        obs[idx++] = type >= 0 ? (float)type / (float)(PS_UPGRADE_COUNT - 1) : 0.0f;
        obs[idx++] = type >= PS_UPGRADE_BUBBLE && type <= PS_UPGRADE_SONAR ? 1.0f : 0.0f;
        obs[idx++] = type == PS_UPGRADE_MIGHT || type == PS_UPGRADE_COOLDOWN || type == PS_UPGRADE_AREA ? 1.0f : 0.0f;
        obs[idx++] = type == PS_UPGRADE_SPEED || type == PS_UPGRADE_MAGNET || type == PS_UPGRADE_PIERCE ? 1.0f : 0.0f;
        obs[idx++] = type == PS_UPGRADE_HEALTH ? 1.0f : 0.0f;
    }

    idx += PS_EXACT_OBSTACLE_FEATURES;

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
    for (int k = 0; k < env->moving_obstacle_count; k++) {
        int i = env->moving_obstacles.dense[k];
        float dx = env->moving_obstacles.x[i] - env->px;
        float dy = env->moving_obstacles.y[i] - env->py;
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
        float dx = env->moving_obstacles.x[i] - env->px;
        float dy = env->moving_obstacles.y[i] - env->py;
        obs[o + 0] = 1.0f;
        obs[o + 1] = env->moving_obstacles.type[i] == PS_MOVING_OBSTACLE_SUBMARINE ? 1.0f : 0.0f;
        obs[o + 2] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
        obs[o + 3] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
    }
    idx += PS_MOVING_OBSTACLE_OBS_FEATURES;

    if (idx != PS_OBS_SIZE) {
        fprintf(stderr, "Puffer Survivors observation layout mismatch: %d != %d\n", idx, PS_OBS_SIZE);
        abort();
    }
}
