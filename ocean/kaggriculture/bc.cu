// Offline actor CE / expert-return regression using the actual upstream network.
// No simulator, PPO loss, optimizer state or training reset bank is instantiated.
#include "../../src/pufferl.cu"
#include <sys/mman.h>
#include <sys/stat.h>

struct KagBCHeader {
    uint32_t magic, version, count, row_obs, row_expert, row_mask, games, steps;
    uint32_t obs_bytes, observation_version, policy_version, macro_mode;
    uint32_t executor, interval, score_features, validation_games;
    uint64_t source_hash, semantics_hash;
    double gamma;
};
static_assert(sizeof(KagBCHeader) == 88, "BC v3 header ABI");

__global__ void kag_bc_loss(Prec output, float* expert, unsigned char* masks, float* targets,
    Float actor_gradient, Float value_gradient, float* stats, float actor_coef, float value_coef,
    float variance, int labeled, int valued) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= output.shape[0]) {
        return;
    }
    int sizes[] = ACT_SIZES;
    int stride = (KAG_ALL_LOGITS + 7) / 8, offset = 0;
    float loss = 0;
    int active = 0, correct = 0;
    for (int h = 0; h < NUM_ATNS; h++) {
        float label = expert[row * NUM_ATNS + h];
        int action = isfinite(label) ? (int)label : -1;
        if (action >= 0 && kag_action_head_active(expert + row * NUM_ATNS, h)) {
            assert(action < sizes[h]);
            assert(masks[row * stride + (offset + action) / 8] & (1 << ((offset + action) % 8)));
            float maximum = -INFINITY;
            int prediction = 0;
            for (int a = 0; a < sizes[h]; a++) {
                if (masks[row * stride + (offset + a) / 8] & (1 << ((offset + a) % 8))) {
                    float logit = to_float(output.data[row * (KAG_ALL_LOGITS + 1) + offset + a]);
                    if (logit > maximum) {
                        maximum = logit;
                        prediction = a;
                    }
                }
            }
            float sum = 0;
            for (int a = 0; a < sizes[h]; a++) {
                if (masks[row * stride + (offset + a) / 8] & (1 << ((offset + a) % 8))) {
                    sum += expf(
                        to_float(output.data[row * (KAG_ALL_LOGITS + 1) + offset + a]) - maximum);
                }
            }
            float lse = maximum + logf(sum);
            loss += lse - to_float(output.data[row * (KAG_ALL_LOGITS + 1) + offset + action]);
            for (int a = 0; a < sizes[h]; a++) {
                if (masks[row * stride + (offset + a) / 8] & (1 << ((offset + a) % 8))) {
                    float probability =
                        expf(to_float(output.data[row * (KAG_ALL_LOGITS + 1) + offset + a]) - lse);
                    actor_gradient.data[row * KAG_ALL_LOGITS + offset + a] =
                        actor_coef * (probability - (a == action)) / fmaxf(1, labeled);
                }
            }
            active++;
            correct += prediction == action;
        }
        offset += sizes[h];
    }
    atomicAdd(stats, loss);
    atomicAdd(stats + 1, active);
    atomicAdd(stats + 2, correct);
    if (isfinite(targets[row])) {
        float error =
            to_float(output.data[row * (KAG_ALL_LOGITS + 1) + KAG_ALL_LOGITS]) - targets[row];
        value_gradient.data[row] = value_coef * error / (variance * fmaxf(1, valued));
        atomicAdd(stats + 3, error * error);
        atomicAdd(stats + 4, 1);
    }
}

// Standalone offline Adam. PPO continues to use the untouched upstream Muon.
__global__ void kag_bc_update(Prec parameters, Float master, Prec gradient, Float first,
    Float second, float lr, int update, int begin, int end, int critic_only, int actor_only) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    bool critic = i >= begin && i < end;
    if (i >= master.shape[0] || (critic_only && !critic) || (actor_only && critic)) {
        return;
    }
    float g = to_float(gradient.data[i]);
    assert(isfinite(g));
    float m = first.data[i] = 0.9f * first.data[i] + 0.1f * g;
    float v = second.data[i] = 0.999f * second.data[i] + 0.001f * g * g;
    float delta = (m / (1 - powf(0.9f, update))) / (sqrtf(v / (1 - powf(0.999f, update))) + 1e-8f);
    master.data[i] -= lr * delta;
    assert(isfinite(master.data[i]));
    parameters.data[i] = from_float(master.data[i]);
}

int main(int argc, char** argv) {
    Ini ini = {};
    puf_ini_load_env(&ini, "kaggriculture", argc - 1, argv + 1);
    const char* data_path = puf_ini_get_str(&ini, "bc", "data");
    const char* output_path = puf_ini_get_str(&ini, "bc", "output");
    const char* mode = puf_ini_get_str(&ini, "bc", "mode");
    int critic_only = !strcmp(mode, "critic"), actor_only = !strcmp(mode, "actor");
    assert(critic_only || actor_only || !strcmp(mode, "joint"));
    FILE* file = fopen(data_path, "rb");
    assert(file);
    struct stat info;
    assert(fstat(fileno(file), &info) == 0 && info.st_size >= sizeof(KagBCHeader));
    char* mapped = (char*)mmap(NULL, info.st_size, PROT_READ, MAP_PRIVATE, fileno(file), 0);
    assert(mapped != MAP_FAILED);
    KagBCHeader h = *(KagBCHeader*)mapped;
    assert(h.magic == 0x4b414742u && h.version == 3 && h.obs_bytes == sizeof(float));
    assert(h.row_obs == OBS_SIZE && h.row_expert == NUM_ATNS &&
           h.row_mask == (KAG_ALL_LOGITS + 7) / 8);
    assert(h.observation_version == 3 && h.policy_version == 5 && h.macro_mode == 2 &&
           h.executor == 2 && h.interval == 1 && h.score_features == 0);
    assert(h.steps == 720 && h.count == h.games * h.steps);
    assert(h.validation_games > 0 && h.validation_games < h.games);
    assert(actor_only || fabs(h.gamma - puf_ini_get(&ini, "train", "gamma")) < 1e-8);
    int packed = (KAG_ALL_LOGITS + 7) / 8;
    size_t count = h.count;
    assert(
        info.st_size == sizeof(h) + count * ((OBS_SIZE + NUM_ATNS + 1) * sizeof(float) + packed));
    float* observations = (float*)(mapped + sizeof(h));
    float* expert = observations + count * OBS_SIZE;
    unsigned char* masks = (unsigned char*)(expert + count * NUM_ATNS);
    float* targets = (float*)(masks + count * packed);
    int train_games = h.games - h.validation_games;
    double mean = 0, m2 = 0;
    int valid = 0;
    for (int i = 0; i < train_games * h.steps; i++) {
        if (isfinite(targets[i])) {
            double delta = targets[i] - mean;
            mean += delta / ++valid;
            m2 += delta * (targets[i] - mean);
        }
    }
    assert(valid > 1);
    float variance = fmax(1, m2 / valid);
    int batch = puf_ini_get(&ini, "bc", "batch");
    int epochs = puf_ini_get(&ini, "bc", "epochs");
    int max_batches = puf_ini_get(&ini, "bc", "max_batches");
    float lr = puf_ini_get(&ini, "bc", "learning_rate");
    float value_coef = actor_only ? 0 : puf_ini_get(&ini, "bc", "value_coef");
    assert(batch > 0 && epochs >= 0 && max_batches >= 0 && lr >= 0 && value_coef >= 0);
    assert(access(output_path, F_OK) != 0 && "refusing to overwrite an offline checkpoint");
    printf("BC data: %d train / %u held-out games; %s; gamma=%.9g mean=%.6g variance=%.6g\n",
        train_games, h.validation_games, mode, h.gamma, mean, variance);
    printf("BC optimizer=offline Adam lr=%g batch=%d epochs=%d max_batches=%d (0=all)\n", lr, batch,
        epochs, max_batches);
    int hidden = puf_ini_get(&ini, "policy", "hidden_size");
    int layers = puf_ini_get(&ini, "policy", "num_layers");
    uint64_t seed = puf_ini_get(&ini, "base", "seed");
    cublas_init_handle();
    cudaStream_t stream;
    assert(cudaStreamCreate(&stream) == cudaSuccess);
    Arch arch = build_arch(OBS_SIZE, hidden, layers, KAG_ALL_LOGITS, false, h.steps);
    Allocator params = {}, grads = {}, acts = {};
    Weights weights = weights_create(&arch, &params);
    int rows = batch * h.steps;
    Activations activation = arch_reg_train(&arch, weights, &acts, &grads, rows);
    Prec state = {.shape = {layers, batch, hidden}}, terminals = {.shape = {batch, h.steps}};
    Prec obs = {.shape = {batch, h.steps, OBS_SIZE}};
    Float raw = {.shape = {rows, OBS_SIZE}}, labels = {.shape = {rows, NUM_ATNS}};
    Float returns = {.shape = {rows}}, stats = {.shape = {5}};
    Float actor_grad = {.shape = {batch, h.steps, KAG_ALL_LOGITS}};
    Float value_grad = {.shape = {batch, h.steps}};
    Float first = {.shape = {params.total_elems}}, second = first;
    alloc_register(&acts, &state);
    alloc_register(&acts, &terminals);
    alloc_register(&acts, &obs);
    Float* floats[] = {&raw, &labels, &returns, &stats, &actor_grad, &value_grad, &first, &second};
    for (int i = 0; i < sizeof(floats) / sizeof(*floats); i++) {
        alloc_register(&acts, floats[i]);
    }
    alloc_create(&params);
    alloc_create(&grads);
    alloc_create(&acts);
    assert(params.total_elems == grads.total_elems && params.total_bytes == grads.total_bytes);
    assert(params.total_bytes == params.total_elems * sizeof(precision_t));
    Prec parameters = {.data = (precision_t*)params.mem, .shape = {params.total_elems}};
    Prec gradient = {.data = (precision_t*)grads.mem, .shape = {grads.total_elems}};
    weights_init(&arch, weights, &seed, stream);
    Float master;
    master_weights_setup(&master, &parameters, true, stream);
    char checkpoint[4096];
    const char* load =
        puf_checkpoint_path_key(&ini, "load_model_path", checkpoint, sizeof(checkpoint));
    if (load) {
        puf_load_weights_into(master, parameters, stream, load);
    }
    KagMLPWeights* critic = &((KagDecoderWeights*)weights.decoder)->branch[2];
    int begin = critic->w1.data - parameters.data;
    int end = critic->w2.data - parameters.data + numel(critic->w2.shape);
    printf("critic_range=%d:%d frozen_encoder_actor=%d\n", begin, end, critic_only);
    unsigned char* device_masks;
    assert(cudaMalloc((void**)&device_masks, rows * packed) == cudaSuccess);
    int* order = (int*)malloc(h.games * sizeof(int));
    for (int i = 0; i < h.games; i++) {
        order[i] = i;
    }
    int updates = 0;
    for (int epoch = 0; epoch <= epochs; epoch++) {
        if (epoch) {
            for (int i = train_games - 1; i > 0; i--) {
                seed = seed * 6364136223846793005ULL + 1;
                int j = (seed >> 32) % (i + 1), temp = order[i];
                order[i] = order[j];
                order[j] = temp;
            }
        }
        for (int split = epoch ? 0 : 1; split < 2; split++) {
            int start = split ? train_games : 0, stop = split ? h.games : train_games;
            float total[5] = {0};
            int batches = 0;
            for (int first_game = start; first_game < stop; first_game += batch) {
                if (max_batches && batches == max_batches) {
                    break;
                }
                cudaMemsetAsync(raw.data, 0, rows * OBS_SIZE * sizeof(float), stream);
                cudaMemsetAsync(labels.data, 0xff, rows * NUM_ATNS * sizeof(float), stream);
                cudaMemsetAsync(returns.data, 0xff, rows * sizeof(float), stream);
                cudaMemsetAsync(device_masks, 0, rows * packed, stream);
                int labeled = 0, valued = 0;
                for (int b = 0; b < batch && first_game + b < stop; b++) {
                    size_t source = (size_t)order[first_game + b] * h.steps, dest = b * h.steps;
                    cudaMemcpyAsync(raw.data + dest * OBS_SIZE, observations + source * OBS_SIZE,
                        h.steps * OBS_SIZE * sizeof(float), cudaMemcpyHostToDevice, stream);
                    cudaMemcpyAsync(labels.data + dest * NUM_ATNS, expert + source * NUM_ATNS,
                        h.steps * NUM_ATNS * sizeof(float), cudaMemcpyHostToDevice, stream);
                    cudaMemcpyAsync(returns.data + dest, targets + source, h.steps * sizeof(float),
                        cudaMemcpyHostToDevice, stream);
                    cudaMemcpyAsync(device_masks + dest * packed, masks + source * packed,
                        h.steps * packed, cudaMemcpyHostToDevice, stream);
                    for (int t = 0; t < h.steps; t++) {
                        int any = 0;
                        for (int head = 0; head < NUM_ATNS; head++) {
                            any |= expert[(source + t) * NUM_ATNS + head] >= 0;
                        }
                        labeled += any;
                        valued += isfinite(targets[source + t]);
                    }
                }
                cast<<<grid_size(rows * OBS_SIZE), BLOCK_SIZE, 0, stream>>>(
                    obs.data, raw.data, rows * OBS_SIZE);
                cudaMemsetAsync(state.data, 0, numel(state.shape) * sizeof(precision_t), stream);
                cudaMemsetAsync(actor_grad.data, 0, rows * KAG_ALL_LOGITS * sizeof(float), stream);
                cudaMemsetAsync(value_grad.data, 0, rows * sizeof(float), stream);
                cudaMemsetAsync(stats.data, 0, 5 * sizeof(float), stream);
                Prec flat = {.data = obs.data, .shape = {rows, OBS_SIZE}};
                Prec encoded =
                    arch.encoder.forward(weights.encoder, activation.encoder, flat, stream);
                Prec recurrent = arch.network.forward_train(weights.network,
                    *puf_unsqueeze(&encoded, 0, batch, h.steps), state, terminals,
                    activation.network, 0, stream);
                Prec decoded = arch.decoder.forward(
                    weights.decoder, activation.decoder, *puf_squeeze(&recurrent, 0), stream);
                kag_bc_loss<<<grid_size(rows), BLOCK_SIZE, 0, stream>>>(decoded, labels.data,
                    device_masks, returns.data, actor_grad, value_grad, stats.data,
                    critic_only ? 0 : 1, value_coef, variance, labeled, valued);
                if (!split) {
                    if (critic_only) {
                        Float ga = actor_grad, gv = value_grad;
                        arch.decoder.backward(weights.decoder, activation.decoder,
                            *puf_squeeze(&ga, 0), {}, *puf_squeeze(&gv, 0), stream);
                        puf_dw_join(stream);
                    } else {
                        arch_backward(
                            &arch, weights, activation, actor_grad, {}, value_grad, stream);
                    }
                    kag_bc_update<<<grid_size(params.total_elems), BLOCK_SIZE, 0, stream>>>(
                        parameters, master, gradient, first, second, lr, ++updates, begin, end,
                        critic_only, actor_only);
                }
                assert(cudaStreamSynchronize(stream) == cudaSuccess);
                float chunk[5];
                cudaMemcpy(chunk, stats.data, sizeof(chunk), cudaMemcpyDeviceToHost);
                for (int i = 0; i < 5; i++) {
                    assert(isfinite(chunk[i]));
                    total[i] += chunk[i];
                }
                batches++;
            }
            printf("BC epoch=%d split=%s batches=%d ce=%.6g accuracy=%.6g value_rmse=%.6g\n", epoch,
                split ? "holdout" : "train", batches, total[0] / fmaxf(1, total[1]),
                total[2] / fmaxf(1, total[1]), sqrtf(total[3] / fmaxf(1, total[4])));
            fflush(stdout);
        }
    }
    float* result = (float*)malloc(params.total_elems * sizeof(float));
    cudaMemcpy(result, master.data, params.total_elems * sizeof(float), cudaMemcpyDeviceToHost);
    FILE* out = fopen(output_path, "wbx");
    assert(out && fwrite(result, sizeof(float), params.total_elems, out) == params.total_elems);
    assert(fclose(out) == 0);
    printf("BC saved %s (%ld parameters, %d updates)\n", output_path, params.total_elems, updates);
    munmap(mapped, info.st_size);
    fclose(file);
    return 0;
}
