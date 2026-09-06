#pragma once

// arpg: native 5c ABI, config mapping, and logging. The CPU path keeps the
// standard Env/Agent ABI over the box3d-backed simulator; the GPU path
// (build.sh arpg --gpu) swaps in the SoA CUDA simulator via arpg.cu.

#include "pufferenv.h"
#include "ar_constants.h"
#include "ar_log.h"

#define OBS_SIZE AR_OBS_SIZE
#define NUM_ATNS 5
#define ACT_SIZES {AR_MOVE_ACTION_COUNT, AR_SUMMON_ACTION_COUNT, AR_ORDER_ACTION_COUNT, AR_ABILITY_ACTION_COUNT, AR_BUILD_ACTION_COUNT}

typedef float obs_t;

#ifndef PUFFER_GPU_ENV
#include "ar_state.h"
#include "ar_sim.h"
#ifndef AR_HEADLESS_BINDING
#include "ar_render.h"
#endif
#endif

static inline int ar_config_cap(int value, int max) {
    return value < 0 ? 0 : (value > max ? max : value);
}

static inline ARConfig ar_config_from_kwargs(Dict* kwargs) {
    ARConfig cfg = {0};
    cfg.arena_size = (float)dict_get(kwargs, "arena_size");
    cfg.max_steps = (int)dict_get(kwargs, "max_steps");
    cfg.wave_length_steps = (int)dict_get(kwargs, "wave_length_steps");
    cfg.enemy_cap = ar_config_cap((int)dict_get(kwargs, "enemy_cap"), AR_MAX_ENEMIES);
    cfg.pet_cap = ar_config_cap((int)dict_get(kwargs, "pet_cap"), AR_MAX_PETS);
    cfg.obstacle_count = ar_config_cap((int)dict_get(kwargs, "obstacle_count"), AR_MAX_OBSTACLES);

    cfg.player_radius = (float)dict_get(kwargs, "player_radius");
    cfg.player_speed = (float)dict_get(kwargs, "player_speed");
    cfg.player_health = (float)dict_get(kwargs, "player_health");
    cfg.invuln_steps = (int)dict_get(kwargs, "invuln_steps");
    cfg.summon_cooldown = (float)dict_get(kwargs, "summon_cooldown");

    cfg.pet_radius[AR_PET_WISP] = (float)dict_get(kwargs, "pet_radius_wisp");
    cfg.pet_radius[AR_PET_FANG] = (float)dict_get(kwargs, "pet_radius_fang");
    cfg.pet_radius[AR_PET_AEGIS] = (float)dict_get(kwargs, "pet_radius_aegis");
    cfg.pet_health[AR_PET_WISP] = (float)dict_get(kwargs, "pet_health_wisp");
    cfg.pet_health[AR_PET_FANG] = (float)dict_get(kwargs, "pet_health_fang");
    cfg.pet_health[AR_PET_AEGIS] = (float)dict_get(kwargs, "pet_health_aegis");
    cfg.pet_speed[AR_PET_WISP] = (float)dict_get(kwargs, "pet_speed_wisp");
    cfg.pet_speed[AR_PET_FANG] = (float)dict_get(kwargs, "pet_speed_fang");
    cfg.pet_speed[AR_PET_AEGIS] = (float)dict_get(kwargs, "pet_speed_aegis");
    cfg.pet_damage[AR_PET_WISP] = (float)dict_get(kwargs, "pet_damage_wisp");
    cfg.pet_damage[AR_PET_FANG] = (float)dict_get(kwargs, "pet_damage_fang");
    cfg.pet_damage[AR_PET_AEGIS] = (float)dict_get(kwargs, "pet_damage_aegis");
    cfg.pet_attack_range = (float)dict_get(kwargs, "pet_attack_range");
    cfg.pet_attack_cooldown = (float)dict_get(kwargs, "pet_attack_cooldown");
    cfg.pet_aggro_range = (float)dict_get(kwargs, "pet_aggro_range");
    cfg.pet_leash_range = (float)dict_get(kwargs, "pet_leash_range");
    cfg.pet_follow_distance = (float)dict_get(kwargs, "pet_follow_distance");
    cfg.pet_invuln_steps = (int)dict_get(kwargs, "pet_invuln_steps");

    cfg.enemy_radius[AR_ENEMY_GRUNT] = (float)dict_get(kwargs, "enemy_radius_grunt");
    cfg.enemy_radius[AR_ENEMY_BRUTE] = (float)dict_get(kwargs, "enemy_radius_brute");
    cfg.enemy_base_hp[AR_ENEMY_GRUNT] = (float)dict_get(kwargs, "enemy_hp_grunt");
    cfg.enemy_base_hp[AR_ENEMY_BRUTE] = (float)dict_get(kwargs, "enemy_hp_brute");
    cfg.enemy_base_speed[AR_ENEMY_GRUNT] = (float)dict_get(kwargs, "enemy_speed_grunt");
    cfg.enemy_base_speed[AR_ENEMY_BRUTE] = (float)dict_get(kwargs, "enemy_speed_brute");
    cfg.enemy_base_damage[AR_ENEMY_GRUNT] = (float)dict_get(kwargs, "enemy_damage_grunt");
    cfg.enemy_base_damage[AR_ENEMY_BRUTE] = (float)dict_get(kwargs, "enemy_damage_brute");
    cfg.enemy_hp_growth_per_wave = (float)dict_get(kwargs, "enemy_hp_growth_per_wave");
    cfg.enemy_speed_growth_per_wave = (float)dict_get(kwargs, "enemy_speed_growth_per_wave");
    cfg.enemy_growth_wave_cap = (int)dict_get(kwargs, "enemy_growth_wave_cap");
    cfg.enemy_kind_switch_wave = (int)dict_get(kwargs, "enemy_kind_switch_wave");

    cfg.enemy_spawn_radius = (float)dict_get(kwargs, "enemy_spawn_radius");
    cfg.spawn_interval = (int)dict_get(kwargs, "spawn_interval");
    cfg.spawn_batch = (int)dict_get(kwargs, "spawn_batch");
    cfg.spawn_min_interval = (int)dict_get(kwargs, "spawn_min_interval");
    cfg.spawn_interval_per_wave = (int)dict_get(kwargs, "spawn_interval_per_wave");

    cfg.obstacle_radius_min = (float)dict_get(kwargs, "obstacle_radius_min");
    cfg.obstacle_radius_max = (float)dict_get(kwargs, "obstacle_radius_max");
    cfg.obstacle_center_clearance = (float)dict_get(kwargs, "obstacle_center_clearance");

    cfg.dash_cooldown = (float)dict_get(kwargs, "dash_cooldown");
    cfg.dash_distance = (float)dict_get(kwargs, "dash_distance");
    cfg.dash_iframes = (float)dict_get(kwargs, "dash_iframes");
    cfg.nova_cooldown = (float)dict_get(kwargs, "nova_cooldown");
    cfg.nova_radius = (float)dict_get(kwargs, "nova_radius");
    cfg.nova_damage = (float)dict_get(kwargs, "nova_damage");
    cfg.frost_cooldown = (float)dict_get(kwargs, "frost_cooldown");
    cfg.frost_range = (float)dict_get(kwargs, "frost_range");
    cfg.frost_half_angle = (float)dict_get(kwargs, "frost_half_angle");
    cfg.frost_damage = (float)dict_get(kwargs, "frost_damage");
    cfg.frost_slow_mult = (float)dict_get(kwargs, "frost_slow_mult");
    cfg.frost_slow_time = (float)dict_get(kwargs, "frost_slow_time");

    cfg.start_shards = (float)dict_get(kwargs, "start_shards");
    cfg.kill_shards = (float)dict_get(kwargs, "kill_shards");
    cfg.summon_cost[AR_PET_WISP] = (float)dict_get(kwargs, "summon_cost_wisp");
    cfg.summon_cost[AR_PET_FANG] = (float)dict_get(kwargs, "summon_cost_fang");
    cfg.summon_cost[AR_PET_AEGIS] = (float)dict_get(kwargs, "summon_cost_aegis");
    cfg.build_cost[AR_BUILD_TOTEM] = (float)dict_get(kwargs, "build_cost_totem");
    cfg.build_cost[AR_BUILD_WALL] = (float)dict_get(kwargs, "build_cost_wall");
    cfg.build_hp[AR_BUILD_TOTEM] = (float)dict_get(kwargs, "build_hp_totem");
    cfg.build_hp[AR_BUILD_WALL] = (float)dict_get(kwargs, "build_hp_wall");
    cfg.build_radius[AR_BUILD_TOTEM] = (float)dict_get(kwargs, "build_radius_totem");
    cfg.build_radius[AR_BUILD_WALL] = (float)dict_get(kwargs, "build_radius_wall");
    cfg.totem_radius = (float)dict_get(kwargs, "totem_radius");
    cfg.totem_damage = (float)dict_get(kwargs, "totem_damage");
    cfg.totem_period = (float)dict_get(kwargs, "totem_period");
    cfg.shard_trickle_period = (float)dict_get(kwargs, "shard_trickle_period");
    cfg.shard_trickle_cap = (int)dict_get(kwargs, "shard_trickle_cap");

    cfg.reward_survival = (float)dict_get(kwargs, "reward_survival");
    cfg.reward_kill = (float)dict_get(kwargs, "reward_kill");
    cfg.reward_damage = (float)dict_get(kwargs, "reward_damage");
    cfg.reward_hurt = (float)dict_get(kwargs, "reward_hurt");
    cfg.reward_summon = (float)dict_get(kwargs, "reward_summon");
    cfg.reward_pet_lose = (float)dict_get(kwargs, "reward_pet_lose");
    cfg.reward_death = (float)dict_get(kwargs, "reward_death");
    cfg.reward_success = (float)dict_get(kwargs, "reward_success");
    return cfg;
}

#ifndef PUFFER_GPU_ENV
void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;
    env->cfg = ar_config_from_kwargs(kwargs);
    env->show_hitboxes = (int)dict_get(kwargs, "show_hitboxes");
}

void c_reset(ARPG* env) {
    ar_reset_env(env, 0);
}

void c_step(ARPG* env) {
    ar_step_env(env, 0);
}

void puf_reset(Env* env) {
    c_reset(env);
}

void puf_step(Env* env) {
    c_step(env);
}

void puf_render(Env* env) {
#ifdef AR_HEADLESS_BINDING
    (void)env;
#else
    c_render(env);
#endif
}

void puf_close(Env* env) {
#ifdef AR_HEADLESS_BINDING
    (void)env;
#else
    c_close(env);
#endif
}
#else
// GPU path: per-env host state is just the training-visible log; all sim
// state lives in the ARCudaSim SoA blob (see cuda/ar_cuda_sim.cu).
struct Env {
    Log log;
};
#endif  // !PUFFER_GPU_ENV

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "reward_survival", log->reward_survival);
    dict_set(out, "reward_kill", log->reward_kill);
    dict_set(out, "reward_damage", log->reward_damage);
    dict_set(out, "reward_hurt", log->reward_hurt);
    dict_set(out, "reward_summon", log->reward_summon);
    dict_set(out, "reward_terminal", log->reward_terminal);
    dict_set(out, "kills", log->kills);
    dict_set(out, "summons", log->summons);
    dict_set(out, "pets_lost", log->pets_lost);
    dict_set(out, "pets_alive", log->pets_alive);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_taken", log->damage_taken);
    dict_set(out, "enemies_alive", log->enemies_alive);
    dict_set(out, "wave", log->wave);
    dict_set(out, "hp", log->hp);
    dict_set(out, "success", log->success);
}
