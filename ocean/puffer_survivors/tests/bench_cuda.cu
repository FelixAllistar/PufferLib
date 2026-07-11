#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>

#include "../ps_constants.h"
#include "../ps_config.h"
#include "../cuda/ps_cuda_sim.cuh"

#define CUDA_CHECK(call) do { \
    cudaError_t err__ = (call); \
    if (err__ != cudaSuccess) { \
        std::fprintf(stderr, "CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err__)); \
        std::exit(1); \
    } \
} while (0)

int main(int argc, char** argv) {
    int num_envs = argc > 1 ? std::atoi(argv[1]) : 8192;
    int steps = argc > 2 ? std::atoi(argv[2]) : 10000;
    if (num_envs < 1 || steps < 1) {
        std::fprintf(stderr, "usage: %s [num_envs] [steps]\n", argv[0]);
        return 2;
    }

    float* observations = nullptr;
    float* actions = nullptr;
    float* rewards = nullptr;
    float* terminals = nullptr;
    CUDA_CHECK(cudaMalloc((void**)&observations, sizeof(float) * (size_t)num_envs * PS_OBS_SIZE));
    CUDA_CHECK(cudaMalloc((void**)&actions, sizeof(float) * (size_t)num_envs * 2));
    CUDA_CHECK(cudaMalloc((void**)&rewards, sizeof(float) * (size_t)num_envs));
    CUDA_CHECK(cudaMalloc((void**)&terminals, sizeof(float) * (size_t)num_envs));
    CUDA_CHECK(cudaMemset(actions, 0, sizeof(float) * (size_t)num_envs * 2));

    PSConfig cfg = ps_default_config();
    cfg.player_health = 1000000.0f;
    cfg.max_steps = 1000000000;
    PSCudaSim* sim = ps_cuda_sim_create(num_envs, cfg, observations, actions, rewards, terminals);
    if (sim == nullptr) {
        std::fprintf(stderr, "ps_cuda_sim_create failed\n");
        return 1;
    }
    ps_cuda_sim_reset(sim, 1, nullptr);
    CUDA_CHECK(cudaDeviceSynchronize());

    for (int t = 0; t < 1000; t++) ps_cuda_sim_step_range(sim, 0, num_envs, nullptr);
    CUDA_CHECK(cudaDeviceSynchronize());

    cudaEvent_t start, stop;
    CUDA_CHECK(cudaEventCreate(&start));
    CUDA_CHECK(cudaEventCreate(&stop));
    CUDA_CHECK(cudaEventRecord(start));
    for (int t = 0; t < steps; t++) ps_cuda_sim_step_range(sim, 0, num_envs, nullptr);
    CUDA_CHECK(cudaEventRecord(stop));
    CUDA_CHECK(cudaEventSynchronize(stop));

    float milliseconds = 0.0f;
    CUDA_CHECK(cudaEventElapsedTime(&milliseconds, start, stop));
    double seconds = (double)milliseconds / 1000.0;
    double env_steps = (double)num_envs * (double)steps;
    float obs0 = 0.0f;
    float reward0 = 0.0f;
    CUDA_CHECK(cudaMemcpy(&obs0, observations, sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&reward0, rewards, sizeof(float), cudaMemcpyDeviceToHost));

    std::printf("num_envs %d\nsteps %d\nseconds %.6f\nenv_steps_per_sec %.2f\nobs0 %.6f\nreward0 %.6f\n",
        num_envs, steps, seconds, env_steps / seconds, obs0, reward0);

    ps_cuda_sim_destroy(sim);
    CUDA_CHECK(cudaEventDestroy(start));
    CUDA_CHECK(cudaEventDestroy(stop));
    CUDA_CHECK(cudaFree(observations));
    CUDA_CHECK(cudaFree(actions));
    CUDA_CHECK(cudaFree(rewards));
    CUDA_CHECK(cudaFree(terminals));
    return 0;
}
