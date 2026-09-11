#define PUFFER_GPU_ENV 1
#include "bomberman.h"
#include "bomberman.cu"
#include <cassert>

#define CUDA_OK(call) do { \
    cudaError_t err = (call); \
    if (err != cudaSuccess) { \
        fprintf(stderr, "%s: %s\n", #call, cudaGetErrorString(err)); \
        exit(1); \
    } \
} while (0)

static void run_suite(int agents, int max_ticks) {
    Ini ini = {};
    puf_ini_load_env(&ini, "bomberman", 0, nullptr);
    Dict* kwargs = puf_ini_section(&ini, "env", 0);
    dict_set(kwargs, "reverse_curriculum", 0);
    dict_set(kwargs, "num_agents", agents);
    dict_set(kwargs, "max_ticks", max_ticks);
    const int matches = 4;
    int rows = matches * agents;
    Env* shells = puf_envs_create(rows, kwargs);
    float *obs, *actions, *rewards, *terminals;
    CUDA_OK(cudaMalloc(&obs, rows * OBS_SIZE * sizeof(float)));
    CUDA_OK(cudaMalloc(&actions, rows * sizeof(float)));
    CUDA_OK(cudaMalloc(&rewards, rows * sizeof(float)));
    CUDA_OK(cudaMalloc(&terminals, rows * sizeof(float)));
    puf_envs_reset(shells, obs, rewards, terminals, rows);
    BMMatch cpu[matches], gpu[matches];
    Log logs[matches] = {};
    for (int mid = 0; mid < matches; mid++)
        bm_reset_match(&cpu[mid], &h_bcfg, (uint32_t)(mid + 1) * 0x9E3779B9u);
    uint32_t rng = 123;
    for (int step = 0; step < 256; step++) {
        float host_actions[matches * BM_MAX_AGENTS];
        float expected_r[matches * BM_MAX_AGENTS], expected_t[matches * BM_MAX_AGENTS];
        for (int mid = 0; mid < matches; mid++) {
            int acts[BM_MAX_AGENTS];
            for (int a = 0; a < agents; a++)
                host_actions[mid * agents + a] = acts[a] = bm_randi(&rng, BM_NUM_ACTIONS);
            bm_step_match(&cpu[mid], &h_bcfg, acts,
                expected_r + mid * agents, expected_t + mid * agents);
            if (cpu[mid].done) {
                int outcome = cpu[mid].winner == 0 ? 1 : cpu[mid].winner > 0 ? -1 : 0;
                bm_log_match(&logs[mid], &cpu[mid], outcome);
                uint32_t seed = cpu[mid].rng ^ (0x85ebca6bu * (uint32_t)(cpu[mid].tick + 1));
                bm_reset_match(&cpu[mid], &h_bcfg, seed);
            }
        }
        CUDA_OK(cudaMemcpy(actions, host_actions, rows * sizeof(float), cudaMemcpyHostToDevice));
        puf_envs_step(shells, actions, obs, rewards, terminals, 0, rows, 0);
        CUDA_OK(cudaDeviceSynchronize());
        CUDA_OK(cudaMemcpy(gpu, d_matches, sizeof(gpu), cudaMemcpyDeviceToHost));
        assert(memcmp(cpu, gpu, sizeof(cpu)) == 0);
        float actual_r[matches * BM_MAX_AGENTS], actual_t[matches * BM_MAX_AGENTS];
        CUDA_OK(cudaMemcpy(actual_r, rewards, rows * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(actual_t, terminals, rows * sizeof(float), cudaMemcpyDeviceToHost));
        for (int row = 0; row < rows; row++) {
            assert(fabsf(actual_r[row] - expected_r[row]) < 1e-6f);
            assert(actual_t[row] == expected_t[row]);
            float expected_obs[OBS_SIZE], actual_obs[OBS_SIZE];
            bm_write_obs(&cpu[row / agents], &h_bcfg, row % agents, expected_obs);
            CUDA_OK(cudaMemcpy(actual_obs, obs + row * OBS_SIZE, sizeof(actual_obs), cudaMemcpyDeviceToHost));
            for (int i = 0; i < OBS_SIZE; i++) assert(fabsf(actual_obs[i] - expected_obs[i]) < 1e-6f);
        }
        for (int mid = 0; mid < matches; mid++) {
            Log actual;
            CUDA_OK(cudaMemcpy(&actual, &shells[mid * agents].log, sizeof(actual), cudaMemcpyDeviceToHost));
            for (int i = 0; i < (int)(sizeof(Log) / sizeof(float)); i++)
                assert(fabsf(((float*)&actual)[i] - ((float*)&logs[mid])[i]) < 1e-4f);
            assert(actual.curriculum_stage == BM_CURRICULUM_STAGES * actual.n);
            assert(actual.curriculum_full_game == actual.n);
        }
    }
    puf_envs_close(shells);
    CUDA_OK(cudaFree(obs));
    CUDA_OK(cudaFree(actions));
    CUDA_OK(cudaFree(rewards));
    CUDA_OK(cudaFree(terminals));
    puf_ini_free(&ini);
    printf("Bomberman CUDA parity: agents=%d max_ticks=%d PASS\n", agents, max_ticks);
}

int main(void) {
    run_suite(2, 1);
    run_suite(2, 128);
    run_suite(4, 128);
}
