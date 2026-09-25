#include "puffercpu.c"
#include "../semantic_cpu.h"

void pk_reference(float* weights, int batch, int hidden, const float* obs,
        const float* state, float* encoded, float* decoded) {
    Weights cursor = {weights, (int)pk_parameter_count(hidden, 1) + 7, 0};
    PKCpuPolicy* p = pk_cpu_make(&cursor, batch, hidden, 1);
    memcpy(encoded, pk_cpu_encode(p, obs), batch*hidden*sizeof(float));
    memcpy(decoded, pk_cpu_decode(p, obs, state), batch*169*sizeof(float));
    pk_cpu_free(p);
}
