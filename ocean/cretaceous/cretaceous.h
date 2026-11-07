// PufferLib binding for Cretaceous. Every agent in an env is trainable; the
// game player (cretaceous.c) and dedicated server run the same CretSim.
// Rewards are a placeholder (travel) until survival systems land.
#ifndef OCEAN_CRETACEOUS_H
#define OCEAN_CRETACEOUS_H
#include <stdlib.h>
#include <stdio.h>
#include "sim.h"
typedef float obs_t;
#include "pufferenv.h"

#define OBS_SIZE CRET_OBS_SIZE
#define NUM_ATNS CRET_ACTION_HEADS
#define ACT_SIZES CRET_ACTION_SIZES

struct Log {
    float perf, score, episode_return, episode_length, distance, swim, n;
};

struct Env {
    Log log;
    Agent agents[CRET_MAX_AGENTS];
    int num_agents, tag, boundary_reached;
    unsigned int rng;
    CretSim* sim;
    float returns[CRET_MAX_AGENTS];
    float last_distance[CRET_MAX_AGENTS];
    int swim_ticks[CRET_MAX_AGENTS];
};

static float kw(Dict* kwargs, const char* key, float fallback) {
    DictItem* it = dict_find(kwargs, key); return it ? (float)it->value : fallback;
}

void puf_init(Env* env, Dict* kwargs) {
    CretConfig cfg = cret_default_config();
    cfg.num_agents = (int)kw(kwargs, "num_agents", cfg.num_agents);
    cfg.world_seed = (uint32_t)kw(kwargs, "world_seed", cfg.world_seed);
    cfg.world_n = (int)kw(kwargs, "world_n", cfg.world_n);
    cfg.world_cell = kw(kwargs, "world_cell", cfg.world_cell);
    cfg.max_ticks = (int)kw(kwargs, "max_ticks", cfg.max_ticks);
    cfg.seed = env->rng ? env->rng : 1;
    if (cfg.num_agents < 1 || cfg.num_agents > CRET_MAX_AGENTS) { fprintf(stderr, "cretaceous: num_agents must be 1..%d\n", CRET_MAX_AGENTS); exit(1); }
    env->num_agents = cfg.num_agents;
    for (int i = 0; i < env->num_agents; i++) { env->agents[i].policy = 0; env->agents[i].action_mask = NULL; }
    env->sim = (CretSim*)calloc(1, sizeof(CretSim));   // stable storage: native vec setup may realloc Env
    if (!env->sim || cret_sim_init(env->sim, cfg) != 0) { fprintf(stderr, "cretaceous: sim init failed\n"); exit(1); }
}

void puf_reset(Env* env) {
    cret_sim_reset(env->sim);
    for (int i = 0; i < env->num_agents; i++) {
        env->returns[i] = 0; env->last_distance[i] = 0; env->swim_ticks[i] = 0;
        cret_sim_observe(env->sim, i, env->agents[i].observations);
    }
}

void puf_step(Env* env) {
    CretSim* s = env->sim; CretInput in[CRET_MAX_AGENTS];
    for (int i = 0; i < env->num_agents; i++) in[i] = cret_decode_action(env->agents[i].actions);
    cret_sim_step(s, in);
    bool done = s->tick >= s->cfg.max_ticks;
    for (int i = 0; i < env->num_agents; i++) {
        CretAgent* a = &s->agents[i];
        float moved = a->distance - env->last_distance[i]; env->last_distance[i] = a->distance;
        float r = 0.002f * moved - (a->swimming ? 0.002f : 0.0f);   // placeholder until survival lands
        env->agents[i].rewards[0] = r; env->agents[i].terminals[0] = done ? 1.0f : 0.0f;
        env->returns[i] += r; env->swim_ticks[i] += a->swimming;
    }
    if (done) {
        for (int i = 0; i < env->num_agents; i++) {
            env->log.episode_return += env->returns[i]; env->log.episode_length += s->tick;
            env->log.distance += s->agents[i].distance; env->log.swim += (float)env->swim_ticks[i] / s->tick;
            env->log.score += s->agents[i].distance; env->log.perf += s->agents[i].distance > 500.0f; env->log.n += 1;
        }
        puf_reset(env);
        return;
    }
    for (int i = 0; i < env->num_agents; i++) cret_sim_observe(s, i, env->agents[i].observations);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf); dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return); dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "distance", log->distance); dict_set(out, "swim", log->swim); dict_set(out, "n", log->n);
}

// Training renders nothing; use ./cretaceous to play or watch.
void puf_render(Env* env) { (void)env; }

void puf_close(Env* env) {
    if (env->sim) { cret_sim_close(env->sim); free(env->sim); env->sim = NULL; }
}
#endif
