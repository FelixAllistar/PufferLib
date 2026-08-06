#pragma once

#include "ps_state.h"

static inline const char* ps_upgrade_name(int type) {
    switch (type) {
        case PS_UPGRADE_BUBBLE: return "Bubble";
        case PS_UPGRADE_WHIRLPOOL: return "Whirlpool";
        case PS_UPGRADE_ORBIT: return "Orbit";
        case PS_UPGRADE_INK: return "Poison Oil";
        case PS_UPGRADE_SONAR: return "Sonar";
        case PS_UPGRADE_SPEED: return "Speed";
        case PS_UPGRADE_MAGNET: return "Magnet";
        case PS_UPGRADE_HEALTH: return "Health";
        case PS_UPGRADE_MIGHT: return "Might";
        case PS_UPGRADE_COOLDOWN: return "Cooldown";
        case PS_UPGRADE_AREA: return "Area";
        case PS_UPGRADE_PIERCE: return "Pierce";
        default: return "-";
    }
}

static inline const char* ps_upgrade_description(int type) {
    switch (type) {
        case PS_UPGRADE_BUBBLE: return "Faster bubbles\nthat pop harder.";
        case PS_UPGRADE_WHIRLPOOL: return "Bigger burst\nand knockback.";
        case PS_UPGRADE_ORBIT: return "More pearls for\nclose defense.";
        case PS_UPGRADE_INK: return "Poison pools and\na longer trail.";
        case PS_UPGRADE_SONAR: return "Huge pulse when\nswarmed.";
        case PS_UPGRADE_SPEED: return "Move faster out\nof danger.";
        case PS_UPGRADE_MAGNET: return "Pull XP and hearts\nfrom farther away.";
        case PS_UPGRADE_HEALTH: return "Gain max HP and\nheal now.";
        case PS_UPGRADE_MIGHT: return "All weapons deal\nmore damage.";
        case PS_UPGRADE_COOLDOWN: return "Weapons fire\nmore often.";
        case PS_UPGRADE_AREA: return "Larger hitboxes\nand pools.";
        case PS_UPGRADE_PIERCE: return "Bubbles pierce\nmore enemies.";
        default: return "Upgrade your survival odds.";
    }
}

static inline float ps_xp_threshold(PufferSurvivors* env) {
    return env->cfg.xp_threshold_base
        + env->cfg.xp_threshold_per_level * (float)(env->level - 1);
}

static inline int ps_wave_index(PufferSurvivors* env) {
    return env->tick / env->cfg.wave_length_steps;
}

static inline float ps_episode_progress(PufferSurvivors* env) {
    float max_steps = (float)env->cfg.max_steps;
    float normal_steps = (float)env->cfg.wave_length_steps
        * (float)env->cfg.progress_normal_wave_count;
    float scale = fminf(max_steps, normal_steps);
    return ps_clampf((float)env->tick / scale, 0.0f, 1.0f);
}

static inline float ps_weapon_cooldown_total(PufferSurvivors* env, int weapon) {
    int level = env->weapon_level[weapon];
    if (level <= 0) return 1.0f;
    float cd = env->cfg.weapon_base_cooldown[weapon]
        + env->cfg.weapon_cooldown_per_level[weapon] * (float)(level - 1);
    cd *= env->cooldown_mult * env->cfg.fire_cooldown
        / fmaxf(env->cfg.weapon_base_cooldown[PS_WEAPON_BUBBLE], 1.0f);
    return cd;
}

static inline float ps_weapon_power(PufferSurvivors* env, int weapon) {
    int level = env->weapon_level[weapon];
    if (level <= 0) return 0.0f;
    float area = 1.0f + env->area_bonus;
    float might = env->cfg.projectile_damage * (1.0f + env->damage_bonus);
    return ps_clampf(((float)level / (float)env->cfg.weapon_max_level) * might * area,
        0.0f, 3.0f) / 3.0f;
}

static inline float ps_weapon_damage(PufferSurvivors* env, int weapon, int level, int first_level_zero) {
    float level_delta = (float)(first_level_zero ? level - 1 : level);
    return (env->cfg.weapon_base_damage[weapon]
        + env->cfg.weapon_damage_per_level[weapon] * level_delta)
        * env->cfg.projectile_damage * (1.0f + env->damage_bonus);
}

static inline int ps_upgrade_available(PufferSurvivors* env, int type) {
    if (type >= PS_UPGRADE_BUBBLE && type <= PS_UPGRADE_SONAR) {
        return env->weapon_level[type] < env->cfg.weapon_max_level;
    }
    return type >= 0 && type < PS_UPGRADE_COUNT;
}

static inline int ps_wave_minimum(PufferSurvivors* env) {
    int wave = ps_wave_index(env);
    int base = wave < PS_WAVE_TABLE_COUNT
        ? env->cfg.wave_minimum[wave]
        : env->cfg.wave_tail_minimum_base
            + env->cfg.wave_tail_minimum_step * (wave - PS_WAVE_TABLE_COUNT);
    float spawn_scale = ps_clampf(env->cfg.enemy_spawn_rate
        / env->cfg.wave_spawn_reference_rate,
        env->cfg.wave_spawn_scale_min, env->cfg.wave_spawn_scale_max);
    float progress = ps_episode_progress(env);
    int scaled = (int)ceilf((float)base * spawn_scale
        * (1.0f + env->cfg.wave_progress_spawn_scale * env->cfg.spawn_ramp * progress));
    int cap = env->cfg.wave_population_cap;
    return scaled > cap ? cap : scaled;
}

static inline int ps_wave_spawn_interval(PufferSurvivors* env) {
    int wave = ps_wave_index(env);
    int base = wave < PS_WAVE_TABLE_COUNT
        ? env->cfg.wave_interval[wave]
        : env->cfg.wave_tail_interval;
    float spawn_scale = ps_clampf(env->cfg.enemy_spawn_rate
        / env->cfg.wave_spawn_reference_rate,
        env->cfg.wave_spawn_scale_min, env->cfg.wave_spawn_scale_max);
    int scaled = (int)ceilf((float)base / spawn_scale);
    return scaled < env->cfg.wave_min_spawn_interval
        ? env->cfg.wave_min_spawn_interval : scaled;
}
