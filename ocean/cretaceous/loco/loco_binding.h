// PufferLib binding of the locomotion env (loco_env.h), included by
// cretaceous.h in place of the game's binding when CRET_LOCO is defined.
// The trainer fixes the observation and action sizes at compile time, so loco
// mode is chosen at build time, with the trainer's own pass-through for extra
// compiler flags (no change to build.sh or the trainer):
//     NVCC_EXTRA=-DCRET_LOCO ./build.sh cretaceous
// and its [env] keys come from config/cretaceous_loco.ini (loco/train.sh
// passes that file to ./puffer as --section.key=value overrides, since the
// trainer itself always reads config/cretaceous.ini).
//
// Every action head is continuous (size 1): LOCO_MAX_DOF of them, of which a
// rig uses its own count (the raptor 51; the rest are ignored). The
// observation is LOCO_OBS floats, padded the same way.
#ifndef CRET_LOCO_BINDING_H
#define CRET_LOCO_BINDING_H
#include <assert.h>       // static_assert in C11
#include "loco_env.h"
// The trainer compiles the game's structured encoder (cretaceous_encoder.cu) for this env
// whatever the mode; it reads sim.h's sizes. Loco mode needs policy.cretaceous_net = 0
// (the encoder asserts on any other observation size).
#include "sim.h"
typedef float obs_t;
#include "pufferenv.h"

#define OBS_SIZE LOCO_OBS
#define NUM_ATNS LOCO_MAX_DOF
#define LOCO_ONES10 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
#define ACT_SIZES {LOCO_ONES10, LOCO_ONES10, LOCO_ONES10, LOCO_ONES10, LOCO_ONES10, LOCO_ONES10, 1, 1, 1, 1, 1, 1, 1, 1, 1}
static_assert(LOCO_MAX_DOF == 69, "ACT_SIZES lists LOCO_MAX_DOF ones");

// Per finished episode, averaged by the trainer over n: survived (perf), the
// return, its length (s), the distance the pelvis travelled (m), and the
// mean tracking errors (joint rotation in degrees per body, end effectors in cm).
// push_level: the shove curriculum's level at episode ends (0..1, push_adapt).
// clip_speed: the mean target speed (m/s) over the steps since the last log
// update, the mix of clips (weighted by the episodes ended, so the trainer's
// division by n gives the mean). cadence: the mean playback rate, the same way.
struct Log { float perf, score, episode_return, episode_length, distance, falls, pose_deg, ee_cm, push_level, clip_speed, cadence, effort, n; };

struct Env {
    Log log;
    Agent agents[LOCO_MAX_AGENTS];
    int num_agents, tag, boundary_reached;   // tag, boundary_reached: written by the trainer's vec setup
    unsigned int rng;                        // the trainer sets each env's index here before puf_init
    LocoWorld* w;
    LocoStats seen;      // w->stats already added to log
};

void puf_init(Env* env, Dict* kwargs) {
    LocoConfig c = loco_default_config();
    DictItem* it = dict_find(kwargs, "dino");
    if (it && it->str) snprintf(c.dino, sizeof c.dino, "%s", it->str);
    it = dict_find(kwargs, "clip");
    if (it && it->str) snprintf(c.clip, sizeof c.clip, "%s", it->str);
#define LOCO_READ(field, key, type) if ((it = dict_find(kwargs, #key))) c.field = (type)it->value;
    LOCO_KEYS(LOCO_READ)
#undef LOCO_READ
    env->w = (LocoWorld*)calloc(1, sizeof(LocoWorld));   // stable storage: native vec setup may realloc Env
    char err[256];
    // Env indices 0, 1, 2 ... become distinct nonzero seeds.
    unsigned int seed = (env->rng + 1u) * 0x9E3779B1u;
    if (!env->w || !loco_world_init(env->w, &c, seed ? seed : 1u, err, sizeof err)) { fprintf(stderr, "cretaceous loco: %s\n", err); exit(1); }
    env->num_agents = c.num_agents;
    for (int i = 0; i < env->num_agents; i++) { env->agents[i].policy = 0; env->agents[i].action_mask = NULL; }
}

static void loco_bind_observe(Env* env) {
    for (int i = 0; i < env->num_agents; i++) loco_observe(env->w, i, env->agents[i].observations);
}

void puf_reset(Env* env) {
    loco_reset_all(env->w);
    loco_bind_observe(env);
}

void puf_step(Env* env) {
    LocoWorld* w = env->w; const float* act[LOCO_MAX_AGENTS]; float rew[LOCO_MAX_AGENTS], term[LOCO_MAX_AGENTS];
    for (int i = 0; i < env->num_agents; i++) act[i] = env->agents[i].actions;
    loco_step(w, act, rew, term);
    for (int i = 0; i < env->num_agents; i++) { env->agents[i].rewards[0] = rew[i]; env->agents[i].terminals[0] = term[i]; }
    const LocoStats* s = &w->stats; LocoStats* p = &env->seen;
    if (s->episodes > p->episodes) {
        Log* l = &env->log; float ne = (float)(s->episodes - p->episodes);
        l->perf += ne - (float)(s->falls - p->falls); l->falls += (float)(s->falls - p->falls);
        l->score += (float)(s->ret - p->ret); l->episode_return += (float)(s->ret - p->ret);
        l->episode_length += (float)(s->length - p->length); l->distance += (float)(s->distance - p->distance);
        l->pose_deg += (float)(s->pose_deg - p->pose_deg); l->ee_cm += (float)(s->ee_cm - p->ee_cm); l->push_level += (float)(s->push_level - p->push_level);
        double ds = s->steps - p->steps;
        l->clip_speed += ne * (float)(ds > 0 ? (s->clip_speed - p->clip_speed) / ds : 0.0);
        l->cadence += ne * (float)(ds > 0 ? (s->rate - p->rate) / ds : 1.0);
        l->effort += ne * (float)(ds > 0 ? (s->effort - p->effort) / ds : 0.0);
        l->n += ne;
        *p = *s;
    }
    loco_bind_observe(env);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf); dict_set(out, "score", log->score);
    dict_set(out, "episode_return", log->episode_return); dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "distance", log->distance); dict_set(out, "falls", log->falls);
    dict_set(out, "pose_deg", log->pose_deg); dict_set(out, "ee_cm", log->ee_cm); dict_set(out, "push_level", log->push_level); dict_set(out, "clip_speed", log->clip_speed); dict_set(out, "cadence", log->cadence); dict_set(out, "effort", log->effort); dict_set(out, "n", log->n);
}

// Training renders nothing; tools/loco_view.c draws the ragdolls.
void puf_render(Env* env) { (void)env; }

void puf_close(Env* env) {
    if (env->w) { loco_world_close(env->w); free(env->w); env->w = NULL; }
}
#endif
