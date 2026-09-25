// Standalone component qualification, not native Pokémon trainer integration.
#define PRECISION_FLOAT
#define ENV_HEADER "ocean/breakout/breakout.h"
#define PUFFER_ENV_NAME "breakout"
#include "../../../src/pufferl.cu"
#include "../pokemon_encoder.cu"
extern "C" void pk_reference(float*, int, int, const float*, const float*, float*, float*);

static void gpu(cudaError_t result) {
    assert(result == cudaSuccess);
}

static void compare(Prec actual, const float* expected, int n) {
    float* host = (float*)calloc(n, sizeof(float));
    gpu(cudaMemcpy(host, actual.data, n*sizeof(float), cudaMemcpyDeviceToHost));
    float largest = 0;
    for (int i = 0; i < n; i++) {
        float error = fabsf(host[i] - expected[i]);
        assert(isfinite(host[i]) && isfinite(expected[i]));
        assert(error < 2e-5f*(1 + fabsf(expected[i])));
        largest = fmaxf(largest, error);
    }
    printf("PASS semantic component n=%d max_error=%g\n", n, largest);
    free(host);
}

static double loss(Prec output, const float* cotangent, int n) {
    float* host = (float*)calloc(n, sizeof(float));
    gpu(cudaMemcpy(host, output.data, n*sizeof(float), cudaMemcpyDeviceToHost));
    double total = 0;
    for (int i = 0; i < n; i++) total += host[i]*cotangent[i];
    free(host);
    return total;
}

static void derivative(double numeric, float analytic, int tensor, int index) {
    if (!isfinite(numeric) || !isfinite(analytic)
            || fabs(numeric-analytic) > 3e-4 + 0.025*fmax(fabs(numeric), fabs(analytic))) {
        fprintf(stderr, "gradient tensor=%d index=%d analytic=%g numeric=%g\n",
            tensor, index, analytic, numeric);
        exit(1);
    }
}

// Isolate decoder semantics from recurrence while exercising the real architecture call.
static Prec fixed_hidden(void* weights, Prec input, Prec state,
        void* activations, cudaStream_t stream) {
    return state;
}

static Prec fixed_hidden_train(void* weights, Prec input, Prec state,
        Prec terminals, void* activations, int offset, cudaStream_t stream) {
    return *puf_unsqueeze(&state, 0, 1, state.shape[0]);
}

int main() {
    const int B = 3, H = 16;
    cublas_init_handle();
    Encoder enc = {.in_dim = 648, .out_dim = H};
    Decoder dec = {.hidden_dim = H, .output_dim = 168};
    create_pokemon_encoder(&enc);
    create_pokemon_decoder(&dec);
    void* ew = enc.create_weights(&enc);
    void* dw = dec.create_weights(&dec);
    PKEncoderActs ea = {};
    PKDecoderActs da = {};
    Allocator params = {}, acts = {}, grads = {};
    enc.reg_params(ew, &params);
    dec.reg_params(dw, &params);
    enc.reg_train(ew, &ea, &acts, &grads, B);
    dec.reg_train(dw, &da, &acts, &grads, B);
    Prec obs = {.shape = {B, 648}}, state = {.shape = {B, H}};
    Prec enc_grad = {.shape = {B, H}};
    Float logits_grad = {.shape = {B, 168}}, value_grad = {.shape = {B}};
    alloc_register(&acts, &enc_grad);
    alloc_register(&acts, &logits_grad);
    alloc_register(&acts, &value_grad);
    alloc_register(&acts, &obs);
    alloc_register(&acts, &state);
    TrainGraph graph = {};
    graph.mb_actions = {.shape = {1, B, 1}};
    graph.mb_logprobs = {.shape = {1, B}};
    graph.mb_action_mask = {.shape = {1, B, 168}};
    graph.mb_imp = {.shape = {1, B}};
    graph.mb_gae_v = {.shape = {1, B}};
    Float logps = {.shape = {B, 168}}, new_lp = {.shape = {B}};
    Int sizes = {.shape = {1}};
    alloc_register(&acts, &graph.mb_actions);
    alloc_register(&acts, &graph.mb_logprobs);
    alloc_register(&acts, &graph.mb_action_mask);
    alloc_register(&acts, &graph.mb_imp);
    alloc_register(&acts, &graph.mb_gae_v);
    alloc_register(&acts, &logps);
    alloc_register(&acts, &new_lp);
    alloc_register(&acts, &sizes);
    alloc_create(&params);
    alloc_create(&acts);
    alloc_create(&grads);
    int action_count = 168;
    gpu(cudaMemcpy(sizes.data, &action_count, sizeof(int), cudaMemcpyHostToDevice));
    gpu(cudaMemset(graph.mb_actions.data, 0, B*sizeof(float)));
    gpu(cudaMemset(graph.mb_logprobs.data, 0, B*sizeof(float)));
    float legal[B*168];
    for (int i = 0; i < B*168; i++) legal[i] = 1;
    gpu(cudaMemcpy(graph.mb_action_mask.data, legal, sizeof(legal), cudaMemcpyHostToDevice));
    assert(params.num_regs == 12 && grads.num_regs == params.num_regs);
    assert(params.total_bytes == grads.total_bytes);
    assert(params.total_bytes == params.total_elems*sizeof(float));
    float* weights = (float*)calloc(params.total_elems + 3*H*H + 7, sizeof(float));
    int offset = 0;
    const int features[12] = {PK_MOVE_IN,64,PK_MON_IN,64,PK_FUSION,H,
        PK_SPECIES_IN,64,PK_MOVE_IN,64,H,H};
    for (int p = 0; p < params.num_regs; p++) {
        int rows = params.regs[p].shape[0], cols = params.regs[p].shape[1];
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                weights[offset+r*cols+c] = c < features[p]
                    ? 0.01f*sinf((offset+r*cols+c)*0.731f) : c == features[p] ? 0.2f : 0;
            }
        }
        gpu(cudaMemcpy(*params.regs[p].data_ptr, weights+offset,
            rows*cols*sizeof(float), cudaMemcpyHostToDevice));
        offset += rows*cols;
    }
    float input[B*648] = {}, hidden[B*H], expected_enc[B*H], expected_dec[B*169];
    for (int i = 0; i < B*H; i++) hidden[i] = sinf(i*0.31f);
    gpu(cudaMemcpy(state.data, hidden, sizeof(hidden), cudaMemcpyHostToDevice));
    float ge[B*H], gd[B*169], gl[B*168], gv[B];
    for (int i = 0; i < B*H; i++) ge[i] = 0.1f*cosf(i*0.73f);
    for (int b = 0; b < B; b++) {
        for (int a = 0; a < 169; a++) {
            gd[b*169+a] = 0.01f*sinf((b*169+a)*0.37f);
            if (a < 168) gl[b*168+a] = gd[b*169+a];
            else gv[b] = gd[b*169+a];
        }
    }
    gpu(cudaMemcpy(enc_grad.data, ge, sizeof(ge), cudaMemcpyHostToDevice));
    gpu(cudaMemcpy(logits_grad.data, gl, sizeof(gl), cudaMemcpyHostToDevice));
    gpu(cudaMemcpy(value_grad.data, gv, sizeof(gv), cudaMemcpyHostToDevice));
    int checks = 0;
    for (int pass = 0; pass < 4; pass++) {
        for (int b = 0; b < B; b++) {
            float* o = input+b*648;
            for (int i = 0; i < 648; i++) o[i] = (i*17+b*13+pass*31)%150;
            o[0] = pass;
            o[4] = b+1;
            o[5] = b+2;
            o[401] = 2;
            o[402] = 3;
            o[433] = 4;
            o[434] = 5;
        }
        gpu(cudaMemcpy(obs.data, input, sizeof(input), cudaMemcpyHostToDevice));
        pk_reference(weights, B, H, input, hidden, expected_enc, expected_dec);
        compare(enc.forward(ew, &ea, obs, 0), expected_enc, B*H);
        Arch arch = {.encoder = enc, .decoder = dec,
            .network = {.forward = fixed_hidden, .forward_train = fixed_hidden_train}};
        Weights arch_weights = {.encoder = ew, .decoder = dw};
        Activations arch_acts = {.encoder = &ea, .decoder = &da};
        da.obs = {};
        compare(arch_forward(&arch, arch_weights, arch_acts, obs, state, 0),
            expected_dec, B*169);
        assert(da.obs.data == obs.data && da.obs.shape[0] == B);
        da.obs = {};
        Prec sequence = obs;
        puf_unsqueeze(&sequence, 0, 1, B);
        compare(arch_forward_train(&arch, arch_weights, arch_acts, sequence, state,
            {}, 0, graph, {}, sizes.data, logps.data, new_lp.data, 0), expected_dec, B*169);
        assert(da.obs.data == obs.data && da.obs.shape[0] == B && da.obs.shape[1] == 648);
        enc.backward(ew, &ea, enc_grad, 0);
        Prec grad_state = dec.backward(dw, &da, logits_grad, {}, value_grad, 0);
        float dh[B*H];
        gpu(cudaMemcpy(dh, grad_state.data, sizeof(dh), cudaMemcpyDeviceToHost));
        for (int p = 0; p < params.num_regs; p++) {
            int n = numel(params.regs[p].shape);
            assert(n == numel(grads.regs[p].shape));
            float* analytic = (float*)calloc(n, sizeof(float));
            gpu(cudaMemcpy(analytic, *grads.regs[p].data_ptr, n*sizeof(float), cudaMemcpyDeviceToHost));
            int largest = 0;
            for (int i = 0; i < n; i++) {
                assert(isfinite(analytic[i]));
                if (fabsf(analytic[i]) > fabsf(analytic[largest])) largest = i;
            }
            int probes[] = {0, n/2, largest};
            for (int idx : probes) {
                float* address = (float*)*params.regs[p].data_ptr + idx;
                float original;
                gpu(cudaMemcpy(&original, address, sizeof(float), cudaMemcpyDeviceToHost));
                double values[2];
                const float epsilon = 0.001f;
                for (int sign = 0; sign < 2; sign++) {
                    float changed = original + (sign ? -epsilon : epsilon);
                    gpu(cudaMemcpy(address, &changed, sizeof(float), cudaMemcpyHostToDevice));
                    values[sign] = p < 6 ? loss(enc.forward(ew, &ea, obs, 0), ge, B*H)
                        : loss(dec.forward(dw, &da, state, 0), gd, B*169);
                }
                gpu(cudaMemcpy(address, &original, sizeof(float), cudaMemcpyHostToDevice));
                derivative((values[0]-values[1])/(2*epsilon), analytic[idx], p, idx);
                checks++;
            }
            free(analytic);
        }
        for (int idx = 0; idx < B*H; idx += 7) {
            double values[2];
            for (int sign = 0; sign < 2; sign++) {
                float changed = hidden[idx] + (sign ? -0.001f : 0.001f);
                gpu(cudaMemcpy(state.data+idx, &changed, sizeof(float), cudaMemcpyHostToDevice));
                values[sign] = loss(dec.forward(dw, &da, state, 0), gd, B*169);
            }
            gpu(cudaMemcpy(state.data+idx, hidden+idx, sizeof(float), cudaMemcpyHostToDevice));
            derivative((values[0]-values[1])/0.002, dh[idx], 12, idx);
            checks++;
        }
    }
    puts("PASS Pokémon FP32 encoder/observation-conditioned decoder CPU parity");
    printf("PASS %d numerical parameter/input derivatives across four observation batches\n", checks);
}
