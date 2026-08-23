#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../kaggriculture.h"

#define PUFFER_GPU_ENV 1
#include "../kaggriculture.cu"

#define CUDA_OK(call) do { \
    cudaError_t err__ = (call); \
    if (err__ != cudaSuccess) { \
        std::fprintf(stderr, "%s:%d: CUDA error: %s\n", \
            __FILE__, __LINE__, cudaGetErrorString(err__)); \
        std::exit(1); \
    } \
} while (0)

enum {
    ADAPTER_CASES = 12,
    ADAPTER_STEPS = 1440,
    ADAPTER_ROWS = 2 * ADAPTER_CASES,
};

__global__ static void adapter_bind_kernel(Env* envs, Env* shells,
        obs_t* observations, float* actions, float* rewards,
        float* terminals, unsigned char* masks,
        const KGScriptTape* tapes) {
    int case_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (case_id >= ADAPTER_CASES) return;
    Env* env = &envs[case_id];
    for (int player = 0; player < 2; player++) {
        int row = 2 * case_id + player;
        env->agents[player].observations = observations + (size_t)row * OBS_SIZE;
        env->agents[player].actions = actions + (size_t)row * NUM_ATNS;
        env->agents[player].rewards = rewards + row;
        env->agents[player].terminals = terminals + row;
        env->agents[player].action_mask = masks
            + (size_t)row * KG_POLICY_ACTION_MASK_SIZE;
    }
    shells[2 * case_id].tag = 0;
    shells[2 * case_id + 1].tag = 0;
    kag_write_all_observations_from_tapes(env, tapes);
}

__global__ static void adapter_step_kernel(Env* envs, Env* shells,
        const KGScriptTape* tapes, KGAction* decoded, int* bank_completed) {
    int case_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (case_id >= ADAPTER_CASES) return;
    int rows[2] = {2 * case_id, 2 * case_id + 1};
    kag_cuda_transition(&envs[case_id], shells, rows, tapes,
        decoded + 2 * case_id, bank_completed);
}

static uint32_t adapter_rng(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void random_policy_actions(float* actions, uint32_t* rng) {
    for (int head = 0; head < NUM_ATNS; head++) {
        uint32_t value = adapter_rng(rng);
        int size = KG_ACTION_SIZES[head];
        switch (value & 7U) {
            case 0: actions[head] = -9.75f; break;
            case 1: actions[head] = (float)(size + 11); break;
            case 2: actions[head] = (float)(size - 1) + 0.99f; break;
            default: actions[head] = (float)(value % (uint32_t)size); break;
        }
    }
}

static void configure_case(Env* env, int case_id, obs_t* observations,
        float* actions, float* rewards, float* terminals,
        unsigned char* masks) {
    std::memset(env, 0, sizeof(*env));
    env->num_agents = 2;
    env->rng = (unsigned)case_id;
    env->tag = case_id == 0 ? 0 : case_id;
    env->policy_market_slots = case_id % 3 == 0 ? 1
        : case_id % 3 == 1 ? 4 : 10;
    env->policy_max_hands = case_id % 2 ? 8 : KG_MAX_HANDS;
    env->opening_turns = case_id == 0 ? 10 : case_id == 7 ? 26 : 0;
    env->reset_opening_turns = case_id == 1 ? 20
        : case_id == 8 ? 26 : 0;
    env->reset_opening_prob = case_id == 1 ? 0.5f
        : case_id == 8 ? 1.0f : 0.0f;
    env->reward_potential_scale = 0.000772047148f;
    env->reward_potential_gamma = 0.9993f;
    env->reward_cash_scale = 0.07f;
    env->reward_money_scale = 0.13f;
    env->reward_progress_scale = 0.31f;
    env->reward_progress_terminal_money_scale = 0.17f;
    env->reward_progress_win_scale = 0.23f;
    env->reward_progress_liquidation_days = 3.0f;
    env->reward_progress_seed_scale = 1.0f;
    env->reward_progress_crop_scale = 0.8f;
    env->reward_progress_animal_scale = 0.9f;
    env->reward_progress_product_scale = 0.7f;
    env->reward_progress_maintenance_scale = 0.0f;
    env->reward_progress_land_scale = 1.0f;
    env->reward_progress_health_ratio = 0.6f;
    for (int crop = 0; crop < KG_NUM_CROPS; crop++) {
        env->reward_progress_crop_units[crop] = 3.0f + 0.25f * crop;
        env->reward_progress_seed_realization[crop] = 1.0f;
    }
    for (int animal = 0; animal < KG_NUM_ANIMALS; animal++) {
        env->reward_progress_animal_units_per_event[animal] =
            1.0f + 0.5f * animal;
        env->reward_progress_animal_realization[animal] = 1.0f;
    }
    for (int product = 0; product < KG_NUM_PRODUCTS; product++) {
        env->reward_progress_product_realization[product] =
            0.6f + 0.02f * product;
    }
    env->bot_first = case_id & 1;
    env->bot_opponent_fraction = 1.0f;
    static const int bots[ADAPTER_CASES] = {
        KAG_BOT_NONE,
        KAG_BOT_PASS,
        KAG_BOT_STARTER,
        KAG_BOT_MIXED,
        KAG_BOT_CROP_BASE + KG_MELON,
        KAG_BOT_SCRIPT_BASE + KG_SCRIPT_FRONTIER,
        KAG_BOT_SCRIPT_BASE + KG_SCRIPT_V20,
        KAG_BOT_SCRIPT_BASE + KG_SCRIPT_TOP,
        KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_HARVEST_PULSE,
        KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_STRUCTURED,
        KAG_BOT_ADAPTIVE_BASE + KAG_ADAPTIVE_TRIAD,
        KAG_BOT_SCRIPT_BASE + KG_SCRIPT_MOON,
    };
    env->bot_opponent = bots[case_id];
    if (bots[case_id] != KAG_BOT_NONE) {
        env->demo_bots[env->bot_first ? 0 : 1] = bots[case_id];
    }

    for (int player = 0; player < 2; player++) {
        int row = 2 * case_id + player;
        env->agents[player].observations = observations + (size_t)row * OBS_SIZE;
        env->agents[player].actions = actions + (size_t)row * NUM_ATNS;
        env->agents[player].rewards = rewards + row;
        env->agents[player].terminals = terminals + row;
        env->agents[player].action_mask = masks
            + (size_t)row * KG_POLICY_ACTION_MASK_SIZE;
        env->agents[player].policy = player;
    }

    KGConfig config;
    kg_config_default(&config);
    config.seed = 0xfedcba987654321ULL + (uint64_t)case_id * 1000003ULL;
    config.episode_steps = case_id == 0 ? 1 : 31 + 7 * case_id;
    config.starting_money = case_id % 4 == 0 ? 500 : 3000 + 97 * case_id;
    config.max_market_orders_per_turn = 1 + case_id % KG_MAX_MARKET_ORDERS;
    config.shed_capacity = case_id % 4 == 0 ? 1
        : case_id % 4 == 1 ? 7 : 100;
    config.weed_spawn_chance = case_id % 4 == 0 ? 0.0
        : case_id % 4 == 1 ? 1.0 : 0.005;
    config.town_shop_unlock_interval = 1 + case_id % 5;
    config.town_shop_sell_interval = 1 + case_id % 7;
    config.town_center_sell_interval = 1 + case_id % 29;
    kg_init(&env->game_storage, &config);
    /* A deterministic terminal case catches both reward regressions that the
     * randomized suite previously missed: nonzero relative-money margin and
     * an inactivity threshold wider than the CUDA path's old hardcoded $2. */
    if (case_id == 0) env->game_storage.players[0].money += 123;
    env->reset_opening_rng = (uint32_t)config.seed ^ 0xa511e9b3u;
    kag_reset_with_opening(env, kag_script_tapes);
    for (int player = 0; player < 2; player++) {
        env->potential[player] = kag_player_potential(env, player);
        env->progress_value[player] = kag_player_progress_value(env, player);
    }
    kag_write_all_observations(env);
}

static void fail_bytes(const char* field, int case_id, int step,
        const void* expected, const void* actual, size_t size) {
    const unsigned char* a = (const unsigned char*)expected;
    const unsigned char* b = (const unsigned char*)actual;
    size_t offset = 0;
    while (offset < size && a[offset] == b[offset]) offset++;
    if (offset == size) return;
    std::fprintf(stderr,
        "CUDA adapter mismatch field=%s case=%d turn=%d offset=%zu/%zu "
        "cpu=%u gpu=%u\n", field, case_id, step, offset, size,
        offset < size ? (unsigned)a[offset] : 0,
        offset < size ? (unsigned)b[offset] : 0);
    std::exit(1);
}

static Log combined_gpu_log(const Env* match, const Env* shells, int case_id) {
    Log out = match->log;
    float* dst = (float*)&out;
    for (int row = 0; row < 2; row++) {
        const float* src = (const float*)&shells[2 * case_id + row].log;
        for (size_t field = 0; field < sizeof(Log) / sizeof(float); field++) {
            dst[field] += src[field];
        }
    }
    return out;
}

static void compare_float(const char* field, int case_id, int step,
        float cpu, float gpu) {
    float tolerance = 2e-5f * (1.0f + std::fabs(cpu));
    if (!std::isfinite(cpu) || !std::isfinite(gpu)
            || std::fabs(cpu - gpu) > tolerance) {
        std::fprintf(stderr,
            "CUDA adapter mismatch field=%s case=%d turn=%d cpu=%.9g gpu=%.9g\n",
            field, case_id, step, cpu, gpu);
        std::exit(1);
    }
}

int main(void) {
    kag_script_init();
    Env* cpu = (Env*)std::calloc(ADAPTER_CASES, sizeof(Env));
    Env* gpu = (Env*)std::calloc(ADAPTER_CASES, sizeof(Env));
    Env* shells = (Env*)std::calloc(ADAPTER_ROWS, sizeof(Env));
    obs_t* cpu_obs = (obs_t*)std::calloc(
        (size_t)ADAPTER_ROWS * OBS_SIZE, sizeof(obs_t));
    obs_t* gpu_obs = (obs_t*)std::calloc(
        (size_t)ADAPTER_ROWS * OBS_SIZE, sizeof(obs_t));
    float* cpu_actions = (float*)std::calloc(
        (size_t)ADAPTER_ROWS * NUM_ATNS, sizeof(float));
    float* cpu_rewards = (float*)std::calloc(ADAPTER_ROWS, sizeof(float));
    float* gpu_rewards = (float*)std::calloc(ADAPTER_ROWS, sizeof(float));
    float* cpu_terminals = (float*)std::calloc(ADAPTER_ROWS, sizeof(float));
    float* gpu_terminals = (float*)std::calloc(ADAPTER_ROWS, sizeof(float));
    unsigned char* cpu_masks = (unsigned char*)std::calloc(
        (size_t)ADAPTER_ROWS * KG_POLICY_ACTION_MASK_SIZE, 1);
    unsigned char* gpu_masks = (unsigned char*)std::calloc(
        (size_t)ADAPTER_ROWS * KG_POLICY_ACTION_MASK_SIZE, 1);
    if (!cpu || !gpu || !shells || !cpu_obs || !gpu_obs || !cpu_actions
            || !cpu_rewards || !gpu_rewards || !cpu_terminals
            || !gpu_terminals || !cpu_masks || !gpu_masks) return 1;

    uint32_t rng[ADAPTER_CASES];
    for (int i = 0; i < ADAPTER_CASES; i++) {
        configure_case(&cpu[i], i, cpu_obs, cpu_actions,
            cpu_rewards, cpu_terminals, cpu_masks);
        rng[i] = 0x9e3779b9U ^ (uint32_t)i * 747796405U;
    }

    Env* d_envs = nullptr;
    Env* d_shells = nullptr;
    obs_t* d_obs = nullptr;
    float* d_actions = nullptr;
    float* d_rewards = nullptr;
    float* d_terminals = nullptr;
    unsigned char* d_masks = nullptr;
    KGScriptTape* d_tapes = nullptr;
    KGAction* d_decoded = nullptr;
    int* d_bank_completed = nullptr;
    CUDA_OK(cudaMalloc((void**)&d_envs, ADAPTER_CASES * sizeof(Env)));
    CUDA_OK(cudaMalloc((void**)&d_shells, ADAPTER_ROWS * sizeof(Env)));
    CUDA_OK(cudaMalloc((void**)&d_obs,
        (size_t)ADAPTER_ROWS * OBS_SIZE * sizeof(obs_t)));
    CUDA_OK(cudaMalloc((void**)&d_actions,
        (size_t)ADAPTER_ROWS * NUM_ATNS * sizeof(float)));
    CUDA_OK(cudaMalloc((void**)&d_rewards, ADAPTER_ROWS * sizeof(float)));
    CUDA_OK(cudaMalloc((void**)&d_terminals, ADAPTER_ROWS * sizeof(float)));
    CUDA_OK(cudaMalloc((void**)&d_masks,
        (size_t)ADAPTER_ROWS * KG_POLICY_ACTION_MASK_SIZE));
    CUDA_OK(cudaMalloc((void**)&d_tapes, KG_SCRIPT_COUNT * sizeof(KGScriptTape)));
    CUDA_OK(cudaMalloc((void**)&d_decoded,
        2 * ADAPTER_CASES * sizeof(KGAction)));
    CUDA_OK(cudaMalloc((void**)&d_bank_completed,
        (ADAPTER_CASES + 1) * sizeof(int)));
    CUDA_OK(cudaMemset(d_bank_completed, 0,
        (ADAPTER_CASES + 1) * sizeof(int)));
    CUDA_OK(cudaMemcpy(d_envs, cpu, ADAPTER_CASES * sizeof(Env),
        cudaMemcpyHostToDevice));
    CUDA_OK(cudaMemset(d_shells, 0, ADAPTER_ROWS * sizeof(Env)));
    CUDA_OK(cudaMemcpy(d_tapes, kag_script_tapes,
        KG_SCRIPT_COUNT * sizeof(KGScriptTape), cudaMemcpyHostToDevice));
    adapter_bind_kernel<<<1, 32>>>(d_envs, d_shells, d_obs, d_actions,
        d_rewards, d_terminals, d_masks, d_tapes);
    CUDA_OK(cudaGetLastError());
    CUDA_OK(cudaDeviceSynchronize());

    for (int step = 0; step < ADAPTER_STEPS; step++) {
        for (int i = 0; i < ADAPTER_CASES; i++) {
            if (i == 0) {
                std::memset(cpu_actions + (size_t)(2 * i) * NUM_ATNS, 0,
                    2 * NUM_ATNS * sizeof(float));
            } else {
                random_policy_actions(cpu_actions
                    + (size_t)(2 * i) * NUM_ATNS, &rng[i]);
                random_policy_actions(cpu_actions
                    + (size_t)(2 * i + 1) * NUM_ATNS, &rng[i]);
            }
            puf_step(&cpu[i]);
        }
        CUDA_OK(cudaMemcpy(d_actions, cpu_actions,
            (size_t)ADAPTER_ROWS * NUM_ATNS * sizeof(float),
            cudaMemcpyHostToDevice));
        adapter_step_kernel<<<1, 32>>>(d_envs, d_shells, d_tapes, d_decoded,
            d_bank_completed);
        CUDA_OK(cudaGetLastError());
        CUDA_OK(cudaDeviceSynchronize());
        CUDA_OK(cudaMemcpy(gpu, d_envs, ADAPTER_CASES * sizeof(Env),
            cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(shells, d_shells, ADAPTER_ROWS * sizeof(Env),
            cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_obs, d_obs,
            (size_t)ADAPTER_ROWS * OBS_SIZE, cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_rewards, d_rewards,
            ADAPTER_ROWS * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_terminals, d_terminals,
            ADAPTER_ROWS * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_OK(cudaMemcpy(gpu_masks, d_masks,
            (size_t)ADAPTER_ROWS * KG_POLICY_ACTION_MASK_SIZE,
            cudaMemcpyDeviceToHost));

        for (int i = 0; i < ADAPTER_CASES; i++) {
            if (std::memcmp(&cpu[i].game_storage, &gpu[i].game_storage,
                    sizeof(KGState)) != 0) {
                fail_bytes("state", i, step, &cpu[i].game_storage,
                    &gpu[i].game_storage, sizeof(KGState));
            }
            if (cpu[i].boundary_reached != gpu[i].boundary_reached) {
                std::fprintf(stderr,
                    "CUDA adapter scalar mismatch case=%d turn=%d "
                    "boundary=(%d,%d)\n", i, step,
                    cpu[i].boundary_reached, gpu[i].boundary_reached);
                return 1;
            }
            for (int player = 0; player < 2; player++) {
                int row = 2 * i + player;
                fail_bytes("observation", i, step,
                    cpu_obs + (size_t)row * OBS_SIZE,
                    gpu_obs + (size_t)row * OBS_SIZE, OBS_SIZE);
                fail_bytes("mask", i, step,
                    cpu_masks + (size_t)row * KG_POLICY_ACTION_MASK_SIZE,
                    gpu_masks + (size_t)row * KG_POLICY_ACTION_MASK_SIZE,
                    KG_POLICY_ACTION_MASK_SIZE);
                compare_float("reward", i, step,
                    cpu_rewards[row], gpu_rewards[row]);
                compare_float("terminal", i, step,
                    cpu_terminals[row], gpu_terminals[row]);
                compare_float("potential", i, step,
                    cpu[i].potential[player], gpu[i].potential[player]);
                compare_float("progress_value", i, step,
                    cpu[i].progress_value[player],
                    gpu[i].progress_value[player]);
                compare_float("episode_return", i, step,
                    cpu[i].episode_returns[player],
                    gpu[i].episode_returns[player]);
            }
            Log gpu_log = combined_gpu_log(&gpu[i], shells, i);
            const float* expected = (const float*)&cpu[i].log;
            const float* actual = (const float*)&gpu_log;
            for (size_t field = 0; field < sizeof(Log) / sizeof(float); field++) {
                compare_float("log", i, step, expected[field], actual[field]);
            }
        }
    }

    if (!(cpu[1].log.reset_games > 0.0f
            && cpu[1].log.reset_games < cpu[1].log.n)) {
        std::fprintf(stderr,
            "reset mixture case failed: reset=%g episodes=%g\n",
            cpu[1].log.reset_games, cpu[1].log.n);
        return 1;
    }
    if (cpu[8].log.n <= 0.0f
            || cpu[8].log.reset_games != cpu[8].log.n) {
        std::fprintf(stderr,
            "reset-only case failed: reset=%g episodes=%g\n",
            cpu[8].log.reset_games, cpu[8].log.n);
        return 1;
    }
    if (cpu[0].log.reset_games != 0.0f) {
        std::fprintf(stderr, "root-only case unexpectedly reset: %g\n",
            cpu[0].log.reset_games);
        return 1;
    }

    CUDA_OK(cudaFree(d_bank_completed));
    CUDA_OK(cudaFree(d_decoded));
    CUDA_OK(cudaFree(d_tapes));
    CUDA_OK(cudaFree(d_masks));
    CUDA_OK(cudaFree(d_terminals));
    CUDA_OK(cudaFree(d_rewards));
    CUDA_OK(cudaFree(d_actions));
    CUDA_OK(cudaFree(d_obs));
    CUDA_OK(cudaFree(d_shells));
    CUDA_OK(cudaFree(d_envs));
    std::printf(
        "Kaggriculture CUDA adapter: PASS (%d adversarial modes x %d turns; "
        "state/obs/mask/reward/reset/log exact)\n",
        ADAPTER_CASES, ADAPTER_STEPS);
    return 0;
}
