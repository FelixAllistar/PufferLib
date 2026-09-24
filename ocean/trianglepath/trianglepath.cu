#pragma once

#define PUF_BACKEND PUF_GPU
#include <cuda_runtime.h>
#include <stdint.h>
typedef uint8_t obs_t;

#include "trianglepath.h"

#ifndef TP_CUDA_BLOCK
#define TP_CUDA_BLOCK 128
#endif

/* The Env struct comes from trianglepath.h (Log is the first member, so the
 * generic device log reducer works). Per-env state lives in the SoA sim. */

typedef struct {
    int height;
    int cell_min;
    int cell_max;
    uint32_t base_seed;
    int reward_mode;
} TPCudaConfig;

static TPCudaConfig h_tp_config;
static __constant__ TPCudaConfig d_tp_config;

typedef struct {
    int count;
    obs_t* cells;
    int* row;
    int* col;
    int* total;
    int* done;
    uint32_t* rng;
    obs_t* obs;
    float* rewards;
    float* terminals;
} TPSim;

static TPSim g_sim;
static cudaStream_t tp_stream;
static float* tp_actions;

__device__ __forceinline__ uint32_t tp_gpu_random(uint32_t* rng) {
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

__global__ static void tp_reset_kernel(TPSim sim, uint32_t seed) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= sim.count) return;
    sim.row[i] = 0;
    sim.col[i] = 0;
    sim.total[i] = 0;
    sim.done[i] = 0;
    sim.rewards[i] = 0;
    sim.terminals[i] = 0;
    sim.rng[i] = seed ^ (0x9e3779b9u * (uint32_t)(i + 1u));
    int span = d_tp_config.cell_max - d_tp_config.cell_min + 1;
    obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    for (int row = 0; row < d_tp_config.height; row++) {
        for (int col = 0; col <= row; col++) {
            int cell = row * (row + 1) / 2 + col;
            cells[cell] = (obs_t)(d_tp_config.cell_min
                + (int)(tp_gpu_random(&sim.rng[i]) % (uint32_t)span));
        }
    }
}

__device__ static void tp_gpu_reset_env(TPSim sim, int i) {
    sim.row[i] = 0;
    sim.col[i] = 0;
    sim.total[i] = 0;
    sim.done[i] = 0;
    int span = d_tp_config.cell_max - d_tp_config.cell_min + 1;
    obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    for (int row = 0; row < d_tp_config.height; row++) {
        for (int col = 0; col <= row; col++) {
            int cell = row * (row + 1) / 2 + col;
            cells[cell] = (obs_t)(d_tp_config.cell_min
                + (int)(tp_gpu_random(&sim.rng[i]) % (uint32_t)span));
        }
    }
}

__global__ static void tp_observe_kernel(TPSim sim) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= sim.count) return;
    obs_t* obs = sim.obs + (size_t)i * OBS_SIZE;
    const obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    for (int k = 0; k < OBS_SIZE; k++) obs[k] = 0;
    for (int row = 0; row < d_tp_config.height; row++) {
        for (int col = 0; col <= row; col++) {
            obs[row * (row + 1) / 2 + col] =
                (obs_t)(d_tp_config.cell_max == d_tp_config.cell_min ? 255
                    : 1 + (cells[row * (row + 1) / 2 + col]
                        - d_tp_config.cell_min) * 254
                        / (d_tp_config.cell_max - d_tp_config.cell_min));
        }
    }
    obs[TP_MAX_CELLS] =
        (obs_t)(sim.row[i] * 255 / (d_tp_config.height - 1));
    obs[TP_MAX_CELLS + 1] =
        (obs_t)(sim.col[i] * 255 / (d_tp_config.height - 1));
}

__global__ static void tp_step_kernel(TPSim sim, Env* envs,
        const float* actions) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= sim.count) return;
    const obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    if (!sim.done[i]) {
        int cell = sim.row[i] * (sim.row[i] + 1) / 2 + sim.col[i];
        int prev = sim.total[i];
        sim.total[i] += cells[cell];
        sim.rewards[i] = d_tp_config.reward_mode == TP_REWARD_DENSE
            ? (float)(sim.total[i] - prev) : 0.0f;
        int action = (int)actions[i];
        if (action == TP_RIGHT) sim.col[i]++;
        sim.row[i]++;
        if (sim.row[i] >= d_tp_config.height - 1) {
            int last = sim.row[i] * (sim.row[i] + 1) / 2 + sim.col[i];
            prev = sim.total[i];
            sim.total[i] += cells[last];
            sim.rewards[i] += (float)(sim.total[i] - prev);
            sim.done[i] = 1;
            sim.terminals[i] = 1.0f;
            int dp[TP_MAX_CELLS];
            tp_solve(cells, d_tp_config.height, dp);
            int optimal = dp[0];
            if (d_tp_config.reward_mode == TP_REWARD_TERMINAL_SCORE) {
                int max_total = d_tp_config.height * d_tp_config.cell_max;
                sim.rewards[i] = max_total > 0
                    ? (float)sim.total[i] / (float)max_total : 0.0f;
            } else if (d_tp_config.reward_mode
                    == TP_REWARD_TERMINAL_OPTIMALITY) {
                sim.rewards[i] = optimal > 0
                    ? (float)sim.total[i] / (float)optimal : 0.0f;
            }
            envs[i].log.score += (float)sim.total[i];
            envs[i].log.optimal += (float)optimal;
            envs[i].log.regret += (float)(optimal - sim.total[i]);
            envs[i].log.perf += optimal > 0
                ? (float)sim.total[i] / (float)optimal : 0.0f;
            envs[i].log.episode_length += (float)(d_tp_config.height - 1);
            envs[i].log.n += 1.0f;
            /* Auto-reset after the terminal transition, preserving the
             * terminal flag in the external buffer (CPU puf_step parity). */
            tp_gpu_reset_env(sim, i);
        } else {
            sim.terminals[i] = 0.0f;
        }
    } else {
        sim.rewards[i] = 0.0f;
        sim.terminals[i] = 0.0f;
    }
}


Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations,
    float* actions, float* rewards, float* terminals) {
    assert(g_sim.cells == NULL);
    Env host = {0};
    puf_init(&host, kwargs);
    TPConfig cfg = host.cfg;
    h_tp_config = {cfg.height, cfg.cell_min, cfg.cell_max, cfg.seed, cfg.reward_mode};
    assert(cudaMemcpyToSymbol(d_tp_config, &h_tp_config, sizeof(h_tp_config))
        == cudaSuccess);
    g_sim.count = total_agents;
    g_sim.obs = observations;
    g_sim.rewards = rewards;
    g_sim.terminals = terminals;
    tp_actions = actions;
    assert(cudaMalloc(&g_sim.cells,
        (size_t)total_agents * TP_MAX_CELLS * sizeof(obs_t)) == cudaSuccess);
    assert(cudaMalloc(&g_sim.row, total_agents * sizeof(int)) == cudaSuccess);
    assert(cudaMalloc(&g_sim.col, total_agents * sizeof(int)) == cudaSuccess);
    assert(cudaMalloc(&g_sim.total, total_agents * sizeof(int)) == cudaSuccess);
    assert(cudaMalloc(&g_sim.done, total_agents * sizeof(int)) == cudaSuccess);
    assert(cudaMalloc(&g_sim.rng, total_agents * sizeof(uint32_t)) == cudaSuccess);
    Env* envs = NULL;
    assert(cudaMalloc(&envs, total_agents * sizeof(Env)) == cudaSuccess);
    assert(cudaMemset(envs, 0, total_agents * sizeof(Env)) == cudaSuccess);
    return envs;
}

void puf_bind_stream(cudaStream_t stream) {
    tp_stream = stream;
}

void puf_reset(Env* envs) {
    int grid = (g_sim.count + TP_CUDA_BLOCK - 1) / TP_CUDA_BLOCK;
    tp_reset_kernel<<<grid, TP_CUDA_BLOCK, 0, tp_stream>>>(g_sim, h_tp_config.base_seed);
    tp_observe_kernel<<<grid, TP_CUDA_BLOCK, 0, tp_stream>>>(g_sim);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_step(Env* envs) {
    int grid = (g_sim.count + TP_CUDA_BLOCK - 1) / TP_CUDA_BLOCK;
    tp_step_kernel<<<grid, TP_CUDA_BLOCK, 0, tp_stream>>>(g_sim, envs, tp_actions);
    tp_observe_kernel<<<grid, TP_CUDA_BLOCK, 0, tp_stream>>>(g_sim);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_close(Env* envs) {
    assert(cudaFree(g_sim.cells) == cudaSuccess);
    assert(cudaFree(g_sim.row) == cudaSuccess);
    assert(cudaFree(g_sim.col) == cudaSuccess);
    assert(cudaFree(g_sim.total) == cudaSuccess);
    assert(cudaFree(g_sim.done) == cudaSuccess);
    assert(cudaFree(g_sim.rng) == cudaSuccess);
    assert(cudaFree(envs) == cudaSuccess);
    g_sim = {};
    tp_stream = 0;
    tp_actions = NULL;
}
