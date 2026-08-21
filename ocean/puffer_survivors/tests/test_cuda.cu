#include <cuda_runtime.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../puffer_survivors.h"
#include "../puffer_survivors.cu"

#define CUDA_CHECK(call) do { \
    cudaError_t err__ = (call); \
    if (err__ != cudaSuccess) { \
        std::fprintf(stderr, "CUDA error %s:%d: %s\n", \
            __FILE__, __LINE__, cudaGetErrorString(err__)); \
        std::exit(1); \
    } \
} while (0)

int main() {
    Ini ini = {};
    puf_ini_load_env(&ini, "puffer_survivors", 0, nullptr);
    Dict* cfg = puf_ini_section(&ini, "env", 0);
    dict_set(cfg, "max_steps", 2);
    dict_set(cfg, "player_health", 1000000.0);
    dict_set(cfg, "reward_xp", 0.0);
    dict_set(cfg, "reward_kill", 0.0);
    dict_set(cfg, "reward_damage", 0.0);
    dict_set(cfg, "reward_survival", 0.125);
    dict_set(cfg, "reward_hurt", 0.0);
    dict_set(cfg, "reward_death", -0.5);
    dict_set(cfg, "reward_success", 0.75);
    dict_set(cfg, "reward_pickup", 0.0);
    dict_set(cfg, "reward_levelup", 0.0);
    dict_set(cfg, "obstacle_penalty", 0.0);

    Env* envs = puf_envs_create(1, cfg);
    PSCudaSim* sim = ps_cuda_get_sim(envs);
    sim->cfg.obstacle_count = 0;
    obs_t* observations = nullptr;
    float* actions = nullptr;
    float* rewards = nullptr;
    float* terminals = nullptr;
    CUDA_CHECK(cudaMalloc((void**)&observations, sizeof(obs_t) * OBS_SIZE));
    CUDA_CHECK(cudaMalloc((void**)&actions, sizeof(float) * NUM_ATNS));
    CUDA_CHECK(cudaMalloc((void**)&rewards, sizeof(float)));
    CUDA_CHECK(cudaMalloc((void**)&terminals, sizeof(float)));
    float dash_actions[NUM_ATNS] = {(float)PS_ACTION_DASH, 0.0f};
    CUDA_CHECK(cudaMemcpy(actions, dash_actions, sizeof(dash_actions), cudaMemcpyHostToDevice));

    puf_envs_reset(envs, observations, rewards, terminals, 1);
    puf_envs_step(envs, actions, observations, rewards, terminals, 0, 1, 0);
    CUDA_CHECK(cudaDeviceSynchronize());
    float dash_px = 0.0f;
    int dash_timer = 0;
    CUDA_CHECK(cudaMemcpy(&dash_px, sim->px, sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&dash_timer, sim->dash_timer, sizeof(int), cudaMemcpyDeviceToHost));
    assert(dash_px > 0.0f);
    assert(dash_timer == sim->cfg.dash_duration - 1);

    float reward = 0.0f;
    float terminal = 0.0f;
    Log log = {};
    CUDA_CHECK(cudaMemcpy(&reward, rewards, sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&terminal, terminals, sizeof(float), cudaMemcpyDeviceToHost));
    assert(terminal == 0.0f);
    assert(std::fabs(reward - 0.125f) < 1e-5f);

    puf_envs_step(envs, actions, observations, rewards, terminals, 0, 1, 0);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(&reward, rewards, sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&terminal, terminals, sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&log, &envs[0].log, sizeof(Log), cudaMemcpyDeviceToHost));
    assert(terminal == 1.0f);
    assert(std::fabs(reward - 0.875f) < 1e-5f);
    assert(log.n == 1.0f);
    assert(log.perf == 1.0f);
    assert(log.survived == 1.0f);
    assert(log.episode_length == 2.0f);
    assert(std::fabs(log.reward_survival - 0.25f) < 1e-5f);
    assert(std::fabs(log.reward_terminal - 0.75f) < 1e-5f);

    puf_envs_close(envs);
    CUDA_CHECK(cudaFree(observations));
    CUDA_CHECK(cudaFree(actions));
    CUDA_CHECK(cudaFree(rewards));
    CUDA_CHECK(cudaFree(terminals));
    puf_ini_free(&ini);
    std::puts("native CUDA reward parity smoke ok");
    return 0;
}
