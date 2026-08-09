#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../kaggriculture_core.h"
#include "../kaggriculture_core.c"

#define CUDA_OK(call) do { \
    cudaError_t err__ = (call); \
    if (err__ != cudaSuccess) { \
        std::fprintf(stderr, "%s:%d: CUDA error: %s\n", \
            __FILE__, __LINE__, cudaGetErrorString(err__)); \
        std::exit(1); \
    } \
} while (0)

enum {
    TEST_CASES = 32,
    TEST_STEPS = 720,
    CUDA_BLOCK = 128,
};

__global__ static void reset_kernel(KGState* states, const KGConfig* configs,
        int count) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) kg_init(&states[i], &configs[i]);
}

__global__ static void step_kernel(KGState* states, const KGAction* actions,
        int count) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) kg_step(&states[i], &actions[2 * i]);
}

static uint32_t test_rng(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int range(uint32_t* rng, int n) {
    return n > 0 ? (int)(test_rng(rng) % (uint32_t)n) : 0;
}

static void random_action(KGAction* action, const KGState* state,
        int player, uint32_t* rng) {
    std::memset(action, 0, sizeof(*action));
    const KGPlayer* farm = &state->players[player];
    action->farmer.op = range(rng, KG_OP_CARE + 7) - 3;
    action->farmer.arg = range(rng, KG_NUM_ITEMS + 8) - 4;
    action->farmer.n = range(rng, 260) - 5;
    action->hand_count = range(rng, KG_MAX_HANDS + 20) - 4;
    int hands = action->hand_count;
    if (hands < 0) hands = 0;
    if (hands > KG_MAX_HANDS) hands = KG_MAX_HANDS;
    for (int h = 0; h < hands; h++) {
        action->hands[h].op = range(rng, KG_OP_CARE + 7) - 3;
        action->hands[h].arg = range(rng, KG_NUM_ITEMS + 8) - 4;
        action->hands[h].n = range(rng, 260) - 5;
    }
    action->market_count = range(rng, KG_MAX_MARKET_ORDERS + 10) - 3;
    int orders = action->market_count;
    if (orders < 0) orders = 0;
    if (orders > KG_MAX_MARKET_ORDERS) orders = KG_MAX_MARKET_ORDERS;
    for (int o = 0; o < orders; o++) {
        action->market[o].op = range(rng, KG_MARKET_BUY_LAND + 7) - 3;
        action->market[o].item = range(rng, KG_NUM_ITEMS + 8) - 4;
        action->market[o].n = range(rng, 260) - 5;
    }

    /* Ensure fuzzing does useful work in addition to hammering invalid input. */
    if ((test_rng(rng) & 7U) == 0) {
        action->market_count = 1;
        action->market[0] = (KGMarketOrder){KG_MARKET_HIRE, 0, 1};
    } else if ((test_rng(rng) & 7U) == 0) {
        action->market_count = 1;
        action->market[0] = (KGMarketOrder){KG_MARKET_BUY_SEED,
            range(rng, KG_NUM_CROPS), 1 + range(rng, 10)};
    }
    if (farm->unit_count > 1 && (test_rng(rng) & 3U) == 0) {
        action->hand_count = farm->hand_count;
    }
}

static void fail_state(int case_id, int step, const KGState* cpu,
        const KGState* gpu, const KGAction* actions) {
    const unsigned char* a = (const unsigned char*)cpu;
    const unsigned char* b = (const unsigned char*)gpu;
    size_t offset = 0;
    while (offset < sizeof(*cpu) && a[offset] == b[offset]) offset++;
    std::fprintf(stderr,
        "CUDA parity mismatch case=%d turn=%d offset=%zu/%zu "
        "cpu=%u gpu=%u state_steps=(%d,%d) money=((%d,%d),(%d,%d))\n",
        case_id, step, offset, sizeof(*cpu),
        offset < sizeof(*cpu) ? (unsigned)a[offset] : 0,
        offset < sizeof(*gpu) ? (unsigned)b[offset] : 0,
        cpu->step, gpu->step,
        cpu->players[0].money, cpu->players[1].money,
        gpu->players[0].money, gpu->players[1].money);
    for (int p = 0; p < 2; p++) {
        const KGAction* action = &actions[p];
        const KGUnitState* cu = &cpu->players[p].units[0];
        const KGUnitState* gu = &gpu->players[p].units[0];
        std::fprintf(stderr,
            "P%d action farmer=(%d,%d,%d) hands=%d market=%d "
            "unit0_order_count=(%u,%u)\n",
            p, action->farmer.op, action->farmer.arg, action->farmer.n,
            action->hand_count, action->market_count,
            (unsigned)cu->inventory_order_count,
            (unsigned)gu->inventory_order_count);
        std::fprintf(stderr, "  cpu inventory:");
        for (int item = 0; item < KG_NUM_ITEMS; item++) {
            std::fprintf(stderr, " %d", cu->inventory[item]);
        }
        std::fprintf(stderr, " order:");
        for (int item = 0; item < cu->inventory_order_count; item++) {
            std::fprintf(stderr, " %u", (unsigned)cu->inventory_order[item]);
        }
        std::fprintf(stderr, "\n  gpu inventory:");
        for (int item = 0; item < KG_NUM_ITEMS; item++) {
            std::fprintf(stderr, " %d", gu->inventory[item]);
        }
        std::fprintf(stderr, " order:");
        for (int item = 0; item < gu->inventory_order_count; item++) {
            std::fprintf(stderr, " %u", (unsigned)gu->inventory_order[item]);
        }
        std::fprintf(stderr, "\n");
    }
    std::exit(1);
}

int main(void) {
    KGConfig* configs = (KGConfig*)std::calloc(TEST_CASES, sizeof(KGConfig));
    KGState* cpu = (KGState*)std::calloc(TEST_CASES, sizeof(KGState));
    KGState* gpu = (KGState*)std::calloc(TEST_CASES, sizeof(KGState));
    KGAction* actions = (KGAction*)std::calloc(2 * TEST_CASES, sizeof(KGAction));
    uint32_t rng[TEST_CASES];
    if (!configs || !cpu || !gpu || !actions) return 1;

    for (int i = 0; i < TEST_CASES; i++) {
        kg_config_default(&configs[i]);
        configs[i].seed = 0x123456789abcdefULL + (uint64_t)i * 1000003ULL;
        configs[i].weed_spawn_chance = (i % 5 == 0) ? 0.0
            : (i % 5 == 1) ? 1.0 : 0.005;
        configs[i].starting_money = (i % 7 == 0) ? 0
            : (i % 7 == 1) ? 500 : 3000 + i * 137;
        configs[i].shed_capacity = (i % 6 == 0) ? 1
            : (i % 6 == 1) ? 7 : 100;
        configs[i].max_market_orders_per_turn = 1 + (i % KG_MAX_MARKET_ORDERS);
        configs[i].town_shop_unlock_interval = 1 + (i % 5);
        configs[i].town_shop_sell_interval = 1 + (i % 7);
        configs[i].town_center_sell_interval = 1 + (i % 29);
        kg_init(&cpu[i], &configs[i]);
        rng[i] = 0x9e3779b9U ^ (uint32_t)i * 747796405U;
    }

    KGConfig* d_configs = nullptr;
    KGState* d_states = nullptr;
    KGAction* d_actions = nullptr;
    CUDA_OK(cudaMalloc((void**)&d_configs, TEST_CASES * sizeof(KGConfig)));
    CUDA_OK(cudaMalloc((void**)&d_states, TEST_CASES * sizeof(KGState)));
    CUDA_OK(cudaMalloc((void**)&d_actions, 2 * TEST_CASES * sizeof(KGAction)));
    CUDA_OK(cudaMemcpy(d_configs, configs, TEST_CASES * sizeof(KGConfig),
        cudaMemcpyHostToDevice));
    reset_kernel<<<(TEST_CASES + CUDA_BLOCK - 1) / CUDA_BLOCK, CUDA_BLOCK>>>(
        d_states, d_configs, TEST_CASES);
    CUDA_OK(cudaGetLastError());
    CUDA_OK(cudaDeviceSynchronize());
    CUDA_OK(cudaMemcpy(gpu, d_states, TEST_CASES * sizeof(KGState),
        cudaMemcpyDeviceToHost));
    for (int i = 0; i < TEST_CASES; i++) {
        if (std::memcmp(&cpu[i], &gpu[i], sizeof(KGState)) != 0) {
            KGAction empty[2] = {};
            fail_state(i, -1, &cpu[i], &gpu[i], empty);
        }
    }

    for (int step = 0; step < TEST_STEPS; step++) {
        for (int i = 0; i < TEST_CASES; i++) {
            random_action(&actions[2 * i], &cpu[i], 0, &rng[i]);
            random_action(&actions[2 * i + 1], &cpu[i], 1, &rng[i]);
            kg_step(&cpu[i], &actions[2 * i]);
        }
        CUDA_OK(cudaMemcpy(d_actions, actions,
            2 * TEST_CASES * sizeof(KGAction), cudaMemcpyHostToDevice));
        step_kernel<<<(TEST_CASES + CUDA_BLOCK - 1) / CUDA_BLOCK, CUDA_BLOCK>>>(
            d_states, d_actions, TEST_CASES);
        CUDA_OK(cudaGetLastError());
        CUDA_OK(cudaDeviceSynchronize());
        CUDA_OK(cudaMemcpy(gpu, d_states, TEST_CASES * sizeof(KGState),
            cudaMemcpyDeviceToHost));
        for (int i = 0; i < TEST_CASES; i++) {
            if (std::memcmp(&cpu[i], &gpu[i], sizeof(KGState)) != 0) {
                fail_state(i, step, &cpu[i], &gpu[i], &actions[2 * i]);
            }
        }
    }

    CUDA_OK(cudaFree(d_actions));
    CUDA_OK(cudaFree(d_states));
    CUDA_OK(cudaFree(d_configs));
    std::free(actions);
    std::free(gpu);
    std::free(cpu);
    std::free(configs);
    std::printf("Kaggriculture CUDA core: PASS (%d cases x %d exact turns)\n",
        TEST_CASES, TEST_STEPS);
    return 0;
}
