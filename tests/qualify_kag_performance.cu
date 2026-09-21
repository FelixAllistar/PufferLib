// Compile against either source tree. Exercise real forward/rollout and PPO,
// not a second sampler implementation. Writes byte-comparable rollout dumps.
#define ENV_HEADER "ocean/kaggriculture/kaggriculture.h"
#define GPU_ENV_HEADER "ocean/kaggriculture/kaggriculture.cu"
#define PUFFER_GPU_ENV
#define PUFFER_ENV_NAME "kaggriculture"
#include "src/pufferl.cu"
#include <vector>

static void checked(cudaError_t err) {
    if (err != cudaSuccess) { fprintf(stderr, "%s\n", cudaGetErrorString(err)); exit(1); }
}
static void write_bytes(FILE* out, const void* data, size_t bytes) {
    if (fwrite(data, 1, bytes, out) != bytes) { perror("rollout dump"); exit(1); }
}
template<class T> static void dump_tensor(FILE* out, T tensor) {
    size_t bytes = numel(tensor.shape) * sizeof(*tensor.data);
    std::vector<unsigned char> data(bytes);
    checked(cudaMemcpy(data.data(), tensor.data, bytes, cudaMemcpyDeviceToHost));
    write_bytes(out, &bytes, sizeof(bytes)); write_bytes(out, data.data(), bytes);
}
// Philox structs can contain uninitialized padding. Compare all discrete RNG
// counters/output/key fields explicitly, not padding or irrelevant Gaussian caches.
static void dump_rng(FILE* out, const curandStatePhilox4_32_10_t* device, int count) {
    std::vector<curandStatePhilox4_32_10_t> states(count);
    checked(cudaMemcpy(states.data(), device, count * sizeof(states[0]), cudaMemcpyDeviceToHost));
    for (const auto& s : states) {
        write_bytes(out, &s.ctr, sizeof(s.ctr)); write_bytes(out, &s.output, sizeof(s.output));
        write_bytes(out, &s.key, sizeof(s.key)); write_bytes(out, &s.STATE, sizeof(s.STATE));
    }
}
int main(int argc, char** argv) {
    setbuf(stdout, nullptr);
    if (argc < 3) {
        fprintf(stderr, "Usage: qualify_kag_performance train|parity OUTPUT_OR_ENV [section.key=value ...]\n");
        return 1;
    }
    Ini ini = {};
    puf_ini_load_env(&ini, "kaggriculture", argc - 3, argv + 3);
#ifdef PUF_CONFIGURE
    PUF_CONFIGURE(&ini, "train");
#endif
    if (!strcmp(argv[1], "train")) { launch_train(&ini); puf_ini_free(&ini); return 0; }
    assert(!strcmp(argv[1], "parity"));
    assert(!puf_ini_get_int(&ini, "base", "async"));
    TrainContext context = {.world_size = 1, .artifact_owner = 1};
    PuffeRL* p = create_pufferl(&ini, &context);
    FILE* out = fopen(argv[2], "wx");
    if (!out) { perror(argv[2]); return 1; }
    // First capture, graph replay with carried recurrence, separate greedy
    // capture, then stochastic graph reuse after the greedy rollout.
    for (int round = 0; round < 4; round++) {
        bool greedy = round == 2;
        double start = rollout_start(p, 0, greedy); rollout_finish(p, start);
        checked(cudaDeviceSynchronize());
        dump_tensor(out, p->rollouts.observations); dump_tensor(out, p->rollouts.actions);
        dump_tensor(out, p->rollouts.logprobs); dump_tensor(out, p->rollouts.values);
        dump_tensor(out, p->rollouts.action_mask); dump_tensor(out, p->rollouts.rewards);
        dump_tensor(out, p->rollouts.terminals);
        for (int b = 0; b < p->hypers.num_buffers; b++) {
            dump_tensor(out, p->buffer_states[b]);
            dump_rng(out, p->rng_states[b], p->hypers.total_agents / p->hypers.num_buffers);
            for (int bank = 0; bank < p->num_frozen_banks; bank++)
                dump_tensor(out, p->frozen_banks[bank].buffer_states[b]);
        }
        std::vector<Env> envs(g_kag_num_matches);
        checked(cudaMemcpy(envs.data(), d_kag_matches, envs.size() * sizeof(Env), cudaMemcpyDeviceToHost));
        for (const Env& env : envs) write_bytes(out, &env.game_storage, sizeof(KGState));
        printf("rollout_dump round=%d greedy=%d complete\n", round, greedy);
    }
    if (fclose(out)) { perror(argv[2]); return 1; }
    close_pufferl(p); puf_ini_free(&ini);
    return 0;
}
