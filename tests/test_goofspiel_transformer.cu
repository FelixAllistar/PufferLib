// Native adapter checks; no Python/framework reference is involved.
#include "../src/pufferl.cu"
#define GS_EXPLOIT_NO_MAIN
#include "../ocean/goofspiel/goofspiel_exploit.cu"

static void checked(cudaError_t error) { puf_transformer::check(error, "adapter test"); }
static std::vector<float> read_floats(const float* pointer, size_t count) {
    std::vector<float> result(count);
    checked(cudaMemcpy(result.data(), pointer, count * sizeof(float), cudaMemcpyDeviceToHost));
    return result;
}
static void upload_floats(float* pointer, const std::vector<float>& values) {
    checked(cudaMemcpy(pointer, values.data(), values.size() * sizeof(float), cudaMemcpyHostToDevice));
    checked(cudaStreamSynchronize(nullptr)); // pageable H2D vs nonblocking test stream
}
static void close_vectors(const std::vector<float>& a, const std::vector<float>& b, float tolerance) {
    assert(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
        assert(isfinite(a[i]) && isfinite(b[i]) && fabsf(a[i] - b[i]) <= tolerance);
}

int main() {
    static_assert(!USE_BF16, "finite-difference adapter test builds in FP32");
    constexpr int batch = 4, width = 8, layers = 2, outputs = GS_NUM_CARDS + 1;
    cublas_init_handle();
    cudaStream_t stream;
    checked(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    Arch baseline = build_arch(OBS_SIZE, width, layers, GS_NUM_CARDS, false, 2);
    assert(baseline.network.forward == mingru_forward);
    assert(baseline.encoder.forward == encoder_forward);
    Arch arch = build_arch(OBS_SIZE, width, layers, GS_NUM_CARDS, false, 2, "transformer", 2);
    Allocator params = {}, acts = {}, grads = {}, snapshot_params = {};
    Weights weights = weights_create(&arch, &params);
    Weights snapshot = weights_create(&arch, &snapshot_params);
    Activations train = arch_reg_train(&arch, weights, &acts, &grads, batch);
    Activations rollout = arch_reg_rollout(&arch, weights, &acts, batch);
    Prec observations = {.shape = {batch, OBS_SIZE}};
    Prec state = {.shape = {layers, batch, width}};
    Prec terminals = {.shape = {2, 2}};
    Float grad_logits = {.shape = {2, 2, GS_NUM_CARDS}};
    Float grad_values = {.shape = {2, 2}};
    alloc_register(&acts, &observations); alloc_register(&acts, &state);
    alloc_register(&acts, &terminals); alloc_register(&acts, &grad_logits);
    alloc_register(&acts, &grad_values);
    alloc_create(&params); alloc_create(&snapshot_params);
    alloc_create(&acts); alloc_create(&grads);
    assert(params.total_elems == grads.total_elems);
    assert(params.total_bytes == params.total_elems * sizeof(float));
    ulong seed = 31;
    weights_init(&arch, weights, &seed, stream);
    checked(cudaStreamSynchronize(stream));
    auto initial_weights = read_floats((float*)params.mem, params.total_elems);

    Ini ini = {};
    puf_ini_load_env(&ini, "goofspiel", 0, nullptr);
    puf_ini_put(&ini, "policy.architecture", "transformer");
    puf_ini_put(&ini, "policy.hidden_size", "8");
    puf_ini_put(&ini, "policy.num_layers", "2");
    GSConfig cfg = gs_solver_config(&ini);
    GSState states[batch] = {};
    std::vector<float> host_obs(batch * OBS_SIZE);
    for (int b = 0; b < batch; ++b) {
        GSHistory history = {};
        uint32_t rng = 100 + b;
        gs_reset(states + b, &history, &cfg, &rng);
        if (b >= 2) {
            uint8_t bids[] = {0, 1};
            gs_step(states + b, &history, &cfg, bids);
        }
        uint8_t observation[OBS_SIZE];
        gs_observe_player(&cfg, states + b, 1, observation);
        for (int i = 0; i < OBS_SIZE; ++i) host_obs[b * OBS_SIZE + i] = observation[i];
    }
    upload_floats(observations.data, host_obs);
    auto forward_train = [&]() {
        Prec encoded = arch.encoder.forward(weights.encoder, train.encoder, observations, stream);
        Prec hidden = arch.network.forward_train(weights.network,
            *puf_unsqueeze(&encoded, 0, 2, 2), state, terminals, train.network, 0, stream);
        return arch.decoder.forward(weights.decoder, train.decoder, *puf_squeeze(&hidden, 0), stream);
    };
    Prec output = forward_train();
    checked(cudaStreamSynchronize(stream));
    auto expected = read_floats(output.data, batch * outputs);
    Prec rollout_output = arch_forward(&arch, weights, rollout, observations, state, stream);
    checked(cudaStreamSynchronize(stream));
    close_vectors(expected, read_floats(rollout_output.data, batch * outputs), 2e-6f);

    // Changing a future rollout row cannot change earlier rows' outputs.
    auto changed_obs = host_obs;
    for (int i = 0; i < OBS_SIZE; ++i) changed_obs[(batch - 1) * OBS_SIZE + i] = host_obs[i];
    upload_floats(observations.data, changed_obs);
    output = forward_train();
    checked(cudaStreamSynchronize(stream));
    auto changed_output = read_floats(output.data, batch * outputs);
    for (int i = 0; i < (batch - 1) * outputs; ++i) assert(fabsf(expected[i] - changed_output[i]) < 2e-6f);
    upload_floats(observations.data, host_obs);

    std::vector<float> upstream(batch * outputs), logit_grads(batch * GS_NUM_CARDS), value_grads(batch);
    for (int i = 0; i < batch * outputs; ++i) upstream[i] = .02f * sinf(float(i + 1));
    for (int b = 0; b < batch; ++b) {
        for (int c = 0; c < GS_NUM_CARDS; ++c) logit_grads[b * GS_NUM_CARDS + c] = upstream[b * outputs + c];
        value_grads[b] = upstream[b * outputs + GS_NUM_CARDS];
    }
    upload_floats(grad_logits.data, logit_grads); upload_floats(grad_values.data, value_grads);
    output = forward_train();
    arch_backward(&arch, weights, train, grad_logits, {}, grad_values, stream);
    checked(cudaStreamSynchronize(stream));
    auto analytical = read_floats((float*)grads.mem, grads.total_elems);
    auto objective = [&]() {
        Prec out = forward_train();
        checked(cudaStreamSynchronize(stream));
        auto values = read_floats(out.data, batch * outputs);
        double sum = 0;
        for (size_t i = 0; i < values.size(); ++i) sum += double(values[i]) * upstream[i];
        return sum;
    };
    size_t offset = 0;
    int derivatives = 0;
    double max_error = 0;
    for (int p = 0; p < params.num_regs; ++p) {
        size_t size = numel(params.regs[p].shape);
        for (size_t local : {size_t(0), size / 2, size - 1}) {
            size_t index = offset + local;
            constexpr float epsilon = 1e-3f;
            float plus = initial_weights[index] + epsilon, minus = initial_weights[index] - epsilon;
            checked(cudaMemcpy((float*)params.mem + index, &plus, sizeof(float), cudaMemcpyHostToDevice));
            checked(cudaStreamSynchronize(nullptr));
            double a = objective();
            checked(cudaMemcpy((float*)params.mem + index, &minus, sizeof(float), cudaMemcpyHostToDevice));
            checked(cudaStreamSynchronize(nullptr));
            double b = objective();
            checked(cudaMemcpy((float*)params.mem + index, &initial_weights[index], sizeof(float), cudaMemcpyHostToDevice));
            checked(cudaStreamSynchronize(nullptr));
            double numerical = (a - b) / (plus - minus);
            double error = fabs(numerical - analytical[index]);
            max_error = std::max(max_error, error);
            if (!(error < 7e-4 + .025 * fabs(numerical))) {
                fprintf(stderr, "gradient mismatch tensor=%d index=%zu numerical=%g analytical=%g\n", p, local, numerical, analytical[index]);
                return 1;
            }
            ++derivatives;
        }
        offset += size;
    }
    assert(offset == (size_t)params.total_elems);

    // Full adapter forward/backward capture, including decoder's side-stream dW.
    checked(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal));
    output = forward_train();
    arch_backward(&arch, weights, train, grad_logits, {}, grad_values, stream);
    cudaGraph_t graph;
    cudaGraphExec_t executable;
    checked(cudaStreamEndCapture(stream, &graph));
    checked(cudaGraphInstantiate(&executable, graph, nullptr, nullptr, 0));
    checked(cudaMemsetAsync(grads.mem, 0xff, grads.total_bytes, stream));
    checked(cudaMemsetAsync(output.data, 0xff, batch * outputs * sizeof(float), stream));
    checked(cudaGraphLaunch(executable, stream));
    checked(cudaStreamSynchronize(stream));
    close_vectors(expected, read_floats(output.data, batch * outputs), 2e-6f);
    close_vectors(analytical, read_floats((float*)grads.mem, grads.total_elems), 2e-5f);
    checked(cudaGraphExecDestroy(executable)); checked(cudaGraphDestroy(graph));

    // Captured rollout reads the actor snapshot, never live learner weights.
    upload_floats((float*)snapshot_params.mem, initial_weights);
    checked(cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal));
    rollout_output = arch_forward(&arch, snapshot, rollout, observations, state, stream);
    checked(cudaStreamEndCapture(stream, &graph));
    checked(cudaGraphInstantiate(&executable, graph, nullptr, nullptr, 0));
    auto changed_weights = initial_weights;
    changed_weights[0] += 1.f;
    upload_floats((float*)params.mem, changed_weights);
    checked(cudaGraphLaunch(executable, stream)); checked(cudaStreamSynchronize(stream));
    close_vectors(expected, read_floats(rollout_output.data, batch * outputs), 2e-6f);
    upload_floats((float*)snapshot_params.mem, changed_weights);
    checked(cudaGraphLaunch(executable, stream)); checked(cudaStreamSynchronize(stream));
    auto fresh = read_floats(rollout_output.data, batch * outputs);
    float delta = 0;
    for (size_t i = 0; i < fresh.size(); ++i) delta = fmaxf(delta, fabsf(fresh[i] - expected[i]));
    if (!(delta > 1e-5f)) {
        fprintf(stderr, "snapshot refresh failed: max output change %.9g\n", delta);
        return 1;
    }
    checked(cudaGraphExecDestroy(executable)); checked(cudaGraphDestroy(graph));
    upload_floats((float*)params.mem, initial_weights);

    // The independent exact-evaluator adapter must reproduce training logits'
    // legal-card probabilities, including a partial inference chunk (513 rows).
    char checkpoint[] = "/tmp/goofspiel-transformer-test-XXXXXX";
    int fd = mkstemp(checkpoint);
    assert(fd >= 0);
    FILE* file = fdopen(fd, "wb");
    assert(file && fwrite(initial_weights.data(), sizeof(float), initial_weights.size(), file) == initial_weights.size());
    fclose(file);
    GSDeviceModel exact = gs_model_load(checkpoint, width, layers, "transformer", 2);
    constexpr int evaluation_batch = 513;
    std::vector<GSState> evaluation_states(evaluation_batch);
    for (int b = 0; b < evaluation_batch; ++b) evaluation_states[b] = states[b % batch];
    GSState* device_states;
    double* probabilities;
    float* hidden;
    checked(cudaMalloc(&device_states, evaluation_batch * sizeof(GSState)));
    checked(cudaMalloc(&probabilities, evaluation_batch * GS_NUM_CARDS * sizeof(double)));
    checked(cudaMalloc(&hidden, evaluation_batch * layers * width * sizeof(float)));
    checked(cudaMemcpy(device_states, evaluation_states.data(), evaluation_batch * sizeof(GSState), cudaMemcpyHostToDevice));
    cublasHandle_t evaluator_blas;
    gs_cublas(cublasCreate(&evaluator_blas));
    gs_policy(evaluator_blas, &cfg, exact, device_states, nullptr, hidden, probabilities, evaluation_batch);
    std::vector<double> host_probabilities(evaluation_batch * GS_NUM_CARDS);
    checked(cudaMemcpy(host_probabilities.data(), probabilities, host_probabilities.size() * sizeof(double), cudaMemcpyDeviceToHost));
    double max_probability_error = 0;
    for (int b = 0; b < evaluation_batch; ++b) {
        double sum = 0;
        double raw[GS_NUM_CARDS] = {};
        for (int c = 0; c < GS_NUM_CARDS; ++c) {
            raw[c] = (states[b % batch].hands[1] >> c & 1u) ? exp(expected[b % batch * outputs + c]) : 0;
            sum += raw[c];
        }
        for (int c = 0; c < GS_NUM_CARDS; ++c)
            max_probability_error = std::max(max_probability_error, fabs(raw[c] / sum - host_probabilities[b * GS_NUM_CARDS + c]));
    }
    assert(max_probability_error < 2e-6);
    gs_cublas(cublasDestroy(evaluator_blas));
    checked(cudaFree(exact.encoder)); checked(cudaFree(exact.decoder)); checked(cudaFree(exact.recurrent));

    // A zero policy head is exactly the solver's uniform-policy known case.
    auto* decoder = (DecoderWeights*)weights.decoder;
    size_t decoder_offset = decoder->weight.data - (float*)params.mem;
    std::fill(initial_weights.begin() + decoder_offset,
        initial_weights.begin() + decoder_offset + outputs * width, 0.f);
    file = fopen(checkpoint, "wb");
    assert(file && fwrite(initial_weights.data(), sizeof(float), initial_weights.size(), file) == initial_weights.size());
    fclose(file);
    double uniform = gs_cuda_exploit("uniform", &ini, nullptr, nullptr);
    double transformer_uniform = gs_cuda_exploit(checkpoint, &ini, nullptr, nullptr);
    assert(fabs(uniform - transformer_uniform) < 1e-10);
    assert(unlink(checkpoint) == 0);
    printf("{\"test\":\"goofspiel_transformer_adapter\",\"passed\":true,\"derivatives\":%d,"
        "\"gradient_max_abs\":%.9g,\"probability_max_abs\":%.9g,\"uniform_exploitability\":%.9g,"
        "\"graph\":true,\"actor_snapshot\":true,\"no_temporal_leakage\":true}\n",
        derivatives, max_error, max_probability_error, uniform);
    return 0;
}
