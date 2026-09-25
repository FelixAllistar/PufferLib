#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV 1
typedef float obs_t;
#include "arpg.h"
#include <assert.h>
#include "cuda/ar_cuda_sim.cu"

static ARCudaSim ar_gpu;
static cudaStream_t ar_stream;

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
}

Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations,
        float* actions, float* rewards, float* terminals) {
    assert(ar_gpu.blob == NULL);
    ar_cuda_alloc(&ar_gpu, total_agents, ar_config_from_kwargs(kwargs));
    cudaFree(ar_gpu.observations);
    cudaFree(ar_gpu.actions);
    cudaFree(ar_gpu.rewards);
    cudaFree(ar_gpu.terminals);
    ar_gpu.owns_io = 0;
    ar_gpu.observations = observations;
    ar_gpu.actions = actions;
    ar_gpu.rewards = rewards;
    ar_gpu.terminals = terminals;
    AR_CUDA_CHECK(cudaMalloc(&ar_gpu.native_envs, total_agents * sizeof(Env)));
    AR_CUDA_CHECK(cudaMemset(ar_gpu.native_envs, 0, total_agents * sizeof(Env)));
    AR_CUDA_CHECK(cudaStreamSynchronize(0));
    return ar_gpu.native_envs;
}

void puf_bind_stream(cudaStream_t stream) {
    ar_stream = stream;
}

void puf_reset(Env* envs) {
    ar_cuda_reset_all(&ar_gpu, 1u, ar_stream);
}

void puf_step(Env* envs) {
    ar_cuda_step_range(&ar_gpu, 0, ar_gpu.num_envs, ar_stream);
}

void puf_close(Env* envs) {
    ar_cuda_free(&ar_gpu);
    cudaFree(envs);
    ar_stream = 0;
}
