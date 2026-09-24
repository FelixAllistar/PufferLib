#pragma once
#include <stddef.h>
#include "retro_resolution.h"

// NHWC images; weights are [output channel, kernel y, kernel x, input channel].
typedef struct RetroConvShape {
    int ih, iw, ic, oc, k, stride, pad, oh, ow;
} RetroConvShape;
enum {
    RETRO_CONV1_H = (RETRO_WINDOW_H - 8)/4 + 1,
    RETRO_CONV1_W = (RETRO_WINDOW_W - 8)/4 + 1,
    RETRO_CONV2_H = RETRO_CONV1_H/2,
    RETRO_CONV2_W = RETRO_CONV1_W/2,
    RETRO_CONV3_H = (RETRO_CONV2_H + 1)/2,
    RETRO_CONV3_W = (RETRO_CONV2_W + 1)/2,
    RETRO_RAM_INPUTS = 112,
    RETRO_IMAGE_INPUTS = RETRO_WINDOW_H*RETRO_WINDOW_W,
    RETRO_POLICY_INPUTS = RETRO_RAM_INPUTS + RETRO_IMAGE_INPUTS,
    RETRO_RAM_HIDDEN = 32,
    RETRO_CNN_FEATURES = RETRO_CONV3_H*RETRO_CONV3_W*32,
    RETRO_FUSED_FEATURES = RETRO_CNN_FEATURES + RETRO_RAM_HIDDEN,
    RETRO_COL_ELEMENTS = RETRO_CONV1_H*RETRO_CONV1_W*8*8,
};
#ifdef __cplusplus
static constexpr RetroConvShape RETRO_CONVS[3] = {
#else
static const RetroConvShape RETRO_CONVS[3] = {
#endif
    {RETRO_WINDOW_H, RETRO_WINDOW_W, 1, 8, 8, 4, 0, RETRO_CONV1_H, RETRO_CONV1_W},
    {RETRO_CONV1_H, RETRO_CONV1_W, 8, 16, 4, 2, 1, RETRO_CONV2_H, RETRO_CONV2_W},
    {RETRO_CONV2_H, RETRO_CONV2_W, 16, 32, 3, 2, 1, RETRO_CONV3_H, RETRO_CONV3_W},
};

static inline size_t retro_encoder_weights(int hidden) {
    size_t count = 0;
    for (int l = 0; l < 3; l++) {
        RetroConvShape c = RETRO_CONVS[l];
        count += c.oc*c.k*c.k*c.ic + c.oc;
    }
    return count + RETRO_RAM_HIDDEN*RETRO_RAM_INPUTS + RETRO_RAM_HIDDEN
        + hidden*RETRO_FUSED_FEATURES + hidden;
}

static inline size_t retro_policy_weights(int hidden, int layers) {
    return retro_encoder_weights(hidden) + 65*hidden + layers*3*hidden*hidden;
}
