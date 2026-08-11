#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../trianglepath.h"

#define PUFFER_GPU_ENV 1
#include "../trianglepath.cu"

#define CUDA_OK(call) do { \
    cudaError_t err__ = (call); \
    if (err__ != cudaSuccess) { \
        std::fprintf(stderr, "%s:%d: CUDA error: %s\n", \
            __FILE__, __LINE__, cudaGetErrorString(err__)); \
        std::exit(1); \
    } \
} while (0)

enum {
    TEST_ENVS = 16,
    TEST_STEPS = 256,
};

static uint32_t test_rng(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void fail_bytes(const char* field, int env, int step,
        const void* expected, const void* actual, size_t size) {
    const unsigned char* a = (const unsigned char*)expected;
    const unsigned char* b = (const unsigned char*)actual;
    size_t offset = 0;
    while (offset < size && a[offset] == b[offset]) offset++;
    std::fprintf(stderr,
        "GPU adapter mismatch field=%s env=%d step=%d offset=%zu/%zu "
        "cpu=%u gpu=%u\n", field, env, step, offset, size,
        offset < size ? (unsigned)a[offset] : 0,
        offset < size ? (unsigned)b[offset] : 0);
    std::exit(1);
}

static void compare_float(const char* field, int env, int step,
        float cpu, float gpu) {
    if (!std::isfinite(cpu) || !std::isfinite(gpu)
            || std::fabs(cpu - gpu) > 1e-6f) {
        std::fprintf(stderr,
            "GPU adapter mismatch field=%s env=%d step=%d cpu=%g gpu=%g\n",
            field, env, step, cpu, gpu);
        std::exit(1);
    }
}

int main(void) {
    Dict kwargs = {0};
    dict_set(&kwargs, "height", 16);
    dict_set(&kwargs, "cell_min", 1);
    dict_set(&kwargs, "cell_max", 9);
    dict_set(&kwargs, "seed", 42);

    Env* cpu = (Env*)std::calloc(TEST_ENVS, sizeof(Env));
    obs_t* cpu_obs = (obs_t*)std::calloc(TEST_ENVS * OBS_SIZE, sizeof(obs_t));
    float* cpu_actions = (float*)std::calloc(TEST_ENVS, sizeof(float));
    float* cpu_rewards = (float*)std::calloc(TEST_ENVS, sizeof(float));
    float* cpu_terms = (float*)std::calloc(TEST_ENVS, sizeof(float));
    for (int e = 0; e < TEST_ENVS; e++) {
        cpu[e].rng = (unsigned)e;
        puf_init(&cpu[e], &kwargs);
        cpu[e].agents[0].observations = cpu_obs + (size_t)e * OBS_SIZE;
        cpu[e].agents[0].actions = cpu_actions + e;
        cpu[e].agents[0].rewards = cpu_rewards + e;
        cpu[e].agents[0].terminals = cpu_terms + e;
        puf_reset(&cpu[e]);
    }

    Env* gpu_envs = puf_envs_create(TEST_ENVS, &kwargs);
    obs_t* d_obs = nullptr;
    float* d_actions = nullptr;
    float* d_rewards = nullptr;
    float* d_terms = nullptr;
    CUDA_OK(cudaMalloc(&d_obs, (size_t)TEST_ENVS * OBS_SIZE));
    CUDA_OK(cudaMalloc(&d_actions, TEST_ENVS * sizeof(float)));
    CUDA_OK(cudaMalloc(&d_rewards, TEST_ENVS * sizeof(float)));
    CUDA_OK(cudaMalloc(&d_terms, TEST_ENVS * sizeof(float)));
    puf_envs_reset(gpu_envs, d_obs, d_rewards, d_terms, TEST_ENVS);
    CUDA_OK(cudaDeviceSynchronize());

    obs_t* gpu_obs = (obs_t*)std::calloc(TEST_ENVS * OBS_SIZE, sizeof(obs_t));
    float* gpu_rewards = (float*)std::calloc(TEST_ENVS, sizeof(float));
    float* gpu_terms = (float*)std::calloc(TEST_ENVS, sizeof(float));
    Log* gpu_logs = (Log*)std::calloc(TEST_ENVS, sizeof(Log));

    uint32_t rng = 0x12345678u;
    for (int step = 0; step < TEST_STEPS; step++) {
        for (int e = 0; e < TEST_ENVS; e++) {
            cpu_actions[e] = (float)(test_rng(&rng) % 2u);
            puf_step(&cpu[e]);
        }
        CUDA_OK(cudaMemcpy(d_actions, cpu_actions,
            TEST_ENVS * sizeof(float), cudaMemcpyHostToDevice));
        puf_envs_step(gpu_envs, d_actions, d_obs, d_rewards, d_terms,
            0, TEST_ENVS, 0);
        CUDA_OK(cudaDeviceSynchronize());
        CUDA_OK(cudaMemcpy(gpu_obs, d_obs, TEST_ENVS * OBS_SIZE,
            cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_rewards, d_rewards,
            TEST_ENVS * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_terms, d_terms,
            TEST_ENVS * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_logs, gpu_envs,
            TEST_ENVS * sizeof(Env), cudaMemcpyDeviceToHost));

        for (int e = 0; e < TEST_ENVS; e++) {
            fail_bytes("state.cells", e, step, cpu[e].state.cells,
                gpu_obs + (size_t)e * OBS_SIZE, TP_MAX_CELLS);
            if (cpu[e].state.row != (int)gpu_obs[(size_t)e * OBS_SIZE + TP_MAX_CELLS]
                    || cpu[e].state.col != (int)gpu_obs[(size_t)e * OBS_SIZE + TP_MAX_CELLS + 1]) {
                std::fprintf(stderr,
                    "GPU adapter mismatch field=pos env=%d step=%d "
                    "cpu=(%d,%d) gpu=(%d,%d)\n", e, step,
                    cpu[e].state.row, cpu[e].state.col,
                    (int)gpu_obs[(size_t)e * OBS_SIZE + TP_MAX_CELLS],
                    (int)gpu_obs[(size_t)e * OBS_SIZE + TP_MAX_CELLS + 1]);
                std::exit(1);
            }
            compare_float("reward", e, step, cpu_rewards[e], gpu_rewards[e]);
            compare_float("terminal", e, step, cpu_terms[e], gpu_terms[e]);
            const float* cl = (const float*)&cpu[e].log;
            const float* gl = (const float*)&gpu_logs[e];
            for (size_t f = 0; f < sizeof(Log) / sizeof(float); f++) {
                compare_float("log", e, step, cl[f], gl[f]);
            }
        }
    }

    std::printf("TrianglePath GPU adapter: PASS (%d envs x %d steps; "
        "cells/pos/reward/terminal exact)\n", TEST_ENVS, TEST_STEPS);
    return 0;
}
