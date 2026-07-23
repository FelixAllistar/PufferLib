#pragma once

#include "pufferenv.h"
#include "ps_config.h"
#include "ps_log.h"
#include "ps_observation_layout.h"
#ifndef PUFFER_GPU_ENV
#include "ps_systems.h"
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

static inline float ps_kwarg(Dict* kwargs, const char* name) {
    return (float)dict_get(kwargs, name);
}

static inline PSConfig ps_config_from_kwargs(Dict* kwargs) {
    PSConfig cfg = ps_default_config();
    cfg.arena_size = ps_kwarg(kwargs, "arena_size");
    cfg.max_steps = (int)ps_kwarg(kwargs, "max_steps");
    cfg.wave_length_steps = (int)ps_kwarg(kwargs, "wave_length_steps");
    cfg.enemy_cap = (int)ps_kwarg(kwargs, "enemy_cap");
    cfg.projectile_cap = (int)ps_kwarg(kwargs, "projectile_cap");
    cfg.drop_cap = (int)ps_kwarg(kwargs, "drop_cap");
    cfg.obstacle_count = (int)ps_kwarg(kwargs, "obstacle_count");
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
    cfg.invuln_steps = (int)ps_kwarg(kwargs, "invuln_steps");
    cfg.enemy_obstacle_stride = (int)ps_kwarg(kwargs, "enemy_obstacle_stride");
    cfg.observation_version = (int)ps_kwarg(kwargs, "observation_version");
    cfg.free_upgrade = (int)ps_kwarg(kwargs, "free_upgrade");
    cfg.free_upgrade_count = (int)ps_kwarg(kwargs, "free_upgrade_count");
    return cfg;
}

#ifndef PUFFER_GPU_ENV
void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;
    env->cfg = ps_config_from_kwargs(kwargs);
    env->show_hitboxes = (int)ps_kwarg(kwargs, "show_hitboxes");
    ps_init(env);
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
    static const char* upgrades[PS_UPGRADE_COUNT] = {
        "bubble", "whirlpool", "orbit", "ink", "sonar", "speed",
        "magnet", "health", "might", "cooldown", "area", "pierce"
    };
    static const char* moves[9] = {
        "stay", "up", "down", "left", "right",
        "up_left", "up_right", "down_left", "down_right"
    };
    char key[48];
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
    for (int i = 0; i < PS_UPGRADE_COUNT; i++) {
        snprintf(key, sizeof(key), "upgrade_%s", upgrades[i]);
        dict_set(out, key, log->upgrade_counts[i]);
        snprintf(key, sizeof(key), "win_upgrade_%s", upgrades[i]);
        dict_set(out, key, log->win_upgrade_counts[i]);
    }
    for (int i = 0; i < 9; i++) {
        snprintf(key, sizeof(key), "move_%s", moves[i]);
        dict_set(out, key, log->move_counts[i]);
        snprintf(key, sizeof(key), "win_move_%s", moves[i]);
        dict_set(out, key, log->win_move_counts[i]);
    }
}
