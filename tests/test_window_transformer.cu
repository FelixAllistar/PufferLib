// Window Transformer network (src/window_transformer.cuh), FP32:
//   1. Training (forward_train over a segment) reproduces rollout (step-by-step forward with
//      in-place state updates and resets), including a non-empty starting window and an
//      episode reset inside the segment.
//   2. Analytic parameter and input gradients match central finite differences.
// Build: nvcc -O2 -std=c++17 -arch=native -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include
//   -Xcompiler=-fopenmp tests/test_window_transformer.cu raylib-5.5_linux_amd64/lib/libraylib.a
//   -lgomp -lcudart -lcublas -lcusolver -lcurand -lnccl -lnvidia-ml -o build/test_window_transformer
#define PRECISION_FLOAT
#define ENV_HEADER "ocean/breakout/breakout.h"
#define PUFFER_ENV_NAME "breakout"
#include "../src/pufferl.cu"
#include <vector>

static void gpu(cudaError_t r) { if (r != cudaSuccess) { fprintf(stderr, "CUDA %s\n", cudaGetErrorString(r)); exit(1); } }
static std::vector<float> down(Prec p) {
    std::vector<float> v(numel(p.shape)); gpu(cudaMemcpy(v.data(), p.data, v.size() * 4, cudaMemcpyDeviceToHost)); return v;
}
static void up(Prec& p, const std::vector<float>& v) { gpu(cudaMemcpy(p.data, v.data(), v.size() * 4, cudaMemcpyHostToDevice)); }

int main() {
    const int B = 3, TT = 6, H = 16, K = 4, D = 16, LAYERS = 2, HEADS = 2, WARM = 3;
    cublas_init_handle();
    Network n = {.hidden = H, .num_layers = K, .horizon = TT, .heads = HEADS, .window_layers = LAYERS, .window_width = D};
    void* w = window_create_weights(&n);
    Allocator params = {}, acts = {}, grads = {};
    window_reg_params(w, &params);
    WindowActivations roll = {}, train = {};
    window_reg_rollout(w, &roll, &acts, B);
    window_reg_train(w, &train, &acts, &grads, B * TT);
    Prec state = {.shape = {K, B, H}}, start = {.shape = {K, B, H}}, xs = {.shape = {B, TT, H}};
    Prec x1 = {.shape = {B, H}}, term = {.shape = {B, TT}};
    for (Prec* p : {&state, &start, &xs, &x1, &term}) alloc_register(&acts, p);
    alloc_create(&params); alloc_create(&acts); alloc_create(&grads);
    ulong seed = 7; window_init_weights(w, &seed, 0);
    // Larger output projection than the default 0.1 gain, so the window path is visible in the test.
    auto* ww = (WindowWeights*)w;
    { auto v = down(ww->out_w); for (auto& e : v) e *= 10; up(ww->out_w, v); }

    std::mt19937 rng(3); std::normal_distribution<float> normal(0, 1);
    auto randn = [&](int count) { std::vector<float> v(count); for (auto& e : v) e = normal(rng); return v; };
    // Warm-up steps fill the window, so the segment starts with history.
    std::vector<float> st(K * B * H, 0.f);
    up(state, st);
    for (int t = 0; t < WARM; ++t) { up(x1, randn(B * H)); window_forward(w, x1, state, &roll, 0); }
    gpu(cudaDeviceSynchronize());
    auto start_host = down(state); up(start, start_host);
    // Segment inputs; agent 1 resets at t = 3, agent 2 at t = 0.
    auto x_host = randn(B * TT * H);
    std::vector<float> term_host(B * TT, 0.f); term_host[1 * TT + 3] = 1; term_host[2 * TT + 0] = 1;
    up(xs, x_host); up(term, term_host);
    // Rollout reference: reset state rows on a terminal before the step, then forward.
    std::vector<float> rollout_out(B * TT * H);
    auto s = start_host;
    for (int t = 0; t < TT; ++t) {
        for (int b = 0; b < B; ++b) if (term_host[b * TT + t] != 0)
            for (int k = 0; k < K; ++k) for (int h = 0; h < H; ++h) s[(k * B + b) * H + h] = 0;
        up(state, s);
        std::vector<float> xt(B * H);
        for (int b = 0; b < B; ++b) for (int h = 0; h < H; ++h) xt[b * H + h] = x_host[(b * TT + t) * H + h];
        up(x1, xt);
        Prec out = window_forward(w, x1, state, &roll, 0);
        gpu(cudaDeviceSynchronize());
        auto o = down(out); s = down(state);
        for (int b = 0; b < B; ++b) for (int h = 0; h < H; ++h) rollout_out[(b * TT + t) * H + h] = o[b * H + h];
    }
    // Training: one call over the segment from the saved starting window.
    up(start, start_host);
    Prec out = window_forward_train(w, xs, start, term, &train, 0, 0);
    gpu(cudaDeviceSynchronize());
    auto train_out = down(out);
    float worst = 0;
    for (size_t i = 0; i < train_out.size(); ++i) worst = fmaxf(worst, fabsf(train_out[i] - rollout_out[i]));
    printf("rollout vs training max |diff| = %g over %zu outputs\n", worst, train_out.size());
    assert(worst < 1e-4f);
    // The window must matter: compare against a run with an empty starting window.
    { up(start, std::vector<float>(K * B * H, 0.f)); auto o2 = down(window_forward_train(w, xs, start, term, &train, 0, 0));
      float diff = 0; for (int h = 0; h < H; ++h) diff = fmaxf(diff, fabsf(o2[h] - train_out[h]));
      printf("starting window changes agent 0 step 0 output by %g\n", diff); assert(diff > 1e-3f); up(start, start_host); }

    // Gradients: loss = sum(out * c).
    auto c = randn(B * TT * H);
    auto loss = [&]() {
        auto o = down(window_forward_train(w, xs, start, term, &train, 0, 0)); gpu(cudaDeviceSynchronize());
        double l = 0; for (size_t i = 0; i < o.size(); ++i) l += (double)o[i] * c[i]; return l; };
    loss();
    Prec gout = {.shape = {B * TT, H}}; Allocator extra = {}; alloc_register(&extra, &gout); alloc_create(&extra); up(gout, c);
    Prec gx = window_backward(w, gout, &train, 0);
    gpu(cudaDeviceSynchronize());
    auto gx_host = down(gx);
    int checks = 0;
    auto check = [&](double numeric, double analytic, const char* what, int index) {
        if (!(fabs(numeric - analytic) <= 2e-3 + 0.03 * fmax(fabs(numeric), fabs(analytic)))) {
            fprintf(stderr, "%s[%d]: analytic %g numeric %g\n", what, index, analytic, numeric); exit(1); }
        ++checks; };
    const float eps = 1e-3f;
    for (int p = 0; p < params.num_regs; ++p) {
        Prec* tensor = (Prec*)params.regs[p].data_ptr;
        int count = numel(params.regs[p].shape);
        std::vector<float> g(count);
        gpu(cudaMemcpy(g.data(), *grads.regs[p].data_ptr, count * 4, cudaMemcpyDeviceToHost));
        int largest = 0; for (int i = 0; i < count; ++i) if (fabsf(g[i]) > fabsf(g[largest])) largest = i;
        for (int i : {0, count / 2, largest}) {
            float* address = (float*)*params.regs[p].data_ptr + i; float original;
            gpu(cudaMemcpy(&original, address, 4, cudaMemcpyDeviceToHost));
            double v[2];
            for (int sgn = 0; sgn < 2; ++sgn) { float changed = original + (sgn ? -eps : eps);
                gpu(cudaMemcpy(address, &changed, 4, cudaMemcpyHostToDevice)); v[sgn] = loss(); }
            gpu(cudaMemcpy(address, &original, 4, cudaMemcpyHostToDevice));
            check((v[0] - v[1]) / (2 * eps), g[i], "param", p * 100000 + i);
        }
        (void)tensor;
    }
    for (int i = 0; i < B * TT * H; i += 5) {
        double v[2];
        for (int sgn = 0; sgn < 2; ++sgn) { auto xc = x_host; xc[i] += sgn ? -eps : eps; up(xs, xc); v[sgn] = loss(); }
        up(xs, x_host);
        check((v[0] - v[1]) / (2 * eps), gx_host[i], "input", i);
    }
    printf("PASS window Transformer: training matches rollout; %d finite-difference gradient checks\n", checks);
}
