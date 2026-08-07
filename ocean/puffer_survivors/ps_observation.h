#pragma once

#include "ps_content.h"
#include "ps_observation_layout.h"

static inline int ps_obs_sector(float dx, float dy) {
    if (dy == 0.0f) return dx >= 0.0f ? 0 : 4;
    if (dx == 0.0f) return dy > 0.0f ? 2 : 6;

    if (dx > 0.0f) {
        if (dy > 0.0f) return dy < dx ? 0 : 1;
        return -dy > dx ? 6 : 7;
    }

    if (dy > 0.0f) return dy > -dx ? 2 : 3;
    return -dy < -dx ? 4 : 5;
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
    obs[idx++] = ps_clampf(env->hp / env->max_hp, 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->hp, 4.0f);
    obs[idx++] = ps_obs_soft_norm(env->max_hp, 8.0f);
    obs[idx++] = ps_obs_soft_norm((float)env->level, 20.0f);
    obs[idx++] = ps_clampf(env->xp / ps_xp_threshold(env), 0.0f, 1.0f);
    obs[idx++] = (float)(env->tick % wave_len) / (float)wave_len;
    obs[idx++] = ps_obs_soft_norm((float)(wave + 1), 12.0f);
    obs[idx++] = (float)(env->tick % boss_period) / (float)boss_period;

    int visible_enemies_idx = idx++;
    int visible_drops_idx = idx++;
    int bubble_ready_idx = idx++;
    obs[bubble_ready_idx] = 1.0f - ps_clampf(env->weapon_cd[PS_WEAPON_BUBBLE]
        / ps_weapon_cooldown_total(env, PS_WEAPON_BUBBLE), 0.0f, 1.0f);
    obs[idx++] = env->pending_upgrade ? 1.0f : 0.0f;
    obs[idx++] = env->cfg.invuln_steps > 0 ? ps_clampf((float)env->invuln_timer / (float)env->cfg.invuln_steps, 0.0f, 1.0f) : 0.0f;
    obs[idx++] = ps_clampf((float)env->queued_upgrades / 4.0f, 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->speed_bonus, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->damage_bonus, 1.0f);
    obs[idx++] = ps_clampf(1.0f - env->cooldown_mult, 0.0f, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->magnet_bonus, 1.0f);
    obs[idx++] = ps_obs_soft_norm(env->area_bonus, 1.0f);
    obs[idx++] = ps_obs_soft_norm((float)env->pierce_bonus, 4.0f);
    int nearest_health_distance_idx = idx++;

    // XP routing hints. These make the policy's pickup objective explicit
    // instead of forcing it to infer nearest-orb direction from coarse bins.
    int nearest_xp_dx_idx = idx++;
    int nearest_xp_dy_idx = idx++;
    int nearest_xp_distance_idx = idx++;
    int visible_xp_value_idx = idx++;
    int visible_xp_can_level_idx = idx++;

    float observe_radius = env->cfg.arena_size * 0.45f;
    float observe_radius2 = observe_radius * observe_radius;
    float inv_observe_radius = 1.0f / observe_radius;
    float inv_enemy_cap = 1.0f / (float)env->cfg.enemy_cap;
    float inv_drop_cap = 1.0f / (float)env->cfg.drop_cap;
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

    for (int k = 0; k < env->enemy_count; k++) {
        int i = env->enemies.dense[k];
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
        obs[o + 0] = fminf(obs[o + 0] + 0.125f, 1.0f);
        obs[o + 1] = fmaxf(obs[o + 1], proximity);
        obs[o + 2] = fmaxf(obs[o + 2], env->enemies.hp[i] / env->enemies.max_hp[i]);
        obs[o + 3] = fmaxf(obs[o + 3], env->enemies.damage[i] / 4.0f);
    }

    if (nearest_boss >= 0) {
        float dx = env->enemies.x[nearest_boss] - env->px;
        float dy = env->enemies.y[nearest_boss] - env->py;
        float d = sqrtf(fmaxf(nearest_boss_d2, 0.0001f));
        obs[boss_base + PS_BOSS_PRESENT] = 1.0f;
        obs[boss_base + PS_BOSS_DX] = ps_clampf(dx * inv_observe_radius, -1.0f, 1.0f);
        obs[boss_base + PS_BOSS_DY] = ps_clampf(dy * inv_observe_radius, -1.0f, 1.0f);
        obs[boss_base + PS_BOSS_PROXIMITY] = 1.0f - ps_clampf(d * inv_observe_radius, 0.0f, 1.0f);
        obs[boss_base + PS_BOSS_HP_FRACTION] = ps_clampf(env->enemies.hp[nearest_boss] / env->enemies.max_hp[nearest_boss], 0.0f, 1.0f);
        obs[boss_base + PS_BOSS_MAX_HP] = ps_obs_soft_norm(env->enemies.max_hp[nearest_boss], 96.0f);
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
        obs[nearest_xp_distance_idx] = 1.0f
            - ps_clampf(nearest_xp_dist * inv_observe_radius, 0.0f, 1.0f);
    }
    if (nearest_health_d2 < 1e29f) {
        float nearest_health_dist = sqrtf(nearest_health_d2);
        obs[nearest_health_distance_idx] = 1.0f
            - ps_clampf(nearest_health_dist * inv_observe_radius, 0.0f, 1.0f);
    }
    obs[visible_xp_value_idx] = ps_clampf(visible_xp_value / ps_xp_threshold(env), 0.0f, 1.0f);
    obs[visible_xp_can_level_idx] = visible_xp_value >= fmaxf(ps_xp_threshold(env) - env->xp, 0.0f) ? 1.0f : 0.0f;

    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        float dx = env->obstacles.x[i] - env->px;
        float dy = env->obstacles.y[i] - env->py;
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
        int level = env->weapon_level[i];
        float cd_total = ps_weapon_cooldown_total(env, i);
        float ready = level > 0 ? 1.0f - ps_clampf(env->weapon_cd[i] / cd_total, 0.0f, 1.0f) : 0.0f;
        obs[idx++] = (float)level / (float)env->cfg.weapon_max_level;
        obs[idx++] = ready;
        obs[idx++] = ps_clampf(env->weapon_active[i], 0.0f, 1.0f);
        obs[idx++] = ps_weapon_power(env, i);
    }

    if (env->pending_upgrade) {
        for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
            obs[idx + env->offered[i]] = 1.0f;
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
}
