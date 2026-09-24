#pragma once

#define PUF_BACKEND PUF_GPU
#include <cuda_runtime.h>
typedef float obs_t;
#include "kaggriculture.h"

#define KAG_GPU_THREADS 128

struct {
    int num_games;
    obs_t* observations;
    float* actions;
    float* rewards;
    float* terminals;
    cudaStream_t stream;
    KGState* reset_states;
} kag_gpu;

Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations, float* actions,
    float* rewards, float* terminals) {
    int num_agents = dict_get(kwargs, "num_agents");
    assert(num_agents == 1 || num_agents == 2);
    assert(total_agents > 0 && total_agents % num_agents == 0);
    int num_games = total_agents / num_agents;
    Env* host = (Env*)calloc(num_games, sizeof(Env));
    int* rows = (int*)malloc(total_agents * sizeof(int));
    int* sampling_rows = NULL;
    assert(cudaMalloc((void**)&sampling_rows, total_agents * sizeof(int)) == cudaSuccess);
    assert(host);
    for (int i = 0; i < num_games; i++) {
        host[i].rng = i;
        puf_init(&host[i], kwargs);
        host[i].sampling_rows = sampling_rows;
        for (int a = 0; a < num_agents; a++) {
            int row = i * num_agents + a;
            host[i].rows[a] = row;
            host[i].agents[a].observations = observations + (long)row * OBS_SIZE;
            host[i].agents[a].actions = actions + (long)row * NUM_ATNS;
            host[i].agents[a].rewards = rewards + row;
            host[i].agents[a].terminals = terminals + row;
            rows[row] = 2 * i + (num_agents == 2 ? a : host[i].learner_seat);
        }
    }
    assert(cudaMemcpy(sampling_rows, rows, total_agents * sizeof(int), cudaMemcpyHostToDevice) ==
           cudaSuccess);
    free(rows);
    KGState* records = kag_load_reset_bank(host, num_games, kwargs);
    KGState* bank = NULL;
    if (records) {
        size_t bytes = (size_t)host[0].reset_count * sizeof(KGState);
        assert(cudaMalloc((void**)&bank, bytes) == cudaSuccess);
        assert(cudaMemcpy(bank, records, bytes, cudaMemcpyHostToDevice) == cudaSuccess);
        free(records);
        for (int i = 0; i < num_games; i++) {
            host[i].reset_states = bank;
        }
    }
    Env* envs = NULL;
    assert(cudaMalloc((void**)&envs, num_games * sizeof(Env)) == cudaSuccess);
    assert(cudaMemcpy(envs, host, num_games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    free(host);
    kag_gpu = {num_games, observations, actions, rewards, terminals, 0, bank};
    return envs;
}

// Group physical rows by policy, as the CPU env_setup path does. Games keep
// their own seat order; both observation/action IO and prefix sampling use it.
void kag_assign_policies(Env* envs, Dict* kwargs, int* layout) {
    int policies = dict_get(kwargs, "num_policies");
    if (policies <= 1) {
        return;
    }
    int games = kag_gpu.num_games;
    float fraction = dict_get(kwargs, "hist_policy_percent");
    Env* host = (Env*)malloc(games * sizeof(Env));
    assert(cudaMemcpy(host, envs, games * sizeof(Env), cudaMemcpyDeviceToHost) == cudaSuccess);
    kag_policy_assignment(host, games, kwargs);
    int* counts = (int*)calloc(policies, sizeof(int));
    int* rows = (int*)malloc(2 * games * sizeof(int));
    for (int i = 0; i < games; i++) {
        for (int a = 0; a < 2; a++) {
            counts[host[i].agents[a].policy]++;
        }
    }
    layout[0] = 0;
    for (int p = 0; p < policies; p++) {
        layout[p + 1] = layout[p] + counts[p];
        counts[p] = layout[p];
    }
    for (int i = 0; i < games; i++) {
        for (int a = 0; a < 2; a++) {
            int row = counts[host[i].agents[a].policy]++;
            host[i].rows[a] = row;
            host[i].agents[a].observations = kag_gpu.observations + (long)row * OBS_SIZE;
            host[i].agents[a].actions = kag_gpu.actions + (long)row * NUM_ATNS;
            host[i].agents[a].rewards = kag_gpu.rewards + row;
            host[i].agents[a].terminals = kag_gpu.terminals + row;
            rows[row] = 2 * i + a;
        }
    }
    assert(cudaMemcpy(host[0].sampling_rows, rows, 2 * games * sizeof(int),
               cudaMemcpyHostToDevice) == cudaSuccess);
    assert(cudaMemcpy(envs, host, games * sizeof(Env), cudaMemcpyHostToDevice) == cudaSuccess);
    printf("GPU policy rows:");
    for (int p = 0; p < policies; p++) {
        printf(" %d:%d", p, layout[p + 1] - layout[p]);
    }
    printf(" (checkpoint games %.3f, balanced seats)\n", fraction);
    free(rows);
    free(counts);
    free(host);
}

void puf_bind_stream(cudaStream_t stream) {
    kag_gpu.stream = stream;
}

Env* kag_sampling_envs(Env* envs) {
    return envs;
}

__global__ void kag_observe_kernel(Env* envs, int games, bool reset) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < games) {
        kag_observe(envs + i, reset);
    }
}

__global__ void kag_step_kernel(Env* envs, int games) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < games) {
        kag_step(envs + i);
    }
}

void puf_reset(Env* envs) {
    int blocks = (kag_gpu.num_games + KAG_GPU_THREADS - 1) / KAG_GPU_THREADS;
    kag_observe_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.num_games, true);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_step(Env* envs) {
    int blocks = (kag_gpu.num_games + KAG_GPU_THREADS - 1) / KAG_GPU_THREADS;
    kag_step_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.num_games);
    kag_observe_kernel<<<blocks, KAG_GPU_THREADS, 0, kag_gpu.stream>>>(
        envs, kag_gpu.num_games, false);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_close(Env* envs) {
    int* sampling_rows;
    assert(cudaMemcpy(&sampling_rows, &envs->sampling_rows, sizeof(sampling_rows),
               cudaMemcpyDeviceToHost) == cudaSuccess);
    assert(cudaFree(sampling_rows) == cudaSuccess);
    assert(cudaFree(kag_gpu.reset_states) == cudaSuccess);
    assert(cudaFree(envs) == cudaSuccess);
    kag_gpu = {};
}
