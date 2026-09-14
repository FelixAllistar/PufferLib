// Native forward, full BPTT, and serialized CPU-reference parity.
#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <curand.h>
#include <curand_kernel.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define PRECISION_FLOAT
#define ENV_HEADER "ocean/kaggriculture/kaggriculture.h"
#define PUFFER_ENV_NAME "kaggriculture"
#include "../src/pufferl_preamble.h"
#include "../src/algo.cu"
#include "../src/puffercpu.h"

static void ok(cudaError_t e) {
    if (e != cudaSuccess) { fprintf(stderr, "CUDA: %s\n", cudaGetErrorString(e)); exit(1); }
}
static void close_enough(float a, float b, const char* label, float tolerance = 2e-5f) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::fabs(a - b) > tolerance * (1 + std::fabs(b))) {
        fprintf(stderr, "%s: %.9g != %.9g\n", label, a, b); exit(1);
    }
}
static PrecisionTensor tensor(Allocator* alloc, int a, int b = 0, int c = 0) {
    // Registration needs a persistent tensor address: only used below by value
    // for CUDA allocations, never for alloc_register.
    (void)alloc;
    PrecisionTensor t = {.shape = {a, b, c}};
    ok(cudaMalloc((void**)&t.data, (size_t)numel(t.shape) * sizeof(float)));
    ok(cudaMemset(t.data, 0, (size_t)numel(t.shape) * sizeof(float)));
    return t;
}
static std::vector<float> read(PrecisionTensor t) {
    std::vector<float> result(numel(t.shape));
    ok(cudaMemcpy(result.data(), t.data, result.size() * sizeof(float), cudaMemcpyDeviceToHost));
    return result;
}
static void write(PrecisionTensor t, const std::vector<float>& v) {
    assert(v.size() == (size_t)numel(t.shape));
    ok(cudaMemcpy(t.data, v.data(), v.size() * sizeof(float), cudaMemcpyHostToDevice));
}

struct Test {
    static constexpr int B = 3, T = 4, H = 16, L = 2, O = KAG_ALL_LOGITS + 1;
    Policy policy;
    PolicyWeights weights;
    PolicyActivations roll, train;
    Allocator params, roll_acts, train_acts, grads;
    PrecisionTensor input, roll_input, state, roll_state, terminals;
    FloatTensor grad_logits, grad_value;
    std::vector<float> inputs, cotangent;
    cudaStream_t stream;
    Test() {
        ok(cudaStreamCreate(&stream)); cublas_init_handle();
        policy = build_policy("kaggriculture", KAG_ENTITY_OBS_SIZE, H, L, KAG_ALL_LOGITS, false, T);
        weights = policy_weights_create(&policy, &params);
        roll = policy_reg_rollout(&policy, weights, &roll_acts, B);
        train = policy_reg_train(&policy, weights, &train_acts, &grads, B * T);
        assert(params.num_regs == 16 + L && grads.num_regs == params.num_regs);
        assert(params.total_bytes == grads.total_bytes);
        // The real trainer's flat optimizer/checkpoint code cannot represent
        // inter-tensor allocation gaps. Test this, not just pointer-wise math.
        assert(params.total_bytes == params.total_elems * (long)sizeof(precision_t));
        for (int i = 0; i < params.num_regs; i++) {
            assert(numel(params.regs[i].shape) == numel(grads.regs[i].shape));
            assert(numel(params.regs[i].shape) % 8 == 0); // bf16 alignment too
        }
        ok(alloc_create(&params)); ok(alloc_create(&roll_acts));
        ok(alloc_create(&train_acts)); ok(alloc_create(&grads));
        uint64_t seed = 11; policy_init_weights(&policy, weights, &seed, stream);
        ok(cudaDeviceSynchronize());
        for (int p = 0; p < params.num_regs; p++) {
            int rows = params.regs[p].shape[0], cols = params.regs[p].shape[1];
            std::vector<float> host(rows * cols);
            for (int i = 0; i < rows * cols; i++) {
                float wave = sinf((i * 13 + p * 19) * 0.037f);
                host[i] = p < 16 ? 0.08f * wave : 0.03f + 0.005f * wave;
            }
            if (p < 16) {
                const int features[16] = {184,64,56,32,24,32,32,16,880,H,H,H/2,H,H/2,H,H/2};
                for (int row = 0; row < rows; row++) {
                    host[row * cols + features[p]] = 0.2f;
                    for (int c = features[p] + 1; c < cols; c++) host[row * cols + c] = 0;
                }
            }
            ok(cudaMemcpy(*params.regs[p].data_ptr, host.data(), host.size() * sizeof(float), cudaMemcpyHostToDevice));
        }
        input = tensor(nullptr, B, T, KAG_ENTITY_OBS_SIZE);
        roll_input = tensor(nullptr, B, KAG_ENTITY_OBS_SIZE);
        state = tensor(nullptr, L, B, H); roll_state = tensor(nullptr, L, B, H);
        terminals = tensor(nullptr, B, T);
        std::vector<float> done(B * T, 0); done[2] = 1; write(terminals, done);
        inputs.resize(B * T * KAG_ENTITY_OBS_SIZE);
        for (size_t i = 0; i < inputs.size(); i++) inputs[i] = 0.1f + 0.05f * sinf(i * 0.021f);
        write(input, inputs);
        grad_logits = {.shape = {B, T, KAG_ALL_LOGITS}};
        grad_value = {.shape = {B, T}};
        ok(cudaMalloc((void**)&grad_logits.data, B * T * KAG_ALL_LOGITS * sizeof(float)));
        ok(cudaMalloc((void**)&grad_value.data, B * T * sizeof(float)));
        std::vector<float> gl(B * T * KAG_ALL_LOGITS), gv(B * T);
        cotangent.resize(B * T * O);
        for (int r = 0; r < B * T; r++) {
            for (int o = 0; o < KAG_ALL_LOGITS; o++) {
                float g = 0.001f + 0.0007f * sinf((r * 11 + o) * 0.029f);
                gl[r * KAG_ALL_LOGITS + o] = cotangent[r * O + o] = g;
            }
            gv[r] = cotangent[r * O + KAG_ALL_LOGITS] = 0.2f;
        }
        ok(cudaMemcpy(grad_logits.data, gl.data(), gl.size() * sizeof(float), cudaMemcpyHostToDevice));
        ok(cudaMemcpy(grad_value.data, gv.data(), gv.size() * sizeof(float), cudaMemcpyHostToDevice));
    }
    double loss() {
        PrecisionTensor output = policy_forward_train(&policy, weights, train, input, state, terminals, stream);
        ok(cudaStreamSynchronize(stream)); auto values = read(output);
        double result = 0;
        for (size_t i = 0; i < values.size(); i++) result += (double)values[i] * cotangent[i];
        return result;
    }
};

static void forward_parity(Test& t) {
    t.loss();
    auto train_output = read(((KagDecoderActs*)t.train.decoder)->out);
    size_t floats = t.params.total_bytes / sizeof(float);
    assert(floats == kag_parameter_count(Test::H, Test::L, 4));
    std::vector<float> flat(floats + 7, 0);
    ok(cudaMemcpy(flat.data(), t.params.mem, floats * sizeof(float), cudaMemcpyDeviceToHost));
    Weights weights = {flat.data(), (int)floats + 7, 0};
    KagCpuPolicy* cpu = kag_cpu_make(&weights, Test::B, Test::H, Test::L, 4);
    std::vector<float> batch(Test::B * KAG_ENTITY_OBS_SIZE);
    float max_cpu = 0, max_train = 0;
    for (int step = 0; step < Test::T; step++) {
        for (int b = 0; b < Test::B; b++) {
            std::memcpy(batch.data() + b * KAG_ENTITY_OBS_SIZE,
                t.inputs.data() + (b * Test::T + step) * KAG_ENTITY_OBS_SIZE, KAG_ENTITY_OBS_SIZE * sizeof(float));
            if (b == 0 && step == 2) for (int l = 0; l < Test::L; l++) {
                std::memset(cpu->mingru->state + (l * Test::B + b) * Test::H, 0, Test::H * sizeof(float));
                ok(cudaMemset(t.roll_state.data + (l * Test::B + b) * Test::H, 0, Test::H * sizeof(float)));
            }
        }
        write(t.roll_input, batch);
        auto output = policy_forward(&t.policy, t.weights, t.roll, t.roll_input, t.roll_state, t.stream);
        ok(cudaStreamSynchronize(t.stream)); auto gpu = read(output);
        float* reference = kag_cpu_forward(cpu, batch.data());
        for (int b = 0; b < Test::B; b++) for (int o = 0; o < Test::O; o++) {
            int i = b * Test::O + o;
            close_enough(gpu[i], reference[i], "CPU/rollout");
            close_enough(gpu[i], train_output[(b * Test::T + step) * Test::O + o], "train/rollout");
            max_cpu = fmaxf(max_cpu, fabsf(gpu[i] - reference[i]));
            max_train = fmaxf(max_train, fabsf(gpu[i] - train_output[(b * Test::T + step) * Test::O + o]));
        }
        auto gpu_state = read(t.roll_state);
        for (size_t i = 0; i < gpu_state.size(); i++) close_enough(gpu_state[i], cpu->mingru->state[i], "recurrent state");
    }
    kag_cpu_free(cpu);
    printf("entity policy forward: CPU/rollout max=%g train/rollout max=%g (including terminal reset)\n", max_cpu, max_train);
}

static void gradients(Test& t) {
    t.loss();
    FloatTensor no_logstd = {};
    policy_backward(&t.policy, t.weights, t.train, t.grad_logits, no_logstd, t.grad_value, t.stream);
    ok(cudaStreamSynchronize(t.stream));
    std::vector<std::vector<float>> gradients(t.params.num_regs);
    for (int p = 0; p < t.params.num_regs; p++) {
        int n = numel(t.params.regs[p].shape);
        gradients[p].resize(n);
        ok(cudaMemcpy(gradients[p].data(), *t.grads.regs[p].data_ptr, n * sizeof(float), cudaMemcpyDeviceToHost));
        double norm = 0;
        for (float g : gradients[p]) { assert(std::isfinite(g)); norm += (double)g * g; }
        if (!(norm > 1e-16)) { fprintf(stderr, "Gradient norm too small for parameter %d: %.12g\n", p, norm); exit(1); }
    }
    float worst = 0;
    for (int p = 0; p < t.params.num_regs; p++) {
        int n = gradients[p].size(), cols = t.params.regs[p].shape[1];
        int indices[3] = {0, n / 2, cols - 1};
        for (int index : indices) {
            float* device = (float*)*t.params.regs[p].data_ptr + index;
            float original; ok(cudaMemcpy(&original, device, sizeof(float), cudaMemcpyDeviceToHost));
            const float eps = 0.002f;
            float value = original + eps; ok(cudaMemcpy(device, &value, sizeof(float), cudaMemcpyHostToDevice));
            double plus = t.loss();
            value = original - eps; ok(cudaMemcpy(device, &value, sizeof(float), cudaMemcpyHostToDevice));
            double minus = t.loss();
            ok(cudaMemcpy(device, &original, sizeof(float), cudaMemcpyHostToDevice));
            float numerical = (plus - minus) / (2 * eps), analytic = gradients[p][index];
            float error = fabsf(numerical - analytic); worst = fmaxf(worst, error);
            if (error > 2e-5f + 0.015f * fabsf(numerical)) {
                fprintf(stderr, "gradient matrix=%d index=%d analytic=%g finite_difference=%g error=%g\n", p, index, analytic, numerical, error);
                exit(1);
            }
        }
    }
    // A real gradient step must reduce this differentiable objective.
    double before = t.loss();
    for (int p = 0; p < t.params.num_regs; p++) {
        int n = gradients[p].size(); std::vector<float> host(n);
        ok(cudaMemcpy(host.data(), *t.params.regs[p].data_ptr, n * sizeof(float), cudaMemcpyDeviceToHost));
        for (int i = 0; i < n; i++) host[i] -= 0.001f * gradients[p][i];
        ok(cudaMemcpy(*t.params.regs[p].data_ptr, host.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    }
    double after = t.loss(); assert(after < before);
    printf("entity policy BPTT: %d matrices, 54 finite differences, max error=%g; update loss %.8g -> %.8g PASS\n",
        t.params.num_regs, worst, before, after);
}
int main() {
    Test test; forward_parity(test); gradients(test);
    ok(cudaDeviceSynchronize()); puts("entity policy CPU/CUDA/gradient tests PASS");
}
