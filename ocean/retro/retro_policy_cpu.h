#pragma once
#include "puffercpu.c"
#include "retro_policy_shape.h"

// Upstream CPU MinGRU/head, with the preserved Retro CNN parameter order.
typedef struct RetroPolicy {
    const float* conv_w[3];
    const float* conv_b[3];
    const float* ram_b;
    const float* fusion_b;
    Linear* ram;
    Linear* fusion;
    Linear* decoder;
    MinGRU* mingru;
    Multidiscrete* multidiscrete;
    float* image[3];
    float* joined;
} RetroPolicy;

static RetroPolicy* make_retro_policy(Weights* weights, int hidden, int layers) {
    assert(hidden >= 8 && hidden % 8 == 0 && layers >= 1 && layers <= 8);
    RetroPolicy* p = calloc(1, sizeof(RetroPolicy));
    for (int l = 0; l < 3; l++) {
        RetroConvShape c = RETRO_CONVS[l];
        p->conv_w[l] = get_weights_aligned(weights, c.oc*c.k*c.k*c.ic);
        p->conv_b[l] = get_weights_aligned(weights, c.oc);
        p->image[l] = calloc(c.oh*c.ow*c.oc, sizeof(float));
    }
    p->ram = make_linear(weights, 1, RETRO_RAM_INPUTS, RETRO_RAM_HIDDEN);
    p->ram_b = get_weights_aligned(weights, RETRO_RAM_HIDDEN);
    p->fusion = make_linear(weights, 1, RETRO_FUSED_FEATURES, hidden);
    p->fusion_b = get_weights_aligned(weights, hidden);
    p->decoder = make_linear(weights, 1, hidden, 65);
    p->mingru = make_mingru(weights, 1, hidden, layers);
    int sizes[] = {64};
    p->multidiscrete = make_multidiscrete(1, sizes, 1);
    p->joined = calloc(RETRO_FUSED_FEATURES, sizeof(float));
    return p;
}

static const float* retro_policy_encode(RetroPolicy* p, const float* obs) {
    for (int l = 0; l < 3; l++) {
        RetroConvShape c = RETRO_CONVS[l];
        int K = c.k*c.k*c.ic;
        const float* input = l ? p->image[l-1] : obs + RETRO_RAM_INPUTS;
        for (int y = 0; y < c.oh; y++) {
            for (int x = 0; x < c.ow; x++) {
                for (int ch = 0; ch < c.oc; ch++) {
                    float sum = p->conv_b[l][ch];
                    for (int ky = 0; ky < c.k; ky++) {
                        for (int kx = 0; kx < c.k; kx++) {
                            int iy = y*c.stride - c.pad + ky;
                            int ix = x*c.stride - c.pad + kx;
                            if (iy < 0 || iy >= c.ih || ix < 0 || ix >= c.iw) {
                                continue;
                            }
                            for (int ic = 0; ic < c.ic; ic++) {
                                sum += input[(iy*c.iw + ix)*c.ic + ic]
                                    * p->conv_w[l][ch*K + (ky*c.k + kx)*c.ic + ic];
                            }
                        }
                    }
                    p->image[l][(y*c.ow + x)*c.oc + ch] = sum > 0 ? sum : 0;
                }
            }
        }
    }
    linear(p->ram, (float*)obs);
    memcpy(p->joined, p->image[2], RETRO_CNN_FEATURES*sizeof(float));
    for (int i = 0; i < RETRO_RAM_HIDDEN; i++) {
        float sum = p->ram->output[i] + p->ram_b[i];
        p->joined[RETRO_CNN_FEATURES + i] = sum > 0 ? sum : 0;
    }
    linear(p->fusion, p->joined);
    for (int i = 0; i < p->fusion->output_dim; i++) {
        float sum = p->fusion->output[i] + p->fusion_b[i];
        p->fusion->output[i] = sum > 0 ? sum : 0;
    }
    return p->fusion->output;
}

static const float* retro_policy_logits(RetroPolicy* p, const float* obs) {
    const float* encoded = retro_policy_encode(p, obs);
    mingru(p->mingru, (float*)encoded);
    linear(p->decoder, p->mingru->output);
    return p->decoder->output;
}

static void retro_policy_act(RetroPolicy* p, float* obs, float* action, bool deterministic) {
    retro_policy_logits(p, obs);
    multidiscrete(p->multidiscrete, p->decoder->output, action, deterministic, NULL);
}

static void free_retro_policy(RetroPolicy* p) {
    if (!p) {
        return;
    }
    for (int l = 0; l < 3; l++) {
        free(p->image[l]);
    }
    free(p->joined);
    free(p->ram);
    free(p->fusion);
    free(p->decoder);
    free_mingru(p->mingru);
    free(p->multidiscrete);
    free(p);
}
