#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../goofspiel.h"

#define PUFFER_GPU_ENV 1
#include "../goofspiel.cu"

#define CUDA_OK(call) do { \
    cudaError_t err__ = (call); \
    if (err__ != cudaSuccess) { \
        std::fprintf(stderr, "%s:%d: CUDA error: %s\n", \
            __FILE__, __LINE__, cudaGetErrorString(err__)); \
        std::exit(1); \
    } \
} while (0)

enum {
    SUITE_MATCHES = 10,
    SUITE_STEPS = 1600,
    SUITE_ROWS = 2 * SUITE_MATCHES,
};

static uint32_t adapter_rng(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void fail_bytes(const char* field, const char* suite, int match, int step,
        const void* expected, const void* actual, size_t size) {
    const unsigned char* a = (const unsigned char*)expected;
    const unsigned char* b = (const unsigned char*)actual;
    size_t offset = 0;
    while (offset < size && a[offset] == b[offset]) offset++;
    if (offset == size) return;
    std::fprintf(stderr,
        "CUDA adapter mismatch field=%s suite=%s match=%d turn=%d offset=%zu/%zu "
        "cpu=%u gpu=%u\n", field, suite, match, step, offset, size,
        offset < size ? (unsigned)a[offset] : 0,
        offset < size ? (unsigned)b[offset] : 0);
    std::exit(1);
}

static void compare_float(const char* field, const char* suite, int match,
        int step, float cpu, float gpu) {
    float tolerance = 2e-5f * (1.0f + std::fabs(cpu));
    if (!std::isfinite(cpu) || !std::isfinite(gpu)
            || std::fabs(cpu - gpu) > tolerance) {
        std::fprintf(stderr,
            "CUDA adapter mismatch field=%s suite=%s match=%d turn=%d "
            "cpu=%.9g gpu=%.9g\n", field, suite, match, step, cpu, gpu);
        std::exit(1);
    }
}

static void set_kwargs(Dict* kwargs, int cards, int turns, int prize,
        int info, int ret, int ties) {
    dict_set(kwargs, "seed", 7);
    dict_set(kwargs, "num_players", 2);
    dict_set(kwargs, "num_cards", cards);
    dict_set(kwargs, "num_turns", turns);
    dict_set(kwargs, "prize_order", prize);
    dict_set(kwargs, "information", info);
    dict_set(kwargs, "egocentric", 1);
    dict_set(kwargs, "open_spiel_obs", 0);
    dict_set(kwargs, "return_type", ret);
    dict_set(kwargs, "tie_rule", ties);
    dict_set(kwargs, "auto_forced_last", 1);
    dict_set(kwargs, "exact_exploiter", 0);
    dict_set(kwargs, "exact_exploiter_banks", 1);
    dict_set(kwargs, "exact_exploiter_history", 16);
    dict_set(kwargs, "exact_exploiter_current_prob", 0.0f);
}

static void run_suite(const char* name, int cards, int turns, int prize,
        int info, int ret, int ties) {
    Dict kwargs = {0};
    set_kwargs(&kwargs, cards, turns, prize, info, ret, ties);

    Env* cpu = (Env*)std::calloc(SUITE_MATCHES, sizeof(Env));
    obs_t* cpu_obs = (obs_t*)std::calloc(
        (size_t)SUITE_ROWS * OBS_SIZE, sizeof(obs_t));
    float* cpu_actions = (float*)std::calloc(
        (size_t)SUITE_ROWS * NUM_ATNS, sizeof(float));
    float* cpu_rewards = (float*)std::calloc(SUITE_ROWS, sizeof(float));
    float* cpu_terminals = (float*)std::calloc(SUITE_ROWS, sizeof(float));
    unsigned char* cpu_masks = (unsigned char*)std::calloc(
        (size_t)SUITE_ROWS * GS_NUM_CARDS, 1);
    if (!cpu || !cpu_obs || !cpu_actions || !cpu_rewards
            || !cpu_terminals || !cpu_masks) std::exit(1);

    uint32_t rng[SUITE_MATCHES];
    int cpu_rows[SUITE_MATCHES][GS_MAX_PLAYERS];
    /* Two matches use frozen banks so we can verify tag/boundary/counters. */
    int frozen_banks = 1;
    int frozen_matches = 2;
    int frozen_start = SUITE_MATCHES - frozen_matches;
    int primary_count = 2 * (SUITE_MATCHES - frozen_matches) + frozen_matches;
    int cpu_cursors[2] = {0, primary_count};
    for (int i = 0; i < SUITE_MATCHES; i++) {
        Env* e = &cpu[i];
        /* puf_init derives rng = gs_mix32(seed ^ 0x9e3779b9u * (stream+1)).
         * The GPU reset kernel uses match_id as the stream, so stream=i here
         * gives the identical per-match stream. */
        e->rng = (unsigned)i;
        puf_init(e, &kwargs);
        /* The GPU sets policy from shell rows at reset; mirror the same
         * frozen-bank layout here so tags agree. */
        int policy[2] = {0, 0};
        if (i >= frozen_start) {
            policy[1] = 1 + (i - frozen_start) % frozen_banks;
        }
        if (frozen_banks && ((i / frozen_banks) & 1)) {
            int swap = policy[0];
            policy[0] = policy[1];
            policy[1] = swap;
        }
        e->tag = policy[0] > policy[1] ? policy[0] : policy[1];
        int rows[2];
        for (int player = 0; player < GS_MAX_PLAYERS; player++) {
            rows[player] = cpu_cursors[policy[player]]++;
            e->agents[player].policy = policy[player];
            cpu_rows[i][player] = rows[player];
        }
        for (int player = 0; player < e->num_agents; player++) {
            int row = rows[player];
            e->agents[player].observations = cpu_obs + (size_t)row * OBS_SIZE;
            e->agents[player].actions = cpu_actions + (size_t)row * NUM_ATNS;
            e->agents[player].rewards = cpu_rewards + row;
            e->agents[player].terminals = cpu_terminals + row;
            e->agents[player].action_mask =
                cpu_masks + (size_t)row * GS_NUM_CARDS;
        }
        puf_reset(e);
        rng[i] = 0x9e3779b9u ^ (uint32_t)i * 747796405U;
    }

    int layout[3] = {0, primary_count, SUITE_ROWS};
    Dict vec_kwargs = {0};
    dict_set(&vec_kwargs, "num_frozen_banks", frozen_banks);
    dict_set(&vec_kwargs, "frozen_bank_pct",
        (double)frozen_matches / SUITE_MATCHES);
    dict_set(&vec_kwargs, "seat_balance", 1);
    Env* gpu_envs = puf_envs_create(SUITE_ROWS, &kwargs, &vec_kwargs, layout);

    obs_t* d_obs = nullptr;
    float* d_actions = nullptr;
    float* d_rewards = nullptr;
    float* d_terminals = nullptr;
    unsigned char* d_masks = nullptr;
    CUDA_OK(cudaMalloc((void**)&d_obs,
        (size_t)SUITE_ROWS * OBS_SIZE * sizeof(obs_t)));
    CUDA_OK(cudaMalloc((void**)&d_actions,
        (size_t)SUITE_ROWS * NUM_ATNS * sizeof(float)));
    CUDA_OK(cudaMalloc((void**)&d_rewards, SUITE_ROWS * sizeof(float)));
    CUDA_OK(cudaMalloc((void**)&d_terminals, SUITE_ROWS * sizeof(float)));
    CUDA_OK(cudaMalloc((void**)&d_masks,
        (size_t)SUITE_ROWS * GS_NUM_CARDS));
    puf_envs_bind_buffers(d_actions, d_masks);
    puf_envs_reset(gpu_envs, d_obs, d_rewards, d_terminals, SUITE_ROWS);
    CUDA_OK(cudaDeviceSynchronize());

    Env* gpu = (Env*)std::calloc(SUITE_MATCHES, sizeof(Env));
    obs_t* gpu_obs = (obs_t*)std::calloc(
        (size_t)SUITE_ROWS * OBS_SIZE, sizeof(obs_t));
    float* gpu_rewards = (float*)std::calloc(SUITE_ROWS, sizeof(float));
    float* gpu_terminals = (float*)std::calloc(SUITE_ROWS, sizeof(float));
    unsigned char* gpu_masks = (unsigned char*)std::calloc(
        (size_t)SUITE_ROWS * GS_NUM_CARDS, 1);
    if (!gpu || !gpu_obs || !gpu_rewards || !gpu_terminals || !gpu_masks)
        std::exit(1);

    for (int step = 0; step < SUITE_STEPS; step++) {
        for (int i = 0; i < SUITE_MATCHES; i++) {
            for (int player = 0; player < GS_MAX_PLAYERS; player++) {
                cpu[i].agents[player].actions[0] =
                    (float)(adapter_rng(&rng[i]) % (uint32_t)cards);
            }
            puf_step(&cpu[i]);
        }
        CUDA_OK(cudaMemcpy(d_actions, cpu_actions,
            (size_t)SUITE_ROWS * NUM_ATNS * sizeof(float),
            cudaMemcpyHostToDevice));
        puf_envs_step(gpu_envs, d_actions, d_obs, d_rewards, d_terminals,
            0, SUITE_ROWS, 0);
        CUDA_OK(cudaDeviceSynchronize());
        CUDA_OK(cudaMemcpy(gpu, d_gs_matches, SUITE_MATCHES * sizeof(Env),
            cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_obs, d_obs,
            (size_t)SUITE_ROWS * OBS_SIZE, cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_rewards, d_rewards,
            SUITE_ROWS * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_terminals, d_terminals,
            SUITE_ROWS * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_masks, d_masks,
            (size_t)SUITE_ROWS * GS_NUM_CARDS, cudaMemcpyDeviceToHost));

        for (int i = 0; i < SUITE_MATCHES; i++) {
            if (std::memcmp(&cpu[i].state, &gpu[i].state, sizeof(GSState)) != 0) {
                fail_bytes("state", name, i, step,
                    &cpu[i].state, &gpu[i].state, sizeof(GSState));
            }
            fail_bytes("history", name, i, step,
                &cpu[i].history, &gpu[i].history, sizeof(GSHistory));
            if (cpu[i].rng != gpu[i].rng) {
                std::fprintf(stderr,
                    "CUDA adapter mismatch field=rng suite=%s match=%d turn=%d "
                    "cpu=%u gpu=%u\n", name, i, step, cpu[i].rng, gpu[i].rng);
                std::exit(1);
            }
            if (cpu[i].tag != gpu[i].tag) {
                std::fprintf(stderr,
                    "CUDA adapter mismatch field=tag suite=%s match=%d turn=%d "
                    "cpu=%d gpu=%d\n", name, i, step, cpu[i].tag, gpu[i].tag);
                std::exit(1);
            }
            if (cpu[i].boundary_reached != gpu[i].boundary_reached) {
                std::fprintf(stderr,
                    "CUDA adapter mismatch field=boundary suite=%s match=%d "
                    "turn=%d cpu=%d gpu=%d\n",
                    name, i, step, cpu[i].boundary_reached,
                    gpu[i].boundary_reached);
                std::exit(1);
            }
            for (int player = 0; player < GS_MAX_PLAYERS; player++) {
                int row = cpu_rows[i][player];
                fail_bytes("observation", name, i, step,
                    cpu_obs + (size_t)row * OBS_SIZE,
                    gpu_obs + (size_t)row * OBS_SIZE, OBS_SIZE);
                fail_bytes("mask", name, i, step,
                    cpu_masks + (size_t)row * GS_NUM_CARDS,
                    gpu_masks + (size_t)row * GS_NUM_CARDS, GS_NUM_CARDS);
                compare_float("reward", name, i, step,
                    cpu_rewards[row], gpu_rewards[row]);
                compare_float("terminal", name, i, step,
                    cpu_terminals[row], gpu_terminals[row]);
            }
            const float* cpu_log = (const float*)&cpu[i].log;
            Log shell_log;
            CUDA_OK(cudaMemcpy(&shell_log, &gpu_envs[cpu_rows[i][0]].log,
                sizeof(Log), cudaMemcpyDeviceToHost));
            const float* gpu_log = (const float*)&shell_log;
            for (size_t field = 0; field < sizeof(Log) / sizeof(float); field++) {
                compare_float("log", name, i, step,
                    cpu_log[field], gpu_log[field]);
            }
        }
    }

    puf_envs_close(gpu_envs);
    CUDA_OK(cudaFree(d_obs));
    CUDA_OK(cudaFree(d_actions));
    CUDA_OK(cudaFree(d_rewards));
    CUDA_OK(cudaFree(d_terminals));
    CUDA_OK(cudaFree(d_masks));
    std::free(cpu);
    std::free(cpu_obs);
    std::free(cpu_actions);
    std::free(cpu_rewards);
    std::free(cpu_terminals);
    std::free(cpu_masks);
    std::free(gpu);
    std::free(gpu_obs);
    std::free(gpu_rewards);
    std::free(gpu_terminals);
    std::free(gpu_masks);
    std::printf("suite %s: PASS (%d matches x %d turns)\n",
        name, SUITE_MATCHES, SUITE_STEPS);
}

int main(void) {
#if GS_NUM_CARDS == 4
    run_suite("4c-random-perfect-discard", 4, 4, 0, 0, 0, 0);
    run_suite("4c-ascending-hidden-carry", 4, 4, 1, 1, 1, 1);
    run_suite("4c-descending-perfect-points", 4, 4, 2, 0, 2, 0);
    run_suite("4c-random-perfect-pointdiff", 4, 3, 0, 0, 1, 1);
#else
    run_suite("13c-random-hidden-discard", 13, 13, 0, 1, 0, 0);
    run_suite("13c-descending-perfect-points", 13, 13, 2, 0, 2, 1);
#endif
    std::printf("Goofspiel CUDA adapter: ALL SUITES PASS\n");
    return 0;
}
