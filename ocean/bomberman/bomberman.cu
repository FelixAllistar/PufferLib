#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV 1
typedef float obs_t;
#include "bomberman.h"
// GPU Bomberman: one CUDA thread owns one match; agents pack into flat tensors.
#ifndef PUFFER_BOMBERMAN_GPU_CU
#define PUFFER_BOMBERMAN_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "bomberman.cu requires -DPUFFER_GPU_ENV (build with --gpu)"
#endif

#include "bm_sim.h"

#include <cuda_runtime.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BM_CUDA_BLOCK
#define BM_CUDA_BLOCK 256
#endif

static BMConfig h_bcfg;
__constant__ BMConfig d_bcfg;

// Match pool: indexed by match id = agent_base / num_agents
static BMMatch* d_matches = NULL;
static int g_num_matches = 0;
static int g_num_agents = 2;
static int g_total_agents = 0;
static obs_t* bm_observations;
static float *bm_actions, *bm_rewards, *bm_terminals;
static cudaStream_t bm_stream;

static inline int bm_grid(int n) {
    return (n + BM_CUDA_BLOCK - 1) / BM_CUDA_BLOCK;
}

__global__ void bm_reset_kernel(Env* envs, BMMatch* matches, obs_t* observations,
        float* rewards, float* terminals, int num_matches, int num_agents) {
    int mid = blockIdx.x * blockDim.x + threadIdx.x;
    if (mid >= num_matches) return;

    envs[mid].num_agents = num_agents;
    BMMatch* m = &matches[mid];
    uint32_t seed = (uint32_t)(mid + 1) * 0x9E3779B9u;
    bm_reset_match(m, &d_bcfg, seed);

    for (int a = 0; a < num_agents; a++) {
        int gi = mid * num_agents + a;
        rewards[gi] = 0.0f;
        terminals[gi] = 0.0f;
        bm_write_obs(m, &d_bcfg, a,
            observations + (size_t)gi * OBS_SIZE);
    }
}

__global__ void bm_step_kernel(Env* envs, BMMatch* matches,
        const float* actions, obs_t* observations,
        float* rewards, float* terminals,
        int start_agent, int agent_count, int num_matches, int num_agents) {
    // Map agent-range to match-range. Full-rollout path uses start=0,count=total.
    int start_match = start_agent / num_agents;
    int end_agent = start_agent + agent_count;
    int end_match = (end_agent + num_agents - 1) / num_agents;
    if (end_match > num_matches) end_match = num_matches;

    int lane = blockIdx.x * blockDim.x + threadIdx.x;
    int mid = start_match + lane;
    if (mid >= end_match) return;

    BMMatch* m = &matches[mid];
    int acts[BM_MAX_AGENTS];
    float rew[BM_MAX_AGENTS];
    float term[BM_MAX_AGENTS];

    for (int a = 0; a < num_agents; a++) {
        int gi = mid * num_agents + a;
        acts[a] = (int)actions[(size_t)gi * NUM_ATNS];
        rew[a] = 0.0f;
        term[a] = 0.0f;
    }

    bm_step_match(m, &d_bcfg, acts, rew, term);

    if (m->done) {
        int outcome = m->winner == 0 ? 1 : m->winner > 0 ? -1 : 0;
        bm_log_match(&envs[mid].log, m, outcome);
        // Reset does not touch the local transition rewards and terminals.
        uint32_t seed = m->rng ^ (0x85ebca6bu * (uint32_t)(m->tick + 1));
        bm_reset_match(m, &d_bcfg, seed);
    }

    for (int a = 0; a < num_agents; a++) {
        int gi = mid * num_agents + a;
        rewards[gi] = rew[a];
        terminals[gi] = term[a];
        bm_write_obs(m, &d_bcfg, a,
            observations + (size_t)gi * OBS_SIZE);
    }
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = (int)dict_get(kwargs, "num_agents");
}

Env* puf_vec_create(int total_agents, Dict* env_kwargs, obs_t* observations,
    float* actions, float* rewards, float* terminals) {
    assert(d_matches == NULL);
    bm_observations = observations;
    bm_actions = actions;
    bm_rewards = rewards;
    bm_terminals = terminals;
    bm_load_config(&h_bcfg, env_kwargs);
    if (h_bcfg.reverse_curriculum) {
        fprintf(stderr, "Bomberman reverse curriculum requires the CPU simulator\n");
        exit(1);
    }
    g_num_agents = h_bcfg.num_agents;
    if (total_agents % g_num_agents != 0) {
        fprintf(stderr,
            "Bomberman GPU: total_agents (%d) must be divisible by num_agents (%d)\n",
            total_agents, g_num_agents);
        exit(1);
    }
    g_total_agents = total_agents;
    g_num_matches = total_agents / g_num_agents;

    cudaMemcpyToSymbol(d_bcfg, &h_bcfg, sizeof(BMConfig));

    Env* envs = NULL;
    cudaMalloc((void**)&envs, (size_t)g_num_matches * sizeof(Env));
    cudaMemset(envs, 0, (size_t)g_num_matches * sizeof(Env));

    cudaMalloc((void**)&d_matches, (size_t)g_num_matches * sizeof(BMMatch));
    cudaMemset(d_matches, 0, (size_t)g_num_matches * sizeof(BMMatch));

    assert(cudaStreamSynchronize(0) == cudaSuccess);
    return envs;
}

void puf_bind_stream(cudaStream_t stream) {
    bm_stream = stream;
}

void puf_reset(Env* envs) {
    bm_reset_kernel<<<bm_grid(g_num_matches), BM_CUDA_BLOCK, 0, bm_stream>>>(
        envs, d_matches, bm_observations, bm_rewards, bm_terminals,
        g_num_matches, g_num_agents);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_step(Env* envs) {
    bm_step_kernel<<<bm_grid(g_num_matches), BM_CUDA_BLOCK, 0, bm_stream>>>(
        envs, d_matches, bm_actions, bm_observations, bm_rewards, bm_terminals,
        0, g_total_agents, g_num_matches, g_num_agents);
    assert(cudaGetLastError() == cudaSuccess);
}

void puf_close(Env* envs) {
    if (d_matches) {
        cudaFree(d_matches);
        d_matches = NULL;
    }
    cudaFree(envs);
    g_num_matches = 0;
    g_total_agents = 0;
    bm_stream = 0;
    bm_observations = NULL;
    bm_actions = bm_rewards = bm_terminals = NULL;
}

#endif
