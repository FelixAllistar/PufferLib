#pragma once

#include <stdint.h>
#include <string.h>

#include "pufferenv.h"

#ifdef __CUDACC__
#define TP_HD __host__ __device__
#else
#define TP_HD
#endif

#define TP_MAX_H 64
#define TP_MAX_CELLS ((TP_MAX_H * (TP_MAX_H + 1)) / 2)

#define NUM_ATNS 1
#define ACT_SIZES {2}
#define OBS_SIZE (TP_MAX_CELLS + 2)

typedef uint8_t obs_t;

enum {
    TP_LEFT = 0,
    TP_RIGHT = 1,
};

enum {
    TP_REWARD_DENSE = 0,
    TP_REWARD_TERMINAL_SCORE = 1,
    TP_REWARD_TERMINAL_OPTIMALITY = 2,
};

typedef struct {
    int height;
    int cell_min;
    int cell_max;
    uint32_t seed;
    int reward_mode;
} TPConfig;

typedef struct {
    uint8_t cells[TP_MAX_CELLS];
    int row;
    int col;
    int total;
    int done;
    int steps;
} TPState;

struct Log {
    float perf;       /* optimality = agent / optimal */
    float score;      /* agent total */
    float optimal;    /* DP optimal total */
    float regret;     /* optimal - agent */
    float episode_length;
    float n;
};

TP_HD static inline uint32_t tp_random(uint32_t* rng) {
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

TP_HD static inline int tp_cell_index(int row, int col) {
    return row * (row + 1) / 2 + col;
}

TP_HD static inline obs_t tp_encode_cell(int value, const TPConfig* cfg) {
    int span = cfg->cell_max - cfg->cell_min;
    if (span == 0) return 255;
    return (obs_t)(1 + (value - cfg->cell_min) * 254 / span);
}

TP_HD static inline obs_t tp_encode_pos(int value, const TPConfig* cfg) {
    return (obs_t)(value * 255 / (cfg->height - 1));
}

TP_HD static inline void tp_fill_triangle(TPState* state, const TPConfig* cfg,
        uint32_t* rng) {
    int span = cfg->cell_max - cfg->cell_min + 1;
    for (int row = 0; row < cfg->height; row++) {
        for (int col = 0; col <= row; col++) {
            state->cells[tp_cell_index(row, col)] =
                (uint8_t)(cfg->cell_min + (int)(tp_random(rng) % (uint32_t)span));
        }
    }
}

TP_HD static inline void tp_reset_state(TPState* state, const TPConfig* cfg,
        uint32_t* rng) {
    memset(state, 0, sizeof(*state));
    state->row = 0;
    state->col = 0;
    tp_fill_triangle(state, cfg, rng);
}

TP_HD static inline void tp_observe(const TPState* state,
        const TPConfig* cfg, obs_t* obs) {
    for (int i = 0; i < OBS_SIZE; i++) obs[i] = 0;
    for (int row = 0; row < cfg->height; row++) {
        for (int col = 0; col <= row; col++) {
            obs[tp_cell_index(row, col)] =
                tp_encode_cell(state->cells[tp_cell_index(row, col)], cfg);
        }
    }
    obs[TP_MAX_CELLS] = tp_encode_pos(state->row, cfg);
    obs[TP_MAX_CELLS + 1] = tp_encode_pos(state->col, cfg);
}

TP_HD static inline void tp_step(TPState* state, const TPConfig* cfg,
        int action) {
    if (state->done || state->row >= cfg->height - 1) {
        state->done = 1;
        return;
    }
    int cell = tp_cell_index(state->row, state->col);
    state->total += state->cells[cell];
    if (action == TP_RIGHT) state->col++;
    state->row++;
    state->steps++;
    if (state->row >= cfg->height - 1) {
        int last = tp_cell_index(state->row, state->col);
        state->total += state->cells[last];
        state->done = 1;
    }
}

#include "trianglepath_solve.h"

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "optimal", log->optimal);
    dict_set(out, "regret", log->regret);
    dict_set(out, "episode_length", log->episode_length);
}

struct Env {
    Log log;
    TPConfig cfg;
    TPState state;
    unsigned int rng;
    int num_agents;
    int tag;
    int boundary_reached;
    Agent agents[1];
};

#ifndef TP_CPU_ADAPTER
#define TP_CPU_ADAPTER

static inline TPConfig tp_load_config(Dict* kwargs) {
    TPConfig cfg = {0};
    cfg.height = (int)dict_get(kwargs, "height");
    cfg.cell_min = (int)dict_get(kwargs, "cell_min");
    cfg.cell_max = (int)dict_get(kwargs, "cell_max");
    cfg.seed = (uint32_t)dict_get(kwargs, "seed");
    cfg.reward_mode = (int)dict_get(kwargs, "reward_mode");
    if (cfg.height < 2 || cfg.height > TP_MAX_H
            || cfg.cell_min < 0 || cfg.cell_max < cfg.cell_min
            || cfg.cell_max > 255
            || cfg.reward_mode < TP_REWARD_DENSE
            || cfg.reward_mode > TP_REWARD_TERMINAL_OPTIMALITY) {
        fprintf(stderr,
            "trianglepath: invalid config height=%d min=%d max=%d reward=%d\n",
            cfg.height, cfg.cell_min, cfg.cell_max, cfg.reward_mode);
        exit(1);
    }
    return cfg;
}

void puf_init(Env* env, Dict* kwargs) {
    env->cfg = tp_load_config(kwargs);
    uint32_t stream = env->rng;
    uint32_t seed = env->cfg.seed;
    env->rng = seed ^ (0x9e3779b9u * (stream + 1u));
    env->num_agents = 1;
    env->tag = 0;
    env->boundary_reached = 0;
    memset(&env->log, 0, sizeof(env->log));
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;
}

void puf_reset(Env* env) {
    env->agents[0].rewards[0] = 0.0f;
    env->agents[0].terminals[0] = 0.0f;
    tp_reset_state(&env->state, &env->cfg, &env->rng);
    tp_observe(&env->state, &env->cfg,
        (obs_t*)env->agents[0].observations);
}

void puf_step(Env* env) {
    env->agents[0].terminals[0] = 0.0f;
    int action = (int)env->agents[0].actions[0];
    int prev_total = env->state.total;
    tp_step(&env->state, &env->cfg, action);
    env->agents[0].rewards[0] = env->cfg.reward_mode == TP_REWARD_DENSE
        ? (float)(env->state.total - prev_total) : 0.0f;
    if (env->state.done) {
        int optimal = tp_optimal_total(&env->state, &env->cfg);
        if (env->cfg.reward_mode == TP_REWARD_TERMINAL_SCORE) {
            int max_total = env->cfg.height * env->cfg.cell_max;
            env->agents[0].rewards[0] = max_total > 0
                ? (float)env->state.total / (float)max_total : 0.0f;
        } else if (env->cfg.reward_mode == TP_REWARD_TERMINAL_OPTIMALITY) {
            env->agents[0].rewards[0] = optimal > 0
                ? (float)env->state.total / (float)optimal : 0.0f;
        }
        env->agents[0].terminals[0] = 1.0f;
        env->log.score += (float)env->state.total;
        env->log.optimal += (float)optimal;
        env->log.regret += (float)(optimal - env->state.total);
        env->log.perf += optimal > 0
            ? (float)env->state.total / (float)optimal : 0.0f;
        env->log.episode_length += (float)env->state.steps;
        env->log.n += 1.0f;
        if (env->tag > 0) env->boundary_reached = 1;
        tp_reset_state(&env->state, &env->cfg, &env->rng);
    }
    tp_observe(&env->state, &env->cfg,
        (obs_t*)env->agents[0].observations);
}

void puf_close(Env* env) { (void)env; }
void puf_render(Env* env) { (void)env; }

#endif /* TP_CPU_ADAPTER */
