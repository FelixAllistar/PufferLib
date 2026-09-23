#pragma once

#define PUF_BACKEND PUF_GPU
#include <cuda_runtime.h>
#include "policy.h"
typedef float obs_t;
#include "pufferenv.h"

#define OBS_SIZE KAG_ENTITY_OBS_SIZE
#define NUM_ATNS KAG_ACTION_HEADS
#define ACT_SIZES KAG_ACTION_SIZES
#define KAG_GPU_THREADS 128

struct Log {
    float perf, score, opponent_score, cash_gain;
    float episode_return, episode_length, draw_rate;
    float production_units, crop_units, animal_units;
    float plants, animal_places, land_purchases, neglect_deaths;
    float n;
};

struct Env {
    Log log;
    Agent agents[KG_NUM_PLAYERS];
    int num_agents, tag, boundary_reached;
    unsigned int rng;
    KGState game;
    KagPolicy policy;
    int bot_policy, learner_seat;
    float reward_money;
};

struct {
    int num_games;
    obs_t* observations;
    float* actions;
    float* rewards;
    float* terminals;
    cudaStream_t stream;
} kag_gpu;

KG_HD void kag_reset_episode(Env* env) {
    env->rng = 1664525u * env->rng + 1013904223u;
    env->game.config.seed = env->rng;
    kg_reset(&env->game);
    kag_policy_reset(&env->policy, &env->game, 0);
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = dict_get(kwargs, "num_agents");
    env->bot_policy = dict_get(kwargs, "bot_policy");
    env->learner_seat = dict_get(kwargs, "learner_seat");
    env->reward_money = dict_get(kwargs, "reward_money");
    env->policy = (KagPolicy){
        .market_slots = (int)dict_get(kwargs, "market_slots"),
        .max_hands = (int)dict_get(kwargs, "max_hands"),
        .land_buy_min_days = (int)dict_get(kwargs, "land_buy_min_days"),
    };
    assert(env->num_agents == 1 || env->num_agents == 2);
    assert(env->bot_policy == 0 || env->bot_policy == 1);
    assert(env->learner_seat == 0 || env->learner_seat == 1);
    assert(isfinite(env->reward_money) && env->reward_money >= 0);
    KGConfig config;
    kg_config_default(&config);
    config.seed = env->rng;
    kg_init(&env->game, &config);
    kag_policy_reset(&env->policy, &env->game, 0);
}

Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations, float* actions,
    float* rewards, float* terminals) {
    int num_agents = dict_get(kwargs, "num_agents");
    assert(num_agents == 1 || num_agents == 2);
    assert(total_agents > 0 && total_agents % num_agents == 0);
    int num_games = total_agents / num_agents;
    Env* host = (Env*)calloc(num_games, sizeof(Env));
    assert(host);
    for (int i = 0; i < num_games; i++) {
        host[i].rng = i;
        puf_init(&host[i], kwargs);
    }
    Env* envs = NULL;
    assert(cudaMalloc((void**)&envs, num_games * sizeof(Env)) == cudaSuccess);
    assert(cudaMemcpy(envs, host, num_games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    free(host);
    kag_gpu = {num_games, observations, actions, rewards, terminals, 0};
    return envs;
}

void puf_bind_stream(cudaStream_t stream) {
    kag_gpu.stream = stream;
}

__global__ void kag_observe_kernel(
    Env* envs, obs_t* observations, float* rewards, float* terminals, int num_games, bool reset) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_games) {
        return;
    }
    Env* env = envs + i;
    if (reset) {
        env->log = (Log){0};
        kag_reset_episode(env);
    }
    for (int a = 0; a < env->num_agents; a++) {
        int row = i * env->num_agents + a;
        int player = env->num_agents == 2 ? a : env->learner_seat;
        if (reset) {
            rewards[row] = terminals[row] = 0;
        }
        kag_write_observation(
            &env->policy, &env->game, player, observations + (long)row * OBS_SIZE);
    }
}

__global__ void kag_step_kernel(
    Env* envs, const float* actions, float* rewards, float* terminals, int num_games) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_games) {
        return;
    }
    Env* env = envs + i;
    KGState* game = &env->game;
    KGAction commands[KG_NUM_PLAYERS] = {0};
    for (int a = 0; a < env->num_agents; a++) {
        int player = env->num_agents == 2 ? a : env->learner_seat;
        kag_decode_multi_action(&commands[player],
            actions + (long)(i * env->num_agents + a) * NUM_ATNS, game, player, &env->policy);
    }
    if (env->num_agents == 1 && env->bot_policy == 1) {
        kg_rule_action(game, 1 - env->learner_seat, &commands[1 - env->learner_seat]);
    }
    kg_step(game, commands);
    kag_policy_step(&env->policy, game);
    for (int a = 0; a < env->num_agents; a++) {
        int row = i * env->num_agents + a;
        rewards[row] = 0;
        terminals[row] = game->done;
        if (!game->done) {
            continue;
        }
        int player = env->num_agents == 2 ? a : env->learner_seat;
        int money = game->players[player].money;
        int opponent_money = game->players[1 - player].money;
        int gain = money - env->policy.history[player].start_cash;
        // Terminal-only cash gain, in units of the initial 3,000 cash budget.
        rewards[row] = env->reward_money * gain / game->config.starting_money;
        Log* log = &env->log;
        log->perf += money > opponent_money ? 1 : (money == opponent_money ? 0.5f : 0);
        log->score += money;
        log->opponent_score += opponent_money;
        log->cash_gain += gain;
        log->episode_return += rewards[row];
        log->episode_length += game->step;
        log->draw_rate += money == opponent_money;
        log->production_units += game->production_units[player];
        for (int product = 0; product < KG_NUM_PRODUCTS; product++) {
            if (product < KG_NUM_CROPS) {
                log->crop_units += game->production_product_units[player][product];
            } else {
                log->animal_units += game->production_product_units[player][product];
            }
        }
        log->plants += game->planted_crops[player];
        log->animal_places += game->placed_animals[player];
        log->land_purchases += kag_popcount(game->players[player].unlocked_mask) - 1;
        log->neglect_deaths += game->neglect_deaths[player];
        log->n++;
    }
    if (game->done) {
        kag_reset_episode(env);
    }
}

void puf_reset(Env* envs) {
    int blocks = (kag_gpu.num_games + KAG_GPU_THREADS - 1) / KAG_GPU_THREADS;
    kag_observe_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.observations, kag_gpu.rewards, kag_gpu.terminals, kag_gpu.num_games, true);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_step(Env* envs) {
    int blocks = (kag_gpu.num_games + KAG_GPU_THREADS - 1) / KAG_GPU_THREADS;
    kag_step_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.actions, kag_gpu.rewards, kag_gpu.terminals, kag_gpu.num_games);
    kag_observe_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.observations, kag_gpu.rewards, kag_gpu.terminals, kag_gpu.num_games, false);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "opponent_score", log->opponent_score);
    dict_set(out, "cash_gain", log->cash_gain);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "terminal_cash_reward", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "production_units", log->production_units);
    dict_set(out, "crop_units", log->crop_units);
    dict_set(out, "animal_units", log->animal_units);
    dict_set(out, "plants", log->plants);
    dict_set(out, "animal_places", log->animal_places);
    dict_set(out, "land_purchases", log->land_purchases);
    dict_set(out, "neglect_deaths", log->neglect_deaths);
}

void puf_render(Env* envs) {
    assert(0 && "Kaggriculture renderer is not ported; use headless train/eval");
}

void puf_close(Env* envs) {
    assert(cudaFree(envs) == cudaSuccess);
    kag_gpu = {};
}
