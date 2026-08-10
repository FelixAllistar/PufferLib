// GPU-resident Goofspiel environment. The state machine, observation writer,
// and action masks run on device; this mirrors the puffer_survivors CUDA
// adapter contract (puf_envs_*) so the generic trainer can drive it.
#ifndef PUFFER_GOOFSPIEL_GPU_ENV_CU
#define PUFFER_GOOFSPIEL_GPU_ENV_CU

#ifndef PUFFER_GPU_ENV
#error "goofspiel.cu requires -DPUFFER_GPU_ENV (build with --gpu)"
#endif

#define PUF_GPU_ENV_BANK_LAYOUT 1
#define PUF_GPU_SELFPLAY 1

#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "goofspiel.h"

#ifndef GS_CUDA_BLOCK
#define GS_CUDA_BLOCK 128
#endif

typedef struct {
    GSConfig game;
    uint64_t base_seed;
    int exact_enabled;
} GSCudaConfig;

static GSCudaConfig h_gs_cuda_config;
static __constant__ GSCudaConfig d_gs_cuda_config;
static Env* d_gs_matches = nullptr;
static int* d_gs_rows = nullptr;
static int g_gs_total_agents = 0;
static int g_gs_num_matches = 0;
static int g_gs_bank_count = 0;
static int g_gs_bound = 0;
static int* d_gs_bank_completed = nullptr;
static float* g_gs_actions = nullptr;
static unsigned char* g_gs_masks = nullptr;
static obs_t* g_gs_observations = nullptr;
static float* g_gs_rewards = nullptr;
static float* g_gs_terminals = nullptr;

static void gs_cuda_check(const char* what) {
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        std::fprintf(stderr, "Goofspiel CUDA %s failed: %s\n",
            what, cudaGetErrorString(err));
        std::exit(1);
    }
}

static inline int gs_cuda_grid(int n) {
    return (n + GS_CUDA_BLOCK - 1) / GS_CUDA_BLOCK;
}

/* Mirror the CPU log accumulation exactly (player-0 perspective). */
__device__ static void gs_cuda_log_game(Env* env, const float* returns) {
    int max_score = -1;
    int winners = 0;
    for (int p = 0; p < env->num_agents; p++) {
        int score = env->state.scores[p];
        if (score > max_score) {
            max_score = score;
            winners = 1;
        } else if (score == max_score) {
            winners++;
        }
    }

    float draw = winners == env->num_agents;
    float slot_0 = draw ? 0.5f
        : env->state.scores[0] == max_score ? 1.0f : 0.0f;
    float slot_1 = env->num_agents > 1
        ? (draw ? 0.5f : env->state.scores[1] == max_score ? 1.0f : 0.0f)
        : 0.0f;
    int decisions = env->cfg.num_turns;
    if (env->cfg.auto_forced_last
            && env->cfg.num_turns == env->cfg.num_cards) {
        decisions--;
    }

    env->log.perf += slot_0;
    env->log.score += env->state.scores[0];
    env->log.episode_return += returns[0];
    env->log.episode_length += decisions;
    env->log.slot_0_points += env->state.scores[0];
    env->log.slot_0_score += slot_0;
    env->log.slot_1_score += slot_1;
    env->log.draw_rate += draw;
    env->log.n += 1.0f;
}

__device__ static void gs_cuda_reset_state(Env* env) {
    gs_reset(&env->state, &env->history, &env->cfg, &env->rng);
    gs_write_masks(env);
    gs_observe(env);
}

__device__ static void gs_cuda_transition(Env* env, Env* shells,
        const int rows[GS_MAX_PLAYERS], int* bank_completed) {
    uint8_t bids[GS_MAX_PLAYERS];
    for (int p = 0; p < env->num_agents; p++) {
        bids[p] = (uint8_t)env->agents[p].actions[0];
        env->agents[p].rewards[0] = 0.0f;
        env->agents[p].terminals[0] = 0.0f;
    }
    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].action_mask[bids[p]] = 0;
    }

    if (!gs_step(&env->state, &env->history, &env->cfg, bids)) {
        gs_observe(env);
        return;
    }

    float returns[GS_MAX_PLAYERS];
    gs_returns(&env->state, &env->cfg, returns);
    float scale = env->cfg.return_type == GS_RETURN_WIN_LOSS
        ? 1.0f : (float)env->cfg.total_points;
    for (int p = 0; p < env->num_agents; p++) {
        returns[p] /= scale;
        env->agents[p].rewards[0] = returns[p];
        env->agents[p].terminals[0] = 1.0f;
    }

    gs_cuda_log_game(env, returns);
    if (env->tag > 0) {
        env->boundary_reached = 1;
        atomicAdd(&bank_completed[env->tag], 1);
    }

    /* Fold into the player-0 shell row so the generic log reducer sees one
     * completed game per match. */
    float* dst = (float*)&shells[rows[0]].log;
    float* src = (float*)&env->log;
    constexpr int fields = (int)(sizeof(Log) / sizeof(float));
    for (int field = 0; field < fields; field++) dst[field] += src[field];
    memset(&env->log, 0, sizeof(env->log));
    shells[rows[0]].tag = env->tag;

    gs_cuda_reset_state(env);
}

__global__ static void gs_cuda_reset_kernel(Env* shells, Env* matches,
        const int* rows, obs_t* observations, float* actions,
        float* rewards, float* terminals, unsigned char* masks,
        int num_matches) {
    int match_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (match_id >= num_matches) return;
    Env* env = &matches[match_id];
    memset(env, 0, sizeof(*env));
    env->cfg = d_gs_cuda_config.game;
    env->num_agents = GS_MAX_PLAYERS;
    env->tag = 0;
    env->boundary_reached = 0;
    /* Match the CPU puf_init stream derivation: every env shares the seed,
     * differentiated by the env index in the RNG mix. */
    env->rng = gs_mix32((uint32_t)d_gs_cuda_config.base_seed
        ^ (0x9e3779b9u * (uint32_t)(match_id + 1u)));

    int match_rows[2] = {rows[2 * match_id], rows[2 * match_id + 1]};
    for (int player = 0; player < GS_MAX_PLAYERS; player++) {
        int row = match_rows[player];
        env->agents[player].observations = observations + (size_t)row * OBS_SIZE;
        env->agents[player].actions = actions + (size_t)row * NUM_ATNS;
        env->agents[player].rewards = rewards + row;
        env->agents[player].terminals = terminals + row;
        env->agents[player].action_mask = masks + (size_t)row * GS_NUM_CARDS;
        env->agents[player].policy = shells[row].agents[0].policy;
    }
    env->tag = env->agents[0].policy > env->agents[1].policy
        ? env->agents[0].policy : env->agents[1].policy;
    gs_cuda_reset_state(env);
    for (int player = 0; player < GS_MAX_PLAYERS; player++) {
        env->agents[player].rewards[0] = 0.0f;
        env->agents[player].terminals[0] = 0.0f;
    }
}

__global__ static void gs_cuda_step_kernel(Env* shells, Env* matches,
        const int* rows, int num_matches, int* bank_completed) {
    int match_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (match_id >= num_matches) return;
    int match_rows[2] = {rows[2 * match_id], rows[2 * match_id + 1]};
    gs_cuda_transition(&matches[match_id], shells, match_rows,
        bank_completed);
}

__global__ static void gs_cuda_clear_shells_kernel(Env* shells,
        int total_agents) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= total_agents) return;
    memset(&shells[row].log, 0, sizeof(shells[row].log));
    shells[row].boundary_reached = 0;
    shells[row].tag = 0;
}

static void gs_cuda_load_config(Dict* kwargs) {
    Env template_env = {};
    template_env.rng = 0;
    puf_init(&template_env, kwargs);
    h_gs_cuda_config = {};
    h_gs_cuda_config.game = template_env.cfg;
    h_gs_cuda_config.base_seed = (uint64_t)dict_get(kwargs, "seed");
    h_gs_cuda_config.exact_enabled =
        (int)dict_get(kwargs, "exact_exploiter");
    cudaMemcpyToSymbol(d_gs_cuda_config, &h_gs_cuda_config,
        sizeof(h_gs_cuda_config));
    gs_cuda_check("config upload");
}

static Env* puf_envs_create(int total_agents, Dict* env_kwargs,
        Dict* vec_kwargs, int* bank_layout) {
    if (total_agents < 2 || total_agents % 2) {
        std::fprintf(stderr,
            "Goofspiel GPU requires an even vec.total_agents >= 2\n");
        std::exit(1);
    }
    g_gs_total_agents = total_agents;
    g_gs_num_matches = total_agents / 2;
    gs_cuda_load_config(env_kwargs);

    int frozen_banks = (int)dict_get(vec_kwargs, "num_frozen_banks");
    g_gs_bank_count = frozen_banks;
    float frozen_pct = (float)dict_get(vec_kwargs, "frozen_bank_pct");
    int frozen_matches = frozen_banks > 0
        ? (int)(frozen_pct * g_gs_num_matches) : 0;
    int frozen_start = g_gs_num_matches - frozen_matches;
    int seat_balance = (int)dict_get(vec_kwargs, "seat_balance");
    int* cursors = (int*)std::calloc((size_t)frozen_banks + 1, sizeof(int));
    int* host_rows = (int*)std::calloc((size_t)total_agents, sizeof(int));
    Env* host_shells = (Env*)std::calloc((size_t)total_agents, sizeof(Env));
    if (!cursors || !host_rows || !host_shells) std::abort();
    for (int bank = 0; bank <= frozen_banks; bank++) {
        cursors[bank] = bank_layout[bank];
    }
    for (int match = 0; match < g_gs_num_matches; match++) {
        int policy[2] = {0, 0};
        if (match >= frozen_start) {
            policy[1] = 1 + (match - frozen_start) % frozen_banks;
        }
        if (seat_balance && frozen_banks > 0
                && ((match / frozen_banks) & 1)) {
            int swap = policy[0];
            policy[0] = policy[1];
            policy[1] = swap;
        }
        for (int player = 0; player < GS_MAX_PLAYERS; player++) {
            int row = cursors[policy[player]]++;
            host_rows[2 * match + player] = row;
            host_shells[row].agents[0].policy = policy[player];
        }
    }
    for (int bank = 0; bank <= frozen_banks; bank++) {
        if (cursors[bank] != bank_layout[bank + 1]) {
            std::fprintf(stderr,
                "Goofspiel GPU bank mapping mismatch at %d\n", bank);
            std::exit(1);
        }
    }

    Env* shells = nullptr;
    cudaMalloc((void**)&shells, (size_t)total_agents * sizeof(Env));
    cudaMemcpy(shells, host_shells, (size_t)total_agents * sizeof(Env),
        cudaMemcpyHostToDevice);
    cudaMalloc((void**)&d_gs_matches,
        (size_t)g_gs_num_matches * sizeof(Env));
    cudaMemset(d_gs_matches, 0,
        (size_t)g_gs_num_matches * sizeof(Env));
    cudaMalloc((void**)&d_gs_rows, (size_t)total_agents * sizeof(int));
    cudaMemcpy(d_gs_rows, host_rows, (size_t)total_agents * sizeof(int),
        cudaMemcpyHostToDevice);
    cudaMalloc((void**)&d_gs_bank_completed,
        (size_t)(g_gs_bank_count + 1) * sizeof(int));
    cudaMemset(d_gs_bank_completed, 0,
        (size_t)(g_gs_bank_count + 1) * sizeof(int));
    gs_cuda_check("device environment allocation");
    std::free(host_shells);
    std::free(host_rows);
    std::free(cursors);
    return shells;
}

static void puf_envs_reset(Env* envs, obs_t* observations, float* rewards,
        float* terminals, int total_agents) {
    if (total_agents != g_gs_total_agents) std::abort();
    if (d_gs_bank_completed) {
        cudaMemset(d_gs_bank_completed, 0,
            (size_t)(g_gs_bank_count + 1) * sizeof(int));
    }
    cudaMemset(rewards, 0, (size_t)total_agents * sizeof(float));
    cudaMemset(terminals, 0, (size_t)total_agents * sizeof(float));
    if (!g_gs_actions || !g_gs_masks) {
        std::fprintf(stderr, "Goofspiel GPU buffers were not bound\n");
        std::exit(1);
    }
    g_gs_observations = observations;
    g_gs_rewards = rewards;
    g_gs_terminals = terminals;
    gs_cuda_clear_shells_kernel<<<gs_cuda_grid(total_agents),
        GS_CUDA_BLOCK>>>(envs, total_agents);
    gs_cuda_reset_kernel<<<gs_cuda_grid(g_gs_num_matches),
        GS_CUDA_BLOCK>>>(envs, d_gs_matches, d_gs_rows,
            observations, g_gs_actions, rewards, terminals,
            g_gs_masks, g_gs_num_matches);
    g_gs_bound = 1;
}

static void puf_envs_step(Env* envs, const float* actions,
        obs_t* observations, float* rewards, float* terminals,
        int start, int count, cudaStream_t stream) {
    if (start != 0 || count != g_gs_total_agents) {
        std::fprintf(stderr, "Goofspiel GPU requires full-batch stepping\n");
        std::exit(1);
    }
    if (!g_gs_bound || actions != g_gs_actions
            || observations != g_gs_observations
            || rewards != g_gs_rewards || terminals != g_gs_terminals) {
        std::fprintf(stderr, "Goofspiel GPU buffer binding changed\n");
        std::exit(1);
    }
    gs_cuda_step_kernel<<<gs_cuda_grid(g_gs_num_matches),
        GS_CUDA_BLOCK, 0, stream>>>(envs, d_gs_matches, d_gs_rows,
            g_gs_num_matches, d_gs_bank_completed);
}

static void puf_envs_bind_buffers(float* actions, unsigned char* masks) {
    g_gs_actions = actions;
    g_gs_masks = masks;
}

/* Per-bank completed-episode counters feed host-side selfplay rotation. */
static void puf_envs_selfplay_counts(int* out, int num_banks) {
    if (num_banks > 0 && d_gs_bank_completed) {
        cudaMemcpy(out, d_gs_bank_completed + 1,
            (size_t)num_banks * sizeof(int), cudaMemcpyDeviceToHost);
    }
}

static void puf_envs_selfplay_clear(int bank) {
    if (bank > 0 && d_gs_bank_completed) {
        cudaMemset(d_gs_bank_completed + bank, 0, sizeof(int));
    }
}

static void puf_envs_close(Env* envs) {
    if (d_gs_rows) cudaFree(d_gs_rows);
    if (d_gs_matches) cudaFree(d_gs_matches);
    if (d_gs_bank_completed) cudaFree(d_gs_bank_completed);
    cudaFree(envs);
    d_gs_rows = nullptr;
    d_gs_matches = nullptr;
    d_gs_bank_completed = nullptr;
    g_gs_bound = 0;
    g_gs_total_agents = 0;
    g_gs_num_matches = 0;
    g_gs_bank_count = 0;
}

#define PUF_GPU_ENV_BIND_BUFFERS 1

#endif
