// Standalone check of cretaceous_encoder.cu (make -C ocean/cretaceous encoder-test; needs nvcc and a GPU).
//   - parameter counts at hidden 256 and 1024, default vs structured encoder, and layout vs the whole policy;
//   - train and rollout activations give the same forward;
//   - permutation invariance over neighbour slots; garbage in empty slots changes nothing; every branch matters;
//   - fp32: finite-difference checks of the hand-written backward for every weight tensor;
//   - CUDA graph capture/replay of forward + backward;
//   - dumps weights, obs, upstream grad, output and weight grads for tools/encoder_reference.py (NumPy forward,
//     PyTorch autograd gradients).
// The env header is minimal.h: the encoder only needs sim.h's layout constants, so no world, sim or Box3D is linked.
#include <cuda_runtime.h>
#ifndef CRET_TEST_BF16
#define PRECISION_FLOAT
#endif
#define ENV_HEADER "../ocean/minimal/minimal.h"
#define PUFFER_ENV_NAME "minimal"
#define PUFFER_CRETACEOUS
#include "sim.h"
#include "../../src/pufferl.cu"
#include <vector>
#include <algorithm>

static void require(bool ok, const char* msg) { if (!ok) { fprintf(stderr, "FAIL encoder test: %s\n", msg); exit(1); } }
static void ck(cudaError_t e) { require(e == cudaSuccess, cudaGetErrorString(e)); }
static std::vector<float> host(Prec t) {
    std::vector<precision_t> a(numel(t.shape));
    ck(cudaMemcpy(a.data(), t.data, a.size() * sizeof(precision_t), cudaMemcpyDeviceToHost));
    std::vector<float> out(a.size());
    for (size_t i = 0; i < a.size(); i++) out[i] = to_float(a[i]);
    return out;
}
static void put(Prec t, const std::vector<float>& v) {
    require(v.size() == (size_t)numel(t.shape), "tensor size");
    std::vector<precision_t> p(v.size());
    for (size_t i = 0; i < v.size(); i++) p[i] = from_float(v[i]);
    ck(cudaMemcpy(t.data, p.data(), p.size() * sizeof(precision_t), cudaMemcpyHostToDevice));
}
static void near(const std::vector<float>& a, const std::vector<float>& b, float atol, float rtol, const char* label) {
    require(a.size() == b.size(), label);
    for (size_t i = 0; i < a.size(); i++)
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || fabsf(a[i] - b[i]) > atol + rtol * fmaxf(fabsf(a[i]), fabsf(b[i]))) {
            fprintf(stderr, "FAIL %s [%zu]: %g vs %g\n", label, i, a[i], b[i]); exit(1);
        }
}
static double absdiff(const std::vector<float>& a, const std::vector<float>& b) {
    double d = 0; for (size_t i = 0; i < a.size(); i++) d += fabs((double)a[i] - b[i]); return d;
}
struct Rng { uint32_t s; float uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0f / 8388608.0f) - 1.0f; } };

// Obs in the sim's ranges: every species, 0..6 neighbours, some with gaps between present slots.
static std::vector<float> make_obs(int B, Rng& r) {
    std::vector<float> o((size_t)B * CRET_OBS_SIZE, 0.0f);
    for (int b = 0; b < B; b++) {
        float* x = &o[(size_t)b * CRET_OBS_SIZE];
        for (int k = 0; k < CRET_SELF_OBS; k++) x[k] = r.uni();
        for (int q = 0; q < CRET_SPECIES; q++) x[CRET_OBS_SPECIES + q] = q == b % CRET_SPECIES;
        x[22] = 1.0f;   // alive
        for (int k = 0; k < CRET_PATCH * CRET_PATCH * 2; k++) x[CRET_OBS_PATCH + k] = (k % 2) ? 1.5f * r.uni() + 0.5f : 2.0f * r.uni();
        int count = b % (CRET_NEIGHBORS + 1);
        for (int n = 0; n < CRET_NEIGHBORS; n++) {
            bool present = b % 4 == 3 ? (n == 1 || n == 3 || n == 4) : n < count;
            if (!present) continue;
            float* e = x + CRET_OBS_NEIGHBOR + n * CRET_NEIGHBOR_OBS;
            e[0] = 1.0f;
            for (int f = 1; f < CRET_NEIGHBOR_OBS; f++) e[f] = r.uni() * (f <= 3 ? 2.0f : 1.0f);
            e[5] = r.uni() > 0; e[8] = r.uni() > 0; e[9] = r.uni() > 0; e[7] = 0.5f + 0.5f * r.uni();
        }
    }
    return o;
}

static long policy_params(int H, int L, bool structured) {
    cret_net_on = structured;
    Arch arch = build_arch(CRET_OBS_SIZE, H, L, 19, false, 16);
    Allocator params = {};
    weights_create(&arch, &params);
    long n = params.total_elems;
    free(params.regs);
    return n;
}

int main(int argc, char** argv) {
    const char* dump = argc > 1 ? argv[1] : (USE_BF16 ? "build/cretaceous/encoder_test_bf16.bin" : "build/cretaceous/encoder_test_fp32.bin");
    cublas_init_handle();
    const int B = 14, H = 256, L = 2;

    // Parameter counts (decoder output 19 logits + value; MinGRU 3H x H per layer).
    for (int h : {256, 1024}) {
        long enc = cret_encoder_param_count(h), def = (long)h * CRET_OBS_SIZE;
        long pol = policy_params(h, L, true), pol_def = policy_params(h, L, false);
        require(pol - enc == pol_def - def, "structured encoder changes the rest of the policy's layout");
        printf("hidden %4d: structured encoder %7ld params (default linear %6ld); whole policy %8ld (default %8ld)\n",
            h, enc, def, pol, pol_def);
    }
    for (int i = 0; i < CE_NW; i++) { long r, c; ce_shape(i, H, &r, &c); printf("  %-11s %4ld x %3ld = %6ld\n", CE_W_NAMES[i], r, c, r * c); }

    cret_net_on = 1;
    Encoder enc = {}; enc.in_dim = CRET_OBS_SIZE; enc.out_dim = H;
    create_custom_encoder(&enc);
    require(enc.forward == cret_encoder_forward, "create_custom_encoder did not select the cretaceous encoder");
    Allocator params = {}, acts = {}, grads = {}, rollout = {};
    auto* ew = (CretEncoderWeights*)enc.create_weights(&enc);
    enc.reg_params(ew, &params); alloc_create(&params);
    ulong seed = 7; enc.init_weights(ew, &seed, 0);
    CretEncoderActivations a = {}, ra = {};
    enc.reg_train(ew, &a, &acts, &grads, B);
    Prec input = {.shape = {B, CRET_OBS_SIZE}}, grad = {.shape = {B, H}};
    alloc_register(&acts, &input); alloc_register(&acts, &grad);
    alloc_create(&acts); alloc_create(&grads);
    enc.reg_rollout(ew, &ra, &rollout, B); alloc_create(&rollout);
    require(params.total_elems == cret_encoder_param_count(H), "registered params != cret_encoder_param_count");

    Rng rng = {12345};
    auto obs = make_obs(B, rng);
    put(input, obs); obs = host(input);   // bf16: compare against what the encoder actually sees
    std::vector<float> g((size_t)B * H);
    for (size_t i = 0; i < g.size(); i++) g[i] = 0.1f * cosf(i * 0.173f);
    auto forward = [&](CretEncoderActivations* act) { return host(enc.forward(ew, act, input, 0)); };
    auto out = forward(&a);
    near(out, forward(&ra), 0, 0, "rollout vs train activations");
    double mag = 0; for (float v : out) mag += fabs(v);
    require(mag > 1e-3, "encoder output is all zero");
    puts("PASS forward is finite and rollout == train activations");

    // Permutation invariance: reverse the six neighbour slots (present and empty) of every row.
    auto perm = obs;
    for (int b = 0; b < B; b++) for (int n = 0; n < CRET_NEIGHBORS; n++) for (int f = 0; f < CRET_NEIGHBOR_OBS; f++)
        perm[(size_t)b * CRET_OBS_SIZE + CRET_OBS_NEIGHBOR + n * CRET_NEIGHBOR_OBS + f] =
            obs[(size_t)b * CRET_OBS_SIZE + CRET_OBS_NEIGHBOR + (CRET_NEIGHBORS - 1 - n) * CRET_NEIGHBOR_OBS + f];
    put(input, perm);
    near(out, forward(&a), USE_BF16 ? 0.02f : 2e-6f, USE_BF16 ? 0.02f : 1e-5f, "neighbour permutation invariance");
    // Empty slots are masked: garbage in their features (present flag 0) must not move the output.
    auto junk = obs;
    for (int b = 0; b < B; b++) for (int n = 0; n < CRET_NEIGHBORS; n++) {
        float* e = &junk[(size_t)b * CRET_OBS_SIZE + CRET_OBS_NEIGHBOR + n * CRET_NEIGHBOR_OBS];
        if (e[0] > 0.5f) continue;
        for (int f = 1; f < CRET_NEIGHBOR_OBS; f++) e[f] = 3.0f * rng.uni();
    }
    put(input, junk);
    near(out, forward(&a), 0, 0, "empty neighbour slots are masked");
    puts("PASS neighbour pooling is permutation-invariant and ignores empty slots");
    // Every branch reaches the output.
    const char* branch[] = {"self", "species", "patch", "neighbours"};
    for (int k = 0; k < 4; k++) {
        auto c = obs;
        for (int b = 0; b < B; b++) {
            float* x = &c[(size_t)b * CRET_OBS_SIZE];
            if (k == 0) for (int f = 0; f < 16; f++) x[f] = 0;
            if (k == 1) for (int q = 0; q < CRET_SPECIES; q++) x[CRET_OBS_SPECIES + q] = q == (b + 1) % CRET_SPECIES;
            if (k == 2) for (int f = 0; f < CRET_PATCH * CRET_PATCH * 2; f++) x[CRET_OBS_PATCH + f] = 0;
            if (k == 3) for (int f = 1; f < CRET_NEIGHBORS * CRET_NEIGHBOR_OBS; f++) if (f % CRET_NEIGHBOR_OBS) x[CRET_OBS_NEIGHBOR + f] *= -1;
        }
        put(input, c);
        if (absdiff(out, forward(&a)) < 1e-4) { fprintf(stderr, "FAIL branch %s does not reach the output\n", branch[k]); return 1; }
    }
    puts("PASS self, species, patch and neighbour branches all reach the output");

    // Backward.
    put(input, obs); forward(&a); put(grad, g);
    enc.backward(ew, &a, grad, 0); ck(cudaDeviceSynchronize());
    std::vector<std::vector<float>> dw(CE_NW);
    for (int i = 0; i < CE_NW; i++) {
        dw[i] = host(a.dw[i]); double n = 0;
        for (float v : dw[i]) { require(std::isfinite(v), "finite gradients"); n += fabs(v); }
        if (n < 1e-8) { fprintf(stderr, "FAIL no gradient reaches %s\n", CE_W_NAMES[i]); return 1; }
    }
    puts("PASS backward reaches every weight tensor");
    // Dump for tools/encoder_reference.py: int32 header {magic, version, B, H, obs, nweights, bf16}, then
    // obs (B, obs), upstream grad (B, H), output (B, H), and per weight: int32 rows, cols, weights, grads (float32).
    FILE* f = fopen(dump, "wb");
    require(f, "cannot write the dump (mkdir -p build/cretaceous)");
    int head[7] = {0x43524554, 1, B, H, CRET_OBS_SIZE, CE_NW, USE_BF16 ? 1 : 0};
    fwrite(head, sizeof(int), 7, f);
    auto gq = host(grad);
    fwrite(obs.data(), sizeof(float), obs.size(), f); fwrite(gq.data(), sizeof(float), gq.size(), f);
    fwrite(out.data(), sizeof(float), out.size(), f);
    for (int i = 0; i < CE_NW; i++) {
        long r, c; ce_shape(i, H, &r, &c); int rc[2] = {(int)r, (int)c};
        auto w = host(ew->w[i]);
        fwrite(rc, sizeof(int), 2, f); fwrite(w.data(), sizeof(float), w.size(), f); fwrite(dw[i].data(), sizeof(float), dw[i].size(), f);
    }
    fclose(f);
    printf("wrote %s (precision %s); check it with: python ocean/cretaceous/tools/encoder_reference.py %s\n",
        dump, USE_BF16 ? "bf16" : "fp32", dump);
#ifndef CRET_TEST_BF16
    {
        auto loss = [&]() { auto x = forward(&a); double s = 0; for (size_t i = 0; i < x.size(); i++) s += (double)x[i] * g[i]; return s; };
        int checks = 0, kinks = 0, worst_i = -1; double worst = 0;
        for (int i = 0; i < CE_NW; i++) {
            auto w = host(ew->w[i]); int n = (int)w.size(), largest = 0;
            for (int k = 1; k < n; k++) if (fabsf(dw[i][k]) > fabsf(dw[i][largest])) largest = k;
            Rng pick = {(uint32_t)(977 + i)};
            std::vector<int> idx = {0, n / 2, n - 1, largest};
            for (int k = 0; k < 8; k++) idx.push_back((int)((pick.uni() * 0.5f + 0.5f) * (n - 1)));
            // Central differences at eps 4e-3, then 1e-3 (fp32 loss noise is ~1e-6). A ReLU or max-pool switch inside
            // +-eps breaks the central difference; there the analytic gradient must match a one-sided one (it picks a
            // side of the kink). The exact check of every element is tools/encoder_reference.py (float64 autograd).
            for (int k : idx) {
                float orig = w[k];
                double ana = dw[i][k], l0 = loss(), cand[6];
                for (int e = 0; e < 2; e++) {
                    float eps = e ? 1e-3f : 4e-3f;
                    w[k] = orig + eps; put(ew->w[i], w); double lp = loss();
                    w[k] = orig - eps; put(ew->w[i], w); double lm = loss();
                    w[k] = orig; put(ew->w[i], w);
                    cand[e] = (lp - lm) / (2 * eps); cand[2 + 2 * e] = (lp - l0) / eps; cand[3 + 2 * e] = (l0 - lm) / eps;
                }
                // central differences first; one-sided ones only when both central ones disagree
                double num = cand[0], best = 1e30; bool one_sided = false;
                for (int c = 0; c < 6 && best > 0; c++) {
                    double e = fabs(cand[c] - ana) - (3e-4 + 2e-2 * fmax(fabs(cand[c]), fabs(ana)));
                    if (e < best) { best = e; num = cand[c]; one_sided = c >= 2; }
                }
                double err = fabs(num - ana) / (1e-3 + fmax(fabs(num), fabs(ana)));
                if (err > worst) { worst = err; worst_i = i; }
                if (best > 0) {
                    fprintf(stderr, "FAIL gradient %s[%d]: analytic %g numeric %g\n", CE_W_NAMES[i], k, ana, num); return 1;
                }
                checks++; kinks += one_sided;
            }
        }
        printf("PASS %d finite-difference weight gradients over all %d tensors, %d at a kink (one-sided); "
            "worst rel err %.2e in %s\n", checks, CE_NW, kinks, worst, worst_i >= 0 ? CE_W_NAMES[worst_i] : "-");
    }
#endif

    // CUDA graph: capture forward + backward once, replay on permuted data, then on the original.
    {
        cudaStream_t st; ck(cudaStreamCreate(&st));
        cublas_init_stream(st);
        cudaGraph_t graph; cudaGraphExec_t exec;
        ck(cudaStreamBeginCapture(st, cudaStreamCaptureModeThreadLocal));
        enc.forward(ew, &a, input, st); enc.backward(ew, &a, grad, st);
        ck(cudaStreamEndCapture(st, &graph)); ck(cudaGraphInstantiate(&exec, graph, 0, 0, 0));
        put(input, perm); put(grad, g); ck(cudaGraphLaunch(exec, st)); ck(cudaStreamSynchronize(st));
        near(out, host(a.out), USE_BF16 ? 0.02f : 2e-6f, USE_BF16 ? 0.02f : 1e-5f, "graph replay (permuted)");
        put(input, obs); put(grad, g); ck(cudaGraphLaunch(exec, st)); ck(cudaStreamSynchronize(st));
        near(out, host(a.out), 0, 0, "graph replay");
        for (int i = 0; i < CE_NW; i++) near(dw[i], host(a.dw[i]), 0, 0, "graph replay gradients");
        puts("PASS CUDA graph replay of forward + backward");
    }

    return 0;
}
