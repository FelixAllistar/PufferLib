// nvcc -O2 -I src tests/test_protein_resume.cu -o /tmp/test_protein_resume -lcublas -lcusolver -lcurand
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
#define BLOCK_SIZE 256
__device__ __forceinline__ void block_reduce_sum(
        float* smem, float* out, int tid, int nthreads, int nchan) {
    __syncthreads();
    for (int stride = nthreads / 2; stride; stride >>= 1) {
        if (tid < stride)
            for (int c = 0; c < nchan; c++)
                smem[c*nthreads+tid] += smem[c*nthreads+tid+stride];
        __syncthreads();
    }
    if (!tid) for (int c = 0; c < nchan; c++) out[c] = smem[c*nthreads];
}
#include "protein.cu"

static void init(ProteinSweep* sweep, float* scratch) {
    *sweep = {};
    sweep->dim = 3;
    sweep->cost_dim = 0;
    sweep->num_random_samples = 10;
    sweep->cost_random_suggestion = -.8f;
    sweep->h_cands = scratch;
    assert(curandCreateGeneratorHost(&sweep->sobol, CURAND_RNG_QUASI_SCRAMBLED_SOBOL32) == CURAND_STATUS_SUCCESS);
    assert(curandSetQuasiRandomGeneratorDimensions(sweep->sobol, 3) == CURAND_STATUS_SUCCESS);
    assert(curandSetGeneratorOffset(sweep->sobol, 73) == CURAND_STATUS_SUCCESS);
}

int main() {
    for (int count : {0, 1, 6, 10, 14}) {
        ProteinSweep uninterrupted, restored;
        float a[3], b[3], scratch_a[3], scratch_b[3];
        init(&uninterrupted, scratch_a);
        init(&restored, scratch_b);
        srand(73);
        for (int i = 0; i < (count < 10 ? count : 10); i++)
            sobol_fallback(&uninterrupted, a, 0, NAN);
        sobol_fallback(&uninterrupted, a, 0, NAN);
        srand(73);
        protein_sweep_restore_suggestions(&restored, count);
        assert(restored.suggestion_idx == count);
        sobol_fallback(&restored, b, 0, NAN);
        for (int i = 0; i < 3; i++) assert(a[i] == b[i]);
        curandDestroyGenerator(uninterrupted.sobol);
        curandDestroyGenerator(restored.sobol);
    }
    puts("Restored sweeps consume their previous random prefix");
}
