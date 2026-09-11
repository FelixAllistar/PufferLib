#include <cuda_runtime.h>
#include <string.h>
#define main phase_cpu_tests
#include "test_phase_rewards.c"
#undef main
__global__ void phase_gpu(KGState* game,float* results) {
    for(int t=0;t<=720;t++) {game->step=t;results[t]=kag_phase_reward(game,0,2);}
}
int main() {
    phase_cpu_tests();
    KGState* h=(KGState*)malloc(sizeof(KGState)); phase_fixture(h);
    KGState* d; float *v, values[721];
    assert(cudaMalloc(&d,sizeof(KGState))==cudaSuccess);
    assert(cudaMalloc(&v,sizeof(values))==cudaSuccess);
    assert(cudaMemcpy(d,h,sizeof(KGState),cudaMemcpyHostToDevice)==cudaSuccess);
    phase_gpu<<<1,1>>>(d,v);assert(cudaDeviceSynchronize()==cudaSuccess);
    assert(cudaMemcpy(values,v,sizeof(values),cudaMemcpyDeviceToHost)==cudaSuccess);
    for(int t=0;t<=720;t++) {h->step=t;assert(fabs(values[t]-kag_phase_reward(h,0,2))<1e-6);}
    cudaFree(d);cudaFree(v);free(h);puts("phase reward CPU/GPU parity passed");
}
