#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define puf_reset tp_cpu_reset
#define puf_step tp_cpu_step
#define puf_close tp_cpu_close
#include "../trianglepath.h"
#undef puf_reset
#undef puf_step
#undef puf_close
#undef PUF_BACKEND
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
    if (offset == size) return;
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

void run_case(int height, int lo, int hi, int mode) {
    Dict kwargs = {0};
    dict_set(&kwargs, "height", height);
    dict_set(&kwargs, "cell_min", lo);
    dict_set(&kwargs, "cell_max", hi);
    dict_set(&kwargs, "seed", 42);
    dict_set(&kwargs, "reward_mode", mode);

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
        tp_cpu_reset(&cpu[e]);
    }

    obs_t* d_obs = nullptr;
    float* d_actions = nullptr;
    float* d_rewards = nullptr;
    float* d_terms = nullptr;
    CUDA_OK(cudaMalloc(&d_obs, (size_t)TEST_ENVS * OBS_SIZE));
    CUDA_OK(cudaMalloc(&d_actions, TEST_ENVS * sizeof(float)));
    CUDA_OK(cudaMalloc(&d_rewards, TEST_ENVS * sizeof(float)));
    CUDA_OK(cudaMalloc(&d_terms, TEST_ENVS * sizeof(float)));
    Env* gpu_envs = puf_vec_create(TEST_ENVS, &kwargs, d_obs, d_actions, d_rewards, d_terms);
    cudaStream_t stream;
    CUDA_OK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    puf_bind_stream(stream);
    puf_reset(gpu_envs);
    CUDA_OK(cudaDeviceSynchronize());

    obs_t* gpu_obs = (obs_t*)std::calloc(TEST_ENVS * OBS_SIZE, sizeof(obs_t));
    float* gpu_rewards = (float*)std::calloc(TEST_ENVS, sizeof(float));
    float* gpu_terms = (float*)std::calloc(TEST_ENVS, sizeof(float));
    Env* gpu_logs = (Env*)std::calloc(TEST_ENVS, sizeof(Env));

    uint32_t rng = 0x12345678u;
    for (int step = 0; step < TEST_STEPS; step++) {
        for (int e = 0; e < TEST_ENVS; e++) {
            cpu_actions[e] = (float)(test_rng(&rng) % 2u);
            tp_cpu_step(&cpu[e]);
        }
        CUDA_OK(cudaMemcpyAsync(d_actions, cpu_actions,
            TEST_ENVS * sizeof(float), cudaMemcpyHostToDevice, stream));
        puf_step(gpu_envs);
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
            assert(gpu_logs[e].num_agents == 1);
            fail_bytes("observation", e, step,
                cpu_obs + (size_t)e * OBS_SIZE,
                gpu_obs + (size_t)e * OBS_SIZE, OBS_SIZE);
            compare_float("reward", e, step, cpu_rewards[e], gpu_rewards[e]);
            compare_float("terminal", e, step, cpu_terms[e], gpu_terms[e]);
            const float* cl = (const float*)&cpu[e].log;
            const float* gl = (const float*)&gpu_logs[e].log;
            for (size_t f = 0; f < sizeof(Log) / sizeof(float); f++) {
                compare_float("log", e, step, cl[f], gl[f]);
            }
        }
    }

    std::printf("TrianglePath GPU adapter: PASS (%d envs x %d steps; "
        "cells/pos/reward/terminal exact)\n", TEST_ENVS, TEST_STEPS);
    puf_close(gpu_envs);
    CUDA_OK(cudaStreamDestroy(stream));
    CUDA_OK(cudaFree(d_actions));
    CUDA_OK(cudaFree(d_rewards));
    CUDA_OK(cudaFree(d_terms));
    CUDA_OK(cudaFree(d_obs));
    std::free(cpu);
    std::free(cpu_obs);
    std::free(cpu_actions);
    std::free(cpu_rewards);
    std::free(cpu_terms);
    std::free(gpu_obs);
    std::free(gpu_rewards);
    std::free(gpu_terms);
    std::free(gpu_logs);
    dict_clear(&kwargs);
}

int main(void) {
    for (int mode = 0; mode < 3; mode++) {
        run_case(2, 0, 0, mode);
        run_case(6, 7, 7, mode);
        run_case(16, 1, 9, mode);
        run_case(64, 0, 255, mode);
    }
    return 0;
}
