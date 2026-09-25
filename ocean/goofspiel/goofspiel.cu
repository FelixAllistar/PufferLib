#pragma once

#define PUF_BACKEND PUF_GPU
#define PUF_GPU_SETUP
#include <cuda_runtime.h>
#include <stdint.h>
typedef uint8_t obs_t;
#include "goofspiel.h"

struct {
    int games;
    obs_t* observations;
    float* actions;
    float* rewards;
    float* terminals;
    cudaStream_t stream;
    Env viewer;
} gs_gpu;

Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations,
        float* actions, float* rewards, float* terminals) {
    int players = dict_get(kwargs, "num_players");
    assert(players == 2 && total_agents > 0 && total_agents % players == 0);
    assert(gs_gpu.games == 0);
    int games = total_agents / players;
    Env* host = (Env*)calloc(games, sizeof(Env));
    for (int i = 0; i < games; i++) {
        host[i].rng = i;
        puf_init(host + i, kwargs);
    }
    Env* envs = NULL;
    assert(cudaMalloc(&envs, games * sizeof(Env)) == cudaSuccess);
    assert(cudaMemcpy(envs, host, games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    free(host);
    gs_gpu.games = games;
    gs_gpu.observations = observations;
    gs_gpu.actions = actions;
    gs_gpu.rewards = rewards;
    gs_gpu.terminals = terminals;
    return envs;
}

// Match CPU policy grouping while retaining the simulator's seat order.
void puf_gpu_setup(Env* envs, Dict* kwargs, int* layout, unsigned char* masks) {
    int policies = dict_get(kwargs, "num_policies");
    assert(policies > 0 && masks);
    int games = gs_gpu.games;
    Env* host = (Env*)malloc(games * sizeof(Env));
    assert(cudaMemcpy(host, envs, games * sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
    gs_assign_policies(host, games, kwargs);
    int* cursors = (int*)calloc(policies, sizeof(int));
    for (int e = 0; e < games; e++) {
        for (int p = 0; p < host[e].num_agents; p++) {
            cursors[host[e].agents[p].policy]++;
        }
    }
    layout[0] = 0;
    for (int p = 0; p < policies; p++) {
        layout[p + 1] = layout[p] + cursors[p];
        cursors[p] = layout[p];
    }
    for (int e = 0; e < games; e++) {
        for (int p = 0; p < host[e].num_agents; p++) {
            Agent* agent = host[e].agents + p;
            int row = cursors[agent->policy]++;
            agent->observations = gs_gpu.observations + row * OBS_SIZE;
            agent->actions = gs_gpu.actions + row;
            agent->rewards = gs_gpu.rewards + row;
            agent->terminals = gs_gpu.terminals + row;
            agent->action_mask = masks + row * GS_NUM_CARDS;
        }
    }
    assert(cudaMemcpy(envs, host, games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    free(cursors);
    free(host);
}

__global__ void gs_reset_kernel(Env* envs, int games) {
    int e = blockIdx.x * blockDim.x + threadIdx.x;
    if (e >= games) {
        return;
    }
    Env* env = envs + e;
    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].rewards[0] = 0;
        env->agents[p].terminals[0] = 0;
    }
    gs_reset_state(env, 0, 0);
}

__global__ void gs_step_kernel(Env* envs, int games) {
    int e = blockIdx.x * blockDim.x + threadIdx.x;
    if (e >= games) {
        return;
    }
    if (gs_transition(envs + e, NULL, 0)) {
        gs_reset_state(envs + e, 0, 0);
    }
}

void puf_bind_stream(cudaStream_t stream) {
    gs_gpu.stream = stream;
}

void puf_reset(Env* envs) {
    gs_reset_kernel<<<(gs_gpu.games + 127) / 128, 128, 0, gs_gpu.stream>>>(envs, gs_gpu.games);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_step(Env* envs) {
    gs_step_kernel<<<(gs_gpu.games + 127) / 128, 128, 0, gs_gpu.stream>>>(envs, gs_gpu.games);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_render(Env* envs) {
    Client* client = gs_gpu.viewer.client;
    assert(cudaMemcpy(&gs_gpu.viewer, envs, sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
    gs_gpu.viewer.client = client;
    gs_render(&gs_gpu.viewer);
}

void puf_close(Env* envs) {
    if (gs_gpu.viewer.client) {
        CloseWindow();
        free(gs_gpu.viewer.client);
    }
    assert(cudaFree(envs) == cudaSuccess);
    gs_gpu = {};
}
