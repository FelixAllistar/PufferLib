#pragma once

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t obs_t;
#include "pufferenv.h"

#define TP_MAX_H 64
#define TP_MAX_CELLS ((TP_MAX_H * (TP_MAX_H + 1)) / 2)
#define NUM_ATNS 1
#define ACT_SIZES {2}
#define OBS_SIZE (TP_MAX_CELLS + 2)

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
    float perf;
    float score;
    float optimal;
    float regret;
    float episode_length;
    float n;
};

struct Env {
    Log log;
    Agent agents[1];
    int num_agents;
    int tag;
    int boundary_reached;
    unsigned int rng;
    TPConfig cfg;
    TPState state;
};

uint32_t tp_random(uint32_t* rng) {
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

int tp_cell_index(int row, int col) {
    return row * (row + 1) / 2 + col;
}

void tp_reset_state(TPState* state, const TPConfig* cfg, uint32_t* rng) {
    memset(state, 0, sizeof(*state));
    int span = cfg->cell_max - cfg->cell_min + 1;
    for (int row = 0; row < cfg->height; row++) {
        for (int col = 0; col <= row; col++) {
            state->cells[tp_cell_index(row, col)] =
                cfg->cell_min + tp_random(rng) % span;
        }
    }
}

void tp_observe(const TPState* state, const TPConfig* cfg, obs_t* obs) {
    memset(obs, 0, OBS_SIZE * sizeof(obs_t));
    int span = cfg->cell_max - cfg->cell_min;
    for (int row = 0; row < cfg->height; row++) {
        for (int col = 0; col <= row; col++) {
            int i = tp_cell_index(row, col);
            obs[i] = span == 0 ? 255
                : 1 + (state->cells[i] - cfg->cell_min) * 254 / span;
        }
    }
    obs[TP_MAX_CELLS] = state->row * 255 / (cfg->height - 1);
    obs[TP_MAX_CELLS + 1] = state->col * 255 / (cfg->height - 1);
}

void tp_step(TPState* state, const TPConfig* cfg, int action) {
    if (state->done || state->row >= cfg->height - 1) {
        state->done = 1;
        return;
    }
    state->total += state->cells[tp_cell_index(state->row, state->col)];
    if (action == TP_RIGHT) {
        state->col++;
    }
    state->row++;
    state->steps++;
    if (state->row >= cfg->height - 1) {
        state->total += state->cells[tp_cell_index(state->row, state->col)];
        state->done = 1;
    }
}

// Shared backward DP for the evaluation oracle and optimal path recovery.
void tp_solve(const TPState* state, const TPConfig* cfg, int* dp) {
    for (int col = 0; col < cfg->height; col++) {
        int i = tp_cell_index(cfg->height - 1, col);
        dp[i] = state->cells[i];
    }
    for (int row = cfg->height - 2; row >= 0; row--) {
        for (int col = 0; col <= row; col++) {
            int left = dp[tp_cell_index(row + 1, col)];
            int right = dp[tp_cell_index(row + 1, col + 1)];
            int i = tp_cell_index(row, col);
            dp[i] = state->cells[i] + (left > right ? left : right);
        }
    }
}

int tp_optimal_total(const TPState* state, const TPConfig* cfg) {
    int dp[TP_MAX_CELLS];
    tp_solve(state, cfg, dp);
    return dp[0];
}

int tp_optimal_action(const TPState* state, const TPConfig* cfg, int row, int col) {
    if (row >= cfg->height - 1) {
        return TP_LEFT;
    }
    int dp[TP_MAX_CELLS];
    tp_solve(state, cfg, dp);
    int left = dp[tp_cell_index(row + 1, col)];
    int right = dp[tp_cell_index(row + 1, col + 1)];
    return right > left ? TP_RIGHT : TP_LEFT;
}

void puf_init(Env* env, Dict* kwargs) {
    env->cfg = (TPConfig){
        .height = dict_get(kwargs, "height"),
        .cell_min = dict_get(kwargs, "cell_min"),
        .cell_max = dict_get(kwargs, "cell_max"),
        .seed = dict_get(kwargs, "seed"),
        .reward_mode = dict_get(kwargs, "reward_mode"),
    };
    TPConfig* cfg = &env->cfg;
    assert(cfg->height >= 2 && cfg->height <= TP_MAX_H);
    assert(cfg->cell_min >= 0 && cfg->cell_min <= cfg->cell_max);
    assert(cfg->cell_max <= 255);
    assert(cfg->reward_mode >= TP_REWARD_DENSE
        && cfg->reward_mode <= TP_REWARD_TERMINAL_OPTIMALITY);
    env->rng = cfg->seed ^ (0x9e3779b9u * (env->rng + 1u));
    env->num_agents = 1;
    env->tag = 0;
    env->boundary_reached = 0;
    memset(&env->log, 0, sizeof(env->log));
    env->agents[0].policy = 0;
    env->agents[0].action_mask = NULL;
}

void puf_reset(Env* env) {
    env->agents[0].rewards[0] = 0;
    env->agents[0].terminals[0] = 0;
    tp_reset_state(&env->state, &env->cfg, &env->rng);
    tp_observe(&env->state, &env->cfg, env->agents[0].observations);
}

void puf_step(Env* env) {
    Agent* agent = &env->agents[0];
    agent->terminals[0] = 0;
    int prev_total = env->state.total;
    tp_step(&env->state, &env->cfg, agent->actions[0]);
    agent->rewards[0] = env->cfg.reward_mode == TP_REWARD_DENSE
        ? env->state.total - prev_total : 0;
    if (env->state.done) {
        int optimal = tp_optimal_total(&env->state, &env->cfg);
        if (env->cfg.reward_mode == TP_REWARD_TERMINAL_SCORE) {
            int max_total = env->cfg.height * env->cfg.cell_max;
            agent->rewards[0] = max_total > 0
                ? (float)env->state.total / max_total : 0;
        } else if (env->cfg.reward_mode == TP_REWARD_TERMINAL_OPTIMALITY) {
            agent->rewards[0] = optimal > 0
                ? (float)env->state.total / optimal : 0;
        }
        agent->terminals[0] = 1;
        env->log.score += env->state.total;
        env->log.optimal += optimal;
        env->log.regret += optimal - env->state.total;
        env->log.perf += optimal > 0 ? (float)env->state.total / optimal : 0;
        env->log.episode_length += env->state.steps;
        env->log.n++;
        if (env->tag > 0) {
            env->boundary_reached = 1;
        }
        tp_reset_state(&env->state, &env->cfg, &env->rng);
    }
    tp_observe(&env->state, &env->cfg, agent->observations);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "optimal", log->optimal);
    dict_set(out, "regret", log->regret);
    dict_set(out, "episode_length", log->episode_length);
}

void puf_close(Env* env) {
}

void puf_render(Env* env) {
}
