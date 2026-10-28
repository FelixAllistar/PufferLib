// GPU-batched frozen league (env.league_gpu) vs the inline CPU path.
// 1. Forward parity: batched cuBLAS forward vs poker_forward, mixed banks, fresh resets, many steps.
// 2. Env parity: CPU-league and GPU-league envs with the same seeds and learner actions stay identical
//    (up to rare sampling flips from float rounding); also times both.
// Build: nvcc -O2 -std=c++17 -Xcompiler=-fopenmp,-march=native -Isrc -Iraylib-5.5_linux_amd64/include \
//   ocean/poker/test_league_gpu.cu -lcublas -o build/poker/test_league_gpu
// Usage: build/poker/test_league_gpu [MANIFEST] [ENVS] [STEPS]
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "poker.h"

static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + 1e-9 * t.tv_nsec;
}

// Double-precision reference of poker_forward (same equations), to judge both float paths.
static void forward_double(const PokerLeague* bank, int policy, double* state, const float* obs, double* logits) {
    int h = bank->hidden;
    const float* w = bank->weights + (size_t)policy * bank->stride;
    const float* rec = w + h * (POKER_OBS + POKER_ACTIONS + 1);
    double x[128], p[384];
    for (int o = 0; o < h; o++) { double sum = 0; for (int i = 0; i < POKER_OBS; i++) sum += (double)obs[i] * w[o * POKER_OBS + i]; x[o] = sum; }
    for (int l = 0; l < bank->layers; l++) {
        for (int o = 0; o < 3 * h; o++) { double sum = 0; for (int i = 0; i < h; i++) sum += x[i] * rec[(size_t)l * 3 * h * h + o * h + i]; p[o] = sum; }
        for (int i = 0; i < h; i++) {
            double value = p[i] >= 0 ? p[i] + 0.5 : 1 / (1 + exp(-p[i]));
            double gate = 1 / (1 + exp(-p[h + i])), highway = 1 / (1 + exp(-p[2 * h + i]));
            double sv = state[l * h + i] + gate * (value - state[l * h + i]);
            state[l * h + i] = sv;
            x[i] = highway * sv + (1 - highway) * x[i];
        }
    }
    for (int o = 0; o < POKER_ACTIONS + 1; o++) { double sum = 0; for (int i = 0; i < h; i++) sum += x[i] * w[h * POKER_OBS + o * h + i]; logits[o] = sum; }
}

static void forward_parity(PokerLeague* source) {
    // Two banks: the manifest's first network and a perturbed copy, so rows of both are grouped.
    PokerLeague bank = *source;
    bank.count = 2;
    bank.weights = (float*)malloc(2 * bank.stride * sizeof(float));
    memcpy(bank.weights, source->weights, bank.stride * sizeof(float));
    uint64_t rng = 4242;
    for (int i = 0; i < bank.stride; i++) {
        bank.weights[bank.stride + i] = source->weights[i] * (0.9f + 0.2f * ((poker_random(&rng) >> 8) * 0x1p-24f));
    }
    const int rows = 1500, steps = 40, width = bank.layers * bank.hidden;
    bank.total_envs = rows;  // one slot per row is enough: slot = row * 5 (seat 1)
    bank.device_weights = bank.device_state = NULL;
    poker_league_gpu_bank(&bank);
    PokerLeagueGPU* c = poker_league_gpu_context(rows, bank.hidden);
    float* cpu_state = (float*)calloc((size_t)rows * width, sizeof(float));
    double* ref_state = (double*)calloc((size_t)rows * width, sizeof(double));
    float* gpu_state = (float*)malloc((size_t)bank.total_envs * (POKER_PLAYERS - 1) * width * sizeof(float));
    int* policy = (int*)malloc(rows * sizeof(int));
    double cpu_err = 0, gpu_err = 0;  // max over rows/steps of |path - double| / row scale
    for (int step = 0; step < steps; step++) {
        int count[16] = {0}, start[16], cursor[16];
        for (int r = 0; r < rows; r++) count[policy[r] = poker_random(&rng) % 2]++;
        start[0] = cursor[0] = 0; start[1] = cursor[1] = count[0];
        for (int r = 0; r < rows; r++) c->order[cursor[policy[r]]++] = r;
        float* obs = (float*)malloc((size_t)rows * POKER_OBS * sizeof(float));
        for (int r = 0; r < rows; r++) {
            for (int i = 0; i < POKER_OBS; i++) obs[r * POKER_OBS + i] = (poker_random(&rng) % 20000) / 20000.0f;
            int fresh = step == 0 || poker_random(&rng) % 11 == 0;
            if (fresh) memset(cpu_state + (size_t)r * width, 0, width * sizeof(float));
            if (fresh) memset(ref_state + (size_t)r * width, 0, width * sizeof(double));
            int k = -1;
            for (int j = 0; j < rows; j++) if (c->order[j] == r) { k = j; break; }
            memcpy(c->host_obs + (size_t)k * POKER_OBS, obs + (size_t)r * POKER_OBS, POKER_OBS * sizeof(float));
            c->host_slot[k] = r * (POKER_PLAYERS - 1);
            c->host_fresh[k] = fresh;
        }
        poker_league_gpu_forward(&bank, c, rows, start, count, bank.count);
        POKER_CUDA(cudaMemcpy(gpu_state, bank.device_state, (size_t)bank.total_envs * (POKER_PLAYERS - 1) * width * sizeof(float), cudaMemcpyDeviceToHost));
        for (int k = 0; k < rows; k++) {
            int r = c->order[k];
            float logits[POKER_ACTIONS + 1];
            double ref[POKER_ACTIONS + 1];
            poker_forward(&bank, policy[r], cpu_state + (size_t)r * width, obs + (size_t)r * POKER_OBS, logits);
            forward_double(&bank, policy[r], ref_state + (size_t)r * width, obs + (size_t)r * POKER_OBS, ref);
            // Normalized by each row's logit/state scale: random observations push this network's logits to
            // about +-1000 and its recurrent state to hundreds, so absolute float error scales with them.
            double ls = 1, ss = 1;
            for (int a = 0; a < POKER_ACTIONS + 1; a++) ls = fmax(ls, fabs(ref[a]));
            for (int i = 0; i < width; i++) ss = fmax(ss, fabs(ref_state[(size_t)r * width + i]));
            for (int a = 0; a < POKER_ACTIONS + 1; a++) {
                cpu_err = fmax(cpu_err, fabs(logits[a] - ref[a]) / ls);
                gpu_err = fmax(gpu_err, fabs(c->host_logits[(size_t)k * (POKER_ACTIONS + 1) + a] - ref[a]) / ls);
            }
            for (int i = 0; i < width; i++) {
                double d = ref_state[(size_t)r * width + i];
                cpu_err = fmax(cpu_err, fabs(cpu_state[(size_t)r * width + i] - d) / ss);
                gpu_err = fmax(gpu_err, fabs(gpu_state[(size_t)r * (POKER_PLAYERS - 1) * width + i] - d) / ss);
            }
        }
        free(obs);
    }
    printf("forward parity: %d rows x %d steps, H%d/L%d, 2 banks: max error vs double, CPU %.2e, GPU %.2e (relative to row scale)\n",
        rows, steps, bank.hidden, bank.layers, cpu_err, gpu_err);
    assert(gpu_err <= fmax(2 * cpu_err, 1e-5));
    poker_league_gpu_free(&bank);
    free(bank.weights); free(cpu_state); free(ref_state); free(gpu_state); free(policy);
}

typedef struct {
    Env* envs;
    float *obs, *actions, *rewards, *terminals;
    uint8_t* masks;
} Run;

static Run make_run(const char* manifest, int envs, int gpu) {
    Dict vk = {0}, ek = {0};
    dict_set(&vk, "total_agents", envs);
    dict_set(&vk, "num_buffers", 2);
    dict_set(&vk, "num_policies", 1);
    dict_set(&ek, "num_players", POKER_PLAYERS);
    dict_set(&ek, "starting_stack", 20000);
    dict_set(&ek, "bot_policy", 7);
    dict_set(&ek, "native_baseline_mask", 0);
    dict_set(&ek, "native_history", 0);
    dict_set(&ek, "equity_samples", 0);
    dict_set(&ek, "seed", 73);
    dict_set(&ek, "rules_fraction", 0.25);
    dict_set(&ek, "league_gpu", gpu);
    dict_set_str(&ek, "native_league", manifest);
    int size, starts[2], counts[2];
    Run run;
    run.envs = my_vec_init(&size, starts, counts, &vk, &ek);
    assert(size == envs);
    run.obs = (float*)calloc((size_t)envs * OBS_SIZE, sizeof(float));
    run.actions = (float*)calloc(envs, sizeof(float));
    run.rewards = (float*)calloc(envs, sizeof(float));
    run.terminals = (float*)calloc(envs, sizeof(float));
    run.masks = (uint8_t*)calloc((size_t)envs * POKER_ACTIONS, 1);
    for (int i = 0; i < envs; i++) {
        assert(run.envs[i].league_gpu == gpu);
        run.envs[i].agents[0] = (Agent){run.obs + (size_t)i * OBS_SIZE, run.actions + i, run.rewards + i,
            run.terminals + i, run.masks + (size_t)i * POKER_ACTIONS, 0};
        puf_reset(run.envs + i);
    }
    poker_league_flush(run.envs, envs);
    dict_clear(&vk); dict_clear(&ek);
    return run;
}

static double step_run(Run* run, int envs, const int* actions) {
    double t0 = seconds();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < envs; i++) {
        run->actions[i] = actions[i];
        puf_step(run->envs + i);
    }
    poker_league_flush(run->envs, envs);
    return seconds() - t0;
}

int main(int argc, char** argv) {
    const char* manifest = argc > 1 ? argv[1] : "leagues/poker/gen0/opponents.txt";
    int envs = argc > 2 ? atoi(argv[2]) : 2048, steps = argc > 3 ? atoi(argv[3]) : 300;
    Dict ek = {0};
    dict_set_str(&ek, "native_league", manifest);
    PokerLeague* source = poker_load_league(&ek);
    assert(source);
    dict_clear(&ek);
    forward_parity(source);

    Run cpu = make_run(manifest, envs, 0), gpu = make_run(manifest, envs, 1);
    int *cpu_actions = (int*)malloc(envs * sizeof(int)), *gpu_actions = (int*)malloc(envs * sizeof(int));
    uint8_t* diverged = (uint8_t*)calloc(envs, 1);
    uint64_t rng = 77;
    double cpu_time = 0, gpu_time = 0;
    long hands = 0;
    int first_divergence = -1;
    for (int step = 0; step < steps; step++) {
        for (int i = 0; i < envs; i++) {
            // Identical envs share one random legal action; diverged ones draw separately.
            const uint8_t *cm = cpu.masks + (size_t)i * POKER_ACTIONS, *gm = gpu.masks + (size_t)i * POKER_ACTIONS;
            int a;
            do { a = poker_random(&rng) % POKER_ACTIONS; } while (!cm[a]);
            cpu_actions[i] = a;
            if (diverged[i]) do { a = poker_random(&rng) % POKER_ACTIONS; } while (!gm[a]);
            gpu_actions[i] = a;
        }
        cpu_time += step_run(&cpu, envs, cpu_actions);
        gpu_time += step_run(&gpu, envs, gpu_actions);
        for (int i = 0; i < envs; i++) {
            hands += gpu.terminals[i] != 0;
            if (diverged[i]) continue;
            if (memcmp(cpu.obs + (size_t)i * OBS_SIZE, gpu.obs + (size_t)i * OBS_SIZE, OBS_SIZE * sizeof(float))
                    || memcmp(cpu.masks + (size_t)i * POKER_ACTIONS, gpu.masks + (size_t)i * POKER_ACTIONS, POKER_ACTIONS)
                    || cpu.rewards[i] != gpu.rewards[i] || cpu.terminals[i] != gpu.terminals[i]) {
                diverged[i] = 1;
                if (first_divergence < 0) first_divergence = step;
            }
        }
    }
    int same = 0;
    for (int i = 0; i < envs; i++) same += !diverged[i];
    double decisions = (double)envs * steps;
    printf("env parity: %d envs x %d learner steps, %ld hands: %d envs (%.2f%%) identical throughout, first divergence at step %d\n",
        envs, steps, hands, same, 100.0 * same / envs, first_divergence);
    printf("speed: CPU league %.0f learner steps/s, GPU league %.0f learner steps/s (%.1fx)\n",
        decisions / cpu_time, decisions / gpu_time, cpu_time / gpu_time);
    assert(same >= envs * 0.95);
    printf("PASS GPU league\n");
    return 0;
}
