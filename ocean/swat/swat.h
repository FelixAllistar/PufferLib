#ifndef OCEAN_SWAT_H
#define OCEAN_SWAT_H
#include <stdlib.h>
#include <stdio.h>
#include "sim.h"
#include "render.h"
typedef float obs_t;
#include "pufferenv.h"

#define OBS_SIZE SWAT_OBS_SIZE
#define NUM_ATNS SWAT_ACTION_HEADS
#define ACT_SIZES SWAT_ACTION_SIZES

struct Log {
    float perf, episode_return, episode_length, score;
    float shots, hostile_damage, civilian_damage, destroyed, n;
};

struct Env {
    Log log;
    Agent agents[1];
    int num_agents, tag, boundary_reached;
    unsigned int rng;
    SwatSim* sim;
    SwatView* view;
    float episode_return;
};

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    env->sim = (SwatSim*)calloc(1,sizeof(SwatSim));
    if (!env->sim) { fprintf(stderr,"swat: allocation failed\n"); exit(1); }
    SwatConfig config = swat_default_config();
    DictItem* item = dict_find(kwargs,"max_ticks");
    if (item) config.max_ticks = (int)item->value;
    item = dict_find(kwargs,"randomize");
    if (item) config.randomize = item->value != 0;
    item = dict_find(kwargs,"hostile_fire");
    if (item) config.hostile_fire = item->value != 0;
    // Simulation owns stable storage: native vector setup may realloc Env.
    swat_sim_init(env->sim,config,env->rng);
}

void puf_reset(Env* env) {
    swat_sim_reset(env->sim);
    env->episode_return = 0;
    swat_sim_observe(env->sim,0,env->agents[0].observations);
}

void puf_step(Env* env) {
    *env->agents[0].terminals = 0;
    SwatInput input = swat_decode_action(env->agents[0].actions);
    swat_sim_step(env->sim,&input);
    SwatSim* s = env->sim;
    float reward = -0.001f - 0.0005f*s->events.shots + 0.01f*s->events.hostile_damage +
        s->events.hostile_down - 0.1f*s->events.civilian_damage - 0.01f*s->events.officer_damage;
    if (s->end == SWAT_SUCCESS) reward += 5;
    if (s->end == SWAT_CIVILIAN_HARMED) reward -= 5;
    if (s->end == SWAT_OFFICER_DOWN || s->end == SWAT_FALL) reward -= 1;
    *env->agents[0].rewards = reward;
    env->episode_return += reward;
    if (s->end != SWAT_RUNNING) {
        env->log.perf += s->end == SWAT_SUCCESS;
        env->log.episode_return += env->episode_return;
        env->log.episode_length += s->tick;
        env->log.score += s->totals.hostile_down;
        env->log.shots += s->totals.shots;
        env->log.hostile_damage += s->totals.hostile_damage;
        env->log.civilian_damage += s->totals.civilian_damage;
        env->log.destroyed += s->totals.destroyed;
        env->log.n += 1;
        *env->agents[0].terminals = 1;
        puf_reset(env);
    } else swat_sim_observe(s,0,env->agents[0].observations);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out,"perf",log->perf);
    dict_set(out,"episode_return",log->episode_return);
    dict_set(out,"episode_length",log->episode_length);
    dict_set(out,"score",log->score);
    dict_set(out,"shots",log->shots);
    dict_set(out,"hostile_damage",log->hostile_damage);
    dict_set(out,"civilian_damage",log->civilian_damage);
    dict_set(out,"destroyed",log->destroyed);
    dict_set(out,"n",log->n);
}

void puf_render(Env* env) {
    if (!env->view) {
        env->view = (SwatView*)calloc(1,sizeof(SwatView));
        if (!env->view) { fprintf(stderr,"swat: view allocation failed\n"); exit(1); }
        swat_view_init(env->view,false);
    }
    swat_view_draw(env->view,env->sim,true);
}

void puf_close(Env* env) {
    if (env->view) { swat_view_close(env->view); free(env->view); env->view = NULL; }
    if (env->sim) { swat_sim_close(env->sim); free(env->sim); env->sim = NULL; }
}
#endif
