#pragma once
typedef float obs_t;
#include "pufferenv.h"
#include "bridge.h"
#include "observation.h"
#include <assert.h>

#define OBS_SIZE WEBNAV_FEATURES
#define ACT_SIZES {WEBNAV_ACTIONS}
#define NUM_ATNS 1
struct Log { float perf, score, episode_length, n; };
struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[WEBNAV_BATCH];
    int tag, boundary_reached;
    uint32_t words[WEBNAV_BATCH*WEBNAV_WORDS];
};

static inline uint32_t webnav_seed(Env *env) {
    env->rng=env->rng*1664525u+1013904223u;
    return env->rng;
}
static void webnav_observe(Env *env) {
    for(int i=0;i<WEBNAV_BATCH;i++) {
        uint32_t *r=env->words+i*WEBNAV_WORDS;
        webnav_features(r+WEBNAV_OBS_OFFSET,(float*)env->agents[i].observations);
        if(env->agents[i].action_mask)
            for(int a=0;a<WEBNAV_ACTIONS;a++)env->agents[i].action_mask[a]=r[WEBNAV_MASK_OFFSET+a];
    }
}
void puf_init(Env *env, Dict *kwargs) {
    (void)kwargs;
    env->num_agents=WEBNAV_BATCH;
    memset(env->words,0,sizeof env->words);
    for(int i=0;i<WEBNAV_BATCH;i++)env->agents[i].policy=0;
}

#define MY_VEC_INIT
Env* my_vec_init(int* size, int* starts, int* counts, Dict* vk, Dict* ek) {
    int total = dict_get(vk, "total_agents");
    int buffers = dict_get(vk, "num_buffers");
    assert(buffers > 0 && total >= WEBNAV_BATCH * buffers);
    assert(total % (WEBNAV_BATCH * buffers) == 0);
    *size = total / WEBNAV_BATCH;
    int per_buffer = *size / buffers;
    Env* envs = (Env*)calloc(*size, sizeof(Env));
    for (int buf = 0; buf < buffers; buf++) {
        starts[buf] = buf * per_buffer;
        counts[buf] = per_buffer;
    }
    for (int e = 0; e < *size; e++) {
        envs[e].rng = e;
        puf_init(envs + e, ek);
    }
    return envs;
}
void puf_reset(Env *env) {
    for(int i=0;i<WEBNAV_BATCH;i++) {
        env->words[i*WEBNAV_WORDS]=webnav_seed(env);
        env->words[i*WEBNAV_WORDS+14]=1;
    }
    webnav_batch(env->words);
    webnav_observe(env);
}
void puf_step(Env *env) {
    for(int i=0;i<WEBNAV_BATCH;i++) {
        env->words[i*WEBNAV_WORDS+13]=(uint32_t)env->agents[i].actions[0];
        env->words[i*WEBNAV_WORDS+14]=0;
        env->agents[i].rewards[0]=0;
        env->agents[i].terminals[0]=0;
    }
    webnav_batch(env->words);
    int reset=0;
    for(int i=0;i<WEBNAV_BATCH;i++) {
        uint32_t *r=env->words+i*WEBNAV_WORDS;
        r[14]=2; /* untouched rows only refresh observations during autoreset */
        if(r[9]) {
            float success=r[10]==1;
            env->agents[i].rewards[0]=success ? 1.0f : -1.0f;
            env->agents[i].terminals[0]=1;
            env->log.perf+=success;
            env->log.score+=success;
            env->log.episode_length+=r[8];
            env->log.n++;
            r[0]=webnav_seed(env);r[14]=1;reset=1;
        }
    }
    if(reset)webnav_batch(env->words);
    webnav_observe(env);
}
void puf_log(Log *log, Dict *out) {
    dict_set(out,"perf",log->perf);
    dict_set(out,"score",log->score);
    dict_set(out,"episode_length",log->episode_length);
}
void puf_render(Env *env) { (void)env; }
void puf_close(Env *env) { (void)env; }

#if defined(PUFFERCPU_EVAL_MAIN)
#error "WebNav browser/policy evaluator port is pending; use simulator tests"
#endif
