// GPU Bomberman: one CUDA thread owns one match; agents pack into flat tensors.
#ifndef PUFFER_BOMBERMAN_GPU_CU
#define PUFFER_BOMBERMAN_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "bomberman.cu requires -DPUFFER_GPU_ENV (build with --gpu)"
#endif

#include "bm_sim.h"

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef BM_CUDA_BLOCK
#define BM_CUDA_BLOCK 256
#endif

static BMConfig h_bcfg;
__constant__ BMConfig d_bcfg;

// Match pool: indexed by match id = agent_base / num_agents
static BMMatch* d_matches = nullptr;
static int g_num_matches = 0;
static int g_num_agents = 2;
static int g_total_agents = 0;

static inline int bm_grid(int n) {
    return (n + BM_CUDA_BLOCK - 1) / BM_CUDA_BLOCK;
}

__global__ void bm_reset_kernel(Env* envs, BMMatch* matches, obs_t* observations,
        float* rewards, float* terminals, int num_matches, int num_agents) {
    int mid = blockIdx.x * blockDim.x + threadIdx.x;
    if (mid >= num_matches) return;

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
        bm_log_match(&envs[mid * num_agents].log, m, outcome);
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

static Env* puf_envs_create(int total_agents, Dict* env_kwargs) {
    bm_load_config(&h_bcfg, env_kwargs);
    if (h_bcfg.reverse_curriculum) {
        fprintf(stderr, "Bomberman reverse curriculum currently requires vec.gpu_env=0\n");
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

    Env* envs = nullptr;
    cudaMalloc((void**)&envs, (size_t)total_agents * sizeof(Env));
    cudaMemset(envs, 0, (size_t)total_agents * sizeof(Env));

    cudaMalloc((void**)&d_matches, (size_t)g_num_matches * sizeof(BMMatch));
    cudaMemset(d_matches, 0, (size_t)g_num_matches * sizeof(BMMatch));

    return envs;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    if (total_agents != g_total_agents) {
        fprintf(stderr, "Bomberman GPU reset: total_agents mismatch\n");
        exit(1);
    }
    bm_reset_kernel<<<bm_grid(g_num_matches), BM_CUDA_BLOCK>>>(
        envs, d_matches, observations, rewards, terminals,
        g_num_matches, g_num_agents);
}

static void puf_envs_step(Env* envs, const float* actions, obs_t* observations,
        float* rewards, float* terminals, int start, int count, cudaStream_t stream) {
    int start_match = start / g_num_agents;
    int end_agent = start + count;
    int end_match = (end_agent + g_num_agents - 1) / g_num_agents;
    if (end_match > g_num_matches) end_match = g_num_matches;
    int nmatch = end_match - start_match;
    if (nmatch <= 0) return;
    bm_step_kernel<<<bm_grid(nmatch), BM_CUDA_BLOCK, 0, stream>>>(
        envs, d_matches, actions, observations, rewards, terminals,
        start, count, g_num_matches, g_num_agents);
}

static void puf_envs_close(Env* envs) {
    if (d_matches) {
        cudaFree(d_matches);
        d_matches = nullptr;
    }
    cudaFree(envs);
    g_num_matches = 0;
    g_total_agents = 0;
}

#endif
