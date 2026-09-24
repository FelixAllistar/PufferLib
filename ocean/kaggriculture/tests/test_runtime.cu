// Compile this unchanged against the reference and candidate trainers.
// Compare full rollout data, discrete RNG counters, recurrent carry and updates.
#include "../../../src/pufferl.cu"

void dump_device(FILE* out, const void* pointer, size_t bytes) {
    void* host = malloc(bytes);
    assert(cudaMemcpy(host, pointer, bytes, cudaMemcpyDeviceToHost) == cudaSuccess);
    assert(fwrite(&bytes, sizeof(bytes), 1, out) == 1);
    assert(fwrite(host, 1, bytes, out) == bytes);
    free(host);
}

void dump_precision(FILE* out, Prec tensor) {
    dump_device(out, tensor.data, numel(tensor.shape) * sizeof(precision_t));
}

int main(int argc, char** argv) {
    assert(argc >= 3);
    setbuf(stdout, NULL);
    Ini ini = {};
    puf_ini_load_env(&ini, "kaggriculture", argc - 3, argv + 3);
    if (!strcmp(argv[1], "train")) {
        launch_train(&ini);
        return 0;
    }
    assert(!strcmp(argv[1], "dump"));
    TrainContext context = {.world_size = 1, .artifact_owner = 1};
    PuffeRL* p = create_pufferl(&ini, &context);
    const char* load = puf_ini_get_str(&ini, "base", "load_model_path");
    if (strcmp(load, "None")) {
        pufferl_load_policy(p, 0, load);
    }
    const char* manifest = puf_ini_get_str(&ini, "selfplay", "initial_opponents");
    FILE* opponents = strcmp(manifest, "None") ? fopen(manifest, "r") : NULL;
    assert(!strcmp(manifest, "None") || opponents);
    for (int b = 1; b < p->num_policies; b++) {
        if (opponents) {
            char path[4096];
            assert(fgets(path, sizeof(path), opponents));
            path[strcspn(path, "\r\n")] = 0;
            pufferl_load_policy(p, b, path);
        } else {
            unsigned long seed = 123 + b;
            Policy* policy = p->policies + b;
            weights_init(&policy->arch, policy->weights, &seed, p->default_stream);
        }
    }
    if (opponents) {
        fclose(opponents);
    }
    assert(cudaDeviceSynchronize() == cudaSuccess);
    FILE* out = fopen(argv[2], "wbx");
    assert(out);
    for (int round = 0; round < 4; round++) {
        int slot = p->hypers.async ? round % 2 : 0;
        double start = rollout_start(p, slot);
        rollout_finish(p, start);
        RolloutBuf r = rollout_time_view(&p->rollouts,
            slot * p->hypers.horizon, p->hypers.horizon);
        dump_precision(out, r.observations);
        dump_device(out, r.actions.data, numel(r.actions.shape) * sizeof(float));
        dump_precision(out, r.logprobs);
        dump_precision(out, r.values);
        dump_precision(out, r.rewards);
        dump_precision(out, r.terminals);
#if PUF_PACKED_MASK
        long rows = r.action_mask.shape[0] * r.action_mask.shape[1];
        int width = p->env.action_mask.shape[1];
        unsigned char* bits = (unsigned char*)malloc(numel(r.action_mask.shape));
        assert(cudaMemcpy(bits, r.action_mask.data, numel(r.action_mask.shape),
            cudaMemcpyDeviceToHost) == cudaSuccess);
        size_t bytes = rows * width * sizeof(precision_t);
        precision_t* dense = (precision_t*)malloc(bytes);
        for (long row = 0; row < rows; row++) {
            for (int j = 0; j < width; j++) {
                dense[row * width + j] = from_float(
                    (bits[row * r.action_mask.shape[2] + j / 8] >> (j % 8)) & 1);
            }
        }
        assert(fwrite(&bytes, sizeof(bytes), 1, out) == 1);
        assert(fwrite(dense, 1, bytes, out) == bytes);
        free(dense);
        free(bits);
#else
        dump_precision(out, r.action_mask);
#endif
        for (int b = 0; b < p->num_policies; b++) {
            for (int buf = 0; buf < p->hypers.num_buffers; buf++) {
                dump_precision(out, p->policies[b].buffer_states[buf]);
            }
        }
        int count = p->vec->agents_per_buf;
        curandStatePhilox4_32_10_t* rng =
            (curandStatePhilox4_32_10_t*)malloc(count * sizeof(*rng));
        for (int buf = 0; buf < p->hypers.num_buffers; buf++) {
            assert(cudaMemcpy(rng, p->rng_states[buf], count * sizeof(*rng),
                cudaMemcpyDeviceToHost) == cudaSuccess);
            for (int row = 0; row < count; row++) {
                assert(fwrite(&rng[row].ctr, sizeof(rng[row].ctr), 1, out) == 1);
                assert(fwrite(&rng[row].output, sizeof(rng[row].output), 1, out) == 1);
                assert(fwrite(&rng[row].key, sizeof(rng[row].key), 1, out) == 1);
                assert(fwrite(&rng[row].STATE, sizeof(rng[row].STATE), 1, out) == 1);
            }
        }
        free(rng);
        p->async_ready_slot = slot;
        train_impl(p, &r);
        for (int b = 0; b < p->num_policies; b++) {
            dump_precision(out, p->policies[b].param);
        }
        printf("runtime dump round=%d slot=%d complete\n", round, slot);
    }
    assert(!fclose(out));
    close_pufferl(p);
    puf_ini_free(&ini);
}
