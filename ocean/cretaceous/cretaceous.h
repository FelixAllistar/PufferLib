// PufferLib binding for Cretaceous. Every agent in an env is trainable; the
// game player (cretaceous.c) and dedicated server run the same CretSim.
// Rewards: survive, eat, drink, grow, fight, finish quests (weights in [env]).
// Contract 4 (sim.h): CRET_OBS_SIZE = 243 floats; action heads {5, 5, 3, 2, 7}
// (the last: none, attack, eat or drink, call, ability 1, 2, 3).
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
    float deaths, kills, food, water, damage_dealt, damage_taken, growth, health;
    float quests, abilities;   // per agent and episode: quests completed, abilities cast
};

// Reward weights ([env] in config/cretaceous.ini).
typedef struct RewardW { float alive, food, water, dealt, taken, kill, death, growth, quest; } RewardW;

struct Env {
    Log log;
    Agent agents[CRET_MAX_AGENTS];
    int num_agents, tag, boundary_reached;
    unsigned int rng;
    CretSim* sim;
    RewardW w;
    float returns[CRET_MAX_AGENTS];
    int swim_ticks[CRET_MAX_AGENTS], deaths[CRET_MAX_AGENTS], kills[CRET_MAX_AGENTS];
    float food[CRET_MAX_AGENTS], water[CRET_MAX_AGENTS], dealt[CRET_MAX_AGENTS], taken[CRET_MAX_AGENTS], distance[CRET_MAX_AGENTS];
    int quests[CRET_MAX_AGENTS], abilities0;   // abilities0: the sim's ability count at the episode start
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
    cfg.respawn_ticks = (int)kw(kwargs, "respawn_ticks", 1);
    cfg.growth_seconds = kw(kwargs, "growth_seconds", cfg.growth_seconds);
    cfg.hunger_seconds = kw(kwargs, "hunger_seconds", cfg.hunger_seconds);
    cfg.thirst_seconds = kw(kwargs, "thirst_seconds", cfg.thirst_seconds);
    cfg.day_seconds = kw(kwargs, "day_seconds", cfg.day_seconds);
    cfg.weather = (int)kw(kwargs, "weather", 0);   // weather never changes the sim; off keeps rollouts identical
    cfg.player_growth = -1;   // every slot spawns like the bots
    cfg.seed = env->rng ? env->rng : 1;
    env->w = (RewardW){
        .alive = kw(kwargs, "reward_alive", 0.001f), .food = kw(kwargs, "reward_food", 1.0f), .water = kw(kwargs, "reward_water", 1.0f),
        .dealt = kw(kwargs, "reward_damage_dealt", 0.5f), .taken = kw(kwargs, "reward_damage_taken", 1.0f),
        .kill = kw(kwargs, "reward_kill", 1.0f), .death = kw(kwargs, "reward_death", 1.0f), .growth = kw(kwargs, "reward_growth", 2.0f),
        .quest = kw(kwargs, "reward_quest", 0.25f)};
    if (cfg.num_agents < 1 || cfg.num_agents > CRET_MAX_AGENTS) { fprintf(stderr, "cretaceous: num_agents must be 1..%d\n", CRET_MAX_AGENTS); exit(1); }
    env->num_agents = cfg.num_agents;
    for (int i = 0; i < env->num_agents; i++) { env->agents[i].policy = 0; env->agents[i].action_mask = NULL; }
    env->sim = (CretSim*)calloc(1, sizeof(CretSim));   // stable storage: native vec setup may realloc Env
    if (!env->sim || cret_sim_init(env->sim, cfg) != 0) { fprintf(stderr, "cretaceous: sim init failed\n"); exit(1); }
}

static int ability_casts(const CretSim* s) { int n = 0; for (int k = 0; k < CRET_AB_COUNT; k++) n += s->stats.ability_uses[k]; return n; }

void puf_reset(Env* env) {
    cret_sim_reset(env->sim); env->abilities0 = ability_casts(env->sim);
    for (int i = 0; i < env->num_agents; i++) {
        env->returns[i] = 0; env->swim_ticks[i] = 0; env->deaths[i] = env->kills[i] = 0;
        env->food[i] = env->water[i] = env->dealt[i] = env->taken[i] = env->distance[i] = 0; env->quests[i] = 0;
        cret_sim_observe(env->sim, i, env->agents[i].observations);
    }
}

// Reward per tick: staying alive, food and water gained (in stomachs), damage
// dealt and taken (fractions of the victim's / own max health), kills,
// growth and death. A death ends that agent's episode (terminal); it respawns
// as a new animal.
void puf_step(Env* env) {
    CretSim* s = env->sim; CretInput in[CRET_MAX_AGENTS]; const RewardW* w = &env->w;
    float growth0[CRET_MAX_AGENTS], dist0[CRET_MAX_AGENTS];
    for (int i = 0; i < env->num_agents; i++) {
        in[i] = cret_decode_action(env->agents[i].actions);
        growth0[i] = s->agents[i].growth; dist0[i] = s->agents[i].distance;
    }
    cret_sim_step(s, in);
    bool done = s->tick >= s->cfg.max_ticks;
    for (int i = 0; i < env->num_agents; i++) {
        CretAgent* a = &s->agents[i];
        bool grew = a->alive && !a->ev_died && a->growth > growth0[i];
        float r = (a->alive ? w->alive : 0) + w->food * a->ev_food + w->water * a->ev_water + w->dealt * a->ev_dealt - w->taken * a->ev_taken
                + w->kill * a->ev_kills - (a->ev_died ? w->death : 0) + (grew ? w->growth * (a->growth - growth0[i]) : 0) + w->quest * a->ev_quest;
        env->agents[i].rewards[0] = r; env->agents[i].terminals[0] = done || a->ev_died ? 1.0f : 0.0f;
        env->returns[i] += r; env->swim_ticks[i] += a->swimming;
        env->deaths[i] += a->ev_died; env->kills[i] += a->ev_kills; env->food[i] += a->ev_food; env->water[i] += a->ev_water;
        env->dealt[i] += a->ev_dealt; env->taken[i] += a->ev_taken; env->quests[i] += a->ev_quest;
        if (a->alive && a->distance > dist0[i]) env->distance[i] += a->distance - dist0[i];
    }
    if (done) {
        for (int i = 0; i < env->num_agents; i++) {
            const CretAgent* a = &s->agents[i];
            env->log.episode_return += env->returns[i]; env->log.episode_length += s->tick;
            env->log.distance += env->distance[i]; env->log.swim += (float)env->swim_ticks[i] / s->tick;
            env->log.deaths += env->deaths[i]; env->log.kills += env->kills[i]; env->log.food += env->food[i]; env->log.water += env->water[i];
            env->log.damage_dealt += env->dealt[i]; env->log.damage_taken += env->taken[i];
            env->log.growth += a->growth; env->log.health += a->alive ? a->health / a->max_health : 0;
            env->log.score += env->returns[i]; env->log.perf += env->deaths[i] == 0; env->log.n += 1;   // perf: survived the episode
            env->log.quests += env->quests[i]; env->log.abilities += (float)(ability_casts(s) - env->abilities0) / env->num_agents;
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
    dict_set(out, "deaths", log->deaths); dict_set(out, "kills", log->kills); dict_set(out, "food", log->food); dict_set(out, "water", log->water);
    dict_set(out, "damage_dealt", log->damage_dealt); dict_set(out, "damage_taken", log->damage_taken);
    dict_set(out, "growth", log->growth); dict_set(out, "health", log->health);
    dict_set(out, "quests", log->quests); dict_set(out, "abilities", log->abilities);
}

// Training renders nothing; use ./cretaceous to play or watch.
void puf_render(Env* env) { (void)env; }

void puf_close(Env* env) {
    if (env->sim) { cret_sim_close(env->sim); free(env->sim); env->sim = NULL; }
}
#endif
