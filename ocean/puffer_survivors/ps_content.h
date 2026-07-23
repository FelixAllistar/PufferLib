#pragma once

#include "ps_state.h"

typedef struct {
    float hp;
    float speed_mult;
    float radius;
    float damage;
} PSEnemyDef;

typedef struct {
    float base_cd;
    float cd_per_level;
    float base_damage;
    float damage_per_level;
    float base_radius;
    float radius_per_level;
} PSWeaponDef;

static const PSEnemyDef PS_ENEMY_DEFS[] = {
    {2.0f, 1.00f, 0.42f, 1.0f},
    {1.0f, 1.28f, 0.34f, 1.0f},
    {4.0f, 0.76f, 0.52f, 1.0f},
    {3.0f, 0.92f, 0.46f, 1.0f},
};

static const PSWeaponDef PS_WEAPON_DEFS[PS_WEAPON_COUNT] = {
    [PS_WEAPON_BUBBLE] = {16.0f, -0.8f, 1.15f, 0.22f, 0.30f, 0.015f},
    [PS_WEAPON_WHIRLPOOL] = {24.0f, -1.8f, 0.90f, 0.34f, 2.20f, 0.24f},
    [PS_WEAPON_ORBIT] = {10.0f, -0.7f, 1.15f, 0.42f, 0.44f, 0.04f},
    [PS_WEAPON_INK] = {96.0f, -6.0f, 0.95f, 0.34f, 1.35f, 0.18f},
    [PS_WEAPON_SONAR] = {135.0f, -7.0f, 1.00f, 0.38f, 4.85f, 0.52f},
};

static const int PS_WAVE_MINS[] = {
    10, 16, 26, 36, 32, 26, 48, 58, 70, 46, 68, 86,
    96, 108, 80, 104, 126, 94, 116, 138, 154, 168, 180, 192,
};

static const int PS_WAVE_INTERVALS[] = {
    74, 68, 48, 30, 68, 62, 40, 38, 88, 40, 34, 24,
    24, 34, 18, 16, 15, 54, 34, 28, 16, 14, 13, 12,
};

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
    return 6.0f + 4.0f * (float)(env->level - 1);
}

static inline int ps_wave_index(PufferSurvivors* env) {
    int len = env->cfg.wave_length_steps > 0 ? env->cfg.wave_length_steps : 600;
    return env->tick / len;
}

static inline float ps_episode_progress(PufferSurvivors* env) {
    float max_steps = fmaxf((float)env->cfg.max_steps, 1.0f);
    float normal_steps = fmaxf((float)(env->cfg.wave_length_steps > 0 ? env->cfg.wave_length_steps : 600) * 7.0f, 1.0f);
    float scale = fminf(max_steps, normal_steps);
    return ps_clampf((float)env->tick / scale, 0.0f, 1.0f);
}

static inline float ps_weapon_cooldown_total(PufferSurvivors* env, int weapon) {
    int level = env->weapon_level[weapon];
    if (level <= 0) return 1.0f;
    const PSWeaponDef* def = &PS_WEAPON_DEFS[weapon];
    float cd = def->base_cd + def->cd_per_level * (float)(level - 1);
    cd *= env->cooldown_mult * env->cfg.fire_cooldown / fmaxf(PS_WEAPON_DEFS[PS_WEAPON_BUBBLE].base_cd, 1.0f);
    return fmaxf(cd, 3.0f);
}

static inline float ps_weapon_power(PufferSurvivors* env, int weapon) {
    int level = env->weapon_level[weapon];
    if (level <= 0) return 0.0f;
    float area = 1.0f + env->area_bonus;
    float might = env->cfg.projectile_damage * (1.0f + env->damage_bonus);
    return ps_clampf(((float)level / 8.0f) * might * area, 0.0f, 3.0f) / 3.0f;
}

static inline float ps_weapon_damage(PufferSurvivors* env, int weapon, int level, int first_level_zero) {
    const PSWeaponDef* def = &PS_WEAPON_DEFS[weapon];
    float level_delta = (float)(first_level_zero ? level - 1 : level);
    return (def->base_damage + def->damage_per_level * level_delta) * env->cfg.projectile_damage * (1.0f + env->damage_bonus);
}

static inline int ps_upgrade_available(PufferSurvivors* env, int type) {
    if (type >= PS_UPGRADE_BUBBLE && type <= PS_UPGRADE_SONAR) {
        return env->weapon_level[type] < 8;
    }
    return type >= 0 && type < PS_UPGRADE_COUNT;
}

static inline int ps_wave_minimum(PufferSurvivors* env) {
    int n = (int)(sizeof(PS_WAVE_MINS) / sizeof(PS_WAVE_MINS[0]));
    int wave = ps_wave_index(env);
    int base = wave < n ? PS_WAVE_MINS[wave] : 210 + 7 * (wave - n);
    float spawn_scale = ps_clampf(env->cfg.enemy_spawn_rate / 0.085f, 0.25f, 3.0f);
    float progress = ps_episode_progress(env);
    int scaled = (int)ceilf((float)base * spawn_scale * (1.0f + 0.12f * env->cfg.spawn_ramp * progress));
    int cap = env->cfg.enemy_cap < 240 ? env->cfg.enemy_cap : 240;
    return scaled > cap ? cap : scaled;
}

static inline int ps_wave_spawn_interval(PufferSurvivors* env) {
    int n = (int)(sizeof(PS_WAVE_INTERVALS) / sizeof(PS_WAVE_INTERVALS[0]));
    int wave = ps_wave_index(env);
    int base = wave < n ? PS_WAVE_INTERVALS[wave] : 8;
    float spawn_scale = ps_clampf(env->cfg.enemy_spawn_rate / 0.085f, 0.25f, 3.0f);
    int scaled = (int)ceilf((float)base / spawn_scale);
    return scaled < 5 ? 5 : scaled;
}
