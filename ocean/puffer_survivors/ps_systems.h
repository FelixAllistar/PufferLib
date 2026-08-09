#pragma once

#include "ps_observation.h"
#include "ps_geometry.h"

static inline int ps_obstacle_position_clear(PufferSurvivors* env, int count, int skip, float x, float y, float radius) {
    if (ps_dist2(x, y, env->px, env->py)
            < env->cfg.obstacle_player_spawn_clearance
            * env->cfg.obstacle_player_spawn_clearance) return 0;
    for (int i = 0; i < count; i++) {
        if (i == skip) continue;
        float min_dist = radius + env->obstacles.radius[i] + env->cfg.obstacle_spawn_clearance;
        float dx = x - env->obstacles.x[i];
        float dy = y - env->obstacles.y[i];
        if (ps_geometry_circle_overlaps(dx, dy, min_dist)) return 0;
    }
    return 1;
}

static inline int ps_overlaps_obstacle(PufferSurvivors* env, float x, float y, float radius, float padding) {
    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        float min_dist = radius + env->obstacles.radius[i] + padding;
        float dx = x - env->obstacles.x[i];
        float dy = y - env->obstacles.y[i];
        if (ps_geometry_circle_overlaps(dx, dy, min_dist)) return 1;
    }
    return 0;
}

static inline void ps_push_out_obstacles(PufferSurvivors* env, float* x, float* y, float radius, int penalize) {
    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        int pushed = ps_geometry_push_out_shape_circle(x, y, PS_SHAPE_CIRCLE,
            radius, radius, radius, env->obstacles.x[i], env->obstacles.y[i],
            env->obstacles.radius[i]);
        if (pushed && penalize) {
            env->agents[0].rewards[0] += env->cfg.obstacle_penalty;
            env->episode_reward_obstacle += env->cfg.obstacle_penalty;
            env->episode_obstacle_hits += 1.0f;
        }
    }
}

static inline void ps_push_out_obstacles_shape(PufferSurvivors* env,
        float* x, float* y, int shape, float radius, float half_width,
        float half_height, int penalize) {
    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        int pushed = ps_geometry_push_out_shape_circle(x, y, shape, radius,
            half_width, half_height, env->obstacles.x[i], env->obstacles.y[i],
            env->obstacles.radius[i]);
        if (pushed && penalize) {
            env->agents[0].rewards[0] += env->cfg.obstacle_penalty;
            env->episode_reward_obstacle += env->cfg.obstacle_penalty;
            env->episode_obstacle_hits += 1.0f;
        }
    }
}

static inline void ps_spawn_obstacles(PufferSurvivors* env) {
    float half = 0.5f * env->cfg.arena_size;
    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        env->obstacles.radius[i] = env->cfg.obstacle_radius_min
            + ps_randf(env) * (env->cfg.obstacle_radius_max - env->cfg.obstacle_radius_min);
        env->obstacles.type[i] = (uint8_t)(ps_rand_u32(env) % 3u);
        int placed = 0;
        for (int tries = 0; tries < 96; tries++) {
            float angle = ps_randf(env) * 2.0f * PI;
            float dist = half * (env->cfg.obstacle_spawn_min_ratio
                + (env->cfg.obstacle_spawn_max_ratio - env->cfg.obstacle_spawn_min_ratio)
                * ps_randf(env));
            float x = env->px + cosf(angle) * dist;
            float y = env->py + sinf(angle) * dist;
            if (!ps_obstacle_position_clear(env, i, -1, x, y, env->obstacles.radius[i])) continue;
            env->obstacles.x[i] = x;
            env->obstacles.y[i] = y;
            placed = 1;
            break;
        }
        if (!placed) {
            float a = (float)i * env->cfg.obstacle_fallback_angle_step
                + ps_randf(env) * env->cfg.obstacle_fallback_angle_jitter;
            float r = half * (env->cfg.obstacle_fallback_min_ratio
                + (env->cfg.obstacle_fallback_max_ratio - env->cfg.obstacle_fallback_min_ratio)
                * ((float)(i % env->cfg.obstacle_fallback_spoke_count)
                / (float)(env->cfg.obstacle_fallback_spoke_count - 1)));
            env->obstacles.x[i] = env->px + cosf(a) * r;
            env->obstacles.y[i] = env->py + sinf(a) * r;
        }
    }
}

static inline void ps_recycle_obstacle(PufferSurvivors* env, int idx) {
    float half = 0.5f * env->cfg.arena_size;
    for (int tries = 0; tries < 64; tries++) {
        float angle = ps_randf(env) * 2.0f * PI;
        float dist = half * (env->cfg.obstacle_recycle_spawn_min_ratio
            + (env->cfg.obstacle_recycle_spawn_max_ratio
            - env->cfg.obstacle_recycle_spawn_min_ratio) * ps_randf(env));
        float x = env->px + cosf(angle) * dist;
        float y = env->py + sinf(angle) * dist;
        if (!ps_obstacle_position_clear(env, env->cfg.obstacle_count, idx, x, y, env->obstacles.radius[idx])) continue;
        env->obstacles.x[idx] = x;
        env->obstacles.y[idx] = y;
        env->obstacles.type[idx] = (uint8_t)(ps_rand_u32(env) % 3u);
        return;
    }
}

static inline void ps_recycle_far_obstacles(PufferSurvivors* env) {
    float recycle_radius = env->cfg.arena_size * env->cfg.obstacle_recycle_radius_ratio;
    float recycle2 = recycle_radius * recycle_radius;
    for (int i = 0; i < env->cfg.obstacle_count; i++) {
        if (ps_dist2(env->obstacles.x[i], env->obstacles.y[i], env->px, env->py) > recycle2) {
            ps_recycle_obstacle(env, i);
        }
    }
}

static inline int ps_find_free_slot(uint8_t* active, int cap, int* cursor);
static inline void ps_dense_add(int* dense, int* dense_pos, int* count, int slot);
static inline void ps_deactivate_moving_obstacle(PufferSurvivors* env, int i);

static inline int ps_spawn_moving_obstacle(PufferSurvivors* env) {
    if (env->cfg.moving_obstacle_cap <= 0
            || env->moving_obstacle_count >= env->cfg.moving_obstacle_cap) return 0;
    int i = ps_find_free_slot(env->moving_obstacles.active,
        PS_MAX_MOVING_OBSTACLES, &env->next_moving_obstacle_slot);

    int type = (int)(ps_rand_u32(env) % PS_MOVING_OBSTACLE_TYPE_COUNT);
    float half = 0.5f * env->cfg.arena_size;
    float margin = env->cfg.moving_obstacle_spawn_margin;
    float hw = env->cfg.moving_obstacle_half_width[type];
    float hh = env->cfg.moving_obstacle_half_height[type];
    float speed = env->cfg.moving_obstacle_speed[type];
    env->moving_obstacles.type[i] = (uint8_t)type;
    env->moving_obstacles.shape[i] = PS_SHAPE_AABB;
    env->moving_obstacles.half_width[i] = hw;
    env->moving_obstacles.half_height[i] = hh;
    env->moving_obstacles.bound_radius[i] = ps_geometry_shape_bound_radius(
        PS_SHAPE_AABB, 0.0f, hw, hh);
    env->moving_obstacles.ttl[i] = env->cfg.moving_obstacle_ttl;
    if (type == PS_MOVING_OBSTACLE_ANCHOR) {
        env->moving_obstacles.x[i] = env->px
            + (ps_randf(env) * 2.0f - 1.0f) * half * 0.72f;
        env->moving_obstacles.y[i] = env->py - half - margin - hh;
        env->moving_obstacles.vx[i] = 0.0f;
        env->moving_obstacles.vy[i] = speed;
    } else {
        int from_left = (int)(ps_rand_u32(env) & 1u) == 0;
        env->moving_obstacles.x[i] = env->px
            + (from_left ? -1.0f : 1.0f) * (half + margin + hw);
        env->moving_obstacles.y[i] = env->py
            + (ps_randf(env) * 2.0f - 1.0f) * half * 0.72f;
        env->moving_obstacles.vx[i] = (from_left ? 1.0f : -1.0f) * speed;
        env->moving_obstacles.vy[i] = 0.0f;
    }
    env->moving_obstacles.active[i] = 1;
    ps_dense_add(env->moving_obstacles.dense,
        env->moving_obstacles.dense_pos, &env->moving_obstacle_count, i);
    return 1;
}

static inline void ps_update_moving_obstacles(PufferSurvivors* env) {
    int wave = ps_wave_index(env);
    if (wave >= env->cfg.moving_obstacle_start_wave
            && env->tick % env->cfg.moving_obstacle_spawn_interval == 0) {
        ps_spawn_moving_obstacle(env);
    }

    float half = 0.5f * env->cfg.arena_size;
    float cleanup = half + env->cfg.moving_obstacle_spawn_margin + half;
    float cleanup2 = cleanup * cleanup;
    int k = 0;
    while (k < env->moving_obstacle_count) {
        int i = env->moving_obstacles.dense[k];
        env->moving_obstacles.x[i] += env->moving_obstacles.vx[i];
        env->moving_obstacles.y[i] += env->moving_obstacles.vy[i];
        env->moving_obstacles.ttl[i]--;
        float dx = env->px - env->moving_obstacles.x[i];
        float dy = env->py - env->moving_obstacles.y[i];
        int outside = dx * dx + dy * dy > cleanup2;
        if (env->moving_obstacles.ttl[i] <= 0 || outside) {
            ps_deactivate_moving_obstacle(env, i);
            continue;
        }
        if (env->invuln_timer <= 0 && env->cfg.moving_obstacle_damage > 0.0f
                && ps_geometry_shape_overlaps_circle(
                    env->moving_obstacles.shape[i],
                    dx, dy, env->moving_obstacles.bound_radius[i],
                    env->moving_obstacles.half_width[i],
                    env->moving_obstacles.half_height[i],
                    env->cfg.player_radius)) {
            float damage = fmaxf(1.0f, ceilf(env->cfg.moving_obstacle_damage));
            env->hp -= damage;
            env->agents[0].rewards[0] += env->cfg.reward_hurt * damage;
            env->episode_reward_hurt += env->cfg.reward_hurt * damage;
            env->episode_damage_taken += damage;
            env->invuln_timer = env->cfg.invuln_steps;
        }
        k++;
    }
}

static inline void ps_clear_entities(PufferSurvivors* env) {
    memset(&env->enemies, 0, sizeof(env->enemies));
    memset(&env->projectiles, 0, sizeof(env->projectiles));
    memset(&env->drops, 0, sizeof(env->drops));
    memset(&env->areas, 0, sizeof(env->areas));
    memset(&env->moving_obstacles, 0, sizeof(env->moving_obstacles));
    memset(env->enemies.dense_pos, 0xff, sizeof(env->enemies.dense_pos));
    memset(env->projectiles.dense_pos, 0xff, sizeof(env->projectiles.dense_pos));
    memset(env->drops.dense_pos, 0xff, sizeof(env->drops.dense_pos));
    memset(env->areas.dense_pos, 0xff, sizeof(env->areas.dense_pos));
    memset(env->moving_obstacles.dense_pos, 0xff,
        sizeof(env->moving_obstacles.dense_pos));

    env->enemy_count = 0;
    env->projectile_count = 0;
    env->drop_count = 0;
    env->area_count = 0;
    env->moving_obstacle_count = 0;
    env->active_ink_count = 0;
    env->next_enemy_slot = 0;
    env->next_projectile_slot = 0;
    env->next_drop_slot = 0;
    env->next_area_slot = 0;
    env->next_moving_obstacle_slot = 0;
    env->nearest_enemy = -1;
    env->nearest_enemy_d2 = 1e30f;

    for (int i = 0; i < PS_GRID_CELLS; i++) {
        env->grid_head[i] = -1;
    }
    env->grid_touched_count = 0;
    env->aabb_count = 0;
}

static inline int ps_find_free_slot(uint8_t* active, int cap, int* cursor) {
    int start = *cursor;
    for (int tries = 0; tries < cap; tries++) {
        int i = start + tries;
        if (i >= cap) i -= cap;

        if (!active[i]) {
            int next = i + 1;
            *cursor = next >= cap ? 0 : next;
            return i;
        }
    }

    return -1;
}

static inline void ps_dense_add(int* dense, int* dense_pos, int* count, int slot) {
    int pos = *count;
    dense[pos] = slot;
    dense_pos[slot] = pos;
    *count = pos + 1;
}

static inline void ps_dense_remove(int* dense, int* dense_pos, int* count, int slot) {
    int pos = dense_pos[slot];
    int last_pos = *count - 1;
    int moved = dense[last_pos];
    dense[pos] = moved;
    dense_pos[moved] = pos;
    dense_pos[slot] = -1;
    *count = last_pos;
}

static inline void ps_deactivate_enemy(PufferSurvivors* env, int i) {
    env->enemies.active[i] = 0;
    ps_dense_remove(env->enemies.dense, env->enemies.dense_pos, &env->enemy_count, i);
}

static inline void ps_deactivate_projectile(PufferSurvivors* env, int i) {
    env->projectiles.active[i] = 0;
    ps_dense_remove(env->projectiles.dense, env->projectiles.dense_pos, &env->projectile_count, i);
}

static inline void ps_deactivate_drop(PufferSurvivors* env, int i) {
    env->drops.active[i] = 0;
    ps_dense_remove(env->drops.dense, env->drops.dense_pos, &env->drop_count, i);
}

static inline void ps_deactivate_area(PufferSurvivors* env, int i) {
    env->areas.active[i] = 0;
    if (env->areas.type[i] == PS_WEAPON_INK) env->active_ink_count--;
    ps_dense_remove(env->areas.dense, env->areas.dense_pos, &env->area_count, i);
}

static inline void ps_deactivate_moving_obstacle(PufferSurvivors* env, int i) {
    env->moving_obstacles.active[i] = 0;
    ps_dense_remove(env->moving_obstacles.dense,
        env->moving_obstacles.dense_pos, &env->moving_obstacle_count, i);
}

#ifdef PS_DEBUG_COUNTS
static inline void ps_verify_counts(PufferSurvivors* env) {
    int enemies = 0;
    int projectiles = 0;
    int drops = 0;
    int areas = 0;

    for (int i = 0; i < env->cfg.enemy_cap; i++) {
        enemies += env->enemies.active[i] ? 1 : 0;
    }

    for (int i = 0; i < env->cfg.projectile_cap; i++) {
        projectiles += env->projectiles.active[i] ? 1 : 0;
    }

    for (int i = 0; i < env->cfg.drop_cap; i++) {
        drops += env->drops.active[i] ? 1 : 0;
    }

    for (int i = 0; i < PS_MAX_AREAS; i++) {
        areas += env->areas.active[i] ? 1 : 0;
    }

    if (
        enemies != env->enemy_count ||
        projectiles != env->projectile_count ||
        drops != env->drop_count ||
        areas != env->area_count
    ) {
        fprintf(stderr,
            "count mismatch: enemies %d/%d projectiles %d/%d drops %d/%d areas %d/%d\n",
            enemies, env->enemy_count,
            projectiles, env->projectile_count,
            drops, env->drop_count,
            areas, env->area_count
        );
        abort();
    }
}
#endif

static inline void ps_offer_upgrades(PufferSurvivors* env) {
    if (env->pending_upgrade) return;
    env->pending_upgrade = 1;
    for (int i = 0; i < PS_UPGRADE_SLOTS; i++) {
        int offer = -1;
        for (;;) {
            int candidate = (int)(ps_rand_u32(env) % PS_UPGRADE_COUNT);
            int duplicate = 0;
            for (int j = 0; j < i; j++) duplicate |= env->offered[j] == candidate;
            if (!duplicate && ps_upgrade_available(env, candidate)) {
                offer = candidate;
                break;
            }
        }
        env->offered[i] = offer;
    }
}

static inline void ps_apply_upgrade_effect(PufferSurvivors* env, int upgrade) {
    switch (upgrade) {
        case PS_UPGRADE_BUBBLE:
        case PS_UPGRADE_WHIRLPOOL:
        case PS_UPGRADE_ORBIT:
        case PS_UPGRADE_INK:
        case PS_UPGRADE_SONAR:
            if (env->weapon_level[upgrade] < env->cfg.weapon_max_level)
                env->weapon_level[upgrade]++;
            break;
        case PS_UPGRADE_SPEED: env->speed_bonus += env->cfg.upgrade_speed_bonus; break;
        case PS_UPGRADE_MAGNET: env->magnet_bonus += env->cfg.upgrade_magnet_bonus; break;
        case PS_UPGRADE_HEALTH:
            env->max_hp += env->cfg.upgrade_health_bonus;
            env->hp = fminf(env->max_hp, env->hp + env->cfg.health_heal);
            break;
        case PS_UPGRADE_MIGHT: env->damage_bonus += env->cfg.upgrade_might_bonus; break;
        case PS_UPGRADE_COOLDOWN: env->cooldown_mult *= env->cfg.upgrade_cooldown_multiplier; break;
        case PS_UPGRADE_AREA: env->area_bonus += env->cfg.upgrade_area_bonus; break;
        case PS_UPGRADE_PIERCE: env->pierce_bonus += 1; break;
    }
}

static inline void ps_apply_upgrade(PufferSurvivors* env, int choice) {
    int upgrade = env->offered[choice];
    ps_apply_upgrade_effect(env, upgrade);
    env->episode_levelups += 1.0f;
    env->pending_upgrade = 0;
    if (env->queued_upgrades > 0) env->queued_upgrades--;
    if (env->queued_upgrades > 0) ps_offer_upgrades(env);
}

static inline void ps_add_log(PufferSurvivors* env, int survived) {
    float perf = ps_clampf((float)env->tick / (float)env->cfg.max_steps, 0.0f, 1.0f);
    env->log.perf += perf;
    env->log.score += env->episode_score;
    env->log.episode_return += env->episode_return;
    env->log.reward_survival += env->episode_reward_survival;
    env->log.reward_damage += env->episode_reward_damage;
    env->log.reward_kill += env->episode_reward_kill;
    env->log.reward_hurt += env->episode_reward_hurt;
    env->log.reward_pickup += env->episode_reward_pickup;
    env->log.reward_xp += env->episode_reward_xp;
    env->log.reward_levelup += env->episode_reward_levelup;
    env->log.reward_obstacle += env->episode_reward_obstacle;
    env->log.reward_terminal += env->episode_reward_terminal;
    env->log.episode_length += (float)env->tick;
    env->log.kills += env->episode_kills;
    env->log.level += (float)env->level;
    env->log.xp += env->episode_xp;
    env->log.damage_dealt += env->episode_damage_dealt;
    env->log.damage_taken += env->episode_damage_taken;
    env->log.pickups += env->episode_pickups;
    env->log.levelups += env->episode_levelups;
    env->log.obstacle_hits += env->episode_obstacle_hits;
    env->log.enemies_alive += (float)env->enemy_count;
    env->log.projectiles_alive += (float)env->projectile_count;
    env->log.drops_alive += (float)env->drop_count;
    env->log.areas_alive += (float)env->area_count;
    int weapon_levels = 0;
    for (int i = 0; i < PS_WEAPON_COUNT; i++) weapon_levels += env->weapon_level[i];
    env->log.weapon_levels += (float)weapon_levels;
    env->log.wave += (float)(ps_wave_index(env) + 1);
    env->log.hp += env->hp;
    env->log.survived += (float)survived;
    env->log.n += 1.0f;
    env->log.peak_enemies += env->episode_peak_enemies;
    env->log.peak_projectiles += env->episode_peak_projectiles;
    env->log.min_hp += env->episode_min_hp;
    if (survived) {
        env->log.success += 1.0f;
    } else {
        float progress = (float)env->tick / (float)env->cfg.max_steps;
        if (progress < 0.25f) env->log.death_0_25 += 1.0f;
        else if (progress < 0.50f) env->log.death_25_50 += 1.0f;
        else if (progress < 0.75f) env->log.death_50_75 += 1.0f;
        else env->log.death_75_100 += 1.0f;
    }
}

static inline int ps_pick_spawn_side(PufferSurvivors* env) {
    float m2 = env->pvx * env->pvx + env->pvy * env->pvy;
    if (m2 > 0.0001f && ps_randf(env) < env->cfg.spawn_velocity_bias_probability) {
        if (fabsf(env->pvx) > fabsf(env->pvy)) return env->pvx > 0.0f ? 1 : 0;
        return env->pvy > 0.0f ? 3 : 2;
    }
    return (int)(ps_rand_u32(env) & 3u);
}

static inline void ps_pick_spawn_position(PufferSurvivors* env, float radius, float* x, float* y) {
    float half = 0.5f * env->cfg.arena_size;
    float edge = half + radius + env->cfg.enemy_spawn_edge_margin;
    float along = (ps_randf(env) * 2.0f - 1.0f) * half * env->cfg.enemy_spawn_along_ratio;
    int side = ps_pick_spawn_side(env);
    if (side == 0) {
        *x = env->px - edge;
        *y = env->py + along;
    } else if (side == 1) {
        *x = env->px + edge;
        *y = env->py + along;
    } else if (side == 2) {
        *x = env->px + along;
        *y = env->py - edge;
    } else {
        *x = env->px + along;
        *y = env->py + edge;
    }
}

static inline PSEnemyDef ps_enemy_stats(PufferSurvivors* env, int* kind_out,
        float* radius_out, int elite, int boss, int ari_k) {
    int wave = ps_wave_index(env);
    float progress = ps_episode_progress(env);
    int kind = 0;
    if (wave >= env->cfg.enemy_mix_start_wave) {
        uint32_t roll = ps_rand_u32(env) % 100u;
        if (wave < env->cfg.enemy_mix_phase_one_end_wave) {
            kind = roll < (uint32_t)env->cfg.enemy_mix_phase_one_jelly_pct ? 1 : 0;
        } else if (wave < env->cfg.enemy_mix_phase_two_end_wave) {
            kind = roll < (uint32_t)env->cfg.enemy_mix_phase_two_urchin_pct
                ? 2
                : (roll < (uint32_t)env->cfg.enemy_mix_phase_two_jelly_pct ? 1 : 0);
        } else {
            kind = roll < (uint32_t)env->cfg.enemy_mix_late_urchin_pct
                ? 2
                : (roll < (uint32_t)env->cfg.enemy_mix_late_eel_pct
                    ? 3
                    : (roll < (uint32_t)env->cfg.enemy_mix_late_jelly_pct ? 1 : 0));
        }
    }

    PSEnemyDef stats = {
        env->cfg.enemy_base_hp[kind],
        env->cfg.enemy_speed_mult[kind],
        env->cfg.enemy_base_damage[kind],
    };
    float radius = env->cfg.enemy_radius[kind];
    float hp_growth = 1.0f + env->cfg.enemy_hp_growth_per_wave * (float)wave
        + env->cfg.enemy_hp_progress_scale * progress * env->cfg.spawn_ramp;
    float speed_growth_wave = (float)(wave < env->cfg.enemy_speed_growth_wave_cap
        ? wave : env->cfg.enemy_speed_growth_wave_cap);
    float speed_growth = 1.0f + env->cfg.enemy_speed_growth_per_wave * speed_growth_wave;
    stats.hp *= hp_growth * env->cfg.enemy_hp_scale;
    stats.speed_mult *= env->cfg.enemy_speed * speed_growth;
    stats.damage *= env->cfg.enemy_damage_scale;

    if (elite) {
        stats.hp *= env->cfg.elite_hp_multiplier;
        stats.speed_mult *= env->cfg.elite_speed_multiplier;
        radius = fmaxf(radius + env->cfg.elite_radius_bonus, env->cfg.elite_min_radius);
        stats.damage *= env->cfg.elite_damage_multiplier;
    }
    if (boss) {
        stats.hp = (env->cfg.boss_hp_base + env->cfg.boss_hp_per_wave * (float)wave)
            * env->cfg.enemy_hp_scale;
        stats.speed_mult = env->cfg.enemy_speed * env->cfg.boss_speed_multiplier;
        radius = env->cfg.boss_radius;
        stats.damage = env->cfg.boss_damage * env->cfg.enemy_damage_scale;
    }
    if (ari_k) {
        stats.hp *= env->cfg.ari_k_hp_multiplier;
        stats.speed_mult = env->cfg.enemy_speed * env->cfg.ari_k_speed_multiplier;
        radius = env->cfg.ari_k_radius;
        stats.damage = env->cfg.ari_k_damage * env->cfg.enemy_damage_scale;
    }

    stats.hp = fmaxf(1.0f, ceilf(stats.hp));
    stats.damage = fmaxf(1.0f, ceilf(stats.damage));
    *kind_out = kind;
    *radius_out = radius;
    return stats;
}

static inline void ps_enemy_geometry(PufferSurvivors* env, int kind, int ari_k,
        float radius, int* shape, float* half_width, float* half_height,
        float* bound_radius) {
    *shape = ari_k ? env->cfg.ari_k_shape : env->cfg.enemy_shape[kind];
    *half_width = ari_k ? env->cfg.ari_k_half_width : env->cfg.enemy_half_width[kind];
    *half_height = ari_k ? env->cfg.ari_k_half_height : env->cfg.enemy_half_height[kind];
    *bound_radius = ps_geometry_shape_bound_radius(*shape, radius,
        *half_width, *half_height);
}

static inline int ps_spawn_enemy(PufferSurvivors* env) {
    if (env->enemy_count >= env->cfg.enemy_cap) return 0;

    int slot = ps_find_free_slot(env->enemies.active, env->cfg.enemy_cap, &env->next_enemy_slot);

    float x = 0.0f, y = 0.0f;
    ps_pick_spawn_position(env, env->cfg.enemy_spawn_radius, &x, &y);
    for (int tries = 0; tries < 16 && ps_overlaps_obstacle(env, x, y,
            env->cfg.enemy_spawn_radius, env->cfg.enemy_spawn_padding); tries++) {
        ps_pick_spawn_position(env, env->cfg.enemy_spawn_radius, &x, &y);
    }

    int elite = ps_randf(env) < env->cfg.elite_spawn_rate
        + env->cfg.elite_spawn_ramp_per_tick * (float)env->tick;
    int wave_len = env->cfg.wave_length_steps;
    int wave = ps_wave_index(env);
    int ari_k_wave = wave >= env->cfg.ari_k_start_wave
        && (wave - env->cfg.ari_k_start_wave) % env->cfg.ari_k_wave_period == 0;
    int ari_k = ari_k_wave && env->tick % wave_len == 1
        && env->last_boss_tick != env->tick;
    int boss = ari_k || (env->tick > 0 && env->tick % env->cfg.boss_period_steps == 0
        && env->last_boss_tick != env->tick);
    if (boss) env->last_boss_tick = env->tick;
    int kind = 0;
    float radius = 0.0f;
    PSEnemyDef stats = ps_enemy_stats(env, &kind, &radius, elite, boss, ari_k);
    uint8_t visual_type = (uint8_t)(kind & PS_ENEMY_KIND_MASK);
    if (elite) visual_type |= PS_ENEMY_ELITE_FLAG;
    if (boss) visual_type = PS_ENEMY_BOSS_FLAG;
    if (ari_k) visual_type |= PS_ENEMY_ARI_K_FLAG;
    env->enemies.type[slot] = visual_type;
    env->enemies.x[slot] = x;
    env->enemies.y[slot] = y;
    env->enemies.vx[slot] = 0.0f;
    env->enemies.vy[slot] = 0.0f;
    env->enemies.max_hp[slot] = stats.hp;
    env->enemies.hp[slot] = stats.hp;
    env->enemies.radius[slot] = radius;
    int shape = PS_SHAPE_CIRCLE;
    float half_width = radius;
    float half_height = radius;
    float bound_radius = radius;
    ps_enemy_geometry(env, kind, ari_k, radius, &shape, &half_width,
        &half_height, &bound_radius);
    env->enemies.shape[slot] = (uint8_t)shape;
    env->enemies.half_width[slot] = half_width;
    env->enemies.half_height[slot] = half_height;
    env->enemies.bound_radius[slot] = bound_radius;
    env->enemies.speed[slot] = stats.speed_mult;
    env->enemies.damage[slot] = stats.damage;
    env->enemies.active[slot] = 1;
    ps_dense_add(env->enemies.dense, env->enemies.dense_pos, &env->enemy_count, slot);
    return slot + 1;
}

static inline void ps_spawn_drop(PufferSurvivors* env, float x, float y, float value, int type) {
    if (env->drop_count >= env->cfg.drop_cap) return;

    int i = ps_find_free_slot(env->drops.active, env->cfg.drop_cap, &env->next_drop_slot);

    ps_push_out_obstacles(env, &x, &y, env->cfg.drop_spawn_radius, 0);
    env->drops.x[i] = x;
    env->drops.y[i] = y;
    env->drops.value[i] = value;
    env->drops.type[i] = (uint8_t)type;
    env->drops.active[i] = 1;
    ps_dense_add(env->drops.dense, env->drops.dense_pos, &env->drop_count, i);
}

static inline void ps_spawn_projectile(PufferSurvivors* env, int type, float sx, float sy, float tx, float ty, float damage, float radius, float speed, int pierce, int ttl) {
    if (env->projectile_count >= env->cfg.projectile_cap) return;

    int i = ps_find_free_slot(env->projectiles.active, env->cfg.projectile_cap, &env->next_projectile_slot);

    float dx = tx - sx;
    float dy = ty - sy;
    float d = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
    env->projectiles.type[i] = (uint8_t)type;
    env->projectiles.x[i] = sx;
    env->projectiles.y[i] = sy;
    env->projectiles.vx[i] = dx / d * speed;
    env->projectiles.vy[i] = dy / d * speed;
    env->projectiles.damage[i] = damage;
    env->projectiles.radius[i] = radius;
    env->projectiles.ttl[i] = ttl;
    env->projectiles.pierce[i] = pierce;
    env->projectiles.active[i] = 1;
    ps_dense_add(env->projectiles.dense, env->projectiles.dense_pos, &env->projectile_count, i);
}

static inline void ps_spawn_area(PufferSurvivors* env, int type, float x, float y, float radius, float damage, int ttl, int tick_rate) {
    if (env->area_count >= env->cfg.area_cap) return;

    int i = ps_find_free_slot(env->areas.active, PS_MAX_AREAS, &env->next_area_slot);

    ps_push_out_obstacles(env, &x, &y, radius, 0);
    env->areas.type[i] = (uint8_t)type;
    env->areas.x[i] = x;
    env->areas.y[i] = y;
    env->areas.radius[i] = radius;
    env->areas.damage[i] = damage;
    env->areas.ttl[i] = ttl;
    env->areas.tick_rate[i] = tick_rate;
    env->areas.tick_timer[i] = 0;
    env->areas.active[i] = 1;
    if (type == PS_WEAPON_INK) env->active_ink_count++;
    ps_dense_add(env->areas.dense, env->areas.dense_pos, &env->area_count, i);
}

static inline void ps_rebuild_grid(PufferSurvivors* env) {
    for (int i = 0; i < env->grid_touched_count; i++) {
        env->grid_head[env->grid_touched[i]] = -1;
    }
    env->grid_touched_count = 0;
    env->aabb_count = 0;
    for (int k = 0; k < env->enemy_count; k++) {
        int i = env->enemies.dense[k];
        env->enemies.next[i] = -1;
        if (env->enemies.shape[i] == PS_SHAPE_AABB) {
            env->aabb_indices[env->aabb_count++] = i;
        }
        int cell = ps_cell(env, env->enemies.x[i], env->enemies.y[i]);
        if (env->grid_head[cell] == -1 && env->grid_touched_count < PS_MAX_ENEMIES) {
            env->grid_touched[env->grid_touched_count++] = cell;
        }
        env->enemies.next[i] = env->grid_head[cell];
        env->grid_head[cell] = i;
    }
}

static inline int ps_grid_needed(PufferSurvivors* env) {
    if (env->enemy_count <= 0) return 0;
    if (env->projectile_count > 0 || env->active_ink_count > 0) return 1;
    for (int weapon = 0; weapon < PS_WEAPON_COUNT; weapon++) {
        if (env->weapon_level[weapon] > 0 && env->weapon_cd[weapon] <= 1.0f)
            return 1;
    }
    return 0;
}

static inline int ps_damage_enemy(PufferSurvivors* env, int eidx, float damage) {
    if (!env->enemies.active[eidx] || damage <= 0.0f) return 0;
    env->enemies.hp[eidx] -= damage;
    env->agents[0].rewards[0] += env->cfg.reward_damage * damage;
    env->episode_reward_damage += env->cfg.reward_damage * damage;
    env->episode_damage_dealt += damage;
    if (env->enemies.hp[eidx] > 0.0f) return 0;

    uint8_t type = env->enemies.type[eidx];
    int elite = (type & PS_ENEMY_ELITE_FLAG) != 0;
    int boss = (type & PS_ENEMY_BOSS_FLAG) != 0;
    env->agents[0].rewards[0] += env->cfg.reward_kill;
    env->episode_reward_kill += env->cfg.reward_kill;
    env->episode_kills += 1.0f;
    env->episode_score += boss ? env->cfg.kill_score_boss
        : (elite ? env->cfg.kill_score_elite : env->cfg.kill_score_default);
    ps_spawn_drop(env, env->enemies.x[eidx], env->enemies.y[eidx],
        boss ? env->cfg.drop_value_boss
        : (elite ? env->cfg.drop_value_elite : env->cfg.drop_value_default), 0);
    float missing_hp = ps_clampf((env->max_hp - env->hp) / env->max_hp, 0.0f, 1.0f);
    float health_chance = env->cfg.health_drop_rate
        * (1.0f + env->cfg.health_drop_elite_bonus * (float)elite
        + env->cfg.health_drop_boss_bonus * (float)boss
        + env->cfg.health_drop_missing_hp_bonus * missing_hp);
    if (ps_randf(env) < health_chance) {
        ps_spawn_drop(env, env->enemies.x[eidx] + env->cfg.health_drop_offset_x,
            env->enemies.y[eidx] + env->cfg.health_drop_offset_y,
            env->cfg.health_heal, 1);
    }
    ps_deactivate_enemy(env, eidx);
    return 1;
}

static inline void ps_wave_spawns(PufferSurvivors* env) {
    int enemies = env->enemy_count;
    int target = ps_wave_minimum(env);
    int burst = 0;
    while (enemies < target && burst < 4) {
        if (!ps_spawn_enemy(env)) break;
        enemies++;
        burst++;
    }

    int interval = ps_wave_spawn_interval(env);
    if (env->tick % interval == 0) ps_spawn_enemy(env);

    int len = env->cfg.wave_length_steps;
    int local = env->tick % len;
    int wave = ps_wave_index(env);
    if (local == 1 && wave >= env->cfg.ari_k_start_wave
            && (wave - env->cfg.ari_k_start_wave) % env->cfg.ari_k_wave_period == 0)
        ps_spawn_enemy(env);
    int special_ring = 0;
    for (int i = 0; i < env->cfg.special_ring_wave_count; i++) {
        special_ring |= wave == env->cfg.special_ring_waves[i];
    }
    if (local == 1 && special_ring) {
        float half = 0.5f * env->cfg.arena_size;
        float radius = half * env->cfg.special_ring_radius_ratio;
        for (int i = 0; i < env->cfg.special_ring_enemy_count; i++) {
            int slot = ps_spawn_enemy(env);
            if (!slot) return;
            int idx = slot - 1;
            float angle = 2.0f * PI * ((float)i
                / (float)env->cfg.special_ring_enemy_count);
            env->enemies.x[idx] = env->px + cosf(angle) * radius;
            env->enemies.y[idx] = env->py + sinf(angle) * radius;
            env->enemies.speed[idx] *= env->cfg.special_ring_speed_mult;
        }
    }
}

static inline void ps_update_enemies(PufferSurvivors* env) {
    float half = 0.5f * env->cfg.arena_size;
    float far2 = (half * env->cfg.enemy_recycle_radius_ratio)
        * (half * env->cfg.enemy_recycle_radius_ratio);
    float player_x = env->px;
    float player_y = env->py;
    float player_radius = env->cfg.player_radius;
    env->nearest_enemy = -1;
    env->nearest_enemy_d2 = 1e30f;
    for (int k = 0; k < env->enemy_count; k++) {
        int i = env->enemies.dense[k];
        float dx = player_x - env->enemies.x[i];
        float dy = player_y - env->enemies.y[i];
        float d = sqrtf(fmaxf(dx * dx + dy * dy, 0.0001f));
        env->enemies.vx[i] = dx / d * env->enemies.speed[i];
        env->enemies.vy[i] = dy / d * env->enemies.speed[i];
        env->enemies.x[i] += env->enemies.vx[i];
        env->enemies.y[i] += env->enemies.vy[i];
        if ((env->tick + i) % env->cfg.enemy_obstacle_stride == 0)
            ps_push_out_obstacles_shape(env, &env->enemies.x[i], &env->enemies.y[i],
                env->enemies.shape[i], env->enemies.radius[i],
                env->enemies.half_width[i], env->enemies.half_height[i], 0);
        float post_dx = env->enemies.x[i] - player_x;
        float post_dy = env->enemies.y[i] - player_y;
        float post_d2 = post_dx * post_dx + post_dy * post_dy;
        if (post_d2 > far2) {
            ps_pick_spawn_position(env, env->enemies.bound_radius[i], &env->enemies.x[i], &env->enemies.y[i]);
            if ((env->enemies.type[i] & PS_ENEMY_KIND_MASK) != 2) env->enemies.hp[i] = env->enemies.max_hp[i];
            continue;
        }
        if (post_d2 < env->nearest_enemy_d2) {
            env->nearest_enemy_d2 = post_d2;
            env->nearest_enemy = i;
        }
        int hit = ps_geometry_shape_overlaps_circle(env->enemies.shape[i],
            player_x - env->enemies.x[i], player_y - env->enemies.y[i],
            env->enemies.radius[i], env->enemies.half_width[i],
            env->enemies.half_height[i], player_radius);
        if (env->invuln_timer <= 0 && hit) {
            float dmg = fmaxf(1.0f, ceilf(env->enemies.damage[i] * env->cfg.contact_damage));
            env->hp -= dmg;
            env->agents[0].rewards[0] += env->cfg.reward_hurt * dmg;
            env->episode_reward_hurt += env->cfg.reward_hurt * dmg;
            env->episode_damage_taken += dmg;
            env->invuln_timer = env->cfg.invuln_steps;
        }
    }
}

static inline void ps_update_projectiles(PufferSurvivors* env) {
    float half = 0.5f * env->cfg.arena_size;
    int k = 0;
    while (k < env->projectile_count) {
        int i = env->projectiles.dense[k];
        env->projectiles.x[i] += env->projectiles.vx[i];
        env->projectiles.y[i] += env->projectiles.vy[i];
        env->projectiles.ttl[i]--;
        if (env->projectiles.ttl[i] <= 0 || fabsf(env->projectiles.x[i] - env->px) > half || fabsf(env->projectiles.y[i] - env->py) > half) {
            ps_deactivate_projectile(env, i);
            continue;
        }
        int blocked = 0;
        for (int o = 0; o < env->cfg.obstacle_count; o++) {
            float r = env->obstacles.radius[o] + env->projectiles.radius[i];
            float dx = env->projectiles.x[i] - env->obstacles.x[o];
            float dy = env->projectiles.y[i] - env->obstacles.y[o];
            if (ps_geometry_circle_overlaps(dx, dy, r)) {
                blocked = 1;
                break;
            }
        }
        if (blocked) {
            ps_deactivate_projectile(env, i);
            continue;
        }

        int cell = ps_cell(env, env->projectiles.x[i], env->projectiles.y[i]);
        int cx = cell % PS_GRID_W;
        int cy = cell / PS_GRID_W;
        for (int oy = -1; oy <= 1 && env->projectiles.active[i]; oy++) {
            int gy = cy + oy;
            if (gy < 0 || gy >= PS_GRID_H) continue;
            for (int ox = -1; ox <= 1 && env->projectiles.active[i]; ox++) {
                int gx = cx + ox;
                if (gx < 0 || gx >= PS_GRID_W) continue;
                for (int eidx = env->grid_head[gy * PS_GRID_W + gx]; eidx >= 0; eidx = env->enemies.next[eidx]) {
                    if (!env->enemies.active[eidx]) continue;
                    if (env->enemies.shape[eidx] != PS_SHAPE_CIRCLE) continue;
                    if (!ps_geometry_shape_overlaps_circle(env->enemies.shape[eidx],
                            env->projectiles.x[i] - env->enemies.x[eidx],
                            env->projectiles.y[i] - env->enemies.y[eidx],
                            env->enemies.radius[eidx],
                            env->enemies.half_width[eidx],
                            env->enemies.half_height[eidx],
                            env->projectiles.radius[i])) continue;
                    ps_damage_enemy(env, eidx, env->projectiles.damage[i]);
                    if (env->projectiles.pierce[i] <= 0) ps_deactivate_projectile(env, i);
                    else env->projectiles.pierce[i]--;
                    break;
                }
            }
        }
        for (int a = 0; a < env->aabb_count && env->projectiles.active[i]; a++) {
            int eidx = env->aabb_indices[a];
            if (!env->enemies.active[eidx]) continue;
            if (!ps_geometry_shape_overlaps_circle(env->enemies.shape[eidx],
                    env->projectiles.x[i] - env->enemies.x[eidx],
                    env->projectiles.y[i] - env->enemies.y[eidx],
                    env->enemies.radius[eidx], env->enemies.half_width[eidx],
                    env->enemies.half_height[eidx], env->projectiles.radius[i])) continue;
            ps_damage_enemy(env, eidx, env->projectiles.damage[i]);
            if (env->projectiles.pierce[i] <= 0) ps_deactivate_projectile(env, i);
            else env->projectiles.pierce[i]--;
        }
        if (env->projectiles.active[i]) k++;
    }
}

static inline void ps_update_drops(PufferSurvivors* env) {
    float magnet = env->cfg.magnet_radius * (1.0f + env->magnet_bonus);
    float magnet2 = magnet * magnet;
    float pickup2 = env->cfg.pickup_radius * env->cfg.pickup_radius;
    int k = 0;
    while (k < env->drop_count) {
        int i = env->drops.dense[k];
        float dx = env->px - env->drops.x[i];
        float dy = env->py - env->drops.y[i];
        float dist2 = dx * dx + dy * dy;

        if (dist2 < pickup2) {
            env->agents[0].rewards[0] += env->cfg.reward_pickup;
            env->episode_reward_pickup += env->cfg.reward_pickup;
            if (env->drops.type[i] == 1) {
                env->hp = fminf(env->max_hp, env->hp + env->drops.value[i]);
            } else {
                env->xp += env->drops.value[i];
                env->episode_xp += env->drops.value[i];
                env->agents[0].rewards[0] += env->cfg.reward_xp * env->drops.value[i];
                env->episode_reward_xp += env->cfg.reward_xp * env->drops.value[i];
                env->episode_score += env->drops.value[i];
            }
            env->episode_pickups += 1.0f;
            ps_deactivate_drop(env, i);
            continue;
        }

        if (dist2 < magnet2) {
            float dist = sqrtf(fmaxf(dist2, 0.0001f));
            env->drops.x[i] += dx / dist * env->cfg.pickup_magnet_speed;
            env->drops.y[i] += dy / dist * env->cfg.pickup_magnet_speed;
        }
        k++;
    }
    while (env->xp >= ps_xp_threshold(env)) {
        env->xp -= ps_xp_threshold(env);
        env->level++;
        env->agents[0].rewards[0] += env->cfg.reward_levelup;
        env->episode_reward_levelup += env->cfg.reward_levelup;
        env->queued_upgrades++;
        ps_offer_upgrades(env);
    }
}

static inline int ps_nearest_enemy(PufferSurvivors* env, float range) {
    float best_d2 = range * range;
    int cached = env->nearest_enemy;
    if (cached >= 0 && cached < env->cfg.enemy_cap && env->enemies.active[cached] && env->nearest_enemy_d2 < best_d2) {
        return cached;
    }
    int best = -1;
    for (int k = 0; k < env->enemy_count; k++) {
        int i = env->enemies.dense[k];
        float d2 = ps_dist2(env->px, env->py, env->enemies.x[i], env->enemies.y[i]);
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

static inline void ps_damage_radius_with_query_pad(PufferSurvivors* env,
        float x, float y, float radius, float damage, float knockback,
        float query_pad) {
    if (knockback > 0.0f) {
        env->nearest_enemy = -1;
        env->nearest_enemy_d2 = 1e30f;
    }

    // The grid was built before weapon updates. Expand the cell query enough to
    // cover the largest enemy and small same-step knockback without rebuilding.
    float half = 0.5f * env->cfg.arena_size;
    float inv_cell_x = (float)PS_GRID_W / env->cfg.arena_size;
    float inv_cell_y = (float)PS_GRID_H / env->cfg.arena_size;
    float qr = radius + query_pad;
    int min_gx = (int)floorf(((x - qr - env->px + half) * inv_cell_x));
    int max_gx = (int)floorf(((x + qr - env->px + half) * inv_cell_x));
    int min_gy = (int)floorf(((y - qr - env->py + half) * inv_cell_y));
    int max_gy = (int)floorf(((y + qr - env->py + half) * inv_cell_y));
    min_gx = min_gx < 0 ? 0 : (min_gx >= PS_GRID_W ? PS_GRID_W - 1 : min_gx);
    max_gx = max_gx < 0 ? 0 : (max_gx >= PS_GRID_W ? PS_GRID_W - 1 : max_gx);
    min_gy = min_gy < 0 ? 0 : (min_gy >= PS_GRID_H ? PS_GRID_H - 1 : min_gy);
    max_gy = max_gy < 0 ? 0 : (max_gy >= PS_GRID_H ? PS_GRID_H - 1 : max_gy);

    for (int gy = min_gy; gy <= max_gy; gy++) {
        for (int gx = min_gx; gx <= max_gx; gx++) {
            int eidx = env->grid_head[gy * PS_GRID_W + gx];
            while (eidx >= 0) {
                int next = env->enemies.next[eidx];
                if (env->enemies.active[eidx]) {
                    if (env->enemies.shape[eidx] != PS_SHAPE_CIRCLE) {
                        eidx = next;
                        continue;
                    }
                    float dx = env->enemies.x[eidx] - x;
                    float dy = env->enemies.y[eidx] - y;
                    float d2 = dx * dx + dy * dy;
                    if (ps_geometry_shape_overlaps_circle(env->enemies.shape[eidx],
                            dx, dy, env->enemies.radius[eidx],
                            env->enemies.half_width[eidx],
                            env->enemies.half_height[eidx], radius)) {
                        int killed = ps_damage_enemy(env, eidx, damage);
                        if (!killed && knockback > 0.0f) {
                            float d = sqrtf(fmaxf(d2, 0.0001f));
                            env->enemies.x[eidx] += dx / d * knockback;
                            env->enemies.y[eidx] += dy / d * knockback;
                        }
                    }
                }
                eidx = next;
            }
        }
    }
    for (int a = 0; a < env->aabb_count; a++) {
        int eidx = env->aabb_indices[a];
        if (!env->enemies.active[eidx]) continue;
        float dx = env->enemies.x[eidx] - x;
        float dy = env->enemies.y[eidx] - y;
        float d2 = dx * dx + dy * dy;
        if (!ps_geometry_shape_overlaps_circle(env->enemies.shape[eidx],
                dx, dy, env->enemies.radius[eidx],
                env->enemies.half_width[eidx], env->enemies.half_height[eidx], radius)) continue;
        int killed = ps_damage_enemy(env, eidx, damage);
        if (!killed && knockback > 0.0f) {
            float d = sqrtf(fmaxf(d2, 0.0001f));
            env->enemies.x[eidx] += dx / d * knockback;
            env->enemies.y[eidx] += dy / d * knockback;
        }
    }
}

static inline void ps_damage_radius(PufferSurvivors* env, float x, float y,
        float radius, float damage, float knockback) {
    ps_damage_radius_with_query_pad(env, x, y, radius, damage, knockback, 1.50f);
}

static inline void ps_update_areas(PufferSurvivors* env) {
    int k = 0;
    while (k < env->area_count) {
        int i = env->areas.dense[k];
        env->areas.ttl[i]--;
        env->areas.tick_timer[i]--;
        if (env->areas.tick_timer[i] <= 0 && env->areas.damage[i] > 0.0f) {
            // The grid query is exact for persistent area ticks. The old
            // knockback safety pad made every oil pool inspect many extra
            // cells while not changing which enemies actually took damage.
            ps_damage_radius_with_query_pad(env, env->areas.x[i], env->areas.y[i],
                env->areas.radius[i], env->areas.damage[i],
                env->cfg.area_tick_knockback, 0.0f);
            env->areas.tick_timer[i] = env->areas.tick_rate[i];
        }
        if (env->areas.ttl[i] <= 0) {
            ps_deactivate_area(env, i);
            continue;
        }
        k++;
    }
    env->weapon_active[PS_WEAPON_INK] = ps_clampf((float)env->active_ink_count / 8.0f, 0.0f, 1.0f);
}

static inline void ps_cast_bubble(PufferSurvivors* env, int level) {
    int target = ps_nearest_enemy(env,
        env->cfg.bubble_target_range + env->cfg.bubble_target_area_range * env->area_bonus);
    if (target < 0) return;
    int shots = 1 + level / 3;
    float damage = ps_weapon_damage(env, PS_WEAPON_BUBBLE, level, 1);
    float radius = ps_geometry_weapon_radius(&env->cfg, PS_WEAPON_BUBBLE, 0)
        * (1.0f + env->area_bonus);
    float speed = env->cfg.projectile_speed * (1.0f + env->projectile_speed_bonus);
    int pierce = env->pierce_bonus + level / 4;
    for (int i = 0; i < shots; i++) {
        float jitter = ((float)i - 0.5f * (float)(shots - 1)) * env->cfg.bubble_shot_spread;
        ps_spawn_projectile(env, PS_WEAPON_BUBBLE, env->px, env->py,
            env->enemies.x[target] + jitter, env->enemies.y[target] - jitter,
            damage, radius, speed, pierce, env->cfg.bubble_projectile_ttl);
    }
    env->weapon_active[PS_WEAPON_BUBBLE] = 1.0f;
}

static inline void ps_cast_whirlpool(PufferSurvivors* env, int level) {
    float radius = ps_geometry_weapon_radius(&env->cfg, PS_WEAPON_WHIRLPOOL, level - 1)
        * (1.0f + env->area_bonus);
    float damage = ps_weapon_damage(env, PS_WEAPON_WHIRLPOOL, level, 0);
    ps_damage_radius(env, env->px, env->py, radius, damage, env->cfg.whirlpool_knockback);
    ps_spawn_area(env, PS_WEAPON_WHIRLPOOL, env->px, env->py, radius, 0.0f,
        env->cfg.whirlpool_ttl, env->cfg.whirlpool_tick_rate);
    env->weapon_active[PS_WEAPON_WHIRLPOOL] = 1.0f;
}

static inline void ps_cast_orbit(PufferSurvivors* env, int level) {
    int count = 1 + level / 2;
    float orbit_r = (env->cfg.weapon_orbit_distance
        + env->cfg.weapon_orbit_distance_per_level * (float)level)
        * (1.0f + env->cfg.orbit_area_distance_bonus * env->area_bonus);
    float hit_r = ps_geometry_weapon_radius(&env->cfg, PS_WEAPON_ORBIT, level)
        * (1.0f + env->area_bonus);
    float damage = ps_weapon_damage(env, PS_WEAPON_ORBIT, level, 0);
    for (int i = 0; i < count; i++) {
        float a = env->orbit_phase + 2.0f * PI * ((float)i / (float)count);
        ps_damage_radius(env, env->px + cosf(a) * orbit_r,
            env->py + sinf(a) * orbit_r, hit_r, damage, env->cfg.orbit_knockback);
    }
    env->weapon_active[PS_WEAPON_ORBIT] = 1.0f;
}

static inline void ps_cast_ink(PufferSurvivors* env, int level) {
    int target = ps_nearest_enemy(env, env->cfg.arena_size * env->cfg.ink_target_range_ratio);
    if (target < 0) return;
    int pools = 1 + level / 3;
    float radius = ps_geometry_weapon_radius(&env->cfg, PS_WEAPON_INK, level)
        * (1.0f + env->area_bonus);
    float damage = ps_weapon_damage(env, PS_WEAPON_INK, level, 0);
    int ttl = env->cfg.ink_pool_ttl_base + env->cfg.ink_pool_ttl_per_level * level;
    for (int i = 0; i < pools; i++) {
        float angle = 2.0f * PI * ((float)i / (float)pools)
            + ps_randf(env) * env->cfg.ink_pool_angle_jitter;
        float dist = pools > 1 ? env->cfg.ink_pool_spread : 0.0f;
        ps_spawn_area(env, PS_WEAPON_INK,
            env->enemies.x[target] + cosf(angle) * dist,
            env->enemies.y[target] + sinf(angle) * dist,
            radius, damage, ttl, env->cfg.ink_pool_tick_rate);
    }
    env->weapon_active[PS_WEAPON_INK] = 1.0f;
}

static inline void ps_update_poison_oil_trail(PufferSurvivors* env, int level) {
    if (level <= 0) return;
    float speed2 = env->pvx * env->pvx + env->pvy * env->pvy;
    if (speed2 < env->cfg.ink_trail_min_speed2) return;

    int active_oil = env->active_ink_count;
    int max_oil = env->cfg.ink_trail_max_base
        + level * env->cfg.ink_trail_max_per_level;
    if (active_oil >= max_oil) return;

    int cadence = env->cfg.ink_trail_cadence_base
        - level / env->cfg.ink_trail_cadence_level_divisor;
    if (cadence < env->cfg.ink_trail_cadence_min)
        cadence = env->cfg.ink_trail_cadence_min;
    if (env->tick % cadence != 0) return;

    float speed = sqrtf(speed2);
    float nx = env->pvx / speed;
    float ny = env->pvy / speed;
    float radius = (env->cfg.ink_trail_radius_base
        + env->cfg.ink_trail_radius_per_level * (float)level
        + env->cfg.ink_trail_radius_config_scale
            * env->cfg.weapon_radius_per_level[PS_WEAPON_INK] * (float)level)
        * (1.0f + env->area_bonus);
    float damage = ps_weapon_damage(env, PS_WEAPON_INK, level, 0)
        * env->cfg.ink_trail_damage_multiplier;
    int ttl = env->cfg.ink_trail_ttl_base + env->cfg.ink_trail_ttl_per_level * level;
    ps_spawn_area(env, PS_WEAPON_INK, env->px - nx * env->cfg.ink_trail_offset,
        env->py - ny * env->cfg.ink_trail_offset, radius, damage, ttl,
        env->cfg.ink_trail_tick_rate);
    env->weapon_active[PS_WEAPON_INK] = 1.0f;
}

static inline void ps_cast_sonar(PufferSurvivors* env, int level) {
    float radius = ps_geometry_weapon_radius(&env->cfg, PS_WEAPON_SONAR, level)
        * (1.0f + env->area_bonus);
    float damage = ps_weapon_damage(env, PS_WEAPON_SONAR, level, 0);
    ps_damage_radius(env, env->px, env->py, radius, damage, env->cfg.sonar_knockback);
    ps_spawn_area(env, PS_WEAPON_SONAR, env->px, env->py, radius, 0.0f,
        env->cfg.sonar_ttl, env->cfg.sonar_tick_rate);
    env->weapon_active[PS_WEAPON_SONAR] = 1.0f;
}

static inline void ps_update_weapons(PufferSurvivors* env) {
    for (int i = 0; i < PS_WEAPON_COUNT; i++) {
        env->weapon_active[i] *= env->cfg.weapon_active_decay;
        if (env->weapon_cd[i] > 0.0f) env->weapon_cd[i] -= 1.0f;
    }
    env->orbit_phase += env->cfg.orbit_phase_speed
        + env->cfg.orbit_phase_per_level
            * (float)env->weapon_level[PS_WEAPON_ORBIT];
    ps_update_areas(env);
    ps_update_poison_oil_trail(env, env->weapon_level[PS_WEAPON_INK]);

    for (int weapon = 0; weapon < PS_WEAPON_COUNT; weapon++) {
        int level = env->weapon_level[weapon];
        if (level <= 0 || env->weapon_cd[weapon] > 0.0f) continue;
        switch (weapon) {
            case PS_WEAPON_BUBBLE: ps_cast_bubble(env, level); break;
            case PS_WEAPON_WHIRLPOOL: ps_cast_whirlpool(env, level); break;
            case PS_WEAPON_ORBIT: ps_cast_orbit(env, level); break;
            case PS_WEAPON_INK: ps_cast_ink(env, level); break;
            case PS_WEAPON_SONAR: ps_cast_sonar(env, level); break;
        }
        env->weapon_cd[weapon] = ps_weapon_cooldown_total(env, weapon);
    }
}

static inline void ps_reset_core(PufferSurvivors* env, int clear_outputs) {
    if (clear_outputs) {
        env->agents[0].rewards[0] = 0.0f;
        env->agents[0].terminals[0] = 0.0f;
    }
    env->px = 0.0f;
    env->py = 0.0f;
    env->pvx = 0.0f;
    env->pvy = 0.0f;
    env->player_facing_left = 0;
    env->max_hp = floorf(env->cfg.player_health);
    env->hp = env->max_hp;
    env->xp = 0.0f;
    env->level = 1;
    env->speed_bonus = 0.0f;
    env->damage_bonus = 0.0f;
    env->cooldown_mult = 1.0f;
    env->projectile_speed_bonus = 0.0f;
    env->magnet_bonus = 0.0f;
    env->area_bonus = 0.0f;
    env->pierce_bonus = 0;
    env->pending_upgrade = 0;
    env->queued_upgrades = 0;
    env->last_boss_tick = -1;
    memset(env->weapon_cd, 0, sizeof(env->weapon_cd));
    memset(env->weapon_active, 0, sizeof(env->weapon_active));
    memset(env->weapon_level, 0, sizeof(env->weapon_level));
    env->weapon_level[PS_WEAPON_BUBBLE] = 1;
    if (env->cfg.free_upgrade >= 0) {
        int count = env->cfg.free_upgrade_count;
        for (int i = 0; i < count; i++) {
            ps_apply_upgrade_effect(env, env->cfg.free_upgrade);
        }
    }
    env->orbit_phase = ps_randf(env) * 2.0f * PI;
    env->tick = 0;
    env->invuln_timer = 0;
    env->episode_return = 0.0f;
    env->episode_reward_survival = 0.0f;
    env->episode_reward_damage = 0.0f;
    env->episode_reward_kill = 0.0f;
    env->episode_reward_hurt = 0.0f;
    env->episode_reward_pickup = 0.0f;
    env->episode_reward_xp = 0.0f;
    env->episode_reward_levelup = 0.0f;
    env->episode_reward_obstacle = 0.0f;
    env->episode_reward_terminal = 0.0f;
    env->episode_score = 0.0f;
    env->episode_kills = 0.0f;
    env->episode_xp = 0.0f;
    env->episode_damage_dealt = 0.0f;
    env->episode_damage_taken = 0.0f;
    env->episode_pickups = 0.0f;
    env->episode_levelups = 0.0f;
    env->episode_obstacle_hits = 0.0f;
    env->episode_peak_enemies = 0.0f;
    env->episode_peak_projectiles = 0.0f;
    env->episode_min_hp = env->hp;
    ps_clear_entities(env);
    ps_spawn_obstacles(env);
    ps_compute_observations(env);
}

static inline void c_reset(PufferSurvivors* env) {
    ps_reset_core(env, 1);
}

static inline void c_step(PufferSurvivors* env) {
    env->agents[0].rewards[0] = env->cfg.reward_survival;
    env->episode_reward_survival += env->cfg.reward_survival;
    env->agents[0].terminals[0] = 0.0f;
    env->tick++;
    if (env->invuln_timer > 0) env->invuln_timer--;

    int upgrade_action = (int)env->agents[0].actions[1];
    if (env->pending_upgrade)
        ps_apply_upgrade(env, (int)((unsigned)upgrade_action % PS_UPGRADE_SLOTS));

    static const float dirs[9][2] = {
        {0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0},
        {-0.70710678f, -0.70710678f}, {0.70710678f, -0.70710678f},
        {-0.70710678f, 0.70710678f}, {0.70710678f, 0.70710678f},
    };
    int action = (int)env->agents[0].actions[0];
    action = (int)((unsigned)action % 9u);
    float speed = env->cfg.player_speed * (1.0f + env->speed_bonus);
    float target_vx = dirs[action][0] * speed;
    float target_vy = dirs[action][1] * speed;
    if (target_vx < -0.001f) env->player_facing_left = 1;
    else if (target_vx > 0.001f) env->player_facing_left = 0;
    env->pvx += (target_vx - env->pvx) * env->cfg.movement_smoothing;
    env->pvy += (target_vy - env->pvy) * env->cfg.movement_smoothing;
    float v2 = env->pvx * env->pvx + env->pvy * env->pvy;
    if (v2 > speed * speed) {
        float inv = speed / sqrtf(v2);
        env->pvx *= inv;
        env->pvy *= inv;
    }
    env->px += env->pvx;
    env->py += env->pvy;
    ps_push_out_obstacles(env, &env->px, &env->py, env->cfg.player_radius, 1);
    ps_recycle_far_obstacles(env);
    ps_update_moving_obstacles(env);

    ps_wave_spawns(env);
    ps_update_enemies(env);
    if (ps_grid_needed(env)) ps_rebuild_grid(env);
    ps_update_weapons(env);
    ps_update_projectiles(env);
    ps_update_drops(env);

    if ((float)env->enemy_count > env->episode_peak_enemies)
        env->episode_peak_enemies = (float)env->enemy_count;
    if ((float)env->projectile_count > env->episode_peak_projectiles)
        env->episode_peak_projectiles = (float)env->projectile_count;
    if (env->hp < env->episode_min_hp) env->episode_min_hp = env->hp;

    env->episode_return += env->agents[0].rewards[0];
    if (env->hp <= 0.0f || env->tick >= env->cfg.max_steps) {
        // Success means reaching max_steps while still alive. If HP reaches
        // zero on the final step, the episode is a failure.
        int survived = env->tick >= env->cfg.max_steps && env->hp > 0.0f;
        float terminal_reward = survived
            ? env->cfg.reward_success
            : env->cfg.reward_death;

        // The ordinary reward for this step was already added to
        // episode_return above. Add only the terminal reward here.
        env->agents[0].rewards[0] += terminal_reward;
        env->episode_reward_terminal += terminal_reward;
        env->episode_return += terminal_reward;
        env->agents[0].terminals[0] = 1.0f;

        // Log before reset because reset clears episode statistics.
        ps_add_log(env, survived);

        // Preserve this final reward and terminal flag for the learner.
        ps_reset_core(env, 0);
        return;
    }

#ifdef PS_DEBUG_COUNTS
    ps_verify_counts(env);
#endif
    ps_compute_observations(env);
}
