#include "box3d_pusher_render.h"

#define OBS_SIZE B3P_OBS_SIZE
#define NUM_ATNS B3P_NUM_ATNS
#define ACT_SIZES {B3P_DIRS, B3P_THROTTLES}
#define OBS_TENSOR_T FloatTensor
#define Env Box3DPusher
#define MY_VEC_INIT 1

#include "vecenv.h"

static float b3p_kwarg(Dict* kwargs, const char* name, float fallback) {
    DictItem* item = dict_get_unsafe(kwargs, name);
    return item ? (float)item->value : fallback;
}

static void b3p_configure(Env* env, Dict* kwargs, int num_agents) {
    env->arena_half = b3p_kwarg(kwargs, "arena_half", 8.0f);
    env->arena_stride = b3p_kwarg(kwargs, "arena_stride", 22.0f);
    env->player_radius = b3p_kwarg(kwargs, "player_radius", 0.38f);
    env->puck_radius = b3p_kwarg(kwargs, "puck_radius", 0.32f);
    env->goal_radius = b3p_kwarg(kwargs, "goal_radius", 0.85f);
    env->bumper_radius = b3p_kwarg(kwargs, "bumper_radius", 0.72f);
    env->goal_local_x = b3p_kwarg(kwargs, "goal_local_x", 6.65f);
    env->goal_z_range = b3p_kwarg(kwargs, "goal_z_range", 4.4f);
    env->player_start_x = b3p_kwarg(kwargs, "player_start_x", -1.2f);
    env->puck_start_shift = b3p_kwarg(kwargs, "puck_start_shift", 0.0f);
    env->puck_start_scale = b3p_kwarg(kwargs, "puck_start_scale", 1.0f);
    env->force = b3p_kwarg(kwargs, "force", 23.0f);
    env->boost_mult = b3p_kwarg(kwargs, "boost_mult", 1.65f);
    env->brake_mult = b3p_kwarg(kwargs, "brake_mult", 0.45f);
    env->max_speed = b3p_kwarg(kwargs, "max_speed", 9.0f);
    env->approach_reward = b3p_kwarg(kwargs, "approach_reward", 0.04f);
    env->push_reward = b3p_kwarg(kwargs, "push_reward", 0.05f);
    env->progress_reward = b3p_kwarg(kwargs, "progress_reward", 0.08f);
    env->contact_reward = b3p_kwarg(kwargs, "contact_reward", 0.004f);
    env->goal_reward = b3p_kwarg(kwargs, "goal_reward", 2.25f);
    env->target_goals = (int)b3p_kwarg(kwargs, "target_goals", 5.0f);
    env->max_steps = (int)b3p_kwarg(kwargs, "max_steps", 720.0f);
    env->substeps = (int)b3p_kwarg(kwargs, "substeps", 1.0f);
    box3d_pusher_init(env, num_agents);
}

void my_init(Env* env, Dict* kwargs) {
    int agents_per_env = (int)b3p_kwarg(kwargs, "agents_per_env", (float)B3P_MAX_AGENTS);
    env->rng = env->rng == 0 ? 1u : env->rng;
    b3p_configure(env, kwargs, agents_per_env);
}

Env* my_vec_init(int* num_envs_out, int* buffer_env_starts, int* buffer_env_counts,
                 Dict* vec_kwargs, Dict* env_kwargs) {
    int total_agents = (int)dict_get(vec_kwargs, "total_agents")->value;
    int num_buffers = (int)dict_get(vec_kwargs, "num_buffers")->value;
    int agents_per_buffer = total_agents / num_buffers;
    int agents_per_env = (int)b3p_kwarg(env_kwargs, "agents_per_env", (float)B3P_MAX_AGENTS);
    if (agents_per_env < 1) agents_per_env = 1;
    if (agents_per_env > B3P_MAX_AGENTS) agents_per_env = B3P_MAX_AGENTS;
    if (num_buffers <= 0 || total_agents <= 0 || total_agents % num_buffers != 0) {
        fprintf(stderr, "box3d_pusher: total_agents (%d) must be positive and divisible by num_buffers (%d)\n", total_agents, num_buffers);
        abort();
    }
    if (agents_per_buffer % agents_per_env != 0) {
        fprintf(stderr, "box3d_pusher: agents_per_env (%d) must divide agents_per_buffer (%d). Try 32 or 64 for total_agents=4096,num_buffers=4.\n", agents_per_env, agents_per_buffer);
        abort();
    }

    int max_envs = (total_agents + agents_per_env - 1) / agents_per_env;
    if (max_envs > 128) {
        fprintf(stderr, "box3d_pusher: requested %d Box3D worlds, but Box3D supports at most 128. Increase agents_per_env.\n", max_envs);
        abort();
    }
    Env* envs = (Env*)calloc((size_t)max_envs, sizeof(Env));

    int num_envs = 0;
    int agents_created = 0;
    while (agents_created < total_agents) {
        int remaining = total_agents - agents_created;
        int n = remaining < agents_per_env ? remaining : agents_per_env;
        envs[num_envs].rng = (unsigned int)(num_envs + 1);
        b3p_configure(&envs[num_envs], env_kwargs, n);
        agents_created += n;
        num_envs++;
    }

    int buf = 0;
    int buf_agents = 0;
    buffer_env_starts[0] = 0;
    buffer_env_counts[0] = 0;
    for (int i = 0; i < num_envs; i++) {
        if (buf_agents >= agents_per_buffer && buf < num_buffers - 1) {
            buf++;
            buffer_env_starts[buf] = i;
            buffer_env_counts[buf] = 0;
            buf_agents = 0;
        }
        buf_agents += envs[i].num_agents;
        buffer_env_counts[buf]++;
    }

    *num_envs_out = num_envs;
    return envs;
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "goals", log->goals);
    dict_set(out, "timeouts", log->timeouts);
    dict_set(out, "wall_hits", log->wall_hits);
    dict_set(out, "bumper_hits", log->bumper_hits);
    dict_set(out, "player_contacts", log->player_contacts);
    dict_set(out, "puck_speed", log->puck_speed);
    dict_set(out, "distance", log->distance);
    dict_set(out, "box3d_step_ms", log->box3d_step_ms);
    dict_set(out, "body_count", log->body_count);
    dict_set(out, "contact_count", log->contact_count);
    dict_set(out, "n", log->n);
}
