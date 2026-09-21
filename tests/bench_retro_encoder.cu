// Isolated, CUDA-graph-timed Retro encoder benchmark. No training checkpoints
// are read or written. Run only with an idle GPU for comparable measurements.
#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <curand.h>
#include <curand_kernel.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define PUFFER_RETRO_CNN
#define ENV_HEADER "ocean/retro/retro.h"
#include "../src/pufferl_preamble.h"
#include "../src/algo.cu"

static void check(cudaError_t error) {
    if (error != cudaSuccess) {
        fprintf(stderr, "CUDA: %s\n", cudaGetErrorString(error));
        exit(1);
    }
}
__global__ void bench_fill(precision_t* data, int n, float scale) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) data[i] = from_float(scale * (0.1f + (i % 997) / 1108.0f));
}
template<class F> static float milliseconds(cudaStream_t stream, int repeats, F fn) {
    for (int i = 0; i < 3; i++) fn();
    check(cudaStreamSynchronize(stream));
    cudaGraph_t graph;
    cudaGraphExec_t executable;
    check(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
    fn();
    check(cudaStreamEndCapture(stream, &graph));
    check(cudaGraphInstantiate(&executable, graph, nullptr, nullptr, 0));
    check(cudaGraphLaunch(executable, stream));
    cudaEvent_t start, end;
    check(cudaEventCreate(&start)); check(cudaEventCreate(&end));
    check(cudaEventRecord(start, stream));
    for (int i = 0; i < repeats; i++) check(cudaGraphLaunch(executable, stream));
    check(cudaEventRecord(end, stream)); check(cudaEventSynchronize(end));
    float elapsed;
    check(cudaEventElapsedTime(&elapsed, start, end));
    check(cudaGraphExecDestroy(executable)); check(cudaGraphDestroy(graph));
    check(cudaEventDestroy(start)); check(cudaEventDestroy(end));
    return elapsed / repeats;
}

int main(int argc, char** argv) {
    setbuf(stdout, nullptr);
    const int B = argc > 1 ? atoi(argv[1]) : 64;
    const int repeats = argc > 2 ? atoi(argv[2]) : 30;
    const bool training = argc <= 3 || strcmp(argv[3], "rollout");
    if (B < 1 || B > 8192 || repeats < 1 || repeats > 1000) return 2;
    cudaStream_t stream;
    check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    cublas_init_handle();
    Policy policy = build_policy("retro", RETRO_POLICY_INPUTS, 128, 2, 64, false, 128);
    Allocator params, acts, grads;
    auto weights = policy_weights_create(&policy, &params);
    check(alloc_create(&params));
    ulong seed = 73;
    policy_init_weights(&policy, weights, &seed, stream);
    RetroEncoderActivations a = {};
    if (training) policy.encoder.reg_train(weights.encoder, &a, &acts, &grads, B);
    else policy.encoder.reg_rollout(weights.encoder, &a, &acts, B);
    PrecisionTensor input = {.shape={B, RETRO_POLICY_INPUTS}}, grad = {.shape={B, 128}};
    alloc_register(&acts, &input); alloc_register(&acts, &grad);
    check(alloc_create(&acts)); check(alloc_create(&grads));
    bench_fill<<<grid_size(B*RETRO_POLICY_INPUTS),BLOCK_SIZE,0,stream>>>(input.data,B*RETRO_POLICY_INPUTS,1);
    bench_fill<<<grid_size(B*128),BLOCK_SIZE,0,stream>>>(grad.data,B*128,0.03f);
    check(cudaStreamSynchronize(stream));
    auto forward = [&]() { policy.encoder.forward(weights.encoder, &a, input, stream); };
    auto backward = [&]() { policy.encoder.backward(weights.encoder, &a, grad, stream); };
    float forward_ms = milliseconds(stream, repeats, forward);
    printf("BENCH precision=%s batch=%d mode=%s activations_mib=%.3f forward_ms=%.6f",
        USE_BF16 ? "bf16" : "float32", B, training ? "train" : "rollout",
        acts.total_bytes / 1048576.0, forward_ms);
    if (training) {
        forward();
        float backward_ms = milliseconds(stream, repeats, backward);
        float total_ms = milliseconds(stream, repeats, [&]() { forward(); backward(); });
        printf(" backward_ms=%.6f forward_backward_ms=%.6f", backward_ms, total_ms);
    }
    puts("");
#if !defined(RETRO_CNN_FUSED) || !RETRO_CNN_FUSED
    // Break down the original materialized-patch path. The full encoder above
    // is the actual benchmark; these component times identify the bottleneck.
    auto* ew = (RetroEncoderWeights*)weights.encoder;
    for (int l = 0; l < 3; l++) {
        auto c = RETRO_CONVS[l]; int N = c.oh*c.ow, K = c.k*c.k*c.ic;
        auto col = retro_matrix(a.columns, B*N, K);
        auto out = retro_matrix(a.image[l], B*N, c.oc);
        auto unfold = [&]() {
            retro_im2col<<<grid_size(B*N*K),BLOCK_SIZE,0,stream>>>(col.data,
                l ? a.image[l-1].data : input.data, c, B,
                l ? c.ih*c.iw*c.ic : RETRO_POLICY_INPUTS, l ? 0 : RETRO_RAM_INPUTS);
        };
        float unfold_ms = milliseconds(stream, repeats, unfold);
        float gemm_ms = milliseconds(stream, repeats, [&]() { puf_mm(&col,&ew->w[l],&out,stream); });
        printf("COMPONENT conv=%d im2col_ms=%.6f gemm_ms=%.6f", l+1, unfold_ms, gemm_ms);
        if (training) {
            auto g = retro_matrix(a.image_grad[l], B*N, c.oc);
            float dw_ms = milliseconds(stream, repeats, [&]() { puf_mm_tn(&g,&col,&a.dw[l],stream); });
            float db_ms = milliseconds(stream, repeats, [&]() {
                retro_bias_grad<<<c.oc,256,0,stream>>>(a.db[l].data,g.data,B*N,c.oc);
            });
            printf(" dw_ms=%.6f db_ms=%.6f", dw_ms, db_ms);
            if (l) {
                float dx_ms = milliseconds(stream, repeats, [&]() {
                    puf_mm_nn(&g,&ew->w[l],&col,stream);
                    retro_col2im<<<grid_size(B*c.ih*c.iw*c.ic),BLOCK_SIZE,0,stream>>>(a.image_grad[l-1].data,col.data,c,B);
                });
                printf(" dx_ms=%.6f", dx_ms);
            }
        }
        puts("");
    }
#endif
    check(cudaDeviceSynchronize());
    return 0;
}
