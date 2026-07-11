#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float arena_size;
    int max_steps;
    int wave_length_steps;
    int enemy_cap;
    int projectile_cap;
    int drop_cap;
    int obstacle_count;
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
    float obstacle_penalty;
    float contact_damage;
    int invuln_steps;
    int enemy_obstacle_stride;
} PSConfig;

static inline PSConfig ps_default_config(void) {
    PSConfig cfg;
    cfg.arena_size = 48.0f;
    cfg.max_steps = 4200;
    cfg.wave_length_steps = 600;
    cfg.enemy_cap = 256;
    cfg.projectile_cap = 512;
    cfg.drop_cap = 192;
    cfg.obstacle_count = 14;
    cfg.enemy_spawn_rate = 0.085f;
    cfg.elite_spawn_rate = 0.006f;
    cfg.player_speed = 0.18f;
    cfg.player_health = 7.0f;
    cfg.enemy_speed = 0.0875f;
    cfg.enemy_hp_scale = 0.65f;
    cfg.enemy_damage_scale = 1.0f;
    cfg.spawn_ramp = 3.2f;
    cfg.projectile_speed = 0.42f;
    cfg.projectile_damage = 1.0f;
    cfg.fire_cooldown = 22.0f;
    cfg.pickup_radius = 0.65f;
    cfg.magnet_radius = 3.4f;
    cfg.health_drop_rate = 0.045f;
    cfg.health_heal = 3.0f;
    cfg.reward_xp = 0.12f;
    cfg.reward_kill = 0.30f;
    cfg.reward_damage = 0.002f;
    cfg.reward_survival = 0.0003f;
    cfg.reward_hurt = -0.25f;
    cfg.reward_death = -3.0f;
    cfg.obstacle_penalty = -0.003f;
    cfg.contact_damage = 1.0f;
    cfg.invuln_steps = 36;
    cfg.enemy_obstacle_stride = 1;
    return cfg;
}

#ifdef __cplusplus
}
#endif
