// Exercise the REAL actor sampler and PPO kernel, not a second implementation.
#define PRECISION_FLOAT
#define ENV_HEADER "ocean/kaggriculture/kaggriculture.h"
#define PUFFER_ENV_NAME "kaggriculture"
#include "../src/pufferl.cu"
#include <vector>

static void checked(cudaError_t status) {
    if (status != cudaSuccess) { fprintf(stderr, "CUDA: %s\n", cudaGetErrorString(status)); exit(1); }
}
template<class T> static T* device(const std::vector<T>& values) {
    T* out; checked(cudaMalloc((void**)&out, values.size() * sizeof(T)));
    checked(cudaMemcpy(out, values.data(), values.size() * sizeof(T), cudaMemcpyHostToDevice));
    return out;
}
template<class T> static std::vector<T> host(const T* ptr, size_t n) {
    std::vector<T> out(n); checked(cudaMemcpy(out.data(), ptr, n * sizeof(T), cudaMemcpyDeviceToHost));
    return out;
}
static bool visited(const float* actions, int h) {
    if (h < 17) return true;
    int slot = (h - 17) / 3, node = (h - 17) % 3;
    for (int prev = 0; prev < slot; prev++) if (actions[17 + 3 * prev] != 1) return false;
    if (node && actions[17 + 3 * slot] != 1) return false;
    return node != 2 || actions[h - 1] < KG_POLICY_MARKET_QUANTITY_COMMANDS;
}
int main(void) {
    constexpr int B = 12, A = KG_POLICY_ACTION_MASK_SIZE, F = A + 1, P = (A + 7) / 8;
    std::vector<Env> envs(6);
    std::vector<int> rows(B), sizes(KG_ACTION_SIZES, KG_ACTION_SIZES + NUM_ATNS);
    std::vector<unsigned char> base_masks(B * A);
    std::vector<float> logits(B * F), cpu_actions(B * NUM_ATNS);
    const int modes[] = {0,1,1,2,2,3}, executors[] = {0,0,1,0,1,0};
    for (int e = 0; e < 6; e++) {
        Env& env = envs[e]; KGConfig cfg; kg_config_default(&cfg); kg_init(&env.game_storage, &cfg);
        env.num_agents = 2; env.macro_mode = modes[e]; env.macro_executor_version = executors[e];
        env.frozen_macro_mode = env.frozen_macro_executor_version = -1;
        env.frozen_macro_decision_interval = env.frozen_macro_score_features = -1;
        env.macro_decision_interval = modes[e] == 1 ? 4 : 1;
        env.policy_market_slots = 10; env.policy_max_hands = 16;
        for (int seat = 0; seat < 2; seat++) {
            KGPlayer* farm = &env.game_storage.players[seat];
            kg_do_hire(&env.game_storage, farm); kg_do_hire(&env.game_storage, farm);
            farm->shed[KG_ITEM_WHEAT] = 3; farm->shed[KG_ITEM_MILK] = 4; farm->money = 90;
            farm->seeds[KG_WHEAT] = 1; env.agents[seat].policy = seat;
            if (seat) kg_inventory_add(&farm->units[0], KG_ITEM_CARROT, 2);
        }
    }
    for (int row = 0; row < B; row++) {
        rows[row] = (row * 5) % B; // Deliberately not match/seat order.
        Env* env = &envs[rows[row] / 2]; int seat = rows[row] % 2;
        env->agents[seat].action_mask = base_masks.data() + row * A;
        env->agents[seat].actions = cpu_actions.data() + row * NUM_ATNS;
        kag_write_mask(env, seat);
        for (int a = 0; a < A; a++) logits[row * F + a] = sinf((a * 17 + row) * 0.73f);
        for (int slot = 0; slot < 10; slot++) {
            logits[row * F + 748 + 31 * slot + 1] = 4; // Explore several market slots.
            logits[row * F + 748 + 31 * slot + 2 + KG_M_SELL + KG_ITEM_FERTILIZER] = 20;
        } // The highest raw logit is an empty sale and must NEVER be sampled.
    }
    Env* d_envs = device(envs); int* d_rows = device(rows); int* d_sizes = device(sizes);
    float* d_logits = device(logits);
    float* d_actions = device(std::vector<float>(B * NUM_ATNS));
    float* d_logp = device(std::vector<float>(B)); float* d_values = device(std::vector<float>(B));
    auto* d_rng = device(std::vector<curandStatePhilox4_32_10_t>(B));
    rng_init<<<1,32>>>(d_rng, 11, B);
    unsigned char* d_masks = device(base_masks);
    unsigned char* d_packed = device(std::vector<unsigned char>(B * P));
    float* d_grad = device(std::vector<float>(B * A));
    float* d_vgrad = device(std::vector<float>(B));
    float* d_partials = device(std::vector<float>(LOSS_N));
    float* d_ones = device(std::vector<float>(B, 1)); float* d_zero = device(std::vector<float>(B));
    float* d_scalars = device(std::vector<float>{0,1,0.01f});
    float* d_ratio = device(std::vector<float>(B));
    float* d_newvalue = device(std::vector<float>(B));
    for (int deterministic = 0; deterministic <= 1; deterministic++) {
        for (int row = 0; row < B; row++) kag_write_mask(&envs[rows[row] / 2], rows[row] % 2);
        checked(cudaMemcpy(d_masks, base_masks.data(), B * A, cudaMemcpyHostToDevice));
        sample_logits<<<1,32>>>({d_logits,{B,F}}, {}, {d_sizes,{NUM_ATNS}},
            d_actions, d_logp, d_values, d_rng, d_masks, A, deterministic, d_envs, d_rows, 0);
        pack_action_mask<<<grid_size(B * P),BLOCK_SIZE>>>(d_packed, d_masks, B, A, P);
        checked(cudaGetLastError()); checked(cudaDeviceSynchronize());
        auto sampled = host(d_actions, B * NUM_ATNS); auto lp = host(d_logp, B);
        auto actual_masks = host(d_masks, B * A); auto packed = host(d_packed, B * P);
        for (int row = 0; row < B; row++) {
            Env* env = &envs[rows[row] / 2]; int seat = rows[row] % 2;
            unsigned char* mask = base_masks.data() + row * A; kag_write_mask(env, seat);
            KagActionMaskState state; kag_action_mask_begin(&state, env, seat);
            float logp = 0; int offset = 0;
            for (int h = 0; h < NUM_ATNS; h++) {
                kag_action_mask_before(&state, h, mask);
                if (visited(sampled.data() + row * NUM_ATNS, h)) {
                    int action = (int)sampled[row * NUM_ATNS + h]; assert(mask[offset + action]);
                    float maximum = -INFINITY, sum = 0;
                    for (int a = 0; a < sizes[h]; a++) if (mask[offset + a]) maximum = fmaxf(maximum, logits[row * F + offset + a]);
                    for (int a = 0; a < sizes[h]; a++) if (mask[offset + a]) sum += expf(logits[row * F + offset + a] - maximum);
                    logp += logits[row * F + offset + action] - maximum - logf(sum);
                    kag_action_mask_commit(&state, h, action);
                }
                offset += sizes[h];
            }
            assert(fabsf(logp - lp[row]) < 2e-4f);
            assert(memcmp(mask, actual_masks.data() + row * A, A) == 0);
            for (int a = 0; a < A; a++) assert(!!mask[a] == !!(packed[row * P + a / 8] & (1 << (a % 8))));
            if (deterministic) {
                kag_sample_cpu_logits(env, seat, logits.data() + row * F, 1);
                assert(memcmp(env->agents[seat].actions, sampled.data() + row * NUM_ATNS, NUM_ATNS * sizeof(float)) == 0);
            }
        }
        PPOKernelArgs a = {};
        a.grad_logits = d_grad; a.grad_values_pred = d_vgrad; a.logits = d_logits;
        a.magnet_logits = d_logits; a.values_pred = d_logits + A;
        a.adv_mean = a.ret_mean = d_scalars; a.adv_var = a.ret_var = d_scalars + 1;
        a.act_sizes = d_sizes; a.action_mask = d_packed; a.num_atns = NUM_ATNS;
        a.clip_coef = a.vf_clip_coef = 0.2f; a.vf_coef = 0.5f; a.emag_kl_coef = 0.1f;
        a.emag_cutoff = 1; a.ent_coef = d_scalars + 2; a.T_seq = 1; a.A_total = A; a.N = B;
        PPOGraphArgs g = {d_ratio,d_newvalue,d_actions,d_logp,d_ones,d_ones,d_zero,d_zero};
        ppo_loss_compute<<<1,PPO_THREADS>>>(d_partials, a, g);
        checked(cudaGetLastError()); checked(cudaDeviceSynchronize());
        auto ratios = host(d_ratio, B); auto grad = host(d_grad, B * A); auto losses = host(d_partials, LOSS_N);
        int nonzero = 0;
        for (int row = 0; row < B; row++) {
            assert(std::isfinite(ratios[row]) && fabsf(ratios[row] - 1) < 3e-4f);
            int offset = 0;
            for (int h = 0; h < NUM_ATNS; h++) {
                for (int j = 0; j < sizes[h]; j++) {
                    float value = grad[row * A + offset + j]; assert(std::isfinite(value));
                    if (!actual_masks[row * A + offset + j] || !visited(sampled.data() + row * NUM_ATNS,h)) assert(value == 0);
                    nonzero += value != 0;
                }
                offset += sizes[h];
            }
        }
        assert(nonzero > 30); assert(fabsf(losses[LOSS_EMAG_KL]) < 1e-6f);
        printf("actor/PPO %s: six modes, permuted rows, CPU/GPU exact masks/actions, archived-prefix ratio=1, finite visited-only gradients PASS\n",
            deterministic ? "greedy" : "stochastic");
    }
    checked(cudaDeviceReset());
}
