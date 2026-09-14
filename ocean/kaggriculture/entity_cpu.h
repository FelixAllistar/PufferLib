#pragma once
/* CPU inference/reference for the exact native entity architecture. Included
 * after Weights, Linear, and MinGRU in puffercpu.h. No legacy weight guessing. */
#include "entity_contract.h"

typedef struct {
    int in, mid, out, rows, relu_out;
    float *w1, *w2, *hidden, *output;
} KagCpuMLP;
typedef struct {
    KagCpuMLP entity[4], fusion, branch[3];
    MinGRU* mingru;
    int batch;
    float *global_input, *fused, *output;
} KagCpuPolicy;

static inline size_t kag_parameter_count(int hidden, int layers, int alignment) {
    const int inputs[8] = {184, 56, 24, 32, KAG_FUSION_WIDTH, hidden, hidden, hidden};
    const int mids[8] = {64, 32, 32, 16, hidden, hidden / 2, hidden / 2, hidden / 2};
    const int outputs[8] = {64, 32, 32, 16, hidden, KAG_TASK_LOGITS, KAG_MARKET_LOGITS, 1};
    size_t count = 0, mask = (size_t)alignment - 1;
    for (int i = 0; i < 8; i++) {
        count = (count + mask) & ~mask; count += (size_t)mids[i] * KAG_AUG_WIDTH(inputs[i]);
        count = (count + mask) & ~mask; count += (size_t)outputs[i] * KAG_AUG_WIDTH(mids[i]);
    }
    for (int l = 0; l < layers; l++) {
        count = (count + mask) & ~mask; count += (size_t)3 * hidden * hidden;
    }
    return count;
}

static inline float* kag_cpu_take(Weights* weights, int count, int alignment) {
    weights->idx = (weights->idx + alignment - 1) & ~(alignment - 1);
    if (count < 0 || weights->idx > weights->size - count) {
        fprintf(stderr, "Truncated entity-v2 weights\n"); exit(1);
    }
    float* result = weights->data + weights->idx;
    weights->idx += count;
    return result;
}
static inline KagCpuMLP kag_cpu_mlp_make(Weights* w, int rows, int in, int mid, int out,
        int relu_out, int alignment) {
    KagCpuMLP m = {in, mid, out, rows, relu_out, NULL, NULL, NULL, NULL};
    m.w1 = kag_cpu_take(w, mid * KAG_AUG_WIDTH(in), alignment);
    m.w2 = kag_cpu_take(w, out * KAG_AUG_WIDTH(mid), alignment);
    m.hidden = (float*)calloc((size_t)rows * mid, sizeof(float));
    m.output = (float*)calloc((size_t)rows * out, sizeof(float));
    return m;
}
static inline void kag_cpu_mlp(KagCpuMLP* m, const float* input, int stride) {
    for (int r = 0; r < m->rows; r++) {
        for (int h = 0; h < m->mid; h++) {
            const float* w = m->w1 + h * KAG_AUG_WIDTH(m->in);
            float v = w[m->in];
            for (int i = 0; i < m->in; i++) v += w[i] * input[r * stride + i];
            m->hidden[r * m->mid + h] = fmaxf(0, v);
        }
        for (int o = 0; o < m->out; o++) {
            const float* w = m->w2 + o * KAG_AUG_WIDTH(m->mid);
            float v = w[m->mid];
            for (int h = 0; h < m->mid; h++) v += w[h] * m->hidden[r * m->mid + h];
            m->output[r * m->out + o] = m->relu_out ? fmaxf(0, v) : v;
        }
    }
}
static inline KagCpuPolicy* kag_cpu_make(Weights* weights, int batch, int hidden,
        int layers, int alignment) {
    if (hidden < 8 || hidden % 8 || layers < 1 || (alignment != 4 && alignment != 8)) {
        fprintf(stderr, "Invalid entity-v2 CPU policy shape\n"); exit(1);
    }
    size_t expected = kag_parameter_count(hidden, layers, alignment);
    if (weights->idx != 0 || (size_t)(weights->size - 7) != expected) {
        fprintf(stderr, "Entity-v2 weights require %zu floats; got %d\n", expected, weights->size - 7); exit(1);
    }
    KagCpuPolicy* p = (KagCpuPolicy*)calloc(1, sizeof(*p));
    p->batch = batch;
    p->entity[0] = kag_cpu_mlp_make(weights, batch, 184, 64, 64, 1, alignment);
    p->entity[1] = kag_cpu_mlp_make(weights, batch * 9, 56, 32, 32, 1, alignment);
    p->entity[2] = kag_cpu_mlp_make(weights, batch * 8, 24, 32, 32, 1, alignment);
    p->entity[3] = kag_cpu_mlp_make(weights, batch * 17, 32, 16, 16, 1, alignment);
    p->fusion = kag_cpu_mlp_make(weights, batch, KAG_FUSION_WIDTH, hidden, hidden, 1, alignment);
    p->branch[0] = kag_cpu_mlp_make(weights, batch, hidden, hidden / 2, KAG_TASK_LOGITS, 0, alignment);
    p->branch[1] = kag_cpu_mlp_make(weights, batch, hidden, hidden / 2, KAG_MARKET_LOGITS, 0, alignment);
    p->branch[2] = kag_cpu_mlp_make(weights, batch, hidden, hidden / 2, 1, 0, alignment);
    p->mingru = (MinGRU*)calloc(1, sizeof(MinGRU));
    MinGRU* gru = p->mingru;
    gru->batch_size = batch; gru->hidden_size = hidden; gru->num_layers = layers;
    gru->state = (float*)calloc((size_t)layers * batch * hidden, sizeof(float));
    gru->output = (float*)calloc((size_t)batch * hidden, sizeof(float));
    gru->proj = (Linear**)calloc(layers, sizeof(Linear*));
    for (int l = 0; l < layers; l++) {
        Linear* linear = (Linear*)calloc(1, sizeof(Linear) + (size_t)batch * 3 * hidden * sizeof(float));
        linear->output = (float*)(linear + 1);
        linear->weights = kag_cpu_take(weights, 3 * hidden * hidden, alignment);
        linear->batch_size = batch; linear->input_dim = hidden; linear->output_dim = 3 * hidden;
        gru->proj[l] = linear;
    }
    p->global_input = (float*)calloc((size_t)batch * 184, sizeof(float));
    p->fused = (float*)calloc((size_t)batch * KAG_FUSION_WIDTH, sizeof(float));
    p->output = (float*)calloc((size_t)batch * (KAG_ALL_LOGITS + 1), sizeof(float));
    return p;
}

static inline float* kag_cpu_forward(KagCpuPolicy* p, const float* observations) {
    for (int b = 0; b < p->batch; b++) {
        const float* obs = observations + b * KAG_ENTITY_OBS_SIZE;
        memcpy(p->global_input + b * 184, obs, KAG_GLOBAL_FEATURES * sizeof(float));
        memcpy(p->global_input + b * 184 + KAG_GLOBAL_FEATURES,
            obs + KAG_TASK_OFFSET, KAG_TASK_FEATURES * sizeof(float));
    }
    kag_cpu_mlp(&p->entity[0], p->global_input, 184);
    const int offsets[4] = {0, KAG_PRODUCT_OFFSET, KAG_PLOT_OFFSET, KAG_WORKER_OFFSET};
    const int entities[4] = {1, 9, 8, 17};
    for (int kind = 1; kind < 4; kind++) {
        /* Rows from different observations are not contiguous in the input. */
        KagCpuMLP one = p->entity[kind]; one.rows = entities[kind];
        for (int b = 0; b < p->batch; b++) {
            one.hidden = p->entity[kind].hidden + b * entities[kind] * one.mid;
            one.output = p->entity[kind].output + b * entities[kind] * one.out;
            kag_cpu_mlp(&one, observations + b * KAG_ENTITY_OBS_SIZE + offsets[kind], one.in);
        }
    }
    for (int b = 0; b < p->batch; b++) {
        int offset = 0;
        for (int kind = 0; kind < 4; kind++) {
            int width = entities[kind] * p->entity[kind].out;
            memcpy(p->fused + b * KAG_FUSION_WIDTH + offset,
                p->entity[kind].output + b * width, width * sizeof(float)); offset += width;
        }
    }
    kag_cpu_mlp(&p->fusion, p->fused, KAG_FUSION_WIDTH);
    mingru(p->mingru, p->fusion.output);
    for (int i = 0; i < 3; i++) kag_cpu_mlp(&p->branch[i], p->mingru->output, p->mingru->hidden_size);
    for (int b = 0; b < p->batch; b++) {
        float* dst = p->output + b * (KAG_ALL_LOGITS + 1);
        memcpy(dst, p->branch[0].output + b * KAG_TASK_LOGITS, KAG_TASK_LOGITS * sizeof(float));
        memcpy(dst + KAG_TASK_LOGITS, p->branch[1].output + b * KAG_MARKET_LOGITS, KAG_MARKET_LOGITS * sizeof(float));
        dst[KAG_ALL_LOGITS] = p->branch[2].output[b];
    }
    return p->output;
}
static inline void kag_cpu_mlp_free(KagCpuMLP* m) { free(m->hidden); free(m->output); }
static inline void kag_cpu_free(KagCpuPolicy* p) {
    for (int i = 0; i < 4; i++) kag_cpu_mlp_free(&p->entity[i]);
    kag_cpu_mlp_free(&p->fusion);
    for (int i = 0; i < 3; i++) kag_cpu_mlp_free(&p->branch[i]);
    free_mingru(p->mingru);
    free(p->global_input); free(p->fused); free(p->output); free(p);
}
