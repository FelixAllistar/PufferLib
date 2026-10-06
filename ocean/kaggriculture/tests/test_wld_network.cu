// GPU forward/backward qualification only. No PPO or optimizer updates.
#include "../../../src/pufferl.cu"

void write_tensor(const char* directory, const char* name, Prec tensor) {
    int count = numel(tensor.shape);
    precision_t* raw = (precision_t*)malloc(count * sizeof(precision_t));
    assert(cudaMemcpy(raw, tensor.data, count * sizeof(precision_t), cudaMemcpyDeviceToHost) == cudaSuccess);
    float* values = (float*)malloc(count * sizeof(float));
    for (int i = 0; i < count; i++) values[i] = to_float(raw[i]);
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s.f32", directory, name);
    FILE* file = fopen(path, "wbx");
    assert(file && fwrite(values, sizeof(float), count, file) == (size_t)count);
    fclose(file);
    free(raw);
    free(values);
}

void test_encoder_stride(cudaStream_t stream) {
    Arch arch = build_arch(OBS_SIZE, 256, 2, KAG_ALL_LOGITS, false, 16);
    void* weights = arch.encoder.create_weights(&arch.encoder);
    Allocator params = {}, activations = {};
    KagEncoderActs narrow_acts = {}, wide_acts = {};
    const int rows = 5;
    arch.encoder.reg_params(weights, &params);
    arch.encoder.reg_rollout(weights, &narrow_acts, &activations, rows);
    arch.encoder.reg_rollout(weights, &wide_acts, &activations, rows);
    Prec narrow = {.shape = {rows, KAG_ENTITY_OBS_SIZE}};
    Prec wide = {.shape = {rows, KAG_TRAIN_OBS_SIZE}};
    alloc_register(&activations, &narrow);
    alloc_register(&activations, &wide);
    alloc_create(&params);
    alloc_create(&activations);
    ulong seed = 73;
    arch.encoder.init_weights(weights, &seed, stream);
    precision_t first[rows * KAG_ENTITY_OBS_SIZE], second[rows * KAG_TRAIN_OBS_SIZE];
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < KAG_TRAIN_OBS_SIZE; c++) {
            second[r * KAG_TRAIN_OBS_SIZE + c] = from_float(((r * 23 + c * 13) % 101 - 50) / 64.0f);
            if (c < KAG_ENTITY_OBS_SIZE) first[r * KAG_ENTITY_OBS_SIZE + c] = second[r * KAG_TRAIN_OBS_SIZE + c];
        }
    }
    assert(cudaMemcpyAsync(narrow.data, first, sizeof(first), cudaMemcpyHostToDevice, stream) == cudaSuccess);
    assert(cudaMemcpyAsync(wide.data, second, sizeof(second), cudaMemcpyHostToDevice, stream) == cudaSuccess);
    Prec a = arch.encoder.forward(weights, &narrow_acts, narrow, stream);
    Prec b = arch.encoder.forward(weights, &wide_acts, wide, stream);
    precision_t first_out[rows * 256], second_out[rows * 256];
    assert(cudaMemcpyAsync(first_out, a.data, sizeof(first_out), cudaMemcpyDeviceToHost, stream) == cudaSuccess);
    assert(cudaMemcpyAsync(second_out, b.data, sizeof(second_out), cudaMemcpyDeviceToHost, stream) == cudaSuccess);
    assert(cudaStreamSynchronize(stream) == cudaSuccess);
    assert(memcmp(first_out, second_out, sizeof(first_out)) == 0);
    printf("actor encoder 1424/1680 observation stride: byte-identical PASS\n");
    cudaFree(params.mem);
    cudaFree(activations.mem);
}

int main(int argc, char** argv) {
    assert(argc == 4);
    int mode = atoi(argv[1]), graphs = atoi(argv[2]);
    assert(mode >= 0 && mode <= 2);
    kag_critic_mode = mode;
    cublas_init_handle();
    Arch arch = build_arch(OBS_SIZE, 256, 2, KAG_ALL_LOGITS, false, 16);
    void* weights = arch.decoder.create_weights(&arch.decoder);
    Allocator params = {}, gradients = {}, activations = {};
    arch.decoder.reg_params(weights, &params);
    KagDecoderActs acts = {};
    const int rows = 5;
    arch.decoder.reg_train(weights, &acts, &activations, &gradients, rows);
    Prec hidden = {.shape = {rows, 256}}, observation = {.shape = {rows, OBS_SIZE}};
    Float gl = {.shape = {rows, KAG_ALL_LOGITS}}, gv = {.shape = {rows, 1}};
    alloc_register(&activations, &hidden);
    alloc_register(&activations, &observation);
    alloc_register(&activations, &gl);
    alloc_register(&activations, &gv);
    alloc_create(&params);
    alloc_create(&gradients);
    alloc_create(&activations);
    assert(params.total_elems == gradients.total_elems);
    ulong seed = 73;
    cudaStream_t stream;
    assert(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess);
    test_encoder_stride(stream);
    arch.decoder.init_weights(weights, &seed, stream);
    precision_t input[rows * 256], obs[rows * OBS_SIZE];
    float logit_grad[rows * KAG_ALL_LOGITS] = {0}, value_grad[rows] = {1, -.5f, .25f, 1, -.75f};
    for (int i = 0; i < rows * 256; i++) input[i] = from_float(((i * 13) % 37 - 18) / 32.0f);
    memset(obs, 0, sizeof(obs));
    for (int row = 0; row < rows; row++) {
        for (int feature = 0; feature < 2 * KAG_CRITIC_FEATURES; feature++) {
            obs[row * OBS_SIZE + KAG_ENTITY_OBS_SIZE + feature] = from_float(
                ((feature * 17 + row * 19) % 101 - 50) / 64.0f);
        }
    }
    // Swapped pairs need not be adjacent, and the row count need not be even.
    for (int feature = 0; feature < KAG_CRITIC_FEATURES; feature++) {
        obs[4 * OBS_SIZE + KAG_ENTITY_OBS_SIZE + feature] = obs[KAG_ENTITY_OBS_SIZE + KAG_CRITIC_FEATURES + feature];
        obs[4 * OBS_SIZE + KAG_ENTITY_OBS_SIZE + KAG_CRITIC_FEATURES + feature] = obs[KAG_ENTITY_OBS_SIZE + feature];
        obs[3 * OBS_SIZE + KAG_ENTITY_OBS_SIZE + KAG_CRITIC_FEATURES + feature] = obs[3 * OBS_SIZE + KAG_ENTITY_OBS_SIZE + feature];
    }
    assert(cudaMemcpy(hidden.data, input, sizeof(input), cudaMemcpyHostToDevice) == cudaSuccess);
    assert(cudaMemcpy(observation.data, obs, sizeof(obs), cudaMemcpyHostToDevice) == cudaSuccess);
    assert(cudaMemcpy(gl.data, logit_grad, sizeof(logit_grad), cudaMemcpyHostToDevice) == cudaSuccess);
    assert(cudaMemcpy(gv.data, value_grad, sizeof(value_grad), cudaMemcpyHostToDevice) == cudaSuccess);
    arch.decoder.bind_observation(&acts, observation);
    if (graphs) assert(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) == cudaSuccess);
    Prec decoded = arch.decoder.forward(weights, &acts, hidden, stream);
    Prec grad_hidden = arch.decoder.backward(weights, &acts, gl, {}, gv, stream);
    if (graphs) {
        cudaGraph_t graph;
        cudaGraphExec_t executable;
        assert(cudaStreamEndCapture(stream, &graph) == cudaSuccess);
        assert(cudaGraphInstantiate(&executable, graph, NULL, NULL, 0) == cudaSuccess);
        assert(cudaGraphLaunch(executable, stream) == cudaSuccess);
    }
    assert(cudaStreamSynchronize(stream) == cudaSuccess);
    KagDecoderWeights* w = (KagDecoderWeights*)weights;
    write_tensor(argv[3], "decoded", decoded);
    write_tensor(argv[3], "grad_hidden", grad_hidden);
    write_tensor(argv[3], "critic_input", acts.branch[2].input_aug);
    write_tensor(argv[3], "critic_middle", acts.branch[2].mid_aug);
    write_tensor(argv[3], "critic_raw", acts.branch[2].out);
    write_tensor(argv[3], "critic_grad_raw", acts.branch[2].grad_out);
    write_tensor(argv[3], "critic_w1", w->branch[2].w1);
    write_tensor(argv[3], "critic_w2", w->branch[2].w2);
    write_tensor(argv[3], "critic_dw1", acts.branch[2].dw1);
    write_tensor(argv[3], "critic_dw2", acts.branch[2].dw2);
    printf("critic mode=%d graphs=%d precision_bytes=%zu rows=%d forward/backward PASS\n",
        mode, graphs, sizeof(precision_t), rows);
}
