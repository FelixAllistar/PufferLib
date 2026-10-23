// CPU window Transformer (semantic_cpu.h) for tests/test_window_parity.cu.
#include "puffercpu.c"
#include "../semantic_cpu.h"

void window_cpu_reference(float* weights, int count, int batch, int hidden, int length, int width, int layers,
        int heads, const float* inputs, const float* resets, int steps, float* outputs) {
    Weights cursor = {weights, count + 7, 0};
    PKWindow config = {1, layers, width, heads};
    PKCpuWindow* w = pk_cpu_window_make(&cursor, batch, hidden, length, config);
    if (cursor.idx != count) { fprintf(stderr, "window reference consumed %d of %d weights\n", cursor.idx, count); exit(1); }
    for (int t = 0; t < steps; t++) {
        for (int b = 0; b < batch; b++) if (resets[t * batch + b] != 0)
            memset(w->state + (size_t)b * length * hidden, 0, (size_t)length * hidden * sizeof(float));
        memcpy(outputs + (size_t)t * batch * hidden, pk_cpu_window(w, inputs + (size_t)t * batch * hidden),
            (size_t)batch * hidden * sizeof(float));
    }
    pk_cpu_window_free(w);
}
