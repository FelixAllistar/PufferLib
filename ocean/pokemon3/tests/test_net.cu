// Parity and gradient test of the structured Gen 3 net (pokemon3_encoder.cu) in FP32, modeled on Gen 1's
// ocean/pokemon/tests/test_encoder.cu: CUDA forward vs the CPU reference (pk3_net_cpu.h), through the real
// arch_forward / arch_forward_train calls, then central-difference checks of every parameter tensor and the decoder's
// hidden input. Build and run: ocean/pokemon3/tests/run_test_net.sh
#define PRECISION_FLOAT
#define ENV_HEADER "ocean/breakout/breakout.h"
#define PUFFER_ENV_NAME "breakout"
#include "../../../src/pufferl.cu"
#include "../pokemon3_encoder.cu"
#include "../pk3_net_cpu.h"

static void gpu(cudaError_t result) { assert(result == cudaSuccess); }

static float compare(Prec actual, const float* expected, int n, const char* what) {
    float* host = (float*)calloc(n, sizeof(float));
    gpu(cudaMemcpy(host, actual.data, n * sizeof(float), cudaMemcpyDeviceToHost));
    float largest = 0;
    for (int i = 0; i < n; i++) {
        float error = fabsf(host[i] - expected[i]);
        if (!isfinite(host[i]) || !isfinite(expected[i]) || error > 2e-5f * (1 + fabsf(expected[i]))) {
            fprintf(stderr, "FAIL %s index %d cuda=%g cpu=%g\n", what, i, host[i], expected[i]);
            exit(1);
        }
        largest = fmaxf(largest, error);
    }
    free(host);
    return largest;
}
static double loss(Prec output, const float* cotangent, int n) {
    float* host = (float*)calloc(n, sizeof(float));
    gpu(cudaMemcpy(host, output.data, n * sizeof(float), cudaMemcpyDeviceToHost));
    double total = 0;
    for (int i = 0; i < n; i++) total += host[i] * cotangent[i];
    free(host);
    return total;
}
static int failures = 0, kinks = 0;
static void derivative(double numeric, float analytic, int tensor, int index) {
    if (!isfinite(numeric) || !isfinite(analytic)
            || fabs(numeric - analytic) > 3e-4 + 0.025 * fmax(fabs(numeric), fabs(analytic))) {
        fprintf(stderr, "gradient tensor=%d index=%d analytic=%g numeric=%g\n", tensor, index, analytic, numeric);
        failures++;
    }
}
static Prec fixed_hidden(void* weights, Prec input, Prec state, void* activations, cudaStream_t stream) { return state; }
static Prec fixed_hidden_train(void* weights, Prec input, Prec state, Prec terminals, void* activations, int offset,
        cudaStream_t stream) { return *puf_unsqueeze(&state, 0, 1, state.shape[0]); }

// Synthetic observations with the real layout: rows 0-2 battle (own active 0, 3, none), row 3 drafting.
static void make_obs(float* obs, int B, int pass) {
    for (int b = 0; b < B; b++) {
        float* o = obs + (long)b * P3_OBS;
        for (int i = 0; i < P3_OBS; i++) o[i] = 0.5f + 0.5f * sinf(i * 0.137f + b * 1.7f + pass * 0.61f);
        for (int m = 0; m < P3_MONS; m++) {
            o[m * P3_MON + 1] = 0;
            for (int j = 0; j < 4; j++)  // pass 0 includes Pokemon with no known moves
                o[m * P3_MON + P3_MOVE_AT + j * P3_MOVE] = (m * 7 + j * 3 + b + pass) % 5 < (pass ? 4 : 2) ? 1 : 0;
        }
        int active = b == 0 ? 0 : b == 1 ? 3 : -1;
        if (active >= 0) o[active * P3_MON + 1] = 1;
        o[6 * P3_MON + 1] = 1;  // opponent active
        o[P3_DRAFTING] = b == 3;
    }
}

int main() {
    const int B = 4, H = 32;
    pk3_net_on = 1; pk3_embed = 16; pk3_mid = 24;
    const int E = pk3_embed, M = pk3_mid, OUT = P3_ACTIONS + 1;
    cublas_init_handle();
    Encoder enc = {.in_dim = P3_OBS, .out_dim = H};
    Decoder dec = {.hidden_dim = H, .output_dim = P3_ACTIONS};
    create_pokemon3_encoder(&enc);
    create_pokemon3_decoder(&dec);
    void* ew = enc.create_weights(&enc);
    void* dw = dec.create_weights(&dec);
    P3EncActs ea = {};
    P3DecActs da = {};
    Allocator params = {}, acts = {}, grads = {};
    enc.reg_params(ew, &params);
    int encoder_regs = params.num_regs;
    dec.reg_params(dw, &params);
    enc.reg_train(ew, &ea, &acts, &grads, B);
    dec.reg_train(dw, &da, &acts, &grads, B);
    Prec obs = {.shape = {B, P3_OBS}}, state = {.shape = {B, H}}, enc_grad = {.shape = {B, H}};
    Float logits_grad = {.shape = {B, P3_ACTIONS}}, value_grad = {.shape = {B}};
    alloc_register(&acts, &enc_grad); alloc_register(&acts, &logits_grad); alloc_register(&acts, &value_grad);
    alloc_register(&acts, &obs); alloc_register(&acts, &state);
    TrainGraph graph = {};
    graph.mb_actions = {.shape = {1, B, 1}};
    graph.mb_logprobs = {.shape = {1, B}};
    graph.mb_action_mask = {.shape = {1, B, P3_ACTIONS}};
    graph.mb_imp = {.shape = {1, B}};
    graph.mb_gae_v = {.shape = {1, B}};
    Float logps = {.shape = {B, P3_ACTIONS}}, new_lp = {.shape = {B}};
    Int sizes = {.shape = {1}};
    alloc_register(&acts, &graph.mb_actions); alloc_register(&acts, &graph.mb_logprobs);
    alloc_register(&acts, &graph.mb_action_mask); alloc_register(&acts, &graph.mb_imp);
    alloc_register(&acts, &graph.mb_gae_v); alloc_register(&acts, &logps); alloc_register(&acts, &new_lp);
    alloc_register(&acts, &sizes);
    alloc_create(&params); alloc_create(&acts); alloc_create(&grads);
    int action_count = P3_ACTIONS;
    gpu(cudaMemcpy(sizes.data, &action_count, sizeof(int), cudaMemcpyHostToDevice));
    gpu(cudaMemset(graph.mb_actions.data, 0, B * sizeof(float)));
    gpu(cudaMemset(graph.mb_logprobs.data, 0, B * sizeof(float)));
    float* legal = (float*)malloc(sizeof(float) * B * P3_ACTIONS);
    for (int i = 0; i < B * P3_ACTIONS; i++) legal[i] = 1;
    gpu(cudaMemcpy(graph.mb_action_mask.data, legal, sizeof(float) * B * P3_ACTIONS, cudaMemcpyHostToDevice));
    assert(encoder_regs == 6 && params.num_regs == 13 && grads.num_regs == params.num_regs);
    assert(params.total_elems * sizeof(float) == params.total_bytes && grads.total_bytes == params.total_bytes);

    // Deterministic weights (pointer weights large enough that the pointer terms matter), in registration order.
    float* weights = (float*)calloc(params.total_elems, sizeof(float));
    long offset = 0;
    for (int p = 0; p < params.num_regs; p++) {
        int rows = params.regs[p].shape[0], cols = numel(params.regs[p].shape) / rows;
        for (long i = 0; i < (long)rows * cols; i++)
            weights[offset + i] = (p > encoder_regs ? 4.0f : 1.5f) * sinf((offset + i) * 0.731f + p) / sqrtf((float)cols);
        gpu(cudaMemcpy(*params.regs[p].data_ptr, weights + offset, rows * cols * sizeof(float), cudaMemcpyHostToDevice));
        offset += (long)rows * cols;
    }
    Pk3CpuEnc cenc; Pk3CpuDec cdec;
    const float* end = pk3_cpu_enc_bind(&cenc, E, M, H, weights);
    end = pk3_cpu_dec_bind(&cdec, E, M, H, end);
    assert(end == weights + params.total_elems);

    float* input = (float*)malloc(sizeof(float) * B * P3_OBS);
    float hidden[B * H], expected_enc[B * H], expected_dec[B * OUT];
    for (int i = 0; i < B * H; i++) hidden[i] = sinf(i * 0.31f);
    gpu(cudaMemcpy(state.data, hidden, sizeof(hidden), cudaMemcpyHostToDevice));
    float ge[B * H], gd[B * OUT], gl[B * P3_ACTIONS], gv[B];
    for (int i = 0; i < B * H; i++) ge[i] = 0.1f * cosf(i * 0.73f);
    for (int b = 0; b < B; b++) for (int a = 0; a < OUT; a++) {
        gd[b * OUT + a] = (a < 10 ? 0.5f : 0.05f) * sinf((b * OUT + a) * 0.37f);
        if (a < P3_ACTIONS) gl[b * P3_ACTIONS + a] = gd[b * OUT + a]; else gv[b] = gd[b * OUT + a];
    }
    gpu(cudaMemcpy(enc_grad.data, ge, sizeof(ge), cudaMemcpyHostToDevice));
    gpu(cudaMemcpy(logits_grad.data, gl, sizeof(gl), cudaMemcpyHostToDevice));
    gpu(cudaMemcpy(value_grad.data, gv, sizeof(gv), cudaMemcpyHostToDevice));
    int checks = 0;
    float worst_enc = 0, worst_dec = 0, pointer = 0;
    for (int pass = 0; pass < 3; pass++) {
        make_obs(input, B, pass);
        gpu(cudaMemcpy(obs.data, input, sizeof(float) * B * P3_OBS, cudaMemcpyHostToDevice));
        for (int b = 0; b < B; b++) {
            pk3_cpu_encode(&cenc, input + (long)b * P3_OBS, expected_enc + b * H);
            pk3_cpu_decode(&cdec, input + (long)b * P3_OBS, hidden + b * H, expected_dec + b * OUT);
            for (int a = 0; a < OUT; a++) {
                float s = 0;
                for (int k = 0; k < H; k++) s += cdec.lin[(long)a * H + k] * hidden[b * H + k];
                pointer = fmaxf(pointer, fabsf(s - expected_dec[b * OUT + a]));
                if (b == 3 || a >= 10 || (b == 2 && a < 4)) assert(s == expected_dec[b * OUT + a]);
            }
        }
        worst_enc = fmaxf(worst_enc, compare(enc.forward(ew, &ea, obs, 0), expected_enc, B * H, "encoder"));
        Arch arch = {.encoder = enc, .decoder = dec, .network = {.forward = fixed_hidden, .forward_train = fixed_hidden_train}};
        Weights aw = {.encoder = ew, .decoder = dw};
        Activations aa = {.encoder = &ea, .decoder = &da};
        da.obs = {};
        worst_dec = fmaxf(worst_dec, compare(arch_forward(&arch, aw, aa, obs, state, 0), expected_dec, B * OUT, "arch_forward"));
        assert(da.obs.data == obs.data);
        da.obs = {};
        Prec sequence = obs;
        puf_unsqueeze(&sequence, 0, 1, B);
        worst_dec = fmaxf(worst_dec, compare(arch_forward_train(&arch, aw, aa, sequence, state, {}, 0, graph, {},
            sizes.data, logps.data, new_lp.data, 0), expected_dec, B * OUT, "arch_forward_train"));
        assert(da.obs.data == obs.data);
        enc.forward(ew, &ea, obs, 0);
        enc.backward(ew, &ea, enc_grad, 0);
        Prec grad_state = dec.backward(dw, &da, logits_grad, {}, value_grad, 0);
        float dh[B * H];
        gpu(cudaMemcpy(dh, grad_state.data, sizeof(dh), cudaMemcpyDeviceToHost));
        for (int p = 0; p < params.num_regs; p++) {
            int n = numel(params.regs[p].shape);
            assert(n == numel(grads.regs[p].shape));
            float* analytic = (float*)calloc(n, sizeof(float));
            gpu(cudaMemcpy(analytic, *grads.regs[p].data_ptr, n * sizeof(float), cudaMemcpyDeviceToHost));
            int largest = 0;
            for (int i = 0; i < n; i++) { assert(isfinite(analytic[i])); if (fabsf(analytic[i]) > fabsf(analytic[largest])) largest = i; }
            int probes[] = {0, n / 3, n / 2, largest};
            for (int idx : probes) {
                float* address = (float*)*params.regs[p].data_ptr + idx;
                float original;
                gpu(cudaMemcpy(&original, address, sizeof(float), cudaMemcpyDeviceToHost));
                // A ReLU kink inside the step spoils a central difference: retry with a 4x smaller step first.
                double numeric = 0;
                for (float epsilon : {1e-3f, 2.5e-4f}) {
                    double values[2];
                    for (int sign = 0; sign < 2; sign++) {
                        float changed = original + (sign ? -epsilon : epsilon);
                        gpu(cudaMemcpy(address, &changed, sizeof(float), cudaMemcpyHostToDevice));
                        values[sign] = p < encoder_regs ? loss(enc.forward(ew, &ea, obs, 0), ge, B * H)
                            : loss(dec.forward(dw, &da, state, 0), gd, B * OUT);
                    }
                    numeric = (values[0] - values[1]) / (2 * epsilon);
                    if (fabs(numeric - analytic[idx]) <= 3e-4 + 0.025 * fmax(fabs(numeric), fabs(analytic[idx]))) break;
                    if (epsilon > 1e-3f - 1e-9f) kinks++;
                }
                gpu(cudaMemcpy(address, &original, sizeof(float), cudaMemcpyHostToDevice));
                derivative(numeric, analytic[idx], p, idx);
                checks++;
            }
            free(analytic);
        }
        for (int idx = 0; idx < B * H; idx += 3) {
            double values[2];
            for (int sign = 0; sign < 2; sign++) {
                float changed = hidden[idx] + (sign ? -1e-3f : 1e-3f);
                gpu(cudaMemcpy(state.data + idx, &changed, sizeof(float), cudaMemcpyHostToDevice));
                values[sign] = loss(dec.forward(dw, &da, state, 0), gd, B * OUT);
            }
            gpu(cudaMemcpy(state.data + idx, hidden + idx, sizeof(float), cudaMemcpyHostToDevice));
            derivative((values[0] - values[1]) / 2e-3, dh[idx], 99, idx);
            checks++;
        }
    }
    printf("encoder CUDA vs CPU max error %g, decoder %g (largest pointer term %g; draft rows linear-only)\n",
        worst_enc, worst_dec, pointer);
    if (failures) { printf("FAIL %d of %d numerical derivatives\n", failures, checks); return 1; }
    printf("PASS %d numerical parameter/input derivatives over 3 observation batches (%d retried at a 4x smaller step)\n", checks, kinks);
}
