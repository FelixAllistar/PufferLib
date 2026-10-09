#pragma once
#define PUF_BACKEND PUF_GPU
#define PUFFER_GPU_ENV 1
typedef float obs_t;
#include <cuda_runtime.h>
#include "mario_lab.h"

#define ML_CUDA(call) do { cudaError_t err = (call); if (err != cudaSuccess) { \
    fprintf(stderr, "mario_lab CUDA: %s: %s\n", #call, cudaGetErrorString(err)); abort(); } } while (0)

static MLConfig ml_host_config;
static __constant__ MLConfig ml_device_config;
static MLState* ml_states;
static int ml_count;
static float *ml_obs, *ml_actions, *ml_rewards, *ml_terminals;
static cudaStream_t ml_stream;

__global__ void ml_reset_kernel(Env* envs, MLState* states, float* rewards, float* terminals, int count) {
    int i = blockIdx.x * blockDim.x + threadIdx.x; if (i >= count) return;
    envs[i].num_agents = 1;
    envs[i].rng = ml_initial_seed(&ml_device_config, i);
    ml_reset(&states[i], &ml_device_config, ml_rand(&envs[i].rng));
    rewards[i] = terminals[i] = 0;
}
__global__ void ml_step_kernel(Env* envs, MLState* states, const float* actions,
        float* rewards, float* terminals, int count) {
    int i = blockIdx.x * blockDim.x + threadIdx.x; if (i >= count) return;
    MLState* s = &states[i];
    rewards[i] = ml_step(s, &ml_device_config, (int)actions[i]);
    int done = s->status != ML_RUNNING;
    terminals[i] = (float)done;
    if (done) {
        ml_log_episode(&envs[i].log, s);
        ml_reset(s, &ml_device_config, ml_rand(&envs[i].rng));
    }
}
__global__ void ml_observe_kernel(const MLState* states, float* observations, int count) {
    int env = blockIdx.x; if (env >= count) return;
    for (int i = threadIdx.x; i < ML_OBS_SIZE; i += blockDim.x)
        observations[(size_t)env * ML_OBS_SIZE + i] = ml_observation_at(&states[env], &ml_device_config, i);
}

void puf_init(Env* e, Dict*) { e->num_agents = 1; }
Env* puf_vec_create(int total_agents, Dict* kwargs, obs_t* observations,
        float* actions, float* rewards, float* terminals) {
    if (ml_states || total_agents < 1) abort();
    ml_host_config = ml_config(kwargs); ml_count = total_agents;
    ml_obs = observations; ml_actions = actions; ml_rewards = rewards; ml_terminals = terminals;
    ML_CUDA(cudaMemcpyToSymbol(ml_device_config, &ml_host_config, sizeof(MLConfig)));
    Env* envs = NULL;
    ML_CUDA(cudaMalloc((void**)&envs, (size_t)ml_count * sizeof(Env)));
    ML_CUDA(cudaMemset(envs, 0, (size_t)ml_count * sizeof(Env)));
    ML_CUDA(cudaMalloc((void**)&ml_states, (size_t)ml_count * sizeof(MLState)));
    ML_CUDA(cudaStreamSynchronize(0));
    return envs;
}
void puf_bind_stream(cudaStream_t stream) { ml_stream = stream; }
void puf_reset(Env* envs) {
    ml_reset_kernel<<<(ml_count + 127) / 128, 128, 0, ml_stream>>>(envs, ml_states, ml_rewards, ml_terminals, ml_count);
    ML_CUDA(cudaGetLastError());
    ml_observe_kernel<<<ml_count, 128, 0, ml_stream>>>(ml_states, ml_obs, ml_count);
    ML_CUDA(cudaGetLastError());
}
void puf_step(Env* envs) {
    ml_step_kernel<<<(ml_count + 127) / 128, 128, 0, ml_stream>>>(envs, ml_states, ml_actions, ml_rewards, ml_terminals, ml_count);
    ML_CUDA(cudaGetLastError());
    ml_observe_kernel<<<ml_count, 128, 0, ml_stream>>>(ml_states, ml_obs, ml_count);
    ML_CUDA(cudaGetLastError());
}
void puf_render(Env*) {
#ifndef ML_HEADLESS
    MLState state;
    ML_CUDA(cudaStreamSynchronize(ml_stream));
    ML_CUDA(cudaMemcpy(&state, ml_states, sizeof(state), cudaMemcpyDeviceToHost));
    ml_render_state(&state, &ml_host_config);
#endif
}
void puf_close(Env* envs) {
    // Render evaluation first probes puf_init with a stack-allocated descriptor.
    if (!ml_states) return;
    ML_CUDA(cudaStreamSynchronize(ml_stream));
    ML_CUDA(cudaFree(ml_states)); ML_CUDA(cudaFree(envs));
    ml_states = NULL; ml_count = 0; ml_stream = 0;
    ml_obs = ml_actions = ml_rewards = ml_terminals = NULL;
}
