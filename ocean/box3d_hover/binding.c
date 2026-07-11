#include "box3d_hover_render.h"

#define OBS_SIZE B3H_OBS_SIZE
#define NUM_ATNS B3H_NUM_ATNS
#define ACT_SIZES {B3H_DIRS, B3H_THROTTLES}
#define OBS_TENSOR_T FloatTensor
#define Env Box3DHover
#define MY_VEC_INIT 1

#include "vecenv.h"

static float b3h_kwarg(Dict* kwargs, const char* name, float fallback) {
    DictItem* item = dict_get_unsafe(kwargs, name);
    return item ? (float)item->value : fallback;
}

static void b3h_configure(Env* env, Dict* kwargs, int num_agents) {
    env->arena_half = b3h_kwarg(kwargs, "arena_half", 8.0f);
    env->arena_stride = b3h_kwarg(kwargs, "arena_stride", 22.0f);
    env->ball_radius = b3h_kwarg(kwargs, "ball_radius", 0.35f);
    env->target_radius = b3h_kwarg(kwargs, "target_radius", 0.55f);
    env->obstacle_radius = b3h_kwarg(kwargs, "obstacle_radius", 0.85f);
    env->force = b3h_kwarg(kwargs, "force", 18.0f);
    env->boost_mult = b3h_kwarg(kwargs, "boost_mult", 1.65f);
    env->brake_mult = b3h_kwarg(kwargs, "brake_mult", 0.55f);
    env->max_speed = b3h_kwarg(kwargs, "max_speed", 8.0f);
    env->max_steps = (int)b3h_kwarg(kwargs, "max_steps", 360.0f);
    env->substeps = (int)b3h_kwarg(kwargs, "substeps", 2.0f);
    box3d_hover_init(env, num_agents);
}

void my_init(Env* env, Dict* kwargs) {
    int agents_per_env = (int)b3h_kwarg(kwargs, "agents_per_env", (float)B3H_MAX_AGENTS);
    env->rng = env->rng == 0 ? 1u : env->rng;
    b3h_configure(env, kwargs, agents_per_env);
}

Env* my_vec_init(int* num_envs_out, int* buffer_env_starts, int* buffer_env_counts,
                 Dict* vec_kwargs, Dict* env_kwargs) {
    int total_agents = (int)dict_get(vec_kwargs, "total_agents")->value;
    int num_buffers = (int)dict_get(vec_kwargs, "num_buffers")->value;
    int agents_per_buffer = total_agents / num_buffers;
    int agents_per_env = (int)b3h_kwarg(env_kwargs, "agents_per_env", (float)B3H_MAX_AGENTS);
    if (agents_per_env < 1) agents_per_env = 1;
    if (agents_per_env > B3H_MAX_AGENTS) agents_per_env = B3H_MAX_AGENTS;
    if (num_buffers <= 0 || total_agents <= 0 || total_agents % num_buffers != 0) {
        fprintf(stderr, "box3d_hover: total_agents (%d) must be positive and divisible by num_buffers (%d)\n", total_agents, num_buffers);
        abort();
    }
    if (agents_per_buffer % agents_per_env != 0) {
        fprintf(stderr, "box3d_hover: agents_per_env (%d) must divide agents_per_buffer (%d). Try 32 or 64 for total_agents=4096,num_buffers=4.\n", agents_per_env, agents_per_buffer);
        abort();
    }

    int max_envs = (total_agents + agents_per_env - 1) / agents_per_env;
    if (max_envs > 128) {
        fprintf(stderr, "box3d_hover: requested %d Box3D worlds, but Box3D supports at most 128. Increase agents_per_env.\n", max_envs);
        abort();
    }
    Env* envs = (Env*)calloc((size_t)max_envs, sizeof(Env));

    int num_envs = 0;
    int agents_created = 0;
    while (agents_created < total_agents) {
        int remaining = total_agents - agents_created;
        int n = remaining < agents_per_env ? remaining : agents_per_env;
        envs[num_envs].rng = (unsigned int)(num_envs + 1);
        b3h_configure(&envs[num_envs], env_kwargs, n);
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
    dict_set(out, "captures", log->captures);
    dict_set(out, "timeouts", log->timeouts);
    dict_set(out, "wall_hits", log->wall_hits);
    dict_set(out, "obstacle_hits", log->obstacle_hits);
    dict_set(out, "speed", log->speed);
    dict_set(out, "distance", log->distance);
    dict_set(out, "box3d_step_ms", log->box3d_step_ms);
    dict_set(out, "body_count", log->body_count);
    dict_set(out, "contact_count", log->contact_count);
    dict_set(out, "n", log->n);
}
