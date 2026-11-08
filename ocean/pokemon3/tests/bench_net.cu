// Speed check of the structured Gen 3 net (2026-11-07): times each stage of the encoder and decoder forward and the
// backward at training batch sizes, in the training precision (bf16). Build/run: ocean/pokemon3/tests/run_bench_net.sh
#define ENV_HEADER "ocean/breakout/breakout.h"
#define PUFFER_ENV_NAME "breakout"
#include "../../../src/pufferl.cu"
#include "../pokemon3_encoder.cu"

static void gpu(cudaError_t r) { if (r != cudaSuccess) { fprintf(stderr, "cuda %s\n", cudaGetErrorString(r)); exit(1); } }
static cudaEvent_t t0, t1;
#define TIME(name, reps, ...) do { for (int _w = 0; _w < 3; _w++) { __VA_ARGS__; } gpu(cudaEventRecord(t0)); \
    for (int _i = 0; _i < reps; _i++) { __VA_ARGS__; } gpu(cudaEventRecord(t1)); gpu(cudaEventSynchronize(t1)); \
    float _ms; cudaEventElapsedTime(&_ms, t0, t1); printf("  %-28s %8.3f ms\n", name, _ms / reps); } while (0)

int main(int argc, char** argv) {
    int B = argc > 1 ? atoi(argv[1]) : 4096, H = 256;
    pk3_net_on = 1; pk3_embed = 32; pk3_mid = 64;
    int E = pk3_embed;
    cublas_init_handle();
    cudaEventCreate(&t0); cudaEventCreate(&t1);
    Encoder enc = {.in_dim = P3_OBS, .out_dim = H};
    Decoder dec = {.hidden_dim = H, .output_dim = P3_ACTIONS};
    create_pokemon3_encoder(&enc); create_pokemon3_decoder(&dec);
    void* ew = enc.create_weights(&enc); void* dw = dec.create_weights(&dec);
    P3EncWeights* w = (P3EncWeights*)ew; P3DecWeights* wd = (P3DecWeights*)dw;
    P3EncActs ea = {}; P3DecActs da = {};
    Allocator params = {}, acts = {}, grads = {};
    enc.reg_params(ew, &params); dec.reg_params(dw, &params);
    enc.reg_train(ew, &ea, &acts, &grads, B); dec.reg_train(dw, &da, &acts, &grads, B);
    Prec obs = {.shape = {B, P3_OBS}}, hidden = {.shape = {B, H}}, flat_w = {.shape = {H, P3_OBS}}, flat_out = {.shape = {B, H}};
    alloc_register(&acts, &obs); alloc_register(&acts, &hidden); alloc_register(&acts, &flat_w); alloc_register(&acts, &flat_out);
    alloc_create(&params); alloc_create(&acts); alloc_create(&grads);
    ulong seed = 1; enc.init_weights(ew, &seed, 0); dec.init_weights(dw, &seed, 0);
    // Realistic-ish obs: everything 0.3, moves known, own active 0.
    precision_t* host = (precision_t*)malloc(sizeof(precision_t) * (long)B * P3_OBS);
    for (long i = 0; i < (long)B * P3_OBS; i++) host[i] = from_float(0.3f);
    for (int b = 0; b < B; b++) {
        for (int m = 0; m < 12; m++) { host[(long)b * P3_OBS + m * P3_MON + 1] = from_float(m == 0 || m == 6);
            for (int j = 0; j < 4; j++) host[(long)b * P3_OBS + m * P3_MON + P3_MOVE_AT + j * P3_MOVE] = from_float(1); }
        host[(long)b * P3_OBS + P3_DRAFTING] = from_float(0);
    }
    gpu(cudaMemcpy(obs.data, host, sizeof(precision_t) * (long)B * P3_OBS, cudaMemcpyHostToDevice));
    dec.bind_observation(&da, obs);
    printf("B=%d H=%d E=%d M=%d precision bytes=%zu\n", B, H, E, pk3_mid, sizeof(precision_t));
    TIME("flat linear 5343->256", 50, puf_mm(&obs, &flat_w, &flat_out, 0));
    TIME("enc move_rows kernel", 50, (p3_move_rows<<<grid_size(B * P3_MONS * 4 * P3_AUG(P3_MOVE_IN)), BLOCK_SIZE>>>(ea.move.input_aug.data, obs.data, B, P3_MONS)));
    TIME("enc move MLP", 50, p3_nn_mlp_forward(&w->move, &ea.move, 0));
    TIME("enc mon_rows kernel", 50, (p3_mon_rows<<<grid_size(B * P3_MONS * P3_AUG(P3_MON_IN(E))), BLOCK_SIZE>>>(ea.mon.input_aug.data, obs.data, ea.move.out.data, B, P3_MONS, E)));
    TIME("enc mon MLP", 50, p3_nn_mlp_forward(&w->mon, &ea.mon, 0));
    TIME("enc fusion_rows kernel", 50, (p3_fusion_rows<<<grid_size(B * P3_AUG(P3_FUSION(E))), BLOCK_SIZE>>>(ea.fusion.input_aug.data, obs.data, ea.mon.out.data, B, E)));
    TIME("enc fusion MLP", 50, p3_nn_mlp_forward(&w->fusion, &ea.fusion, 0));
    TIME("ENCODER forward total", 50, enc.forward(ew, &ea, obs, 0));
    TIME("DECODER forward total", 50, dec.forward(dw, &da, hidden, 0));
    Prec g = {.shape = {B, H}}; g.data = ea.fusion.out.data;
    TIME("ENCODER backward total", 20, enc.backward(ew, &ea, ea.fusion.out, 0));
    return 0;
}
