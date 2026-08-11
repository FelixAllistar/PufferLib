#ifndef PUFFER_TRIANGLEPATH_GPU_CU
#define PUFFER_TRIANGLEPATH_GPU_CU

#ifndef PUFFER_GPU_ENV
#error "trianglepath.cu requires build.sh trianglepath --gpu"
#endif

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "trianglepath.h"

#ifndef TP_CUDA_BLOCK
#define TP_CUDA_BLOCK 128
#endif

/* The Env struct comes from trianglepath.h (Log is the first member, so the
 * generic device log reducer works). Per-env state lives in the SoA sim. */

typedef struct {
    int height;
    int cell_min;
    int cell_max;
    uint32_t base_seed;
} TPCudaConfig;

static TPCudaConfig h_tp_config;
static __constant__ TPCudaConfig d_tp_config;

typedef struct {
    int count;
    obs_t* cells;
    int* row;
    int* col;
    int* total;
    int* done;
    uint32_t* rng;
    obs_t* obs;
    float* rewards;
    float* terminals;
} TPSim;

static TPSim g_sim;

static void tp_cuda_check(const char* what) {
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        std::fprintf(stderr, "TrianglePath CUDA %s: %s\n",
            what, cudaGetErrorString(err));
        std::exit(1);
    }
}

__device__ __forceinline__ uint32_t tp_gpu_random(uint32_t* rng) {
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

__global__ static void tp_reset_kernel(TPSim sim, uint32_t seed) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= sim.count) return;
    sim.row[i] = 0;
    sim.col[i] = 0;
    sim.total[i] = 0;
    sim.done[i] = 0;
    sim.rng[i] = seed ^ (0x9e3779b9u * (uint32_t)(i + 1u));
    int span = d_tp_config.cell_max - d_tp_config.cell_min + 1;
    obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    for (int row = 0; row < d_tp_config.height; row++) {
        for (int col = 0; col <= row; col++) {
            int cell = row * (row + 1) / 2 + col;
            cells[cell] = (obs_t)(d_tp_config.cell_min
                + (int)(tp_gpu_random(&sim.rng[i]) % (uint32_t)span));
        }
    }
}

__device__ static void tp_gpu_reset_env(TPSim sim, int i) {
    sim.row[i] = 0;
    sim.col[i] = 0;
    sim.total[i] = 0;
    sim.done[i] = 0;
    int span = d_tp_config.cell_max - d_tp_config.cell_min + 1;
    obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    for (int row = 0; row < d_tp_config.height; row++) {
        for (int col = 0; col <= row; col++) {
            int cell = row * (row + 1) / 2 + col;
            cells[cell] = (obs_t)(d_tp_config.cell_min
                + (int)(tp_gpu_random(&sim.rng[i]) % (uint32_t)span));
        }
    }
}

__global__ static void tp_observe_kernel(TPSim sim) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= sim.count) return;
    obs_t* obs = sim.obs + (size_t)i * OBS_SIZE;
    const obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    for (int k = 0; k < OBS_SIZE; k++) obs[k] = 0;
    for (int row = 0; row < d_tp_config.height; row++) {
        for (int col = 0; col <= row; col++) {
            obs[row * (row + 1) / 2 + col] =
                cells[row * (row + 1) / 2 + col];
        }
    }
    obs[TP_MAX_CELLS] = (obs_t)sim.row[i];
    obs[TP_MAX_CELLS + 1] = (obs_t)sim.col[i];
}

__global__ static void tp_step_kernel(TPSim sim, Env* envs,
        const float* actions) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= sim.count) return;
    const obs_t* cells = sim.cells + (size_t)i * TP_MAX_CELLS;
    if (!sim.done[i]) {
        int cell = sim.row[i] * (sim.row[i] + 1) / 2 + sim.col[i];
        int prev = sim.total[i];
        sim.total[i] += cells[cell];
        sim.rewards[i] = (float)(sim.total[i] - prev);
        int action = (int)actions[i];
        if (action == TP_RIGHT && sim.col[i] < sim.row[i]) sim.col[i]++;
        sim.row[i]++;
        if (sim.row[i] >= d_tp_config.height - 1) {
            int last = sim.row[i] * (sim.row[i] + 1) / 2 + sim.col[i];
            prev = sim.total[i];
            sim.total[i] += cells[last];
            sim.rewards[i] += (float)(sim.total[i] - prev);
            sim.done[i] = 1;
            sim.terminals[i] = 1.0f;
            int optimal = tp_solve_value_cells(cells, d_tp_config.height);
            envs[i].log.score += (float)sim.total[i];
            envs[i].log.optimal += (float)optimal;
            envs[i].log.regret += (float)(optimal - sim.total[i]);
            envs[i].log.perf += optimal > 0
                ? (float)sim.total[i] / (float)optimal : 0.0f;
            envs[i].log.episode_length += (float)(d_tp_config.height - 1);
            envs[i].log.n += 1.0f;
            /* Auto-reset after the terminal transition, preserving the
             * terminal flag in the external buffer (CPU puf_step parity). */
            tp_gpu_reset_env(sim, i);
        } else {
            sim.terminals[i] = 0.0f;
        }
    } else {
        sim.rewards[i] = 0.0f;
        sim.terminals[i] = 0.0f;
    }
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    (void)envs;
    g_sim.obs = observations;
    g_sim.rewards = rewards;
    g_sim.terminals = terminals;
    int grid = (g_sim.count + TP_CUDA_BLOCK - 1) / TP_CUDA_BLOCK;
    tp_reset_kernel<<<grid, TP_CUDA_BLOCK>>>(g_sim, h_tp_config.base_seed);
    tp_observe_kernel<<<grid, TP_CUDA_BLOCK>>>(g_sim);
    tp_cuda_check("reset");
}

static void puf_envs_step(Env* envs, const float* actions,
        obs_t* observations, float* rewards, float* terminals,
        int start, int count, cudaStream_t stream) {
    (void)envs; (void)observations;
    if (start != 0 || count != g_sim.count) {
        std::fprintf(stderr, "TrianglePath GPU requires full-batch stepping\n");
        std::exit(1);
    }
    int grid = (g_sim.count + TP_CUDA_BLOCK - 1) / TP_CUDA_BLOCK;
    tp_step_kernel<<<grid, TP_CUDA_BLOCK, 0, stream>>>(g_sim, envs, actions);
    tp_observe_kernel<<<grid, TP_CUDA_BLOCK, 0, stream>>>(g_sim);
    tp_cuda_check("step");
}

static Env* puf_envs_create(int total_agents, Dict* env_kwargs) {
    g_sim.count = total_agents;
    h_tp_config.height = (int)dict_get(env_kwargs, "height");
    h_tp_config.cell_min = (int)dict_get(env_kwargs, "cell_min");
    h_tp_config.cell_max = (int)dict_get(env_kwargs, "cell_max");
    h_tp_config.base_seed = (uint32_t)dict_get(env_kwargs, "seed");
    cudaMemcpyToSymbol(d_tp_config, &h_tp_config, sizeof(h_tp_config));
    cudaMalloc(&g_sim.cells,
        (size_t)total_agents * TP_MAX_CELLS * sizeof(obs_t));
    cudaMalloc(&g_sim.row, (size_t)total_agents * sizeof(int));
    cudaMalloc(&g_sim.col, (size_t)total_agents * sizeof(int));
    cudaMalloc(&g_sim.total, (size_t)total_agents * sizeof(int));
    cudaMalloc(&g_sim.done, (size_t)total_agents * sizeof(int));
    cudaMalloc(&g_sim.rng, (size_t)total_agents * sizeof(uint32_t));
    tp_cuda_check("create");
    Env* envs = nullptr;
    cudaMalloc(&envs, (size_t)total_agents * sizeof(Env));
    cudaMemset(envs, 0, (size_t)total_agents * sizeof(Env));
    return envs;
}

static void puf_envs_close(Env* envs) {
    cudaFree(g_sim.cells);
    cudaFree(g_sim.row);
    cudaFree(g_sim.col);
    cudaFree(g_sim.total);
    cudaFree(g_sim.done);
    cudaFree(g_sim.rng);
    cudaFree(envs);
    memset(&g_sim, 0, sizeof(g_sim));
}

static void puf_envs_selfplay_counts(int* out, int num_banks) {
    for (int i = 0; i < num_banks; i++) out[i] = 0;
}
static void puf_envs_selfplay_clear(int bank) { (void)bank; }
static void puf_envs_bind_buffers(float* actions, unsigned char* masks) {
    (void)actions; (void)masks;
}

#define PUF_GPU_ENV_BIND_BUFFERS 1
#define PUF_GPU_SELFPLAY 1

#endif
