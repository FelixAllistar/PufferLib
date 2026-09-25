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
    Allocator params = {}, acts = {};
    enc.reg_params(ew, &params);
    dec.reg_params(dw, &params);
    enc.reg_rollout(ew, &ea, &acts, B);
    dec.reg_rollout(dw, &da, &acts, B);
    Prec obs = {.shape = {B, 648}}, state = {.shape = {B, H}};
    alloc_register(&acts, &obs);
    alloc_register(&acts, &state);
    alloc_create(&params);
    alloc_create(&acts);
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
    for (int pass = 0; pass < 4; pass++) {
        for (int b = 0; b < B; b++) {
            float* o = input+b*648;
            for (int i = 0; i < 648; i++) o[i] = (i*17+b*13+pass*31)%150;
            o[0] = pass;
        }
        gpu(cudaMemcpy(obs.data, input, sizeof(input), cudaMemcpyHostToDevice));
        pk_reference(weights, B, H, input, hidden, expected_enc, expected_dec);
        compare(enc.forward(ew, &ea, obs, 0), expected_enc, B*H);
        pk_decoder_bind(&da, obs);
        compare(dec.forward(dw, &da, state, 0), expected_dec, B*169);
    }
    puts("PASS Pokémon FP32 encoder/observation-conditioned decoder CPU parity");
}
