#include "puffer_survivors.h"

#ifdef PS_HEADLESS_BINDING
static inline void c_render(PufferSurvivors* env) {
    (void)env;
}

static inline void c_close(PufferSurvivors* env) {
    (void)env;
}
#else
#include "ps_render.h"
#endif

#define OBS_SIZE PS_OBS_SIZE
#define NUM_ATNS 2
#define ACT_SIZES {9, 3}
#define OBS_TENSOR_T FloatTensor
#ifdef PS_ENABLE_CUDA_VEC
#define MY_CUDA_VEC
#endif

#define Env PufferSurvivors
#include "vecenv.h"

#ifdef PS_ENABLE_CUDA_VEC
#include "cuda/ps_cuda_vec.h"
#endif

static float ps_kwarg(Dict* kwargs, const char* name, float fallback) {
    DictItem* item = dict_get_unsafe(kwargs, name);
    return item ? (float)item->value : fallback;
}

static PSConfig ps_config_from_kwargs(Dict* kwargs) {
    PSConfig cfg = ps_default_config();
    cfg.arena_size = ps_kwarg(kwargs, "arena_size", cfg.arena_size);
    cfg.max_steps = (int)ps_kwarg(kwargs, "max_steps", (float)cfg.max_steps);
    cfg.wave_length_steps = (int)ps_kwarg(kwargs, "wave_length_steps", (float)cfg.wave_length_steps);
    cfg.enemy_cap = (int)ps_kwarg(kwargs, "enemy_cap", (float)cfg.enemy_cap);
    cfg.projectile_cap = (int)ps_kwarg(kwargs, "projectile_cap", (float)cfg.projectile_cap);
    cfg.drop_cap = (int)ps_kwarg(kwargs, "drop_cap", (float)cfg.drop_cap);
    cfg.obstacle_count = (int)ps_kwarg(kwargs, "obstacle_count", (float)cfg.obstacle_count);
    cfg.enemy_spawn_rate = ps_kwarg(kwargs, "enemy_spawn_rate", cfg.enemy_spawn_rate);
    cfg.elite_spawn_rate = ps_kwarg(kwargs, "elite_spawn_rate", cfg.elite_spawn_rate);
    cfg.player_speed = ps_kwarg(kwargs, "player_speed", cfg.player_speed);
    cfg.player_health = ps_kwarg(kwargs, "player_health", cfg.player_health);
    cfg.enemy_speed = ps_kwarg(kwargs, "enemy_speed", cfg.enemy_speed);
    cfg.enemy_hp_scale = ps_kwarg(kwargs, "enemy_hp_scale", cfg.enemy_hp_scale);
    cfg.enemy_damage_scale = ps_kwarg(kwargs, "enemy_damage_scale", cfg.enemy_damage_scale);
    cfg.spawn_ramp = ps_kwarg(kwargs, "spawn_ramp", cfg.spawn_ramp);
    cfg.projectile_speed = ps_kwarg(kwargs, "projectile_speed", cfg.projectile_speed);
    cfg.projectile_damage = ps_kwarg(kwargs, "projectile_damage", cfg.projectile_damage);
    cfg.fire_cooldown = ps_kwarg(kwargs, "fire_cooldown", cfg.fire_cooldown);
    cfg.pickup_radius = ps_kwarg(kwargs, "pickup_radius", cfg.pickup_radius);
    cfg.magnet_radius = ps_kwarg(kwargs, "magnet_radius", cfg.magnet_radius);
    cfg.health_drop_rate = ps_kwarg(kwargs, "health_drop_rate", cfg.health_drop_rate);
    cfg.health_heal = ps_kwarg(kwargs, "health_heal", cfg.health_heal);
    cfg.reward_xp = ps_kwarg(kwargs, "reward_xp", cfg.reward_xp);
    cfg.reward_kill = ps_kwarg(kwargs, "reward_kill", cfg.reward_kill);
    cfg.reward_damage = ps_kwarg(kwargs, "reward_damage", cfg.reward_damage);
    cfg.reward_survival = ps_kwarg(kwargs, "reward_survival", cfg.reward_survival);
    cfg.reward_hurt = ps_kwarg(kwargs, "reward_hurt", cfg.reward_hurt);
    cfg.reward_death = ps_kwarg(kwargs, "reward_death", cfg.reward_death);
    cfg.obstacle_penalty = ps_kwarg(kwargs, "obstacle_penalty", cfg.obstacle_penalty);
    cfg.contact_damage = ps_kwarg(kwargs, "contact_damage", cfg.contact_damage);
    cfg.invuln_steps = (int)ps_kwarg(kwargs, "invuln_steps", (float)cfg.invuln_steps);
    cfg.enemy_obstacle_stride = (int)ps_kwarg(kwargs, "enemy_obstacle_stride", (float)cfg.enemy_obstacle_stride);
    return cfg;
}

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->cfg = ps_config_from_kwargs(kwargs);
    env->show_hitboxes = (int)ps_kwarg(kwargs, "show_hitboxes", 0.0f);
    ps_init(env);
}

#ifdef PS_ENABLE_CUDA_VEC
int my_cuda_vec_enabled(Dict* vec_kwargs, Dict* env_kwargs) {
    (void)vec_kwargs;
    return ps_kwarg(env_kwargs, "cuda_sim", 0.0f) != 0.0f
        || ps_kwarg(env_kwargs, "use_cuda_sim", 0.0f) != 0.0f
        || ps_kwarg(env_kwargs, "cuda_env", 0.0f) != 0.0f;
}

void* my_cuda_vec_init(StaticVec* vec, Dict* vec_kwargs, Dict* env_kwargs) {
    (void)vec_kwargs;
    if (vec->size != vec->total_agents) {
        fprintf(stderr, "puffer_survivors cuda_sim requires one agent per env; got %d envs for %d agents\n",
            vec->size, vec->total_agents);
        return NULL;
    }

    PSConfig cfg = ps_config_from_kwargs(env_kwargs);
    void* handle = ps_survivors_cuda_vec_create(
        vec->total_agents,
        cfg,
        (float*)vec->gpu_observations,
        vec->gpu_actions,
        vec->gpu_rewards,
        vec->gpu_terminals);
    if (handle == NULL) {
        fprintf(stderr, "puffer_survivors cuda_sim failed to initialize; falling back to CPU env\n");
    } else {
        printf("puffer_survivors cuda_sim enabled for %d envs\n", vec->total_agents);
    }
    return handle;
}

void my_cuda_vec_reset(void* cuda_env, unsigned int seed, cudaStream_t stream) {
    ps_survivors_cuda_vec_reset(cuda_env, seed, (void*)stream);
}

void my_cuda_vec_step(void* cuda_env, int start, int count, cudaStream_t stream) {
    ps_survivors_cuda_vec_step_range(cuda_env, start, count, (void*)stream);
}

float my_cuda_vec_log(void* cuda_env, Log* out, int clear) {
    return ps_survivors_cuda_vec_log(cuda_env, out, clear);
}

void my_cuda_vec_close(void* cuda_env) {
    ps_survivors_cuda_vec_destroy(cuda_env);
}
#endif

void my_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
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
    dict_set(out, "n", log->n);
}
