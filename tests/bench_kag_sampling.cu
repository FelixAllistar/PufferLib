// Read-only fixed-state diagnostic using the actual BF16 production sampler.
// This is not an end-to-end training benchmark or a learned-policy evaluation.
#define ENV_HEADER "ocean/kaggriculture/kaggriculture.h"
#define GPU_ENV_HEADER "ocean/kaggriculture/kaggriculture.cu"
#define PUFFER_GPU_ENV
#define PUFFER_ENV_NAME "kaggriculture"
#include "../src/pufferl.cu"
#include <algorithm>
#include <climits>
#include <vector>

static void checked(cudaError_t status) {
    if (status != cudaSuccess) {
        fprintf(stderr, "CUDA: %s\n", cudaGetErrorString(status)); exit(1);
    }
}
static void require(bool condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
template<class T> static T* upload(const std::vector<T>& values) {
    T* out;
    checked(cudaMalloc((void**)&out, values.size() * sizeof(T)));
    checked(cudaMemcpy(out, values.data(), values.size() * sizeof(T), cudaMemcpyHostToDevice));
    return out;
}
template<class T> static std::vector<T> download(const T* ptr, size_t n) {
    std::vector<T> out(n);
    checked(cudaMemcpy(out.data(), ptr, n * sizeof(T), cudaMemcpyDeviceToHost));
    return out;
}
template<class T> static bool equal(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && !memcmp(a.data(), b.data(), a.size() * sizeof(T));
}

static std::vector<KGState> states(int count, const char* path) {
    std::vector<KGState> result(count);
    if (!path) {
        for (int i = 0; i < count; i++) {
            KGConfig config; kg_config_default(&config); config.seed = i + 1;
            kg_init(&result[i], &config);
        }
        return result;
    }
    FILE* input = fopen(path, "rb");
    require(input != nullptr, "Cannot open reset bank");
    KagStateBankHeader header = {};
    require(fread(&header, sizeof(header), 1, input) == 1
        && !memcmp(header.magic, "KGRSTB1\0", 8)
        && header.bank_format_version == 1 && header.reserved == 0
        && header.native_state_version == kg_state_serialization_version()
        && header.native_state_size == sizeof(KGState) && header.record_count > 0,
        "Incompatible reset bank");
    std::vector<unsigned char> record(sizeof(KGState));
    for (int i = 0; i < count; i++) {
        uint64_t index = (uint64_t)i * (header.record_count - 1) / (count - 1);
        require(!fseeko(input, sizeof(header) + index * sizeof(KGState), SEEK_SET)
            && fread(record.data(), record.size(), 1, input) == 1
            && kg_state_deserialize(&result[i], record.data(), record.size()),
            "Invalid sampled reset state");
    }
    fclose(input);
    printf("bank_records=%u sampled_evenly=%d\n", header.record_count, count);
    return result;
}

static void run_case(const std::vector<KGState>& games, int executor, const char* fixture) {
    const int B = 2 * games.size(), A = KG_POLICY_ACTION_MASK_SIZE, F = A + 1;
    std::vector<Env> envs(games.size());
    std::vector<int> rows(B), sizes(KG_ACTION_SIZES, KG_ACTION_SIZES + NUM_ATNS);
    std::vector<unsigned char> masks((size_t)B * A);
    std::vector<float> host_actions((size_t)B * NUM_ATNS);
    std::vector<precision_t> logits((size_t)B * F);
    int min_units = KG_MAX_UNITS, max_units = 0, min_step = INT_MAX, max_step = 0;
    double units = 0;
    for (size_t i = 0; i < games.size(); i++) {
        Env& env = envs[i]; env.game_storage = games[i]; env.num_agents = 2;
        env.macro_mode = 2; env.macro_executor_version = executor;
        env.macro_decision_interval = 1; env.policy_max_hands = 16; env.policy_market_slots = 10;
        env.observation_version = 3;
        env.frozen_macro_mode = env.frozen_macro_executor_version = -1;
        env.frozen_macro_decision_interval = env.frozen_macro_score_features = -1;
        min_step = std::min(min_step, games[i].step); max_step = std::max(max_step, games[i].step);
        for (int seat = 0; seat < 2; seat++) {
            env.agents[seat].policy = seat;
            int n = games[i].players[seat].unit_count;
            min_units = std::min(min_units, n); max_units = std::max(max_units, n); units += n;
        }
    }
    for (int row = 0; row < B; row++) {
        rows[row] = (row * 5) % B; // Coprime permutation for power-of-two row counts.
        Env& env = envs[rows[row] / 2]; int seat = rows[row] % 2;
        env.agents[seat].action_mask = masks.data() + (size_t)row * A;
        env.agents[seat].actions = host_actions.data() + (size_t)row * NUM_ATNS;
        kag_write_mask(&env, seat);
        for (int a = 0; a < A; a++)
            logits[(size_t)row * F + a] = from_float(sinf((a * 17 + row) * 0.73f));
    }
    printf("fixture=%s executor=%d rows=%d step=%d..%d units=%d..%d mean_units=%.2f precision=bf16\n",
        fixture, executor, B, min_step, max_step, min_units, max_units, units / B);
    Env* d_envs = upload(envs);
    int* d_rows = upload(rows); int* d_sizes = upload(sizes);
    precision_t* d_logits = upload(logits);
    auto* d_actions = upload(std::vector<precision_t>((size_t)B * NUM_ATNS));
    auto* d_logp = upload(std::vector<precision_t>(B));
    auto* d_values = upload(std::vector<precision_t>(B));
    auto* d_rng = upload(std::vector<curandStatePhilox4_32_10_t>(B));
    auto* d_base = upload(masks); auto* d_masks = upload(masks);
    cudaStream_t stream; checked(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    cudaEvent_t begin, end; checked(cudaEventCreate(&begin)); checked(cudaEventCreate(&end));
    std::vector<precision_t> reference_actions, reference_logp, reference_values;
    std::vector<unsigned char> reference_masks;
    std::vector<curandStatePhilox4_32_10_t> reference_rng;
    for (int banked = 0; banked <= 1; banked++) {
        // Same rows/logits/RNG, only split into the actual learner + 8-bank sizes.
        // No neural forward is included, so combining launches is NOT a trainer patch.
        checked(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
        int offset = 0, bank_count = banked ? 9 : 1;
        for (int bank = 0; bank < bank_count; bank++) {
            int n = banked ? (bank ? 3 * B / 64 : 5 * B / 8) : B;
            sample_logits<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
                {d_logits + (size_t)offset * F, {n, F}}, {}, {d_sizes, {NUM_ATNS}},
                d_actions + (size_t)offset * NUM_ATNS, d_logp + offset, d_values + offset,
                d_rng + offset, d_masks + (size_t)offset * A, A, false, d_envs, d_rows, offset);
            offset += n;
        }
        require(offset == B, "Invalid bank geometry");
        cudaGraph_t graph; cudaGraphExec_t exec;
        checked(cudaStreamEndCapture(stream, &graph));
        checked(cudaGraphInstantiate(&exec, graph, 0));
        std::vector<float> elapsed;
        for (int repeat = -3; repeat < 12; repeat++) {
            checked(cudaMemcpyAsync(d_masks, d_base, masks.size(), cudaMemcpyDeviceToDevice, stream));
            rng_init<<<grid_size(B), BLOCK_SIZE, 0, stream>>>(d_rng, 17, B);
            checked(cudaEventRecord(begin, stream));
            checked(cudaGraphLaunch(exec, stream));
            checked(cudaEventRecord(end, stream)); checked(cudaEventSynchronize(end));
            float ms; checked(cudaEventElapsedTime(&ms, begin, end));
            if (repeat >= 0) elapsed.push_back(ms);
        }
        auto actions = download(d_actions, (size_t)B * NUM_ATNS);
        auto logp = download(d_logp, B), values = download(d_values, B);
        auto actual_masks = download(d_masks, masks.size()); auto rng = download(d_rng, B);
        if (!banked) {
            reference_actions = actions; reference_logp = logp; reference_values = values;
            reference_masks = actual_masks; reference_rng = rng;
        } else {
            require(equal(actions, reference_actions) && equal(logp, reference_logp)
                && equal(values, reference_values) && equal(actual_masks, reference_masks)
                && equal(rng, reference_rng), "Batched/banked sampler parity FAILED");
        }
        std::sort(elapsed.begin(), elapsed.end());
        printf("  layout=%s sample_ms_median=%.6f min=%.6f max=%.6f exact_parity=%s\n",
            banked ? "learner_plus_8_banks" : "one_batch", (elapsed[5] + elapsed[6]) / 2,
            elapsed.front(), elapsed.back(), banked ? "PASS(actions,masks,logp,values,rng)" : "reference");
        fflush(stdout);
        checked(cudaGraphExecDestroy(exec)); checked(cudaGraphDestroy(graph));
    }
    checked(cudaEventDestroy(begin)); checked(cudaEventDestroy(end)); checked(cudaStreamDestroy(stream));
    checked(cudaFree(d_envs)); checked(cudaFree(d_rows)); checked(cudaFree(d_sizes));
    checked(cudaFree(d_logits)); checked(cudaFree(d_actions)); checked(cudaFree(d_logp));
    checked(cudaFree(d_values)); checked(cudaFree(d_rng)); checked(cudaFree(d_base)); checked(cudaFree(d_masks));
}

int main(int argc, char** argv) {
    require(argc == 2 || argc == 3, "Usage: bench_kag_sampling RESET_BANK [2048|4096]");
    int rows = argc == 3 ? atoi(argv[2]) : 2048;
    require(rows == 2048 || rows == 4096, "Rows must be 2048 or 4096");
    cudaDeviceProp props; checked(cudaGetDeviceProperties(&props, 0));
    printf("GPU=%s rows=%d fixed_logits_seed=17 warmup=3 repeats=12\n", props.name, rows);
    auto fresh = states(rows / 2, nullptr), replay = states(rows / 2, argv[1]);
    for (int executor = 1; executor <= 2; executor++) {
        run_case(fresh, executor, "fresh"); run_case(replay, executor, "replay_reset_bank");
    }
    checked(cudaDeviceReset());
}
