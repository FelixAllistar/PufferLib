// GPU window Transformer rollout (src/window_transformer.cuh) vs the CPU inference used by
// the Metamon evaluator (semantic_cpu.h), FP32, over a sequence with episode resets.
#define PRECISION_FLOAT
#define ENV_HEADER "ocean/breakout/breakout.h"
#define PUFFER_ENV_NAME "breakout"
#include "../../../src/pufferl.cu"
#include <vector>
extern "C" void window_cpu_reference(float*, int, int, int, int, int, int, int, const float*, const float*, int, float*);

int main() {
    const int B = 2, H = 24, K = 5, D = 16, LAYERS = 2, HEADS = 4, STEPS = 12;
    cublas_init_handle();
    Network n = {.hidden = H, .num_layers = K, .horizon = 1, .heads = HEADS, .window_layers = LAYERS, .window_width = D};
    void* w = window_create_weights(&n);
    Allocator params = {}, acts = {};
    window_reg_params(w, &params);
    WindowActivations roll = {};
    window_reg_rollout(w, &roll, &acts, B);
    Prec state = {.shape = {K, B, H}}, x = {.shape = {B, H}};
    alloc_register(&acts, &state); alloc_register(&acts, &x);
    alloc_create(&params); alloc_create(&acts);
    ulong seed = 11; window_init_weights(w, &seed, 0);
    std::vector<float> weights(params.total_elems);
    assert(cudaMemcpy(weights.data(), params.mem, weights.size() * 4, cudaMemcpyDeviceToHost) == cudaSuccess);
    std::mt19937 rng(5); std::normal_distribution<float> normal(0, 1);
    // Scale the output projection up so the window path dominates the residual.
    auto* ww = (WindowWeights*)w;
    size_t out_off = ((float*)ww->out_w.data - (float*)params.mem);
    for (size_t i = out_off; i < weights.size(); i++) weights[i] *= 10;
    assert(cudaMemcpy(params.mem, weights.data(), weights.size() * 4, cudaMemcpyHostToDevice) == cudaSuccess);
    std::vector<float> inputs(STEPS * B * H), resets(STEPS * B, 0.f), gpu_out(STEPS * B * H), cpu_out(STEPS * B * H);
    for (auto& v : inputs) v = normal(rng);
    resets[7 * B + 1] = 1;
    std::vector<float> st(K * B * H, 0.f);
    for (int t = 0; t < STEPS; t++) {
        assert(cudaMemcpy(st.data(), state.data, st.size() * 4, cudaMemcpyDeviceToHost) == cudaSuccess);
        for (int b = 0; b < B; b++) if (resets[t * B + b] != 0) for (int k = 0; k < K; k++) for (int h = 0; h < H; h++) st[(k * B + b) * H + h] = 0;
        assert(cudaMemcpy(state.data, st.data(), st.size() * 4, cudaMemcpyHostToDevice) == cudaSuccess);
        assert(cudaMemcpy(x.data, inputs.data() + t * B * H, B * H * 4, cudaMemcpyHostToDevice) == cudaSuccess);
        Prec out = window_forward(w, x, state, &roll, 0);
        assert(cudaMemcpy(gpu_out.data() + t * B * H, out.data, B * H * 4, cudaMemcpyDeviceToHost) == cudaSuccess);
    }
    window_cpu_reference(weights.data(), (int)weights.size(), B, H, K, D, LAYERS, HEADS, inputs.data(), resets.data(), STEPS, cpu_out.data());
    float worst = 0, spread = 0;
    for (size_t i = 0; i < gpu_out.size(); i++) { worst = fmaxf(worst, fabsf(gpu_out[i] - cpu_out[i])); spread = fmaxf(spread, fabsf(gpu_out[i] - inputs[i])); }
    printf("GPU vs CPU window max |diff| = %g (window contribution up to %g)\n", worst, spread);
    assert(worst < 1e-4f && spread > 1e-2f);
    puts("PASS window Transformer GPU rollout matches CPU inference");
}
