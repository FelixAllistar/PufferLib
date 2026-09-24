#pragma once

#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV
#include <cuda_runtime.h>
#include <assert.h>
typedef float obs_t;
#include "puffer_survivors.h"
#include "cuda/ps_cuda_sim.cu"

PSCudaSim ps_gpu;
cudaStream_t ps_stream;

Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations,
    float* actions, float* rewards, float* terminals) {
    assert(ps_gpu.blob == NULL);
    ps_cuda_alloc(&ps_gpu, total_agents, ps_config_from_kwargs(kwargs));
    ps_gpu.observations = observations;
    ps_gpu.actions = actions;
    ps_gpu.rewards = rewards;
    ps_gpu.terminals = terminals;
    assert(cudaMalloc((void**)&ps_gpu.native_envs,
        (size_t)total_agents * sizeof(Env)) == cudaSuccess);
    assert(cudaMemset(ps_gpu.native_envs, 0,
        (size_t)total_agents * sizeof(Env)) == cudaSuccess);
    return ps_gpu.native_envs;
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
}

void puf_bind_stream(cudaStream_t stream) {
    ps_stream = stream;
}

void puf_reset(Env* envs) {
    ps_cuda_reset_all(&ps_gpu, 1u, ps_stream);
}

void puf_step(Env* envs) {
    ps_cuda_step_range(&ps_gpu, 0, ps_gpu.num_envs, ps_stream);
}

void puf_close(Env* envs) {
    assert(cudaFree(envs) == cudaSuccess);
    ps_cuda_free(&ps_gpu);
    ps_stream = 0;
}
